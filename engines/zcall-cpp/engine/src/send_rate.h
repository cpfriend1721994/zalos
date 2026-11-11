// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "wire/zrtp_packet.h"

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace zcall {

struct SendProfile {
    int width{0};
    int height{0};
    double fps{0};
    double bitrate{0};
    double keyMs{0};
    bool captureBig{false};
    QJsonValue rung;

    QJsonObject toJson() const;
};

struct SendRateTotals {
    int64_t twccPkts{0};
    int64_t twccLost{0};
    int64_t rrBlocks{0};
    int changes{0};
    int downs{0};
};

class SendRate {
public:
    struct Options {
        uint32_t localSsrc{0};
        std::function<void(const QString&)> log;
        std::function<void(const SendProfile&)> onProfile;
        std::function<double()> now;
        std::optional<QVector<VideoRow>> layers;
        int startIndex{0};
    };

    SendRate(const QJsonObject& config, Options opts);

    SendProfile profile() const;
    void onSent(uint16_t twSeq, std::optional<double> ms = std::nullopt);
    void onRtcp(char kind, const QByteArray& buf);
    void tick(bool sending, std::optional<double> downLoss);
    QString totalsJson() const;

    const SendRateTotals& totals() const { return m_totals; }
    int level() const { return m_level; }

private:
    struct Rung {
        QJsonValue id;
        double res{0};
        int width{0};
        int height{0};
        double fps{0};
        double kbps{0};
    };
    struct Window {
        int64_t twccRecv{0};
        int64_t twccLost{0};
        std::vector<double> rr;
        std::vector<double> delays;
    };

    std::pair<int, int> dims(const Rung& p) const;
    QString name(int level) const;
    QString name() const { return name(m_level); }
    void onTwcc(const QByteArray& pkt);
    void set(int level, const QString& why);

    uint32_t m_localSsrc;
    std::function<void(const QString&)> m_log;
    std::function<void(const SendProfile&)> m_onProfile;
    std::function<double()> m_now;
    bool m_seenTwcc{false};
    QVector<Rung> m_ladder;
    int m_level{0};
    int m_ceiling{0};
    bool m_armTimeout{false};
    bool m_fixed{false};
    double m_highLoss{0.1};
    double m_lowLoss{0.02};
    double m_keyMs{10000};
    bool m_captureBig{false};
    std::unordered_map<uint16_t, double> m_sent;
    std::deque<uint16_t> m_sentOrder;
    Window m_win;
    std::vector<std::pair<double, double>> m_minDelays;
    std::vector<std::pair<QString, int>> m_types;
    int m_good{0};
    int m_noFeedback{0};
    double m_lastDownAt{0};
    double m_lastUpAt{0};
    double m_backoffMs{10000};
    double m_lastLogAt{0};
    QStringList m_history;
    SendRateTotals m_totals;
};

}
