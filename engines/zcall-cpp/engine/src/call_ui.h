// SPDX-License-Identifier: BSD-3-Clause
#pragma once

#include "media.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>

#include <functional>
#include <optional>

namespace zcall {

struct UiOpen {
    QString title;
    QString text;
    QString avatar;
    bool video{false};
    bool group{false};
    QString selfAvatar;
    std::optional<QJsonArray> members;
};

struct UiIncoming {
    QString title;
    QString text;
    QString avatar;
    bool video{false};
    bool noAnswer{false};
    QString inviter;
    bool group{false};
    QString selfAvatar;
    std::optional<QJsonArray> members;
};

struct UiVideo {
    bool key{false};
    QString codec;
    QByteArray data;
    std::optional<uint32_t> src;
};

class UiListener {
public:
    virtual ~UiListener() = default;
    virtual void uiHangup() = 0;
    virtual void uiMute(bool on) = 0;
    virtual void uiSpeaker(bool on) = 0;
    virtual void uiVideoFrame(const OutVideoFrame& f) = 0;
    virtual void uiCamera(bool on) = 0;
    virtual void uiDevice(const QString& kind, const QString& name) = 0;
    virtual void uiMicVolume(double pct) = 0;
    virtual void uiScreen(bool on) = 0;
    virtual void uiNeedKey(const QString& why, const QJsonValue& src) = 0;
    virtual void uiTile(uint32_t src, double width) = 0;
};

class CallUi {
public:
    using AcceptFn = std::function<void(bool camStartOff)>;
    using RejectFn = std::function<void(const QString& why)>;

    virtual ~CallUi() = default;
    void setListener(UiListener* l) { m_listener = l; }

    virtual void open(const UiOpen& o) = 0;
    virtual void incoming(const UiIncoming& o, AcceptFn onAccept, RejectFn onReject) = 0;
    virtual void status(const QString& text, double since = 0) = 0;
    virtual void close(const QString& text) = 0;

    virtual bool rich() const { return false; }
    virtual void notice(const QString&) {}
    virtual void micVolume(int) {}
    virtual void micLevel(int) {}
    virtual void peer(bool, bool, bool) {}
    virtual void setCameraEncode(const QJsonObject&) {}
    virtual void setScreenEncode(const QJsonObject&) {}
    virtual void requestKeyFrame() {}
    virtual void rename(const QString&) {}
    virtual void members(const QJsonArray&) {}
    virtual void speaking(uint32_t, bool) {}
    virtual void video(const UiVideo&) {}

protected:
    UiListener* m_listener{nullptr};
};

class HeadlessUi : public CallUi {
public:
    void open(const UiOpen&) override {}
    void incoming(const UiIncoming& o, AcceptFn onAccept, RejectFn onReject) override;
    void status(const QString&, double) override {}
    void close(const QString&) override {}
};

}
