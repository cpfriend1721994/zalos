// SPDX-License-Identifier: MIT
#include <QTest>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QDirIterator>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <functional>

namespace {

const QString kBareControls = QStringLiteral("Button|TextField|ComboBox|TextArea|Switch|CheckBox|RadioButton|SpinBox|Slider|ToolButton|RoundButton");

QStringList readLines(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'));
}

QStringList blockFrom(const QStringList& lines, int start) {
    int depth = 0;
    QStringList out;
    for (int j = start; j < lines.size(); ++j) {
        depth += lines[j].count(QLatin1Char('{')) - lines[j].count(QLatin1Char('}'));
        out << lines[j];
        if (depth <= 0 && j > start) break;
    }
    return out;
}

QStringList qmlFiles(const QString& dir) {
    return QDir(dir).entryList({QStringLiteral("*.qml")}, QDir::Files, QDir::Name);
}

}

class TestQmlGates : public QObject {
    Q_OBJECT

private slots:
    void qmlStyle();
    void qmlAotCoverage();
    void uiVisualDiff();
};

void TestQmlGates::qmlStyle() {
    const QString qmlDir = QStringLiteral(ZALOS_SOURCE_DIR) + QStringLiteral("/src/qml");
    const QStringList iconLines = readLines(qmlDir + QStringLiteral("/ZIcon.qml"));
    QVERIFY2(!iconLines.isEmpty(), "src/qml/ZIcon.qml not readable");

    QSet<QString> icons;
    const QRegularExpression caseRe(QStringLiteral(R"re(case "([a-z_0-9]+)")re"));
    for (const QString& line : iconLines) {
        for (auto it = caseRe.globalMatch(line); it.hasNext();) icons.insert(it.next().captured(1));
    }

    const QRegularExpression bareControl(QStringLiteral(R"re(^\s*()re") + kBareControls + QStringLiteral(R"re()\s*\{)re"));
    const QRegularExpression zIcon(QStringLiteral(R"re(\bZIcon\b)re"));
    const QRegularExpression zIconOpen(QStringLiteral(R"re(\bZIcon\s*\{)re"));
    const QRegularExpression iconProp(QStringLiteral(R"re(^\s*(name|iconName|icon)\s*:)re"));
    const QRegularExpression iconAssign(QStringLiteral(R"re(\b(name|iconName|icon)\s*:\s*([^;}\n]+))re"));
    const QRegularExpression stringCompare(QStringLiteral(R"re([!=]==\s*"[^"]*")re"));
    const QRegularExpression quotedIcon(QStringLiteral(R"re("([a-z_0-9]+)")re"));
    const QRegularExpression zaloBlueLight(QStringLiteral(R"re(^\s*color:.*zaloBlueLight)re"));
    const QRegularExpression zaloBlueColor(QStringLiteral(R"re((?<!border\.)color:\s*Theme\.zaloBlue\s*(\}|$))re"));
    const QRegularExpression shortcut(QStringLiteral(R"re(\bsequence:\s*"([^"]+)")re"));
    const QRegularExpression hoverEnd(QStringLiteral(R"re(hoverColor|containsMouse|hovered)re"));

    QStringList errors;
    for (const QString& name : qmlFiles(qmlDir)) {
        const QStringList lines = readLines(qmlDir + QLatin1Char('/') + name);
        for (int i = 0; i < lines.size(); ++i) {
            const QString& line = lines[i];
            const QString where = QStringLiteral("%1:%2").arg(name).arg(i + 1);

            const QRegularExpressionMatch control = bareControl.match(line);
            if (control.hasMatch() && !name.startsWith(QLatin1Char('Z'))) {
                bool hasBackground = false;
                for (const QString& l : blockFrom(lines, i)) hasBackground |= l.contains(QStringLiteral("background:"));
                if (!hasBackground)
                    errors << QStringLiteral("%1: R1 bare %2 (use ZButton/ZTextField/ZComboBox/ZSwitch/ZCheckBox/ZRadio)")
                                  .arg(where, control.captured(1));
            }

            if (zaloBlueLight.match(line).hasMatch()) {
                const QStringList blk = blockFrom(lines, i);
                for (int k = 1; k < blk.size(); ++k) {
                    if (zaloBlueColor.match(blk[k]).hasMatch())
                        errors << QStringLiteral("%1:%2: R2 Theme.zaloBlue on a zaloBlueLight background (use Theme.zaloBlueTonalText)")
                                      .arg(name).arg(i + k + 1);
                }
            }

            if (zIcon.match(line).hasMatch() || iconProp.match(line).hasMatch()) {
                bool inIcon = zIcon.match(line).hasMatch();
                for (int k = qMax(0, i - 3); k < i && !inIcon; ++k) inIcon = zIconOpen.match(lines[k]).hasMatch();
                for (auto it = iconAssign.globalMatch(line); it.hasNext();) {
                    const QRegularExpressionMatch assign = it.next();
                    const QString prop = assign.captured(1);
                    if (prop == QLatin1String("name") && !inIcon) continue;
                    QString value = assign.captured(2);
                    value.remove(stringCompare);
                    for (auto lit = quotedIcon.globalMatch(value); lit.hasNext();) {
                        const QString literal = lit.next().captured(1);
                        if (!icons.contains(literal))
                            errors << QStringLiteral("%1: R3 unknown icon \"%2\" (ZIcon draws: see its case list)").arg(where, literal);
                    }
                }
            }

            const QRegularExpressionMatch sequence = shortcut.match(line);
            if (sequence.hasMatch() && sequence.captured(1) != QLatin1String("Esc"))
                errors << QStringLiteral("%1: R4 literal shortcut \"%2\" (add it to ShortcutMap)").arg(where, sequence.captured(1));

            if (line.contains(QStringLiteral("\"transparent\"")) && line.contains(QLatin1Char('?')) && hoverEnd.match(line).hasMatch())
                errors << QStringLiteral("%1: R5 \"transparent\" as a hover end (use ThemeManager.hoverClearColor)").arg(where);
        }
    }
    for (const QString& e : errors) qInfo("%s", qPrintable(e));
    QVERIFY2(errors.isEmpty(), qPrintable(QStringLiteral("QML style: %1 problem(s)").arg(errors.size())));
}

void TestQmlGates::qmlAotCoverage() {
#ifndef ZALOS_AOT_GATE
    QSKIP("qmlcachegen writes .aotstats from Qt 6.5; this build has no AOT gate");
#else
    QStringList files;
    QDirIterator it(QStringLiteral(ZALOS_BUILD_DIR) + QStringLiteral("/.rcc/qmlcache"), {QStringLiteral("*.aotstats")},
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) files << it.next();
    QVERIFY2(!files.isEmpty(), "no .aotstats found: build the zalos target first");

    qsizetype total = 0;
    qsizetype compiled = 0;
    QMap<QString, int> reasons;
    const QRegularExpression named(QStringLiteral(R"re((name|property) "?\w+"?)re"));
    for (const QString& path : files) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        // Qt <= 6.10: a list of modules with codegenSuccessfull; Qt 6.11+: {"formatRevision", "modules"} with codegenResult (0 = compiled).
        const QJsonArray modules = doc.isObject() ? doc.object().value(QStringLiteral("modules")).toArray() : doc.array();
        for (const QJsonValue& module : modules) {
            for (const QJsonValue& moduleFile : module.toObject().value(QStringLiteral("moduleFiles")).toArray()) {
                for (const QJsonValue& entryValue : moduleFile.toObject().value(QStringLiteral("entries")).toArray()) {
                    const QJsonObject entry = entryValue.toObject();
                    ++total;
                    const bool ok = entry.contains(QStringLiteral("codegenResult"))
                                        ? entry.value(QStringLiteral("codegenResult")).toInt(-1) == 0
                                        : entry.value(QStringLiteral("codegenSuccessfull")).toBool();
                    if (ok) {
                        ++compiled;
                    } else {
                        const QString message = entry.contains(QStringLiteral("message"))
                                                    ? entry.value(QStringLiteral("message")).toString()
                                                    : entry.value(QStringLiteral("errorMessage")).toString();
                        QString key = message;
                        key.replace(named, QStringLiteral("\\1 X"));
                        ++reasons[key.left(90)];
                    }
                }
            }
        }
    }
    const double share = 100.0 * double(compiled) / double(qMax<qsizetype>(1, total));
    qInfo("QML AOT: %lld/%lld bindings/functions compiled to C++ (%.1f%%, minimum %d%%)",
          compiled, total, share, ZALOS_AOT_MIN);
    QList<QPair<int, QString>> top;
    for (auto it2 = reasons.cbegin(); it2 != reasons.cend(); ++it2) top << qMakePair(it2.value(), it2.key());
    std::sort(top.begin(), top.end(), [](const QPair<int, QString>& a, const QPair<int, QString>& b) { return a.first > b.first; });
    for (int i = 0; i < qMin(5, int(top.size())); ++i) qInfo("  %5d  %s", top[i].first, qPrintable(top[i].second));
    QVERIFY2(share >= ZALOS_AOT_MIN, qPrintable(QStringLiteral("AOT coverage %1% is below %2%").arg(share, 0, 'f', 1).arg(ZALOS_AOT_MIN)));
#endif
}

void TestQmlGates::uiVisualDiff() {
    const QString buildDir = QStringLiteral(ZALOS_BUILD_DIR);
    QSet<QString> found;
    for (const QString& pattern : {QStringLiteral("test_chat_*.png"), QStringLiteral("test_qr_view.png"),
                                   QStringLiteral("test_call_*.png"), QStringLiteral("ui_*.png")}) {
        for (const QString& name : QDir(buildDir).entryList({pattern}, QDir::Files)) found << buildDir + QLatin1Char('/') + name;
    }
    if (found.isEmpty()) QSKIP("no screenshots found in the build directory: run test_system first");

    QStringList paths = found.values();
    std::sort(paths.begin(), paths.end());
    QStringList failures;
    for (const QString& path : paths) {
        const QString filename = QFileInfo(path).fileName();
        const QImage image(path);
        if (image.isNull()) {
            failures << QStringLiteral("ERROR reading %1").arg(filename);
            continue;
        }
        if (image.width() < 300 || image.height() < 300) {
            failures << QStringLiteral("FAIL: %1 dimensions too small: %2x%3").arg(filename).arg(image.width()).arg(image.height());
            continue;
        }
        const uchar* bits = image.constBits();
        const qsizetype size = image.sizeInBytes();
        const qsizetype step = qMax<qsizetype>(1, size / 1000);
        QSet<uchar> unique;
        for (qsizetype i = 0; i < size; i += step) unique.insert(bits[i]);
        if (unique.size() < 2)
            failures << QStringLiteral("FAIL: %1 has zero visual entropy (completely dead canvas)").arg(filename);
    }
    for (const QString& f : failures) qInfo("  %s", qPrintable(f));
    QVERIFY2(failures.isEmpty(), qPrintable(QStringLiteral("%1 of %2 screenshots failed").arg(failures.size()).arg(paths.size())));
}

QTEST_APPLESS_MAIN(TestQmlGates)
#include "test_qml_gates.moc"
