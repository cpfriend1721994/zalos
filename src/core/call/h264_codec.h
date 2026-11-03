#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QVideoFrame>

class ISVCDecoder;
class ISVCEncoder;

namespace Zalos::Core::Call {

// OpenH264, loaded at run time with dlopen() from the user's system (Fedora fedora-cisco-openh264,
// Debian/Ubuntu libopenh264-7/8, Flatpak openh264 extension). It is never linked or bundled: Cisco's
// patent licence only covers binaries that users get from Cisco. Built without the headers
// (ZALOS_HAVE_OPENH264 unset), it always reports itself unavailable.
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

// One H.264 stream (Annex-B access units in, I420 pictures out).
class H264Decoder {
public:
    H264Decoder();
    ~H264Decoder();
    H264Decoder(const H264Decoder&) = delete;
    H264Decoder& operator=(const H264Decoder&) = delete;

    bool isOpen() const { return m_decoder != nullptr; }
    // Decodes one access unit. Returns the picture when one is ready (invalid frame otherwise);
    // *damaged is set when the decoder reported a bitstream error (ask the sender for a key frame).
    QVideoFrame decode(const QByteArray& accessUnit, bool* damaged = nullptr);

private:
    ISVCDecoder* m_decoder{nullptr};
};

// Camera / screen encoder: I420 pictures in, Annex-B access units out (Constrained Baseline, one layer).
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

    // Encodes one tightly packed I420 picture of size(). Empty result: frame skipped by rate control.
    QByteArray encodeI420(const QByteArray& i420, qint64 timestampMs, bool forceKey, bool* isKey = nullptr);

private:
    ISVCEncoder* m_encoder{nullptr};
    QSize m_size;
};

// Converts a mapped video frame of any common pixel format to tightly packed I420 at `size`
// (nearest-neighbour scale). Returns an empty array when the format is not supported.
QByteArray frameToI420(const QVideoFrame& frame, QSize size);

} // namespace Zalos::Core::Call
