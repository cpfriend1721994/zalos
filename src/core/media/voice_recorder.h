#pragma once

#include <QtQml/qqmlregistration.h>

#include <QObject>
#include <QString>
#include <QTimer>

class QAudioInput;
class QMediaCaptureSession;
class QMediaRecorder;

namespace Zalos::Core::Media {

class VoiceRecorder : public QObject {
    Q_OBJECT
    QML_ANONYMOUS
    Q_PROPERTY(bool available READ available NOTIFY availabilityChanged FINAL)
    Q_PROPERTY(bool recording READ recording NOTIFY recordingChanged FINAL)
    Q_PROPERTY(int durationMs READ durationMs NOTIFY durationChanged FINAL)
    Q_PROPERTY(QString unavailableReason READ unavailableReason NOTIFY availabilityChanged FINAL)

public:
    static constexpr int kMaxDurationMs = 5 * 60 * 1000;
    static constexpr int kMinDurationMs = 700;

    explicit VoiceRecorder(QObject* parent = nullptr);
    ~VoiceRecorder() override;

    bool available() const { ensurePassiveProbe(); return m_unavailableReason.isEmpty(); }
    bool recording() const { return m_recording; }
    int durationMs() const { return m_durationMs; }
    QString unavailableReason() const { ensurePassiveProbe(); return m_unavailableReason; }

    Q_INVOKABLE void refreshAvailability();
    Q_INVOKABLE bool start();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void cancel();

signals:
    void availabilityChanged();
    void recordingChanged();
    void durationChanged();
    void finished(const QString& filePath, int durationMs);
    void failed(const QString& message);
    void startRefused();

private:
    struct Clip {
        quint64 id{0};
        QString path;
        int durationMs{0};
        bool keep{false};
    };

    void finish(bool keep);
    void settleStopped();
    void ensurePassiveProbe() const;

    QMediaCaptureSession* m_session{nullptr};
    QAudioInput* m_input{nullptr};
    QMediaRecorder* m_recorder{nullptr};
    QTimer m_tick;
    Clip m_active;
    Clip m_stopping;
    quint64 m_clipSeq{0};
    mutable QString m_unavailableReason;
    mutable bool m_probed{false};
    qint64 m_startedAt{0};
    int m_durationMs{0};
    bool m_recording{false};
};

}
