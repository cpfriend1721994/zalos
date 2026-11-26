#include "voice_recorder.h"
#include "platform/multimedia_support.h"

#include <QAudioDevice>
#include <QAudioInput>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMediaCaptureSession>
#include <QMediaDevices>
#include <QMediaFormat>
#include <QMediaRecorder>
#include <QStandardPaths>
#include <QUrl>

#include <utility>

namespace Zalos::Core::Media {

namespace {

QMediaFormat aacFormat() {
    QMediaFormat format(QMediaFormat::AAC);
    format.setAudioCodec(QMediaFormat::AudioCodec::AAC);
    return format;
}

bool hasCaptureDeviceNode() {
    const QDir snd(QStringLiteral("/dev/snd"));
    return snd.exists() && !snd.entryList({QStringLiteral("pcm*c")}, QDir::System).isEmpty();
}

}

VoiceRecorder::VoiceRecorder(QObject* parent) : QObject(parent) {
    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, [this]() {
        m_durationMs = int(QDateTime::currentMSecsSinceEpoch() - m_startedAt);
        emit durationChanged();
        if (m_durationMs >= kMaxDurationMs) stop();
    });
}

VoiceRecorder::~VoiceRecorder() {
    if (m_recording) finish(false);
    if (m_stopping.id != 0) QFile::remove(m_stopping.path);
    if (m_active.id != 0) QFile::remove(m_active.path);
}

void VoiceRecorder::ensurePassiveProbe() const {
    if (m_probed) return;
    m_probed = true;
    if (!hasCaptureDeviceNode()) {
        m_unavailableReason = QStringLiteral("no_microphone");
    }
}

void VoiceRecorder::refreshAvailability() {
    m_probed = true;
    QString reason;
    if (!Platform::MultimediaSupport::available()) {
        reason = QStringLiteral("no_multimedia");
    } else if (QMediaDevices::audioInputs().isEmpty()) {
        reason = QStringLiteral("no_microphone");
    } else if (!aacFormat().isSupported(QMediaFormat::Encode)) {
        reason = QStringLiteral("no_aac_encoder");
    }
    if (reason != m_unavailableReason) {
        m_unavailableReason = reason;
        emit availabilityChanged();
    }
}

bool VoiceRecorder::start() {
    if (m_recording) return true;
    refreshAvailability();
    if (!available()) {
        emit failed(m_unavailableReason);
        return false;
    }
    if (!m_session) {
        m_session = new QMediaCaptureSession(this);
        m_input = new QAudioInput(this);
        m_recorder = new QMediaRecorder(this);
        m_session->setAudioInput(m_input);
        m_session->setRecorder(m_recorder);
        connect(m_recorder, &QMediaRecorder::errorOccurred, this, [this](QMediaRecorder::Error, const QString& text) {
            if (!m_recording) return;
            finish(false);
            emit failed(text);
        });
        connect(m_recorder, &QMediaRecorder::recorderStateChanged, this, [this](QMediaRecorder::RecorderState state) {
            if (state == QMediaRecorder::StoppedState) settleStopped();
        });
    }
    if (m_recorder->recorderState() != QMediaRecorder::StoppedState) {
        emit startRefused();
        return false;
    }
    settleStopped();
    m_input->setDevice(QMediaDevices::defaultAudioInput());
    m_recorder->setMediaFormat(aacFormat());
    m_recorder->setQuality(QMediaRecorder::NormalQuality);
    m_recorder->setAudioChannelCount(1);
    m_recorder->setAudioSampleRate(16000);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/voice");
    QDir().mkpath(dir);
    m_active = Clip{++m_clipSeq,
                    dir + QStringLiteral("/voice-%1-%2.aac").arg(QDateTime::currentMSecsSinceEpoch()).arg(m_clipSeq),
                    0, true};
    m_recorder->setOutputLocation(QUrl::fromLocalFile(m_active.path));
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    m_durationMs = 0;
    m_recording = true;
    m_recorder->record();
    if (m_recorder->recorderState() == QMediaRecorder::StoppedState) {
        QFile::remove(m_active.path);
        m_active = Clip{};
        m_recording = false;
        m_tick.stop();
        emit startRefused();
        emit recordingChanged();
        return false;
    }
    m_tick.start();
    emit durationChanged();
    emit recordingChanged();
    return true;
}

void VoiceRecorder::stop() { finish(true); }

void VoiceRecorder::cancel() { finish(false); }

void VoiceRecorder::finish(bool keep) {
    if (!m_recording) return;
    m_tick.stop();
    m_durationMs = int(QDateTime::currentMSecsSinceEpoch() - m_startedAt);
    m_active.durationMs = m_durationMs;
    m_active.keep = keep;
    m_stopping = std::exchange(m_active, Clip{});
    m_recording = false;
    emit recordingChanged();
    if (!m_recorder) {
        settleStopped();
        return;
    }
    m_recorder->stop();
    if (m_recorder->recorderState() == QMediaRecorder::StoppedState) settleStopped();
}

void VoiceRecorder::settleStopped() {
    if (m_stopping.id == 0) return;
    const Clip clip = std::exchange(m_stopping, Clip{});
    if (clip.keep && clip.durationMs >= kMinDurationMs && QFile::exists(clip.path)) emit finished(clip.path, clip.durationMs);
    else QFile::remove(clip.path);
}

}
