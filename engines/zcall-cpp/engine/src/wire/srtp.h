// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <QByteArray>

#include <cstdint>
#include <optional>

namespace zcall {

struct SrtpKeys {
    QByteArray enc;
    QByteArray auth;
    QByteArray salt;
};

struct RtpView {
    uint16_t seq{0};
    uint32_t ssrc{0};
    int headerLen{0};
};

struct Unprotected {
    uint16_t seq{0};
    uint32_t ssrc{0};
    QByteArray payload;
};

QByteArray aesCmExpand(const QByteArray& key, const QByteArray& iv16, int n);
std::optional<SrtpKeys> sessionKeys(const QByteArray& master30);
std::optional<QByteArray> masterFromSessId(const QByteArray& sessIdUtf8);
QByteArray packetIv(const QByteArray& sessionSalt, uint32_t ssrc, uint16_t seq, uint32_t roc = 0);
std::optional<RtpView> parseRtp(const QByteArray& buf);
std::optional<Unprotected> unprotectRtp(const QByteArray& rtp, const SrtpKeys& keys, uint32_t roc = 0);
QByteArray protectRtp(const QByteArray& headerAndExt, const QByteArray& payload, const SrtpKeys& keys, uint16_t seq,
                      uint32_t ssrc, uint32_t roc = 0);

}
