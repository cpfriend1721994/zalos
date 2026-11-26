#pragma once

#include <QProcessEnvironment>
#include <QString>

namespace Zalos::Core::Fs {

class FileUtils {
public:
    static bool openLocalPath(const QString& path);

    static bool sandboxed();

    static QProcessEnvironment launchEnvironment(const QProcessEnvironment& env);

    static QString urlLauncher();

    static bool openUrlDetached(const QString& url, const QString& failureFile);
};

}
