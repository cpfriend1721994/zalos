#include "h264_codec.h"

#include <QImage>
#include <QVector>
#include <cstring>
#include <dlfcn.h>

#ifdef ZALOS_HAVE_OPENH264
#include <wels/codec_api.h>
#endif

namespace Zalos::Core::Call {

#ifdef ZALOS_HAVE_OPENH264
namespace {

// The encoder's output structs changed in OpenH264 2.6.0 (Fedora soname 8): SLayerBSInfo gained a trailing
// `float rPsnr[3]`, which moves every sLayerInfo[i] after the first. Both layouts are mirrored here (the build
// headers may be either version) and picked from the run-time version. The decoder API is unchanged.
struct LayerBSInfo24 {
    unsigned char uiTemporalId;
    unsigned char uiSpatialId;
    unsigned char uiQualityId;
    EVideoFrameType eFrameType;
    unsigned char uiLayerType;
    int iSubSeqId;
    int iNalCount;
    int* pNalLengthInByte;
    unsigned char* pBsBuf;
};
struct FrameBSInfo24 {
    int iLayerNum;
    LayerBSInfo24 sLayerInfo[MAX_LAYER_NUM_OF_FRAME];
    EVideoFrameType eFrameType;
    int iFrameSizeInBytes;
    long long uiTimeStamp;
};
struct LayerBSInfo26 {
    unsigned char uiTemporalId;
    unsigned char uiSpatialId;
    unsigned char uiQualityId;
    EVideoFrameType eFrameType;
    unsigned char uiLayerType;
    int iSubSeqId;
    int iNalCount;
    int* pNalLengthInByte;
    unsigned char* pBsBuf;
    float rPsnr[3];
};
struct FrameBSInfo26 {
    int iLayerNum;
    LayerBSInfo26 sLayerInfo[MAX_LAYER_NUM_OF_FRAME];
    EVideoFrameType eFrameType;
    int iFrameSizeInBytes;
    long long uiTimeStamp;
};

// The build headers must match one of the mirrored layouts (a new OpenH264 layout needs a new mirror).
static_assert(sizeof(SFrameBSInfo) == sizeof(FrameBSInfo24) || sizeof(SFrameBSInfo) == sizeof(FrameBSInfo26),
              "unknown OpenH264 SFrameBSInfo layout");

// Structs that only gained trailing fields (SEncParamExt, SSourcePicture) are handed over inside a zeroed,
// larger buffer so a newer library never reads or writes past them.
constexpr size_t kTailSlack = 256;

bool g_layout26 = false;

template <typename Frame>
QByteArray collectNals(const Frame& info) {
    QByteArray out;
    for (int i = 0; i < info.iLayerNum && i < MAX_LAYER_NUM_OF_FRAME; ++i) {
        const auto& layer = info.sLayerInfo[i];
        int size = 0;
        for (int n = 0; n < layer.iNalCount; ++n) size += layer.pNalLengthInByte[n];
        out.append(reinterpret_cast<const char*>(layer.pBsBuf), size);
    }
    return out;
}

} // namespace
#endif

OpenH264& OpenH264::instance() {
    static OpenH264 lib;
    return lib;
}

OpenH264::OpenH264() {
#ifndef ZALOS_HAVE_OPENH264
    m_error = QStringLiteral("built without OpenH264 headers");
#else
    QStringList names;
    const QByteArray overridePath = qgetenv("ZALOS_OPENH264");
    if (!overridePath.isEmpty()) names << QString::fromLocal8Bit(overridePath);
    names << "libopenh264.so.8" << "libopenh264.so.7" << "libopenh264.so.6" << "libopenh264.so";
    for (const QString& name : std::as_const(names)) {
        m_handle = dlopen(name.toLocal8Bit().constData(), RTLD_NOW | RTLD_LOCAL);
        if (m_handle) break;
    }
    if (!m_handle) {
        m_error = QStringLiteral("libopenh264 not found");
        return;
    }
    auto createDecoder = reinterpret_cast<CreateDecoderFn>(dlsym(m_handle, "WelsCreateDecoder"));
    auto destroyDecoder = reinterpret_cast<DestroyDecoderFn>(dlsym(m_handle, "WelsDestroyDecoder"));
    auto createEncoder = reinterpret_cast<CreateEncoderFn>(dlsym(m_handle, "WelsCreateSVCEncoder"));
    auto destroyEncoder = reinterpret_cast<DestroyEncoderFn>(dlsym(m_handle, "WelsDestroySVCEncoder"));
    using VersionFn = void (*)(OpenH264Version*);
    auto versionEx = reinterpret_cast<VersionFn>(dlsym(m_handle, "WelsGetCodecVersionEx"));
    if (!createDecoder || !destroyDecoder || !createEncoder || !destroyEncoder || !versionEx) {
        m_error = QStringLiteral("libopenh264 lacks the expected symbols");
        dlclose(m_handle);
        m_handle = nullptr;
        return;
    }
    OpenH264Version v{};
    versionEx(&v);
    m_version = QStringLiteral("%1.%2.%3").arg(v.uMajor).arg(v.uMinor).arg(v.uRevision);
    if (v.uMajor < 1 || v.uMajor > 2) { // a 3.x could change the vtables
        m_error = QStringLiteral("unsupported libopenh264 %1").arg(m_version);
        dlclose(m_handle);
        m_handle = nullptr;
        return;
    }
    g_layout26 = v.uMajor > 2 || (v.uMajor == 2 && v.uMinor >= 6);
    m_createDecoder = createDecoder;
    m_destroyDecoder = destroyDecoder;
    m_createEncoder = createEncoder;
    m_destroyEncoder = destroyEncoder;
#endif
}

ISVCDecoder* OpenH264::createDecoder() const {
    ISVCDecoder* decoder = nullptr;
    if (m_createDecoder && m_createDecoder(&decoder) != 0) decoder = nullptr;
    return decoder;
}

void OpenH264::destroyDecoder(ISVCDecoder* decoder) const {
    if (decoder && m_destroyDecoder) m_destroyDecoder(decoder);
}

ISVCEncoder* OpenH264::createEncoder() const {
    ISVCEncoder* encoder = nullptr;
    if (m_createEncoder && m_createEncoder(&encoder) != 0) encoder = nullptr;
    return encoder;
}

void OpenH264::destroyEncoder(ISVCEncoder* encoder) const {
    if (encoder && m_destroyEncoder) m_destroyEncoder(encoder);
}

// ---------------------------------------------------------------------------

H264Decoder::H264Decoder() {
#ifdef ZALOS_HAVE_OPENH264
    const OpenH264& lib = OpenH264::instance();
    m_decoder = lib.createDecoder();
    if (!m_decoder) return;
    SDecodingParam param{};
    param.sVideoProperty.size = sizeof(param.sVideoProperty);
    param.sVideoProperty.eVideoBsType = VIDEO_BITSTREAM_AVC;
    param.eEcActiveIdc = ERROR_CON_SLICE_MV_COPY_CROSS_IDR_FREEZE_RES_CHANGE;
    if (m_decoder->Initialize(&param) != 0) {
        lib.destroyDecoder(m_decoder);
        m_decoder = nullptr;
    }
#endif
}

H264Decoder::~H264Decoder() {
#ifdef ZALOS_HAVE_OPENH264
    if (m_decoder) {
        m_decoder->Uninitialize();
        OpenH264::instance().destroyDecoder(m_decoder);
    }
#endif
}

QVideoFrame H264Decoder::decode(const QByteArray& accessUnit, bool* damaged) {
    if (damaged) *damaged = false;
#ifdef ZALOS_HAVE_OPENH264
    if (!m_decoder || accessUnit.isEmpty()) return {};
    unsigned char* planes[3] = {nullptr, nullptr, nullptr};
    SBufferInfo info{};
    const DECODING_STATE state = m_decoder->DecodeFrameNoDelay(
        reinterpret_cast<const unsigned char*>(accessUnit.constData()), int(accessUnit.size()), planes, &info);
    if (damaged) *damaged = state != dsErrorFree;
    if (info.iBufferStatus != 1 || !planes[0] || !planes[1] || !planes[2]) return {};

    const int w = info.UsrData.sSystemBuffer.iWidth;
    const int h = info.UsrData.sSystemBuffer.iHeight;
    const int strideY = info.UsrData.sSystemBuffer.iStride[0];
    const int strideUV = info.UsrData.sSystemBuffer.iStride[1];
    if (w <= 0 || h <= 0) return {};

    QVideoFrame frame(QVideoFrameFormat(QSize(w, h), QVideoFrameFormat::Format_YUV420P));
    if (!frame.map(QVideoFrame::WriteOnly)) return {};
    auto copyPlane = [&](int plane, const unsigned char* src, int srcStride, int rows, int cols) {
        uchar* dst = frame.bits(plane);
        const int dstStride = frame.bytesPerLine(plane);
        for (int y = 0; y < rows; ++y) std::memcpy(dst + y * dstStride, src + y * srcStride, size_t(cols));
    };
    copyPlane(0, planes[0], strideY, h, w);
    copyPlane(1, planes[1], strideUV, (h + 1) / 2, (w + 1) / 2);
    copyPlane(2, planes[2], strideUV, (h + 1) / 2, (w + 1) / 2);
    frame.unmap();
    return frame;
#else
    Q_UNUSED(accessUnit);
    return {};
#endif
}

// ---------------------------------------------------------------------------

H264Encoder::~H264Encoder() { close(); }

bool H264Encoder::open(QSize size, int fps, int bitrate, bool screenContent) {
    close();
#ifdef ZALOS_HAVE_OPENH264
    if (size.width() < 16 || size.height() < 16 || (size.width() | size.height()) & 1) return false;
    const OpenH264& lib = OpenH264::instance();
    m_encoder = lib.createEncoder();
    if (!m_encoder) return false;

    QByteArray storage(int(sizeof(SEncParamExt) + kTailSlack), '\0');
    auto* p = reinterpret_cast<SEncParamExt*>(storage.data());
    m_encoder->GetDefaultParams(p);
    p->iUsageType = screenContent ? SCREEN_CONTENT_REAL_TIME : CAMERA_VIDEO_REAL_TIME;
    p->iPicWidth = size.width();
    p->iPicHeight = size.height();
    p->iTargetBitrate = bitrate;
    p->iMaxBitrate = bitrate * 3 / 2;
    p->iRCMode = RC_BITRATE_MODE;
    p->fMaxFrameRate = float(fps);
    p->iSpatialLayerNum = 1;
    p->iTemporalLayerNum = 1;
    p->uiIntraPeriod = 0; // key frames on request (peer PLI) and on the caller's timer
    p->iEntropyCodingModeFlag = 0; // CAVLC: Constrained Baseline, what the peer is told (avc1.42E0xx)
    p->bEnableFrameSkip = true;
    p->iMultipleThreadIdc = 1;
    p->eSpsPpsIdStrategy = CONSTANT_ID;
    p->bPrefixNalAddingCtrl = false;
    p->bSimulcastAVC = false;
    SSpatialLayerConfig& layer = p->sSpatialLayers[0];
    layer.iVideoWidth = size.width();
    layer.iVideoHeight = size.height();
    layer.fFrameRate = float(fps);
    layer.iSpatialBitrate = bitrate;
    layer.iMaxSpatialBitrate = bitrate * 3 / 2;
    layer.uiProfileIdc = PRO_BASELINE;
    layer.sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
    if (m_encoder->InitializeExt(p) != 0) {
        lib.destroyEncoder(m_encoder);
        m_encoder = nullptr;
        return false;
    }
    int videoFormat = videoFormatI420;
    m_encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat);
    m_size = size;
    return true;
#else
    Q_UNUSED(size);
    Q_UNUSED(fps);
    Q_UNUSED(bitrate);
    Q_UNUSED(screenContent);
    return false;
#endif
}

void H264Encoder::close() {
#ifdef ZALOS_HAVE_OPENH264
    if (m_encoder) {
        m_encoder->Uninitialize();
        OpenH264::instance().destroyEncoder(m_encoder);
    }
#endif
    m_encoder = nullptr;
    m_size = {};
}

QByteArray H264Encoder::encodeI420(const QByteArray& i420, qint64 timestampMs, bool forceKey, bool* isKey) {
    if (isKey) *isKey = false;
#ifdef ZALOS_HAVE_OPENH264
    const int w = m_size.width();
    const int h = m_size.height();
    if (!m_encoder || i420.size() < w * h * 3 / 2) return {};
    if (forceKey) m_encoder->ForceIntraFrame(true);

    QByteArray picStorage(int(sizeof(SSourcePicture) + kTailSlack), '\0');
    auto* pic = reinterpret_cast<SSourcePicture*>(picStorage.data());
    auto* base = reinterpret_cast<unsigned char*>(const_cast<char*>(i420.constData()));
    pic->iColorFormat = videoFormatI420;
    pic->iPicWidth = w;
    pic->iPicHeight = h;
    pic->iStride[0] = w;
    pic->iStride[1] = w / 2;
    pic->iStride[2] = w / 2;
    pic->pData[0] = base;
    pic->pData[1] = base + w * h;
    pic->pData[2] = base + w * h + (w / 2) * (h / 2);
    pic->uiTimeStamp = timestampMs;

    QByteArray infoStorage(int(qMax(sizeof(FrameBSInfo24), sizeof(FrameBSInfo26)) + kTailSlack), '\0');
    if (m_encoder->EncodeFrame(pic, reinterpret_cast<SFrameBSInfo*>(infoStorage.data())) != cmResultSuccess) return {};
    EVideoFrameType type;
    QByteArray out;
    if (g_layout26) {
        const auto& info = *reinterpret_cast<const FrameBSInfo26*>(infoStorage.constData());
        type = info.eFrameType;
        if (type != videoFrameTypeSkip && type != videoFrameTypeInvalid) out = collectNals(info);
    } else {
        const auto& info = *reinterpret_cast<const FrameBSInfo24*>(infoStorage.constData());
        type = info.eFrameType;
        if (type != videoFrameTypeSkip && type != videoFrameTypeInvalid) out = collectNals(info);
    }
    if (isKey) *isKey = type == videoFrameTypeIDR;
    return out;
#else
    Q_UNUSED(i420);
    Q_UNUSED(timestampMs);
    Q_UNUSED(forceKey);
    return {};
#endif
}

// ---------------------------------------------------------------------------

QByteArray frameToI420(const QVideoFrame& source, QSize size) {
    const int w = size.width() & ~1;
    const int h = size.height() & ~1;
    if (w <= 0 || h <= 0 || !source.isValid()) return {};
    QImage image = source.toImage();
    if (image.isNull()) return {};
    // Keep the aspect ratio: crop the centre to w:h (a 4:3 webcam sent at 16:9 is not stretched).
    const qreal target = qreal(w) / h;
    const qreal aspect = qreal(image.width()) / image.height();
    if (aspect > target + 0.01) {
        const int cw = int(image.height() * target);
        image = image.copy((image.width() - cw) / 2, 0, cw, image.height());
    } else if (aspect < target - 0.01) {
        const int ch = int(image.width() / target);
        image = image.copy(0, (image.height() - ch) / 2, image.width(), ch);
    }
    image = image.scaled(w, h, Qt::IgnoreAspectRatio, Qt::FastTransformation).convertToFormat(QImage::Format_RGB32);

    QByteArray out(w * h * 3 / 2, '\0');
    auto* y = reinterpret_cast<uchar*>(out.data());
    uchar* u = y + w * h;
    uchar* v = u + (w / 2) * (h / 2);
    // BT.601 limited range, chroma from the top-left pixel of each 2x2 block.
    for (int row = 0; row < h; ++row) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(row));
        for (int col = 0; col < w; ++col) {
            const int r = qRed(line[col]), g = qGreen(line[col]), b = qBlue(line[col]);
            y[row * w + col] = uchar(((66 * r + 129 * g + 25 * b + 128) >> 8) + 16);
            if (!(row & 1) && !(col & 1)) {
                const int ci = (row / 2) * (w / 2) + col / 2;
                u[ci] = uchar(((-38 * r - 74 * g + 112 * b + 128) >> 8) + 128);
                v[ci] = uchar(((112 * r - 94 * g - 18 * b + 128) >> 8) + 128);
            }
        }
    }
    return out;
}

} // namespace Zalos::Core::Call
