// SPDX-License-Identifier: BSD-3-Clause
#include "wire/rtp.h"

#include "wire/bytes.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace zcall {

namespace {

constexpr int kTwccExtId = 5;
constexpr int kAudioLevelExtId = 2;
constexpr int kAbsSendTimeExtId = 3;
constexpr int kCaptureNtpExtId = 13;
constexpr double kNtpUnixOffset = 2208988800.0;

int sfuByte0() {
    static const int v = [] {
        const QByteArray env = qgetenv("ZCALL_SFU_BYTE0");
        if (env.isEmpty()) return 0x03;
        bool ok = false;
        const int n = env.trimmed().toInt(&ok, 16);
        return ok ? (n & 0xff) : 0;
    }();
    return v;
}

void baseHeader(QByteArray& h, const RtpHeaderArgs& a) {
    h[0] = char(0x90);
    h[1] = char((a.marker ? 0x80 : 0) | (a.pt & 0x7f));
    wr16be(h, 2, a.seq);
    wr32be(h, 4, a.ts);
    wr32be(h, 8, a.ssrc);
    wr16be(h, 12, 0xbede);
}

int writeCaptureNtp(QByteArray& h, int o, double captureMs) {
    h[o++] = char((kCaptureNtpExtId << 4) | 6);
    const double ntp = captureMs / 1000.0 + kNtpUnixOffset;
    const double sec = std::floor(ntp);
    const uint32_t frac = uint32_t(uint64_t(std::floor((ntp - sec) * 4294967296.0)) & 0xffffffffULL);
    const uint64_t s = uint64_t(sec);
    h[o++] = char((s >> 16) & 0xff);
    h[o++] = char((s >> 8) & 0xff);
    h[o++] = char(s & 0xff);
    wr32be(h, o, frac);
    return o + 4;
}

}

QByteArray buildAudioRtpHeader(const RtpHeaderArgs& a) {
    QByteArray h(20, '\0');
    baseHeader(h, a);
    wr16be(h, 14, 1);
    h[16] = char((kTwccExtId << 4) | 1);
    wr16be(h, 17, a.twSeq);
    h[19] = 0;
    return h;
}

QByteArray buildGroupVideoRtpHeader(const RtpHeaderArgs& a, const SfuExt& sfu) {
    const bool lng = sfu.longForm;
    const int id = sfu.id ? sfu.id : (lng ? 14 : 12);
    const int dataLen = lng ? 7 : 3;
    const int extBytes = 4 + 3 + (1 + dataLen) + 8;
    const int padded = (extBytes + 3) & ~3;
    QByteArray h(12 + 4 + padded, '\0');
    baseHeader(h, a);
    wr16be(h, 14, uint32_t(padded / 4));
    int o = 16;
    h[o++] = char((kAbsSendTimeExtId << 4) | 2);
    const uint64_t abs = uint64_t(std::floor(a.sendMs * 262.144)) % 0x1000000ULL;
    h[o++] = char((abs >> 16) & 0xff);
    h[o++] = char((abs >> 8) & 0xff);
    h[o++] = char(abs & 0xff);
    h[o++] = char((kTwccExtId << 4) | 1);
    wr16be(h, o, a.twSeq);
    o += 2;
    h[o++] = char(((id & 0x0f) << 4) | (lng ? 6 : 2));
    h[o++] = char(sfuByte0());
    wr16be(h, o, a.layerSeq);
    o += 2;
    if (lng) o += 4;
    writeCaptureNtp(h, o, a.captureMs);
    return h;
}

QByteArray buildGroupAudioRtpHeader(const RtpHeaderArgs& a) {
    QByteArray h(12 + 4 + 16, '\0');
    baseHeader(h, a);
    wr16be(h, 14, 4);
    h[16] = char((kAudioLevelExtId << 4) | 0);
    h[17] = char(std::min(127, std::max(0, a.level)));
    h[18] = char((kTwccExtId << 4) | 1);
    wr16be(h, 19, a.twSeq);
    writeCaptureNtp(h, 21, a.captureMs);
    return h;
}

std::optional<AudioRtpFields> readAudioRtpHeader(const QByteArray& rtp) {
    if (rtp.size() < 20 || u8at(rtp, 0) != 0x90 || rd16be(rtp, 12) != 0xbede) return std::nullopt;
    AudioRtpFields f;
    f.marker = (u8at(rtp, 1) & 0x80) != 0;
    f.pt = u8at(rtp, 1) & 0x7f;
    f.seq = rd16be(rtp, 2);
    f.ts = rd32be(rtp, 4);
    f.ssrc = rd32be(rtp, 8);
    if ((u8at(rtp, 16) >> 4) == kTwccExtId) f.twSeq = rd16be(rtp, 17);
    return f;
}

std::optional<int> rtpAudioLevel(const QByteArray& rtp) {
    if (rtp.size() < 16 || !(u8at(rtp, 0) & 0x10)) return std::nullopt;
    int o = 12 + 4 * (u8at(rtp, 0) & 0x0f);
    if (o + 4 > rtp.size() || rd16be(rtp, o) != 0xbede) return std::nullopt;
    const int end = std::min(int(rtp.size()), o + 4 + 4 * rd16be(rtp, o + 2));
    o += 4;
    while (o < end) {
        if (u8at(rtp, o) == 0) {
            ++o;
            continue;
        }
        const int id = u8at(rtp, o) >> 4;
        const int len = (u8at(rtp, o) & 0x0f) + 1;
        if (id == 15) return std::nullopt;
        if (id == 2 && o + 1 < end) return u8at(rtp, o + 1) & 0x7f;
        o += 1 + len;
    }
    return std::nullopt;
}

}
