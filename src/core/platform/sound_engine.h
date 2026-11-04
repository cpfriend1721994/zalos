#pragma once

#include <QHash>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

class QAudioOutput;
class QProcess;
class QMediaPlayer;
class QSoundEffect;

namespace Zalos::Core::Platform {

class SoundEngine : public QObject {
    Q_OBJECT

public:
    explicit SoundEngine(QObject* parent = nullptr);

    static QStringList notificationKeys();
    static QStringList ringtoneKeys();

    static QVariantList systemSounds();
    static bool isSystemKey(const QString& key) { return key.startsWith(QLatin1String("system:")); }
    static bool isValidKey(const QString& kind, const QString& key);

    void play(const QString& kind, const QString& key, bool loop = false);
    void stop();
    bool isPlaying() const;

    static int durationMs(const QString& key);

    static QString renderToFile(const QString& key);

signals:
    void playingChanged(bool playing);

private:
    QSoundEffect* ensureEffect();
    QMediaPlayer* ensurePlayer();
    static QString externalPlayer();
    void startExternal();
    QProcess* m_process{nullptr};
    QString m_externalFile;
    bool m_externalLoop{false};
    QSoundEffect* m_effect{nullptr};
    QMediaPlayer* m_player{nullptr};
};

}
