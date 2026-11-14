// SPDX-License-Identifier: BSD-3-Clause
#include "call_ui.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>

namespace zcall {

namespace {

const QHash<QString, QString>& templates() {
    static const QHash<QString, QString> map{
        {QStringLiteral("calling"), QStringLiteral("Đang gọi…")},
        {QStringLiteral("ringing"), QStringLiteral("Đang đổ chuông…")},
        {QStringLiteral("connecting"), QStringLiteral("Đang kết nối…")},
        {QStringLiteral("connected"), QStringLiteral("Đã kết nối")},
        {QStringLiteral("ended"), QStringLiteral("Đã kết thúc")},
        {QStringLiteral("endedWithDuration"), QStringLiteral("Kết thúc — {duration} s")},
        {QStringLiteral("disconnected"), QStringLiteral("Mất kết nối với bộ gọi điện")},
        {QStringLiteral("incomingVoice"), QStringLiteral("Cuộc gọi thoại đến")},
        {QStringLiteral("incomingVideo"), QStringLiteral("Cuộc gọi video đến")},
        {QStringLiteral("recipientUnreachable"), QStringLiteral("Người nhận không liên lạc được")},
        {QStringLiteral("recipientNoAnswer"), QStringLiteral("Người nhận không trả lời")},
        {QStringLiteral("recipientDeclined"), QStringLiteral("Người nhận đã từ chối")},
        {QStringLiteral("callFailed"), QStringLiteral("Không gọi được")},
        {QStringLiteral("serverUnreachable"), QStringLiteral("Không kết nối được máy chủ")},
        {QStringLiteral("groupUnsupported"), QStringLiteral("Gọi nhóm chưa được hỗ trợ trên Linux")},
        {QStringLiteral("groupCallIgnored"), QStringLiteral("Đã bỏ qua cuộc gọi nhóm")},
        {QStringLiteral("groupEnded"), QStringLiteral("Cuộc gọi nhóm đã kết thúc")},
        {QStringLiteral("groupCallFailed"), QStringLiteral("Không gọi được nhóm")},
        {QStringLiteral("groupCallFailedReason"), QStringLiteral("Không gọi được: {message}")},
        {QStringLiteral("groupJoinRejected"), QStringLiteral("Không vào được cuộc gọi nhóm (máy chủ từ chối)")},
        {QStringLiteral("groupDeclined"), QStringLiteral("Đã từ chối cuộc gọi nhóm")},
        {QStringLiteral("groupKicked"), QStringLiteral("Bạn đã bị mời ra khỏi cuộc gọi")},
        {QStringLiteral("groupNoAnswer"), QStringLiteral("Không ai trả lời")},
        {QStringLiteral("groupCancelled"), QStringLiteral("Đã huỷ cuộc gọi nhóm")},
        {QStringLiteral("groupCalling"), QStringLiteral("Đang gọi nhóm…")},
        {QStringLiteral("groupRinging"), QStringLiteral("Đang đổ chuông…")},
        {QStringLiteral("groupWaitingOthers"), QStringLiteral("Đang chờ người khác…")},
        {QStringLiteral("groupParticipants"), QStringLiteral("{count} người: Bạn, {names}")},
        {QStringLiteral("incomingGroupInvite"), QStringLiteral("{inviter} mời bạn vào cuộc gọi nhóm")},
        {QStringLiteral("incomingGroupUnsupported"),
         QStringLiteral("{inviter} đang gọi nhóm. Gọi nhóm chưa được hỗ trợ trên Linux.")},
        {QStringLiteral("audioMicSpeakerFailed"), QStringLiteral("Không mở được micro và loa. Kiểm tra PipeWire / PulseAudio.")},
        {QStringLiteral("audioMicFailed"),
         QStringLiteral("Không mở được micro, bên kia không nghe thấy bạn. Kiểm tra PipeWire / PulseAudio.")},
        {QStringLiteral("audioSpeakerFailed"), QStringLiteral("Không phát được âm thanh ra loa. Kiểm tra PipeWire / PulseAudio.")},
    };
    return map;
}

QString paramText(const QString& key, const QJsonValue& v) {
    if (v.isArray()) {
        QStringList parts;
        for (const QJsonValue& item : v.toArray()) {
            const QString name = item.toString();
            parts << (name.isEmpty() ? QStringLiteral("Thành viên") : name);
        }
        return parts.join(QStringLiteral(", "));
    }
    if (v.isDouble()) return QString::number(qint64(v.toDouble()));
    const QString text = v.toString();
    if (text.isEmpty() && key == QLatin1String("inviter")) return QStringLiteral("Ai đó");
    return text;
}

}

QString statusText(const QString& code, const QJsonObject& params) {
    const QString tpl = templates().value(code, code.isEmpty() ? QString() : templates().value(QStringLiteral("ended")));
    static const QRegularExpression placeholder(QStringLiteral("\\{([A-Za-z][A-Za-z0-9_]*)\\}"));
    QString out;
    qsizetype last = 0;
    for (auto it = placeholder.globalMatch(tpl); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += tpl.mid(last, m.capturedStart() - last);
        out += paramText(m.captured(1), params.value(m.captured(1)));
        last = m.capturedEnd();
    }
    out += tpl.mid(last);
    return out;
}

}
