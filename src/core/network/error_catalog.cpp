#include "error_catalog.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QStandardPaths>

namespace Zalos::Core::Network::ErrorCatalog {

namespace {

Message make(const char* vi, const char* en, bool showCode = false) {
    return Message{QString::fromUtf8(vi), QString::fromUtf8(en), showCode, false};
}

struct PhoneEntry {
    const char* name;
    Message message;
};

const QHash<int, PhoneEntry>& phoneTable() {
    static const QHash<int, PhoneEntry> table{
        {1, {"NO_NETWORK",
             make("Không có kết nối. Vui lòng kiểm tra lại mạng và thử lại.",
                  "No connection. Please check your internet and then try again.")}},
        {2, {"ABORTED", make("Việc đồng bộ đã bị dừng.", "The sync was stopped.")}},
        {6, {"CLIENT_NOT_SUPPORT",
             make("Phiên bản Zalo trên điện thoại của bạn đã quá cũ. Vui lòng cập nhật phiên bản Zalo mới nhất "
                  "trên điện thoại và thử lại.",
                  "Your phone's Zalo app version is outdated. Please update to the latest version of Zalo on "
                  "your phone and try again.")}},
        {7, {"BUSY_RESTORING",
             make("Điện thoại đang bận sao lưu dữ liệu. Vui lòng thử lại sau khi sao lưu hoàn tất.",
                  "Your phone is in a backup process. Please try again when the backup is finished.")}},
        {8, {"USER_REJECT_BACKUP",
             make("Bạn đã hủy yêu cầu đồng bộ trên điện thoại.", "You rejected the sync request on your phone.")}},
        {9, {"USER_DONT_CONFIRM",
             make("Bạn cần xác nhận trên điện thoại để bắt đầu đồng bộ.",
                  "You need to confirm on your phone to start syncing.")}},
        {10, {"BACKUP_TIMEOUT",
              make("Vui lòng giữ Zalo mở và không tắt màn hình điện thoại khi đang chuẩn bị dữ liệu.",
                   "Please keep the Zalo app open and do not turn off your phone while preparing the data.")}},
        {11, {"BACKUP_FAILED",
              make("Vui lòng giữ Zalo mở và không tắt màn hình điện thoại khi đang chuẩn bị dữ liệu.",
                   "Please keep the Zalo app open and do not turn off your phone while preparing the data.")}},
        {16, {"LOW_STORAGE",
              make("Dung lượng trống trên ổ đĩa không đủ để đồng bộ tin nhắn từ điện thoại.",
                   "There is not enough free space on your drive to sync message from your phone.")}},
        {3, {"UNKNOWN_ERROR", {}}},
        {4, {"PREPARE_NEW_SYNC_FAILED", {}}},
        {5, {"REQUEST_BACKUP_FAILED", {}}},
        {12, {"NO_PERMISSION", {}}},
        {13, {"INVALID_BACKUP_INFO", {}}},
        {14, {"INVALID_BACKUP_ENCRYPT_FORMAT", {}}},
        {15, {"DOWNLOAD_FAILED", {}}},
        {17, {"DECRYPT_FAILED", {}}},
        {18, {"SYNC_FAILED", {}}},
        {19, {"GET_UID_FAILED", {}}},
    };
    return table;
}

Message phoneDefault() {
    return make("Đã xảy ra lỗi trong quá trình đồng bộ. Vui lòng thử lại.",
                "There were some error(s) in the process. Please try again.");
}

Message fallbackServer() {
    Message m = make("lỗi từ máy chủ Zalo", "an error from Zalo's servers", true);
    m.traceRaw = true;
    return m;
}

Message unknownError() {
    return make("lỗi không xác định", "unknown error");
}

}

Message apiError(int code) {
    switch (code) {
    case 600:
    case -102: return make("phiên đăng nhập đã hết hạn", "your session has expired");
    case 114: return make("tham số không hợp lệ", "invalid parameters");
    case 429: return make("thao tác quá nhanh, hãy thử lại sau ít phút", "too many requests, try again in a few minutes");
    case -69: return make("máy chủ yêu cầu ngừng thử lại (dịch vụ không dành cho phiên đăng nhập này)",
                          "the server said to stop retrying (the service is not offered to this session)");
    case 18022: return make("chữ ký yêu cầu không hợp lệ", "the request signature was rejected");
    case 18060: return make("máy chủ yêu cầu tham số được mã hoá", "the server requires encrypted parameters");
    case 126: return make("Tài khoản đã ngừng hoạt động.", "This account is no longer active.");
    case 164: return make("Bạn không còn là thành viên trong nhóm này", "You are no longer a member of this group");
    case 166: return make("Chỉ có trưởng/phó nhóm thực hiện được tính năng này.",
                          "Only the group owner or deputies can do this.");
    case 204: return make("Hiện tại Zalo chưa hỗ trợ chia sẻ tập tin định dạng này.",
                          "Zalo doesn't support sharing this file type yet.");
    case 211: return make("Phiên bản Zalo bên người nhận chưa hỗ trợ thu hồi tin nhắn",
                          "The recipient's Zalo version doesn't support recalling messages");
    case 216: return make("Danh tính người dùng này không hợp lệ.", "This user's identity is invalid.");
    case 301: return make("Bạn đã thu hồi quá số lần cho phép. Vui lòng thử lại sau 24 giờ.",
                          "You've recalled too many times. Please try again in 24 hours.");
    case 120: return make("Zalo: Hiện tại bạn không thể nhắn tin cho người lạ.",
                          "Zalo: You can't message strangers right now.");
    case 121: return make("Zalo: Tin nhắn có nội dung không phù hợp để gửi cho người lạ. Vui lòng kiểm tra lại.",
                          "Zalo: This message isn't appropriate to send to a stranger. Please check it.");
    case 122: return make("Bạn chưa thể gửi tin nhắn đến người này vì người này chặn không nhận tin nhắn từ người lạ.",
                          "You can't message this person yet because they don't accept messages from strangers.");
    case 206: return make("Bạn không thể gửi File cho người lạ.", "You can't send files to strangers.");
    case 804: return make("Bạn đã đạt giới hạn trả lời người lạ trong tháng. Để tránh rủi ro bị lừa đảo, bạn tạm thời sẽ "
                          "không thể trả lời người này.",
                          "You've reached this month's limit for replying to strangers. To avoid scams, you temporarily "
                          "can't reply to this person.");
    default: return {};
    }
}

Message transportError(const QString& raw) {
    if (raw.isEmpty()) return {};
    if (raw.startsWith(QLatin1String("HTTP "))) {
        bool ok = false;
        const int status = raw.mid(5).trimmed().toInt(&ok);
        if (!ok) return {};
        Message m;
        switch (status) {
        case 400: m = make("Máy chủ Zalo từ chối yêu cầu.", "Zalo's server rejected the request.", true); break;
        case 401:
        case 403: m = make("Bạn không có quyền thực hiện thao tác này.",
                           "You don't have permission for this action.", true); break;
        case 404: m = make("Không tìm thấy nội dung này trên máy chủ Zalo. Có thể nó đã bị xoá hoặc hết hạn.",
                           "This content was not found on Zalo's servers. It may have been deleted or expired.", true);
            break;
        case 413: m = make("Tệp quá lớn để gửi.", "The file is too large to send.", true); break;
        case 429: m = make("Thao tác quá nhanh, hãy thử lại sau ít phút.",
                           "Too many requests, try again in a few minutes.", true); break;
        default:
            if (status >= 500 && status < 600) {
                m = make("Máy chủ Zalo đang bận. Vui lòng thử lại sau.",
                         "Zalo's servers are busy. Please try again later.", true);
            } else {
                m = make("Lỗi kết nối tới máy chủ Zalo.", "Connection to Zalo's servers failed.", true);
            }
        }
        m.traceRaw = true;
        return m;
    }
    if (raw == QLatin1String("local_file_error")) {
        return make("không đọc hoặc ghi được tệp trên máy này", "couldn't read or write the file on this computer");
    }
    if (raw == QLatin1String("upload_timeout")) {
        return make("máy chủ không phản hồi", "the server didn't respond");
    }
    if (raw == QLatin1String("not_logged_in")) return make("bạn chưa đăng nhập", "you're not signed in");
    if (raw == QLatin1String("service_unavailable")) {
        return make("máy chủ Zalo chưa cung cấp dịch vụ này cho phiên đăng nhập hiện tại",
                    "Zalo's servers don't offer this service to the current session");
    }
    if (raw == QLatin1String("decrypt_failed")) {
        return make("phản hồi từ máy chủ không hợp lệ", "the server reply was invalid");
    }
    const QString lower = raw.toLower();
    if (lower.contains(QLatin1String("canceled")) || lower.contains(QLatin1String("cancelled"))
        || lower.contains(QLatin1String("aborted"))) {
        return make("yêu cầu đã bị huỷ", "the request was cancelled");
    }
    if (lower.contains(QLatin1String("host")) || lower.contains(QLatin1String("refused"))
        || lower.contains(QLatin1String("timed out")) || lower.contains(QLatin1String("timeout"))
        || lower.contains(QLatin1String("network")) || lower.contains(QLatin1String("unreachable"))
        || lower.contains(QLatin1String("closed")) || lower.contains(QLatin1String("temporary failure"))
        || lower.contains(QLatin1String("session"))) {
        Message m;
        if (lower.contains(QLatin1String("host")) && lower.contains(QLatin1String("found"))) {
            m = make("không tìm thấy máy chủ Zalo, kiểm tra kết nối mạng", "Zalo's server could not be found, check your network");
        } else if (lower.contains(QLatin1String("refused"))) {
            m = make("máy chủ Zalo từ chối kết nối, thử lại sau", "Zalo's server refused the connection, try again later");
        } else if (lower.contains(QLatin1String("timed out")) || lower.contains(QLatin1String("timeout"))) {
            m = make("kết nối tới máy chủ Zalo quá lâu", "the connection to Zalo's servers timed out");
        } else {
            m = make("lỗi kết nối mạng", "network error");
        }
        m.traceRaw = true;
        return m;
    }
    if (lower.contains(QLatin1String("ssl")) || lower.contains(QLatin1String("tls"))
        || lower.contains(QLatin1String("certificate"))) {
        Message m = make("không thiết lập được kết nối an toàn tới máy chủ Zalo",
                         "a secure connection to Zalo's servers couldn't be established");
        m.traceRaw = true;
        return m;
    }
    return {};
}

Message phoneSyncError(int code) {
    const auto& table = phoneTable();
    const auto it = table.constFind(code);
    if (it == table.cend() || it->message.isEmpty()) return phoneDefault();
    return it->message;
}

QString phoneSyncName(int code) {
    const auto& table = phoneTable();
    const auto it = table.constFind(code);
    return it == table.cend() ? QStringLiteral("UNMAPPED") : QString::fromLatin1(it->name);
}

Message describe(int code, const QString& raw) {
    if (code == 0 && raw.isEmpty()) return unknownError();
    const Message api = apiError(code);
    if (!api.isEmpty()) return api;
    const Message transport = transportError(raw);
    if (!transport.isEmpty()) return transport;
    return fallbackServer();
}

QString pick(const Message& message, bool english) {
    return english ? message.en : message.vi;
}

QString render(int code, const QString& raw, bool english) {
    const Message m = describe(code, raw);
    QString text = pick(m, english);
    if (m.showCode && code != 0) {
        text += english ? QStringLiteral(" (code %1)").arg(code) : QStringLiteral(" (mã %1)").arg(code);
    }
    return text;
}

void trace(const QString& scope, const QString& line) {
    const QString dir = QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath(QStringLiteral("logs"));
    QDir().mkpath(dir);
    QFile f(QDir(dir).filePath(QStringLiteral("errors.log")));
    if (f.size() > 512 * 1024) f.remove();
    if (!f.open(QIODevice::Append | QIODevice::Text)) return;
    f.write((QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + " [" + scope + "] " + line + "\n").toUtf8());
}

}
