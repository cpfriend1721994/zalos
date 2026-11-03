#include "voice_recorder.h"

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

} // namespace

VoiceRecorder::VoiceRecorder(QObject* parent) : QObject(parent) {
    m_tick.setInterval(200);
    connect(&m_tick, &QTimer::timeout, this, [this]() {
        m_durationMs = int(QDateTime::currentMSecsSinceEpoch() - m_startedAt);
        emit durationChanged();
        if (m_durationMs >= kMaxDurationMs) stop();
    });
    refreshAvailability();
}

VoiceRecorder::~VoiceRecorder() {
    if (m_recording) finish(false);
}

void VoiceRecorder::refreshAvailability() {
    QString reason;
    if (QMediaDevices::audioInputs().isEmpty()) reason = QStringLiteral("no_microphone");
    else if (!aacFormat().isSupported(QMediaFormat::Encode)) reason = QStringLiteral("no_aac_encoder");
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
            if (state != QMediaRecorder::StoppedState || m_recording || m_path.isEmpty()) return;
            // The file is complete only once the recorder reports Stopped.
            const QString path = std::exchange(m_path, QString());
            if (m_keepOnStop && m_durationMs >= kMinDurationMs && QFile::exists(path)) emit finished(path, m_durationMs);
            else QFile::remove(path);
        });
    }
    m_input->setDevice(QMediaDevices::defaultAudioInput());
    m_recorder->setMediaFormat(aacFormat());
    m_recorder->setQuality(QMediaRecorder::NormalQuality);
    m_recorder->setAudioChannelCount(1);
    m_recorder->setAudioSampleRate(16000);
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/voice");
    QDir().mkpath(dir);
    m_path = dir + QStringLiteral("/voice-%1.aac").arg(QDateTime::currentMSecsSinceEpoch());
    m_recorder->setOutputLocation(QUrl::fromLocalFile(m_path));
    m_keepOnStop = true;
    m_startedAt = QDateTime::currentMSecsSinceEpoch();
    m_durationMs = 0;
    m_recording = true;
    m_recorder->record();
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
    m_recording = false;
    m_keepOnStop = keep;
    emit recordingChanged();
    if (m_recorder) m_recorder->stop(); // finished() follows on StoppedState
}

} // namespace Zalos::Core::Media
