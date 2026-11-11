// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <QByteArray>

#include <cstdint>
#include <optional>

namespace zcall {

constexpr int kAudioPt = 112;
constexpr uint32_t kFrameSamples = 320;

struct SfuExt {
    int id{12};
    bool longForm{false};
};

struct RtpHeaderArgs {
    uint16_t seq{0};
    uint32_t ts{0};
    uint32_t ssrc{0};
    uint16_t twSeq{0};
    bool marker{false};
    int pt{kAudioPt};
    uint16_t layerSeq{0};
    double captureMs{0};
    double sendMs{0};
    int level{127};
};

QByteArray buildAudioRtpHeader(const RtpHeaderArgs& a);
QByteArray buildGroupVideoRtpHeader(const RtpHeaderArgs& a, const SfuExt& sfu);
QByteArray buildGroupAudioRtpHeader(const RtpHeaderArgs& a);

struct AudioRtpFields {
    bool marker{false};
    int pt{0};
    uint16_t seq{0};
    uint32_t ts{0};
    uint32_t ssrc{0};
    std::optional<uint16_t> twSeq;
};
std::optional<AudioRtpFields> readAudioRtpHeader(const QByteArray& rtp);

std::optional<int> rtpAudioLevel(const QByteArray& rtp);

}
