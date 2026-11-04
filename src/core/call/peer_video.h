#pragma once

#include <QByteArray>
#include <QObject>
#include <QVideoFrame>
#include <map>
#include <memory>

namespace Zalos::Core::Call {

class H264Decoder;

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
    void keyFrameNeeded(const QString& why, int src);

private:
    struct Stream {
        std::unique_ptr<H264Decoder> decoder;
        bool waitingForKey{false};
    };
    std::map<int, Stream> m_streams;
};

}
