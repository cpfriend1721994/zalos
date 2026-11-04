#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QVideoFrame>

class ISVCDecoder;
class ISVCEncoder;

namespace Zalos::Core::Call {

class OpenH264 {
public:
    static OpenH264& instance();

    bool available() const { return m_createDecoder != nullptr; }
    QString unavailableReason() const { return m_error; }
    QString version() const { return m_version; }

    ISVCDecoder* createDecoder() const;
    void destroyDecoder(ISVCDecoder* decoder) const;
    ISVCEncoder* createEncoder() const;
    void destroyEncoder(ISVCEncoder* encoder) const;

private:
    OpenH264();
    void* m_handle{nullptr};
    QString m_error;
    QString m_version;
    using CreateDecoderFn = long (*)(ISVCDecoder**);
    using DestroyDecoderFn = void (*)(ISVCDecoder*);
    using CreateEncoderFn = int (*)(ISVCEncoder**);
    using DestroyEncoderFn = void (*)(ISVCEncoder*);
    CreateDecoderFn m_createDecoder{nullptr};
    DestroyDecoderFn m_destroyDecoder{nullptr};
    CreateEncoderFn m_createEncoder{nullptr};
    DestroyEncoderFn m_destroyEncoder{nullptr};
};

class H264Decoder {
public:
    H264Decoder();
    ~H264Decoder();
    H264Decoder(const H264Decoder&) = delete;
    H264Decoder& operator=(const H264Decoder&) = delete;

    bool isOpen() const { return m_decoder != nullptr; }
    QVideoFrame decode(const QByteArray& accessUnit, bool* damaged = nullptr);

private:
    ISVCDecoder* m_decoder{nullptr};
};

class H264Encoder {
public:
    H264Encoder() = default;
    ~H264Encoder();
    H264Encoder(const H264Encoder&) = delete;
    H264Encoder& operator=(const H264Encoder&) = delete;

    bool open(QSize size, int fps, int bitrate, bool screenContent = false);
    void close();
    bool isOpen() const { return m_encoder != nullptr; }
    QSize size() const { return m_size; }

    QByteArray encodeI420(const QByteArray& i420, qint64 timestampMs, bool forceKey, bool* isKey = nullptr);

private:
    ISVCEncoder* m_encoder{nullptr};
    QSize m_size;
};

QByteArray frameToI420(const QVideoFrame& frame, QSize size);

}
