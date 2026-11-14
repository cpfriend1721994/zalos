#include "sound_engine.h"

#include <QDir>
#include <QFile>
#include <QAudioOutput>
#include <QDirIterator>
#include <QMediaPlayer>
#include <QProcess>
#include <QSettings>
#include <QSoundEffect>
#include <QVariantMap>
#include <QStandardPaths>
#include <QUrl>
#include <QtEndian>

#include <cmath>
#include <vector>

namespace Zalos::Core::Platform {

namespace {

constexpr int kSampleRate = 44100;

struct Note {
    double freq;
    double startMs;
    double durMs;
    double gain;
    double decay;
    double harmonic;
};

struct Recipe {
    const char* key;
    double totalMs;
    std::vector<Note> notes;
};

const std::vector<Recipe>& recipes() {
    static const std::vector<Recipe> all = {
        {"pop", 220, {{880, 0, 160, 0.8, 18, 0.1}, {1320, 20, 140, 0.4, 22, 0.0}}},
        {"bubble", 300, {{520, 0, 110, 0.6, 14, 0.0}, {780, 90, 110, 0.6, 14, 0.0}, {1040, 180, 120, 0.5, 14, 0.0}}},
        {"chime", 900, {{1046.5, 0, 800, 0.6, 4, 0.3}, {1568, 120, 700, 0.4, 4, 0.3}}},
        {"bell", 1200, {{659.3, 0, 1100, 0.7, 3, 0.6}, {1318.5, 0, 900, 0.25, 4, 0.2}}},
        {"digital", 360, {{1500, 0, 80, 0.5, 6, 0.0}, {1500, 140, 80, 0.5, 6, 0.0}, {2000, 260, 90, 0.5, 6, 0.0}}},
        {"positive", 520, {{523.3, 0, 180, 0.6, 8, 0.2}, {659.3, 150, 180, 0.6, 8, 0.2}, {784, 300, 220, 0.6, 7, 0.2}}},
        {"confirm", 380, {{784, 0, 160, 0.6, 10, 0.2}, {1046.5, 160, 220, 0.6, 9, 0.2}}},
        {"magic", 800, {{1318.5, 0, 300, 0.4, 7, 0.1}, {1568, 120, 300, 0.4, 7, 0.1},
                        {1975.5, 240, 300, 0.4, 7, 0.1}, {2637, 360, 420, 0.3, 6, 0.1}}},
        {"classic", 3000, {{440, 0, 400, 0.5, 1, 0.5}, {480, 0, 400, 0.5, 1, 0.5},
                           {440, 600, 400, 0.5, 1, 0.5}, {480, 600, 400, 0.5, 1, 0.5}}},
        {"marimba", 2400, {{784, 0, 260, 0.7, 9, 0.3}, {1046.5, 200, 260, 0.7, 9, 0.3}, {1318.5, 400, 260, 0.7, 9, 0.3},
                           {1046.5, 600, 260, 0.7, 9, 0.3}, {1568, 800, 400, 0.7, 7, 0.3}}},
        {"digital_ring", 2000, {{1200, 0, 120, 0.5, 2, 0.0}, {1600, 150, 120, 0.5, 2, 0.0},
                                {1200, 300, 120, 0.5, 2, 0.0}, {1600, 450, 120, 0.5, 2, 0.0}}},
        {"soft", 3200, {{523.3, 0, 900, 0.5, 2.5, 0.2}, {659.3, 300, 900, 0.5, 2.5, 0.2}, {784, 600, 1200, 0.5, 2, 0.2}}},
    };
    return all;
}

const Recipe* findRecipe(const QString& key) {
    for (const auto& r : recipes()) {
        if (key == QLatin1String(r.key)) return &r;
    }
    return nullptr;
}

QByteArray renderWav(const Recipe& recipe) {
    const int frames = int(recipe.totalMs * kSampleRate / 1000.0);
    std::vector<double> mix(frames, 0.0);
    for (const Note& n : recipe.notes) {
        const int start = int(n.startMs * kSampleRate / 1000.0);
        const int len = int(n.durMs * kSampleRate / 1000.0);
        for (int i = 0; i < len && start + i < frames; ++i) {
            const double t = double(i) / kSampleRate;
            const double attack = std::min(1.0, i / (0.004 * kSampleRate));
            const double release = std::min(1.0, double(len - i) / (0.01 * kSampleRate));
            const double env = attack * release * std::exp(-n.decay * t);
            const double w = 2.0 * M_PI * n.freq * t;
            const double wave = std::sin(w) + n.harmonic * (0.5 * std::sin(2 * w) + 0.25 * std::sin(3 * w));
            mix[start + i] += n.gain * env * wave / (1.0 + 0.75 * n.harmonic);
        }
    }
    double peak = 0.0;
    for (double v : mix) peak = std::max(peak, std::abs(v));
    const double scale = peak > 0.0 ? 0.6 / peak : 0.0;

    QByteArray pcm(frames * 2, Qt::Uninitialized);
    for (int i = 0; i < frames; ++i) {
        qToLittleEndian<qint16>(qint16(std::lround(mix[i] * scale * 32767.0)), pcm.data() + i * 2);
    }

    QByteArray wav;
    auto u32 = [&](quint32 v) { char b[4]; qToLittleEndian(v, b); wav.append(b, 4); };
    auto u16 = [&](quint16 v) { char b[2]; qToLittleEndian(v, b); wav.append(b, 2); };
    wav.append("RIFF"); u32(36 + pcm.size()); wav.append("WAVE");
    wav.append("fmt "); u32(16); u16(1); u16(1); u32(kSampleRate); u32(kSampleRate * 2); u16(2); u16(16);
    wav.append("data"); u32(pcm.size());
    wav.append(pcm);
    return wav;
}

}

SoundEngine::SoundEngine(QObject* parent)
    : QObject(parent) {
}

QSoundEffect* SoundEngine::ensureEffect() {
    if (!m_effect) {
        m_effect = new QSoundEffect(this);
        m_effect->setVolume(0.8);
        connect(m_effect, &QSoundEffect::playingChanged, this, [this]() { emit playingChanged(m_effect->isPlaying()); });
    }
    return m_effect;
}

QStringList SoundEngine::notificationKeys() {
    return {"pop", "bubble", "chime", "bell", "digital", "positive", "confirm", "magic"};
}

QStringList SoundEngine::ringtoneKeys() {
    return {"classic", "marimba", "digital_ring", "soft"};
}

int SoundEngine::durationMs(const QString& key) {
    const Recipe* recipe = findRecipe(key);
    return recipe ? int(recipe->totalMs) : 0;
}

QString SoundEngine::renderToFile(const QString& key) {
    const Recipe* recipe = findRecipe(key);
    if (!recipe) return {};
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/sounds";
    QDir().mkpath(dir);
    const QString path = dir + "/" + key + ".wav";
    if (!QFile::exists(path)) {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) return {};
        file.write(renderWav(*recipe));
    }
    return path;
}

QVariantList SoundEngine::systemSounds() {
    static const QStringList extensions = {"*.oga", "*.ogg", "*.wav", "*.flac", "*.mp3"};
    QVariantList out;
    QStringList seen;
    const QStringList roots = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, "sounds",
                                                        QStandardPaths::LocateDirectory);
    auto pretty = [](const QString& file) {
        QString name = QFileInfo(file).completeBaseName();
        name.replace('-', ' ').replace('_', ' ');
        if (!name.isEmpty()) name[0] = name[0].toUpper();
        return name;
    };
    auto add = [&](const QString& path, const QString& theme) {
        const QString base = QFileInfo(path).completeBaseName();
        if (base.startsWith("audio-channel-") || base == "audio-test-signal" || seen.contains(path)) return;
        seen << path;
        out.append(QVariantMap{{"key", "system:" + path},
                               {"label", QStringLiteral("%1 · %2").arg(theme, pretty(path))}});
    };
    for (const QString& root : roots) {
        const QDir rootDir(root);
        for (const QString& f : rootDir.entryList(extensions, QDir::Files, QDir::Name)) add(rootDir.filePath(f), "Của bạn");
        for (const QString& themeName : rootDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QDir themeDir(rootDir.filePath(themeName));
            if (!themeDir.exists("index.theme")) continue;
            const QString title = QSettings(themeDir.filePath("index.theme"), QSettings::IniFormat)
                                      .value("Sound Theme/Name", themeName).toString();
            QDirIterator it(themeDir.path(), extensions, QDir::Files, QDirIterator::Subdirectories);
            QStringList files;
            while (it.hasNext()) files << it.next();
            files.sort();
            for (const QString& f : std::as_const(files)) add(f, title);
        }
    }
    return out;
}

bool SoundEngine::isValidKey(const QString& kind, const QString& key) {
    if (isSystemKey(key)) return QFile::exists(key.mid(7));
    return kind == QLatin1String("ringtone") ? ringtoneKeys().contains(key) : notificationKeys().contains(key);
}

QMediaPlayer* SoundEngine::ensurePlayer() {
    if (!m_player) {
        m_player = new QMediaPlayer(this);
        auto* output = new QAudioOutput(m_player);
        output->setVolume(0.8f);
        m_player->setAudioOutput(output);
        connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState state) {
            emit playingChanged(state == QMediaPlayer::PlayingState);
        });
    }
    return m_player;
}

QString SoundEngine::externalPlayer() {
    for (const char* tool : {"pw-play", "paplay", "canberra-gtk-play"}) {
        const QString path = QStandardPaths::findExecutable(QString::fromLatin1(tool));
        if (!path.isEmpty()) return path;
    }
    return {};
}

void SoundEngine::startExternal() {
    const QString tool = externalPlayer();
    if (!m_process) {
        m_process = new QProcess(this);
        m_process->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        m_process->setStandardOutputFile(QProcess::nullDevice());
        connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus status) {
            if (m_externalStopping) {
                m_externalStopping = false;
                emit playingChanged(false);
                return;
            }
            const bool healthy = status == QProcess::NormalExit && exitCode == 0 && m_externalStarted.elapsed() >= 200;
            if (healthy && m_externalLoop && !m_externalFile.isEmpty()) {
                startExternal();
                return;
            }
            if (!healthy && !m_externalFile.isEmpty()) {
                playWithMediaPlayer(m_externalFile, m_externalLoop);
                return;
            }
            emit playingChanged(false);
        });
        connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error != QProcess::FailedToStart || m_externalFile.isEmpty()) return;
            playWithMediaPlayer(m_externalFile, m_externalLoop);
        });
    }
    QStringList args;
    if (tool.endsWith(QLatin1String("canberra-gtk-play"))) args << "-f" << m_externalFile;
    else args << m_externalFile;
    m_externalStopping = false;
    m_externalStarted.start();
    m_process->start(tool, args);
    emit playingChanged(true);
}

void SoundEngine::playWithMediaPlayer(const QString& file, bool loop) {
    ensurePlayer();
    m_player->stop();
    m_player->setSource(QUrl::fromLocalFile(file));
    m_player->setLoops(loop ? QMediaPlayer::Infinite : 1);
    m_player->play();
}

void SoundEngine::play(const QString& kind, const QString& key, bool loop) {
    if (isSystemKey(key) && QFile::exists(key.mid(7))) {
        if (m_effect) m_effect->stop();
        if (m_player) m_player->stop();
        if (!externalPlayer().isEmpty()) {
            if (m_process && m_process->state() != QProcess::NotRunning) {
                m_externalLoop = false;
                m_externalStopping = true;
                m_process->kill();
                m_process->waitForFinished(500);
            }
            m_externalFile = key.mid(7);
            m_externalLoop = loop;
            startExternal();
            return;
        }
        playWithMediaPlayer(key.mid(7), loop);
        return;
    }
    if (m_player) m_player->stop();
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_externalLoop = false;
        m_externalStopping = true;
        m_process->kill();
    }
    QString effective = key;
    if (!findRecipe(effective)) effective = kind == "ringtone" ? QStringLiteral("classic") : QStringLiteral("pop");
    const QString path = renderToFile(effective);
    if (path.isEmpty()) return;
    ensureEffect();
    m_effect->stop();
    m_effect->setSource(QUrl::fromLocalFile(path));
    m_effect->setLoopCount(loop ? QSoundEffect::Infinite : 1);
    m_effect->play();
}

void SoundEngine::stop() {
    if (m_effect) m_effect->stop();
    if (m_player) m_player->stop();
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_externalLoop = false;
        m_externalFile.clear();
        m_externalStopping = true;
        m_process->kill();
    }
}

bool SoundEngine::isPlaying() const {
    return (m_effect && m_effect->isPlaying()) || (m_player && m_player->playbackState() == QMediaPlayer::PlayingState)
           || (m_process && m_process->state() != QProcess::NotRunning);
}

}
