// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <utility>
#include <vector>

namespace zcall {

constexpr int kKeyPt = 98;
constexpr int kDeltaPt = 97;
constexpr int kMaxChunk = 1068;

struct VideoPacket {
    uint16_t seq{0};
    uint32_t ts{0};
    int pt{0};
    bool marker{false};
    QByteArray payload;
    uint32_t roc{0};
    uint32_t ssrc{0};
};

struct VideoFrame {
    bool key{false};
    QByteArray data;
    uint32_t ts{0};
};

struct PictureSize {
    int64_t width{0};
    int64_t height{0};
};

struct AssemblerStats {
    int frames{0};
    int dropped{0};
    int keys{0};
    int gaps{0};
};

class VideoAssembler {
public:
    VideoAssembler(std::function<void(const VideoFrame&)> onFrame, std::function<double()> now, bool group,
                   std::function<void()> onGap);
    void push(const VideoPacket& pkt);

    AssemblerStats stats;
    std::map<int, int64_t> nalBytes;
    QString spsSize7;
    QString spsSize15;

private:
    struct Pending {
        std::map<int64_t, QByteArray> parts;
        bool key{false};
        std::optional<int64_t> end;
    };
    struct Ready {
        bool key{false};
        QByteArray data;
        uint32_t ts{0};
        int64_t last{0};
        double since{0};
    };
    void complete(uint32_t ts, Pending& f);
    void flush();
    void emitFrame(int64_t firstIdx, Ready fr);
    std::vector<std::pair<uint32_t, Pending>>::iterator findPending(uint32_t ts);

    std::function<void(const VideoFrame&)> m_onFrame;
    std::function<double()> m_now;
    bool m_group;
    std::function<void()> m_onGap;
    std::vector<std::pair<uint32_t, Pending>> m_pending;
    std::map<int64_t, Ready> m_ready;
    int64_t m_lastIdx{-1};
    bool m_started{false};
    bool m_needKey{false};
    QByteArray m_sps;
    QByteArray m_pps;
};

QVector<QByteArray> nalUnits(const QByteArray& annexB);
std::optional<PictureSize> spsSize(const QByteArray& nal);
bool isFragment(const QByteArray& p);
QString avcCodecString(const QByteArray& annexB);
QVector<QByteArray> packetizeFrame(const QByteArray& annexB, int maxChunk = kMaxChunk);

}
