#include "file_picker.h"

#include "fs/file_utils.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QStandardPaths>

namespace Zalos::Core::Platform::Detail {

struct PortalFilterRule {
    uint type = 0;
    QString pattern;
};

struct PortalFilter {
    QString name;
    QList<PortalFilterRule> rules;
};

using PortalFilterList = QList<PortalFilter>;

QDBusArgument& operator<<(QDBusArgument& arg, const PortalFilterRule& rule) {
    arg.beginStructure();
    arg << rule.type << rule.pattern;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, PortalFilterRule& rule) {
    arg.beginStructure();
    arg >> rule.type >> rule.pattern;
    arg.endStructure();
    return arg;
}

QDBusArgument& operator<<(QDBusArgument& arg, const PortalFilter& filter) {
    arg.beginStructure();
    arg << filter.name << filter.rules;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, PortalFilter& filter) {
    arg.beginStructure();
    arg >> filter.name >> filter.rules;
    arg.endStructure();
    return arg;
}

}

Q_DECLARE_METATYPE(Zalos::Core::Platform::Detail::PortalFilterRule)
Q_DECLARE_METATYPE(Zalos::Core::Platform::Detail::PortalFilter)

namespace Zalos::Core::Platform {

namespace {

const QString kPortalService = QStringLiteral("org.freedesktop.portal.Desktop");
const QString kPortalPath = QStringLiteral("/org/freedesktop/portal/desktop");
const QString kRequestInterface = QStringLiteral("org.freedesktop.portal.Request");

std::function<void()>& unavailableHandler() {
    static std::function<void()> handler;
    return handler;
}

void registerPortalTypes() {
    static const bool done = [] {
        qDBusRegisterMetaType<Detail::PortalFilterRule>();
        qDBusRegisterMetaType<QList<Detail::PortalFilterRule>>();
        qDBusRegisterMetaType<Detail::PortalFilter>();
        qDBusRegisterMetaType<Detail::PortalFilterList>();
        return true;
    }();
    Q_UNUSED(done);
}

QString localPathOf(const QUrl& url) {
    if (url.isEmpty()) return {};
    return url.isLocalFile() ? url.toLocalFile() : url.toString();
}

}

FilePicker::FilePicker(QObject* parent)
    : QObject(parent) {}

FilePicker::~FilePicker() {
    disconnectPortal();
    if (m_helper) {
        m_helper->disconnect(this);
        m_helper->kill();
        m_helper->deleteLater();
    }
}

void FilePicker::setTitle(const QString& title) {
    if (title == m_title) return;
    m_title = title;
    emit titleChanged();
}

void FilePicker::setNameFilters(const QStringList& filters) {
    if (filters == m_nameFilters) return;
    m_nameFilters = filters;
    emit nameFiltersChanged();
}

void FilePicker::setFileMode(FileMode mode) {
    if (mode == m_fileMode) return;
    m_fileMode = mode;
    emit fileModeChanged();
}

void FilePicker::setCurrentFile(const QUrl& file) {
    if (file == m_currentFile) return;
    m_currentFile = file;
    emit currentFileChanged();
}

void FilePicker::setUnavailableHandler(std::function<void()> handler) {
    unavailableHandler() = std::move(handler);
}

void FilePicker::setBusy(bool busy) {
    if (busy == m_busy) return;
    m_busy = busy;
    emit busyChanged();
}

QList<FilePicker::Filter> FilePicker::parseNameFilters(const QStringList& nameFilters) {
    static const QRegularExpression re(QStringLiteral(R"(^\s*(.*?)\s*\(([^()]*)\)\s*$)"));
    QList<Filter> out;
    for (const QString& entry : nameFilters) {
        Filter filter;
        const QRegularExpressionMatch m = re.match(entry);
        const QString patterns = m.hasMatch() ? m.captured(2) : entry;
        filter.name = entry.trimmed();
        for (QString p : patterns.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
            if (p == QLatin1String("*.*")) p = QStringLiteral("*");
            if (!filter.patterns.contains(p)) filter.patterns << p;
        }
        if (filter.patterns.isEmpty()) filter.patterns << QStringLiteral("*");
        out << filter;
    }
    return out;
}

QList<QUrl> FilePicker::urlsFromLines(const QByteArray& output) {
    QList<QUrl> urls;
    for (const QString& line : QString::fromLocal8Bit(output).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString path = line.trimmed();
        if (!path.isEmpty()) urls << QUrl::fromLocalFile(path);
    }
    return urls;
}

QStringList FilePicker::zenityArguments(FileMode mode, const QString& title, const QList<Filter>& filters,
                                        const QString& currentPath) {
    QStringList args{QStringLiteral("--file-selection")};
    if (!title.isEmpty()) args << QStringLiteral("--title=") + title;
    if (mode == OpenFiles) args << QStringLiteral("--multiple") << QStringLiteral("--separator=\n");
    if (mode == SaveFile) args << QStringLiteral("--save") << QStringLiteral("--confirm-overwrite");
    if (mode == SelectFolder) args << QStringLiteral("--directory");
    if (!currentPath.isEmpty()) args << QStringLiteral("--filename=") + currentPath;
    for (const Filter& f : filters) args << QStringLiteral("--file-filter=%1 | %2").arg(f.name, f.patterns.join(QLatin1Char(' ')));
    return args;
}

QStringList FilePicker::kdialogArguments(FileMode mode, const QString& title, const QList<Filter>& filters,
                                         const QString& currentPath) {
    QStringList filterLines;
    for (const Filter& f : filters) filterLines << f.patterns.join(QLatin1Char(' ')) + QLatin1Char('|') + f.name;
    QStringList args;
    if (!title.isEmpty()) args << QStringLiteral("--title") << title;
    if (mode == SaveFile) {
        args << QStringLiteral("--getsavefilename");
    } else if (mode == SelectFolder) {
        args << QStringLiteral("--getexistingdirectory") << (currentPath.isEmpty() ? QDir::homePath() : currentPath);
        return args;
    } else {
        args << QStringLiteral("--getopenfilename");
        if (mode == OpenFiles) args << QStringLiteral("--multiple") << QStringLiteral("--separate-output");
    }
    args << (currentPath.isEmpty() ? QDir::homePath() : currentPath);
    if (!filterLines.isEmpty()) args << filterLines.join(QLatin1Char('\n'));
    return args;
}

void FilePicker::open() {
    if (m_busy) return;
    ++m_generation;
    setBusy(true);
    if (openWithPortal()) return;
    if (!Fs::FileUtils::sandboxed() && openWithHelper()) return;
    setBusy(false);
    if (const auto& handler = unavailableHandler()) handler();
    emit rejected();
}

bool FilePicker::openWithPortal() {
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) return false;
    registerPortalTypes();

    const QString token = QStringLiteral("zalos%1_%2")
                              .arg(m_generation)
                              .arg(QRandomGenerator::global()->generate() & 0xffffff);
    QString sender = bus.baseService();
    sender.remove(0, sender.startsWith(QLatin1Char(':')) ? 1 : 0).replace(QLatin1Char('.'), QLatin1Char('_'));
    m_requestPath = QStringLiteral("/org/freedesktop/portal/desktop/request/%1/%2").arg(sender, token);
    bus.connect(kPortalService, m_requestPath, kRequestInterface, QStringLiteral("Response"), this,
                SLOT(onPortalResponse(uint,QVariantMap)));

    QVariantMap options{{QStringLiteral("handle_token"), token}, {QStringLiteral("modal"), true}};
    Detail::PortalFilterList filters;
    for (const Filter& f : parseNameFilters(m_nameFilters)) {
        Detail::PortalFilter pf;
        pf.name = f.name;
        for (const QString& p : f.patterns) pf.rules << Detail::PortalFilterRule{0u, p};
        filters << pf;
    }
    if (!filters.isEmpty()) options.insert(QStringLiteral("filters"), QVariant::fromValue(filters));

    QString method = QStringLiteral("OpenFile");
    if (m_fileMode == SaveFile) {
        method = QStringLiteral("SaveFile");
        const QString path = localPathOf(m_currentFile);
        if (!path.isEmpty()) {
            const QFileInfo fi(path);
            options.insert(QStringLiteral("current_name"), fi.fileName());
            if (QFileInfo(fi.absolutePath()).isDir()) {
                QByteArray folder = QFile::encodeName(fi.absolutePath());
                folder.append('\0');
                options.insert(QStringLiteral("current_folder"), folder);
            }
        }
    } else if (m_fileMode == OpenFiles) {
        options.insert(QStringLiteral("multiple"), true);
    } else if (m_fileMode == SelectFolder) {
        options.insert(QStringLiteral("directory"), true);
    }

    QDBusMessage msg = QDBusMessage::createMethodCall(kPortalService, kPortalPath,
                                                      QStringLiteral("org.freedesktop.portal.FileChooser"), method);
    msg << QString() << m_title << options;
    auto* watcher = new QDBusPendingCallWatcher(bus.asyncCall(msg, 5000), this);
    const quint64 gen = m_generation;
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, gen]() {
        watcher->deleteLater();
        if (gen != m_generation || !m_busy) return;
        QDBusPendingReply<QDBusObjectPath> reply = *watcher;
        if (reply.isError()) {
            disconnectPortal();
            if (!Fs::FileUtils::sandboxed() && openWithHelper()) return;
            setBusy(false);
            if (const auto& handler = unavailableHandler()) handler();
            emit rejected();
            return;
        }
        const QString handle = reply.value().path();
        if (!handle.isEmpty() && handle != m_requestPath) {
            disconnectPortal();
            m_requestPath = handle;
            QDBusConnection::sessionBus().connect(kPortalService, m_requestPath, kRequestInterface,
                                                  QStringLiteral("Response"), this,
                                                  SLOT(onPortalResponse(uint,QVariantMap)));
        }
    });
    return true;
}

void FilePicker::onPortalResponse(uint response, const QVariantMap& results) {
    if (!m_busy) return;
    disconnectPortal();
    if (response != 0) {
        setBusy(false);
        emit rejected();
        return;
    }
    QList<QUrl> urls;
    for (const QString& uri : results.value(QStringLiteral("uris")).toStringList()) urls << QUrl(uri);
    finish(urls);
}

bool FilePicker::openWithHelper() {
    const QString zenity = QStandardPaths::findExecutable(QStringLiteral("zenity"));
    const QString kdialog = QStandardPaths::findExecutable(QStringLiteral("kdialog"));
    if (zenity.isEmpty() && kdialog.isEmpty()) return false;
    const QList<Filter> filters = parseNameFilters(m_nameFilters);
    const QString current = localPathOf(m_currentFile);
    auto* proc = new QProcess(this);
    m_helper = proc;
    proc->setProcessEnvironment(Fs::FileUtils::launchEnvironment(QProcessEnvironment::systemEnvironment()));
    if (!zenity.isEmpty()) {
        proc->setProgram(zenity);
        proc->setArguments(zenityArguments(m_fileMode, m_title, filters, current));
    } else {
        proc->setProgram(kdialog);
        proc->setArguments(kdialogArguments(m_fileMode, m_title, filters, current));
    }
    const quint64 gen = m_generation;
    connect(proc, &QProcess::finished, this, [this, proc, gen](int code, QProcess::ExitStatus status) {
        proc->deleteLater();
        if (gen != m_generation) return;
        if (status == QProcess::NormalExit && code == 0) {
            finish(urlsFromLines(proc->readAllStandardOutput()));
            return;
        }
        setBusy(false);
        emit rejected();
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc, gen](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || gen != m_generation) return;
        proc->deleteLater();
        setBusy(false);
        if (const auto& handler = unavailableHandler()) handler();
        emit rejected();
    });
    proc->start();
    return true;
}

void FilePicker::finish(const QList<QUrl>& urls) {
    setBusy(false);
    if (urls.isEmpty()) {
        emit rejected();
        return;
    }
    m_selectedFiles = urls;
    emit selectionChanged();
    emit accepted();
}

void FilePicker::disconnectPortal() {
    if (m_requestPath.isEmpty()) return;
    QDBusConnection::sessionBus().disconnect(kPortalService, m_requestPath, kRequestInterface,
                                             QStringLiteral("Response"), this,
                                             SLOT(onPortalResponse(uint,QVariantMap)));
    m_requestPath.clear();
}

}
