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

// Notification sounds and ringtones synthesised at runtime (no third-party audio assets).
// Each sound is rendered once to a WAV file in the cache directory and played with QSoundEffect.
class SoundEngine : public QObject {
    Q_OBJECT

public:
    explicit SoundEngine(QObject* parent = nullptr);

    static QStringList notificationKeys();
    static QStringList ringtoneKeys();

    // Sounds of the desktop's freedesktop sound themes (`$XDG_DATA_DIRS/sounds/<theme>/stereo/*`, e.g. KDE
    // "Ocean", "freedesktop") and loose files in `~/.local/share/sounds`: [{key: "system:<path>", label}].
    // Speaker test sounds are left out. Played with QMediaPlayer (.oga/.ogg/.wav/.flac/.mp3).
    static QVariantList systemSounds();
    static bool isSystemKey(const QString& key) { return key.startsWith(QLatin1String("system:")); }
    // True for a built-in key or a system key whose file still exists.
    static bool isValidKey(const QString& kind, const QString& key);

    // Plays a sound once (notification) or looped (ringtone). Unknown keys fall back to the defaults.
    void play(const QString& kind, const QString& key, bool loop = false);
    void stop();
    bool isPlaying() const;

    // Length of one play of `key` in milliseconds (0 when unknown).
    static int durationMs(const QString& key);

    // Renders the WAV for `key` and returns its path (exposed for tests).
    static QString renderToFile(const QString& key);

signals:
    void playingChanged(bool playing);

private:
    QSoundEffect* ensureEffect();
    QMediaPlayer* ensurePlayer();
    // pw-play / paplay / canberra-gtk-play: how the desktop itself plays theme sounds (no FFmpeg log noise on
    // the terminal, the user's audio routing); empty when none is installed.
    static QString externalPlayer();
    void startExternal();
    QProcess* m_process{nullptr};
    QString m_externalFile;
    bool m_externalLoop{false};
    QSoundEffect* m_effect{nullptr}; // created lazily
    QMediaPlayer* m_player{nullptr}; // system sounds, created lazily
};

} // namespace Zalos::Core::Platform
