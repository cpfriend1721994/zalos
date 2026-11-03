#pragma once

#include <QByteArray>
#include <QObject>
#include <QVideoFrame>
#include <map>
#include <memory>

namespace Zalos::Core::Call {

class H264Decoder;

// Decodes the peers' H.264 (one stream per sender `src`; 0 in a 1-1 call) on a worker thread.
// CallBridge moves it to its video thread and talks to it through queued signals only.
class PeerVideoDecoder : public QObject {
    Q_OBJECT
public:
    explicit PeerVideoDecoder(QObject* parent = nullptr);
    ~PeerVideoDecoder() override;

public slots:
    void decode(bool isKey, const QString& codec, const QByteArray& data, int src);
    void reset();

signals:
    void frameDecoded(const QVideoFrame& frame, int src);
    // The stream cannot be decoded until the next key frame (start without one, or damage).
    void keyFrameNeeded(const QString& why, int src);

private:
    struct Stream {
        std::unique_ptr<H264Decoder> decoder;
        bool waitingForKey{false};
    };
    std::map<int, Stream> m_streams;
};

} // namespace Zalos::Core::Call
