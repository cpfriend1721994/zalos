// SPDX-License-Identifier: BSD-3-Clause
#include "ui_client.h"

#include "js.h"
#include "log.h"

#include <QJsonArray>
#include <QJsonDocument>

namespace zcall {

namespace {

UiOpen fallbackOpen(const QJsonObject& s) {
    UiOpen o;
    o.title = s.value("name").toString();
    o.statusCode = s.value("statusCode").toString();
    o.statusParams = s.value("statusParams").toObject();
    return o;
}

}

ElectronCallUi::ElectronCallUi(quint16 port, const QString& token, QObject* parent)
    : QObject(parent), m_port(port), m_token(token) {
    m_sock = new QTcpSocket(this);
    connect(m_sock, &QTcpSocket::connected, this, [this] { m_sock->write(m_token.toUtf8() + '\n'); });
    connect(m_sock, &QTcpSocket::readyRead, this, &ElectronCallUi::onData);
    connect(m_sock, &QTcpSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        zlog(QStringLiteral("call ui: %1").arg(m_sock->errorString()));
        useFallback();
    });
    connect(m_sock, &QTcpSocket::disconnected, this, [this] { useFallback(); });
    m_sock->connectToHost(QStringLiteral("127.0.0.1"), m_port);
}

ElectronCallUi::~ElectronCallUi() = default;

void ElectronCallUi::setState(const QJsonObject& s) {
    m_state = s;
    m_hasState = true;
    QJsonObject msg = s;
    msg.insert(QStringLiteral("type"), QStringLiteral("state"));
    send(msg);
    if (m_fallback) {
        const QString phase = s.value(QStringLiteral("phase")).toString();
        if (phase == QLatin1String("outgoing")) {
            m_fallback->open(fallbackOpen(s));
        } else if (phase != QLatin1String("incoming")) {
            m_fallback->status(s.value("statusCode").toString(), s.value("since").toDouble(), s.value("statusParams").toObject());
        }
    }
}

void ElectronCallUi::send(const QJsonObject& msg) {
    if (m_fallback || m_sock->state() != QAbstractSocket::ConnectedState) return;
    m_sock->write(QJsonDocument(msg).toJson(QJsonDocument::Compact) + '\n');
}

void ElectronCallUi::open(const UiOpen& o) {
    m_accept = nullptr;
    m_reject = nullptr;
    QJsonObject s{{"phase", "outgoing"}, {"name", o.title}, {"avatar", o.avatar}, {"statusCode", o.statusCode},
                  {"statusParams", o.statusParams}, {"text", statusText(o.statusCode, o.statusParams)}, {"since", 0},
                  {"video", o.video}, {"group", o.group}, {"selfAvatar", o.selfAvatar}, {"muted", false}, {"speakerOff", false}};
    if (o.members) s.insert("members", *o.members);
    setState(s);
}

void ElectronCallUi::incoming(const UiIncoming& o, AcceptFn onAccept, RejectFn onReject) {
    m_accept = std::move(onAccept);
    m_reject = std::move(onReject);
    QJsonObject s{{"phase", "incoming"}, {"name", o.title}, {"avatar", o.avatar}, {"statusCode", o.statusCode},
                  {"statusParams", o.statusParams}, {"text", statusText(o.statusCode, o.statusParams)}, {"since", 0},
                  {"video", o.video}, {"group", o.group}, {"selfAvatar", o.selfAvatar}, {"muted", false}, {"speakerOff", false},
                  {"noAnswer", o.noAnswer}, {"inviter", o.inviter}};
    if (o.members) s.insert("members", *o.members);
    setState(s);
    if (m_fallback) {
        UiIncoming f;
        f.title = o.title;
        f.statusCode = o.statusCode;
        f.statusParams = o.statusParams;
        m_fallback->incoming(f, [this](bool) { if (m_accept) { auto a = m_accept; m_accept = nullptr; a(false); } },
                             [this](const QString& why) { if (m_reject) { auto r = m_reject; m_reject = nullptr; r(why); } });
    }
}

void ElectronCallUi::status(const QString& code, double since, const QJsonObject& params) {
    if (!m_hasState) return;
    QJsonObject s = m_state;
    QString phase = s.value("phase").toString();
    if (since != 0) phase = QStringLiteral("connected");
    else if (phase == QLatin1String("incoming")) phase = QStringLiteral("connecting");
    s.insert("phase", phase);
    s.insert("statusCode", code);
    s.insert("statusParams", params);
    s.insert("text", statusText(code, params));
    s.insert("since", since);
    setState(s);
}

void ElectronCallUi::close(const QString& code, const QJsonObject& params) {
    m_accept = nullptr;
    m_reject = nullptr;
    m_hasState = false;
    m_state = QJsonObject();
    const QString closeCode = code.isEmpty() ? QStringLiteral("ended") : code;
    send(QJsonObject{{"type", "close"}, {"statusCode", closeCode}, {"statusParams", params}, {"text", statusText(closeCode, params)}});
    if (m_fallback) m_fallback->close(closeCode, params);
}

void ElectronCallUi::notice(const QString& code, const QJsonObject& params) {
    if (!m_hasState) return;
    m_state.insert("noticeCode", code);
    m_state.insert("noticeParams", params);
    m_state.insert("notice", statusText(code, params));
    QJsonObject msg = m_state;
    msg.insert("type", "state");
    send(msg);
    if (m_fallback) m_fallback->status(code, m_state.value("since").toDouble(), params);
}

void ElectronCallUi::micVolume(int pct) {
    if (!m_hasState) return;
    m_state.insert("micVolume", pct);
    QJsonObject msg = m_state;
    msg.insert("type", "state");
    send(msg);
}

void ElectronCallUi::micLevel(int level) { send(QJsonObject{{"type", "micLevel"}, {"level", level}}); }

void ElectronCallUi::peer(bool camOff, bool muted, bool sharing) {
    if (!m_hasState) return;
    QJsonObject s = m_state;
    s.insert("peerCamOff", camOff);
    s.insert("peerMuted", muted);
    s.insert("peerSharing", sharing);
    setState(s);
}

void ElectronCallUi::setCameraEncode(const QJsonObject& p) {
    if (!m_hasState) return;
    QJsonObject s = m_state;
    s.insert("cameraEncode", p);
    setState(s);
}

void ElectronCallUi::setScreenEncode(const QJsonObject& p) {
    if (!m_hasState) return;
    QJsonObject s = m_state;
    s.insert("screenEncode", p);
    setState(s);
}

void ElectronCallUi::requestKeyFrame() { send(QJsonObject{{"type", "keyframe"}}); }

void ElectronCallUi::rename(const QString& name) {
    if (!m_hasState || name.isEmpty()) return;
    QJsonObject s = m_state;
    s.insert("name", name);
    setState(s);
}

void ElectronCallUi::members(const QJsonArray& list) {
    if (!m_hasState) return;
    QJsonObject s = m_state;
    s.insert("members", list);
    setState(s);
}

void ElectronCallUi::speaking(uint32_t src, bool on) {
    send(QJsonObject{{"type", "speaking"}, {"src", double(src)}, {"on", on}});
}

void ElectronCallUi::video(const UiVideo& v) {
    if (m_fallback || m_sock->state() != QAbstractSocket::ConnectedState) return;
    QJsonObject head{{"type", "video"}, {"key", v.key}, {"codec", v.codec}, {"bin", v.data.size()}};
    if (v.src) head.insert("src", double(*v.src));
    m_sock->write(QJsonDocument(head).toJson(QJsonDocument::Compact) + '\n');
    m_sock->write(v.data);
}

void ElectronCallUi::onData() {
    m_buf += m_sock->readAll();
    for (int i = m_buf.indexOf('\n'); i >= 0; i = m_buf.indexOf('\n')) {
        const QByteArray line = m_buf.left(i);
        m_buf.remove(0, i + 1);
        QJsonParseError err{};
        const QJsonDocument d = QJsonDocument::fromJson(line, &err);
        if (err.error != QJsonParseError::NoError || !d.isObject()) continue;
        onAction(d.object());
    }
}

void ElectronCallUi::onAction(const QJsonObject& m) {
    const QString action = m.value("action").toString();
    if (action == QLatin1String("accept")) {
        if (m_accept) {
            auto a = m_accept;
            m_accept = nullptr;
            a(m.value("camStartOff").toBool());
        }
        return;
    }
    if (action == QLatin1String("reject")) {
        if (m_reject) {
            auto r = m_reject;
            m_reject = nullptr;
            r(m.value("why").toString(QStringLiteral("reject")));
        }
        return;
    }
    if (m_listener == nullptr) return;
    if (action == QLatin1String("hangup")) {
        if (m_reject) {
            auto r = m_reject;
            m_reject = nullptr;
            r(QStringLiteral("reject"));
            return;
        }
        m_listener->uiHangup();
    } else if (action == QLatin1String("camera")) {
        m_listener->uiCamera(m.value("on").toBool());
    } else if (action == QLatin1String("screen")) {
        m_listener->uiScreen(m.value("on").toBool());
    } else if (action == QLatin1String("tile")) {
        m_listener->uiTile(js::toUint32(js::number(m.value("src"))), js::number(m.value("width")));
    } else if (action == QLatin1String("needkey")) {
        m_listener->uiNeedKey(m.value("why").toString(QStringLiteral("decoder")).left(40), m.value("src"));
    } else if (action == QLatin1String("log")) {
        zlog(QStringLiteral("window: %1").arg(m.value("text").toString().left(300)));
    } else if (action == QLatin1String("device")) {
        const QString kind = m.value("kind").toString();
        if (kind == QLatin1String("mic") || kind == QLatin1String("speaker")) m_listener->uiDevice(kind, m.value("id").toString());
    } else if (action == QLatin1String("micVolume")) {
        if (m.value("value").isDouble()) m_listener->uiMicVolume(m.value("value").toDouble());
    } else if (action == QLatin1String("mute")) {
        m_state.insert("muted", m.value("on").toBool());
        m_listener->uiMute(m.value("on").toBool());
    } else if (action == QLatin1String("speaker")) {
        m_state.insert("speakerOff", m.value("on").toBool());
        m_listener->uiSpeaker(m.value("on").toBool());
    } else if (action == QLatin1String("videoFrame")) {
        const QByteArray b64 = m.value("data").toString().toLatin1();
        if (m.value("data").isString())
            m_listener->uiVideoFrame(OutVideoFrame{m.value("key").toBool(), QByteArray::fromBase64(b64), m.value("screen").toBool(),
                                                   m.value("w").toInt(), m.value("h").toInt()});
    } else {
        zlog(QStringLiteral("call ui: unknown action %1").arg(action));
    }
}

void ElectronCallUi::useFallback() {
    if (m_fallback) return;
    m_fallback = std::make_unique<CallWindow>();
    m_fallback->setHangup([this] { if (m_listener) m_listener->uiHangup(); });
    if (!m_hasState) return;
    const QString phase = m_state.value("phase").toString();
    if (phase == QLatin1String("incoming") && m_accept) {
        UiIncoming f;
        f.title = m_state.value("name").toString();
        f.statusCode = m_state.value("statusCode").toString();
        f.statusParams = m_state.value("statusParams").toObject();
        m_fallback->incoming(f,
                             [this](bool) { if (m_accept) { auto a = m_accept; m_accept = nullptr; a(false); } },
                             [this](const QString& why) { if (m_reject) { auto r = m_reject; m_reject = nullptr; r(why); } });
    } else {
        m_fallback->open(fallbackOpen(m_state));
    }
}

}
