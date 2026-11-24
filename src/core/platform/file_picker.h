#pragma once

#include <QList>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

#include <functional>

class QProcess;

namespace Zalos::Core::Platform {

class FilePicker : public QObject {
    Q_OBJECT
    QML_NAMED_ELEMENT(ZFilePicker)
    Q_PROPERTY(QString title READ title WRITE setTitle NOTIFY titleChanged FINAL)
    Q_PROPERTY(QStringList nameFilters READ nameFilters WRITE setNameFilters NOTIFY nameFiltersChanged FINAL)
    Q_PROPERTY(FileMode fileMode READ fileMode WRITE setFileMode NOTIFY fileModeChanged FINAL)
    Q_PROPERTY(QUrl currentFile READ currentFile WRITE setCurrentFile NOTIFY currentFileChanged FINAL)
    Q_PROPERTY(QUrl selectedFile READ selectedFile NOTIFY selectionChanged FINAL)
    Q_PROPERTY(QList<QUrl> selectedFiles READ selectedFiles NOTIFY selectionChanged FINAL)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged FINAL)

public:
    enum FileMode { OpenFile, OpenFiles, SaveFile };
    Q_ENUM(FileMode)

    struct Filter {
        QString name;
        QStringList patterns;
    };

    explicit FilePicker(QObject* parent = nullptr);
    ~FilePicker() override;

    QString title() const { return m_title; }
    void setTitle(const QString& title);
    QStringList nameFilters() const { return m_nameFilters; }
    void setNameFilters(const QStringList& filters);
    FileMode fileMode() const { return m_fileMode; }
    void setFileMode(FileMode mode);
    QUrl currentFile() const { return m_currentFile; }
    void setCurrentFile(const QUrl& file);
    QUrl selectedFile() const { return m_selectedFiles.isEmpty() ? QUrl() : m_selectedFiles.first(); }
    QList<QUrl> selectedFiles() const { return m_selectedFiles; }
    bool busy() const { return m_busy; }

    Q_INVOKABLE void open();

    static QList<Filter> parseNameFilters(const QStringList& nameFilters);
    static QList<QUrl> urlsFromLines(const QByteArray& output);
    static QStringList zenityArguments(FileMode mode, const QString& title, const QList<Filter>& filters,
                                       const QString& currentPath);
    static QStringList kdialogArguments(FileMode mode, const QString& title, const QList<Filter>& filters,
                                        const QString& currentPath);
    static void setUnavailableHandler(std::function<void()> handler);

signals:
    void titleChanged();
    void nameFiltersChanged();
    void fileModeChanged();
    void currentFileChanged();
    void selectionChanged();
    void busyChanged();
    void accepted();
    void rejected();

private slots:
    void onPortalResponse(uint response, const QVariantMap& results);

private:
    bool openWithPortal();
    bool openWithHelper();
    void finish(const QList<QUrl>& urls);
    void setBusy(bool busy);
    void disconnectPortal();

    QString m_title;
    QStringList m_nameFilters;
    FileMode m_fileMode = OpenFile;
    QUrl m_currentFile;
    QList<QUrl> m_selectedFiles;
    bool m_busy = false;
    QString m_requestPath;
    QPointer<QProcess> m_helper;
    quint64 m_generation = 0;
};

}
