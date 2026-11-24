#include "h264_codec.h"

#include <QImage>
#include <QVarLengthArray>
#include <QVector>
#include <cstring>
#include <dlfcn.h>

#ifdef ZALOS_HAVE_OPENH264
#include <wels/codec_api.h>
#endif

namespace Zalos::Core::Call {

#ifdef ZALOS_HAVE_OPENH264
namespace {

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

static_assert(sizeof(SFrameBSInfo) == sizeof(FrameBSInfo24) || sizeof(SFrameBSInfo) == sizeof(FrameBSInfo26),
              "unknown OpenH264 SFrameBSInfo layout");

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

}
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
    if (v.uMajor < 1 || v.uMajor > 2) {
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
    p->uiIntraPeriod = 0;
    p->iEntropyCodingModeFlag = 0;
    p->bEnableFrameSkip = true;
    if (screenContent) {
        p->bEnableAdaptiveQuant = false;
        p->bEnableBackgroundDetection = false;
    }
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
    layer.uiLevelIdc = qMax(size.width(), size.height()) <= 640 ? LEVEL_3_0 : LEVEL_4_0;
    layer.sSliceArgument.uiSliceMode = SM_SINGLE_SLICE;
    if (m_encoder->InitializeExt(p) != 0) {
        lib.destroyEncoder(m_encoder);
        m_encoder = nullptr;
        return false;
    }
    int videoFormat = videoFormatI420;
    if (m_encoder->SetOption(ENCODER_OPTION_DATAFORMAT, &videoFormat) != 0) {
        close();
        return false;
    }
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
    m_picStorage.clear();
    m_infoStorage.clear();
}

QByteArray H264Encoder::encodeI420(const QByteArray& i420, qint64 timestampMs, bool forceKey, bool* isKey) {
    if (isKey) *isKey = false;
#ifdef ZALOS_HAVE_OPENH264
    const int w = m_size.width();
    const int h = m_size.height();
    if (!m_encoder || i420.size() < w * h * 3 / 2) return {};
    if (forceKey) m_encoder->ForceIntraFrame(true);

    const int picSize = int(sizeof(SSourcePicture) + kTailSlack);
    if (m_picStorage.size() != picSize) m_picStorage.resize(picSize);
    std::memset(m_picStorage.data(), 0, size_t(picSize));
    auto* pic = reinterpret_cast<SSourcePicture*>(m_picStorage.data());
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

    const int infoSize = int(qMax(sizeof(FrameBSInfo24), sizeof(FrameBSInfo26)) + kTailSlack);
    if (m_infoStorage.size() != infoSize) m_infoStorage.resize(infoSize);
    std::memset(m_infoStorage.data(), 0, size_t(infoSize));
    if (m_encoder->EncodeFrame(pic, reinterpret_cast<SFrameBSInfo*>(m_infoStorage.data())) != cmResultSuccess) return {};
    EVideoFrameType type;
    QByteArray out;
    if (g_layout26) {
        const auto& info = *reinterpret_cast<const FrameBSInfo26*>(m_infoStorage.constData());
        type = info.eFrameType;
        if (type != videoFrameTypeSkip && type != videoFrameTypeInvalid) out = collectNals(info);
    } else {
        const auto& info = *reinterpret_cast<const FrameBSInfo24*>(m_infoStorage.constData());
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

bool imageToI420Into(const QImage& source, QSize size, QByteArray& out) {
    const int w = size.width() & ~1;
    const int h = size.height() & ~1;
    if (w <= 0 || h <= 0 || source.isNull()) return false;

    const int srcW = source.width();
    const int srcH = source.height();
    if (srcW <= 0 || srcH <= 0) return false;

    int cropX = 0, cropY = 0, cropW = srcW, cropH = srcH;
    const qreal target = qreal(w) / h;
    const qreal aspect = qreal(srcW) / srcH;
    if (aspect > target + 0.01) {
        cropW = qMax(2, int(srcH * target) & ~1);
        cropX = ((srcW - cropW) / 2) & ~1;
    } else if (aspect < target - 0.01) {
        cropH = qMax(2, int(srcW / target) & ~1);
        cropY = ((srcH - cropH) / 2) & ~1;
    }

    QImage image = source;
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32
        && image.format() != QImage::Format_ARGB32_Premultiplied) {
        image = image.convertToFormat(QImage::Format_RGB32);
    }

    const int need = w * h * 3 / 2;
    if (out.size() != need) out.resize(need);
    auto* y = reinterpret_cast<uchar*>(out.data());
    uchar* u = y + w * h;
    uchar* v = u + (w / 2) * (h / 2);
    QVarLengthArray<int, 1280> sxTable(w);
    for (int col = 0; col < w; ++col) {
        sxTable[col] = cropX + int((qint64(col) * cropW) / w);
    }
    const int cw = w >> 1;
    for (int row = 0; row < h; row += 2) {
        const int sy0 = cropY + int((qint64(row) * cropH) / h);
        const int sy1 = qMin(srcH - 1, cropY + int((qint64(row + 1) * cropH) / h));
        const auto* line0 = reinterpret_cast<const QRgb*>(image.constScanLine(sy0));
        const auto* line1 = reinterpret_cast<const QRgb*>(image.constScanLine(sy1));
        uchar* yRow0 = y + row * w;
        uchar* yRow1 = yRow0 + w;
        uchar* uRow = u + (row >> 1) * cw;
        uchar* vRow = v + (row >> 1) * cw;
        for (int col = 0; col < w; col += 2) {
            const int sx0 = sxTable[col];
            const int sx1 = sxTable[col + 1];
            const QRgb p00 = line0[sx0];
            const QRgb p01 = line0[sx1];
            const QRgb p10 = line1[sx0];
            const QRgb p11 = line1[sx1];

            const int r00 = qRed(p00), g00 = qGreen(p00), b00 = qBlue(p00);
            const int r01 = qRed(p01), g01 = qGreen(p01), b01 = qBlue(p01);
            const int r10 = qRed(p10), g10 = qGreen(p10), b10 = qBlue(p10);
            const int r11 = qRed(p11), g11 = qGreen(p11), b11 = qBlue(p11);

            yRow0[col] = uchar(((66 * r00 + 129 * g00 + 25 * b00 + 128) >> 8) + 16);
            yRow0[col + 1] = uchar(((66 * r01 + 129 * g01 + 25 * b01 + 128) >> 8) + 16);
            yRow1[col] = uchar(((66 * r10 + 129 * g10 + 25 * b10 + 128) >> 8) + 16);
            yRow1[col + 1] = uchar(((66 * r11 + 129 * g11 + 25 * b11 + 128) >> 8) + 16);

            const int rAvg = (r00 + r01 + r10 + r11 + 2) >> 2;
            const int gAvg = (g00 + g01 + g10 + g11 + 2) >> 2;
            const int bAvg = (b00 + b01 + b10 + b11 + 2) >> 2;
            const int ci = col >> 1;
            uRow[ci] = uchar(((-38 * rAvg - 74 * gAvg + 112 * bAvg + 128) >> 8) + 128);
            vRow[ci] = uchar(((112 * rAvg - 94 * gAvg - 18 * bAvg + 128) >> 8) + 128);
        }
    }
    return true;
}

QByteArray imageToI420(const QImage& source, QSize size) {
    QByteArray out;
    if (!imageToI420Into(source, size, out)) return {};
    return out;
}

bool frameToI420Into(const QVideoFrame& source, QSize size, QByteArray& out) {
    const int w = size.width() & ~1;
    const int h = size.height() & ~1;
    if (w <= 0 || h <= 0 || !source.isValid()) return false;

    const int srcW = source.width();
    const int srcH = source.height();
    if (srcW <= 0 || srcH <= 0) return false;

    int cropX = 0, cropY = 0, cropW = srcW, cropH = srcH;
    const qreal target = qreal(w) / h;
    const qreal aspect = qreal(srcW) / srcH;
    if (aspect > target + 0.01) {
        cropW = qMax(2, int(srcH * target) & ~1);
        cropX = ((srcW - cropW) / 2) & ~1;
    } else if (aspect < target - 0.01) {
        cropH = qMax(2, int(srcW / target) & ~1);
        cropY = ((srcH - cropH) / 2) & ~1;
    }

    const int need = w * h * 3 / 2;
    const auto fmt = source.pixelFormat();
    if (fmt == QVideoFrameFormat::Format_YUV420P || fmt == QVideoFrameFormat::Format_NV12) {
        QVideoFrame mapped(source);
        if (mapped.map(QVideoFrame::ReadOnly)) {
            const uchar* srcY = mapped.bits(0);
            const int strideY = mapped.bytesPerLine(0);
            const uchar* srcU = mapped.bits(1);
            const int strideU = mapped.bytesPerLine(1);
            const uchar* srcV = (fmt == QVideoFrameFormat::Format_YUV420P) ? mapped.bits(2) : nullptr;
            const int strideV = (fmt == QVideoFrameFormat::Format_YUV420P) ? mapped.bytesPerLine(2) : 0;

            if (srcY && srcU && (fmt == QVideoFrameFormat::Format_NV12 || srcV)) {
                if (out.size() != need) out.resize(need);
                auto* dstY = reinterpret_cast<uchar*>(out.data());
                uchar* dstU = dstY + w * h;
                uchar* dstV = dstU + (w / 2) * (h / 2);
                const int cw = w / 2, ch = h / 2;

                if (cropW == w && cropH == h) {
                    for (int row = 0; row < h; ++row) {
                        std::memcpy(dstY + row * w, srcY + (cropY + row) * strideY + cropX, size_t(w));
                    }
                    const int cx = cropX / 2, cy = cropY / 2;
                    if (fmt == QVideoFrameFormat::Format_YUV420P) {
                        for (int row = 0; row < ch; ++row) {
                            std::memcpy(dstU + row * cw, srcU + (cy + row) * strideU + cx, size_t(cw));
                            std::memcpy(dstV + row * cw, srcV + (cy + row) * strideV + cx, size_t(cw));
                        }
                    } else {
                        for (int row = 0; row < ch; ++row) {
                            const uchar* uvLine = srcU + (cy + row) * strideU + cx * 2;
                            uchar* outU = dstU + row * cw;
                            uchar* outV = dstV + row * cw;
                            for (int col = 0; col < cw; ++col) {
                                outU[col] = uvLine[col * 2];
                                outV[col] = uvLine[col * 2 + 1];
                            }
                        }
                    }
                } else {
                    QVarLengthArray<int, 1280> sxY(w);
                    for (int col = 0; col < w; ++col) sxY[col] = cropX + int((qint64(col) * cropW) / w);
                    for (int row = 0; row < h; ++row) {
                        const int sy = cropY + int((qint64(row) * cropH) / h);
                        const uchar* yLine = srcY + sy * strideY;
                        uchar* outRow = dstY + row * w;
                        for (int col = 0; col < w; ++col) {
                            outRow[col] = yLine[sxY[col]];
                        }
                    }
                    QVarLengthArray<int, 640> sxC(cw);
                    const int cropCx = cropX / 2, cropCy = cropY / 2, cropCw = cropW / 2, cropCh = cropH / 2;
                    for (int col = 0; col < cw; ++col) sxC[col] = cropCx + int((qint64(col) * cropCw) / cw);
                    for (int row = 0; row < ch; ++row) {
                        const int sy = cropCy + int((qint64(row) * cropCh) / ch);
                        uchar* outU = dstU + row * cw;
                        uchar* outV = dstV + row * cw;
                        if (fmt == QVideoFrameFormat::Format_YUV420P) {
                            const uchar* uLine = srcU + sy * strideU;
                            const uchar* vLine = srcV + sy * strideV;
                            for (int col = 0; col < cw; ++col) {
                                const int sx = sxC[col];
                                outU[col] = uLine[sx];
                                outV[col] = vLine[sx];
                            }
                        } else {
                            const uchar* uvLine = srcU + sy * strideU;
                            for (int col = 0; col < cw; ++col) {
                                const int sx2 = sxC[col] * 2;
                                outU[col] = uvLine[sx2];
                                outV[col] = uvLine[sx2 + 1];
                            }
                        }
                    }
                }
                mapped.unmap();
                return true;
            }
            mapped.unmap();
        }
    }
    if (fmt == QVideoFrameFormat::Format_YUYV || fmt == QVideoFrameFormat::Format_UYVY) {
        QVideoFrame mapped(source);
        if (mapped.map(QVideoFrame::ReadOnly)) {
            const uchar* src = mapped.bits(0);
            const int stride = mapped.bytesPerLine(0);
            if (src && stride > 0) {
                if (out.size() != need) out.resize(need);
                auto* dstY = reinterpret_cast<uchar*>(out.data());
                uchar* dstU = dstY + w * h;
                uchar* dstV = dstU + (w / 2) * (h / 2);
                const int yShift = (fmt == QVideoFrameFormat::Format_YUYV) ? 0 : 1;
                const int uOff = (fmt == QVideoFrameFormat::Format_YUYV) ? 1 : 0;
                const int vOff = (fmt == QVideoFrameFormat::Format_YUYV) ? 3 : 2;
                QVarLengthArray<int, 1280> sxYByte(w);
                for (int col = 0; col < w; ++col) {
                    sxYByte[col] = (cropX + int((qint64(col) * cropW) / w)) * 2 + yShift;
                }
                for (int row = 0; row < h; ++row) {
                    const int sy = cropY + int((qint64(row) * cropH) / h);
                    const uchar* line = src + sy * stride;
                    uchar* outRow = dstY + row * w;
                    for (int col = 0; col < w; ++col) {
                        outRow[col] = line[sxYByte[col]];
                    }
                }
                const int cw = w / 2, ch = h / 2;
                QVarLengthArray<int, 640> sxCByte(cw);
                for (int col = 0; col < cw; ++col) {
                    const int sx = cropX + int((qint64(col * 2) * cropW) / w);
                    sxCByte[col] = (sx & ~1) * 2;
                }
                for (int row = 0; row < ch; ++row) {
                    const int sy0 = cropY + int((qint64(row * 2) * cropH) / h);
                    const int sy1 = qMin(srcH - 1, cropY + int((qint64(row * 2 + 1) * cropH) / h));
                    const uchar* line0 = src + sy0 * stride;
                    const uchar* line1 = src + sy1 * stride;
                    uchar* outU = dstU + row * cw;
                    uchar* outV = dstV + row * cw;
                    for (int col = 0; col < cw; ++col) {
                        const int pairBase = sxCByte[col];
                        outU[col] = static_cast<uchar>((int(line0[pairBase + uOff]) + int(line1[pairBase + uOff]) + 1) >> 1);
                        outV[col] = static_cast<uchar>((int(line0[pairBase + vOff]) + int(line1[pairBase + vOff]) + 1) >> 1);
                    }
                }
                mapped.unmap();
                return true;
            }
            mapped.unmap();
        }
    }

    return imageToI420Into(source.toImage(), size, out);
}

QByteArray frameToI420(const QVideoFrame& source, QSize size) {
    QByteArray out;
    if (!frameToI420Into(source, size, out)) return {};
    return out;
}

}
