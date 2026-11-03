#include "peer_video.h"

#include "h264_codec.h"

namespace Zalos::Core::Call {

PeerVideoDecoder::PeerVideoDecoder(QObject* parent) : QObject(parent) {}

PeerVideoDecoder::~PeerVideoDecoder() = default;

void PeerVideoDecoder::decode(bool isKey, const QString& codec, const QByteArray& data, int src) {
    Q_UNUSED(codec); // the engine names avc1.* or hev1.* here; the phone sends H.264 when we offer only h264
    Stream& stream = m_streams[src];
    if (!stream.decoder) {
        // A decoder starts on a key frame (SPS/PPS/IDR); deltas before it cannot be decoded.
        if (!isKey) {
            if (!stream.waitingForKey) {
                stream.waitingForKey = true;
                emit keyFrameNeeded(QStringLiteral("start"), src);
            }
            return;
        }
        stream.decoder = std::make_unique<H264Decoder>();
        if (!stream.decoder->isOpen()) {
            stream.decoder.reset();
            return;
        }
    }
    stream.waitingForKey = false;
    bool damaged = false;
    const QVideoFrame frame = stream.decoder->decode(data, &damaged);
    // The decoder conceals damage; a fresh key frame repairs the picture (CallBridge rate-limits the asks).
    if (damaged) emit keyFrameNeeded(QStringLiteral("decode"), src);
    if (frame.isValid()) emit frameDecoded(frame, src);
}

void PeerVideoDecoder::reset() { m_streams.clear(); }

} // namespace Zalos::Core::Call
