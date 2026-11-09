pragma Singleton
import QtQuick

QtObject {
    id: root

    property string currentLanguage: "vi"
    property var s: translations[currentLanguage] || translations["vi"]
    property int _rev: 0

    signal languageChanged(string lang)

    function setLanguage(lang: string): void {
        if (lang === "en" || lang === "vi") {
            currentLanguage = lang;
            s = translations[currentLanguage] || translations["vi"];
            _rev++;
            languageChanged(lang);
        }
    }

    readonly property var translations: ({
        "vi": {
            "app_title": "Zalo",
            "search": "Tìm kiếm",
            "priority": "Ưu tiên",
            "other": "Khác",
            "classify": "Phân loại",
            "today": "Hôm nay",
            "yesterday": "Hôm qua",
            "just_now": "Vừa xong",
            "minutes_ago": "phút",
            "hours_ago": "giờ",
            "days_ago": "ngày",
            "active_now": "Đang hoạt động",
            "offline": "Ngoại tuyến",
            "you_prefix": "Bạn: ",
            "no_conversations": "Chưa có cuộc trò chuyện nào",
            "no_search_results": "Không tìm thấy kết quả phù hợp",
            "create_group": "Tạo nhóm",
            "add_friend": "Thêm bạn",
            "message_placeholder": "Nhập @, tin nhắn tới ",
            "send_hint": "Nhấn Enter để gửi, Shift + Enter để xuống dòng",
            "add_member_tooltip": "Thêm thành viên vào trò chuyện",
            "search_in_chat_tooltip": "Tìm kiếm trong tin nhắn",
            "voice_call_tooltip": "Bắt đầu cuộc gọi thoại",
            "video_call_tooltip": "Bắt đầu cuộc gọi video",
            "conversation_info_tooltip": "Thông tin hội thoại",
            "e2ee_encrypted_msg": "🔒 [Tin nhắn mã hóa E2EE]",
            "photo_msg": "[Hình ảnh]",
            "file_msg": "[Tập tin]",
            "sent": "Đã gửi",
            "delivered": "Đã nhận",
            "seen": "Đã xem",

            "nav_messages": "Tin nhắn",
            "nav_contacts": "Danh bạ",
            "nav_cloud": "zCloud Lưu trữ",
            "nav_tools": "Công cụ doanh nghiệp",
            "nav_settings": "Cài đặt Zalos",

            "menu_account_info": "Thông tin tài khoản",
            "menu_settings": "Cài đặt",
            "menu_data": "Quản lý dữ liệu",
            "menu_language": "Ngôn ngữ",
            "menu_about": "Giới thiệu Zalo",
            "menu_logout": "Đăng xuất",

            "contacts_title": "Danh bạ",
            "friend_list": "Danh sách bạn bè",
            "group_list": "Danh sách nhóm",
            "friend_requests": "Lời mời kết bạn",
            "search_contacts_placeholder": "Tìm bạn trong danh bạ...",
            "chat_action": "Nhắn tin",
            "accept": "Đồng ý",
            "decline": "Từ chối",
            "empty_contacts_title": "Danh bạ chưa có bạn bè",
            "empty_contacts_sub": "Bấm 'Thêm bạn' ở góc trên để tìm kiếm và kết nối bạn bè",
            "empty_groups_title": "Chưa tham gia nhóm nào",
            "empty_invites_title": "Không có lời mời kết bạn nào",
            "friend_count_label": "Bạn bè",
            "group_count_label": "Nhóm",

            "settings_title": "Cài đặt",
            "general_settings": "Cài đặt chung",
            "appearance_settings": "Giao diện",
            "language_settings": "Ngôn ngữ",
            "privacy_settings": "Quyền riêng tư",
            "notification_settings": "Thông báo",
            "sound_settings": "Âm thanh",
            "select_language": "Chọn ngôn ngữ hiển thị:",
            "dark_mode": "Chế độ tối",
            "light_mode": "Chế độ sáng",
            "system_mode": "Theo hệ thống",
            "theme_light": "Sáng",
            "theme_dark": "Tối",
            "theme_system": "Hệ thống",
            "vietnamese": "Tiếng Việt (Mặc định)",
            "english": "English (Tiếng Anh)",
            "save": "Lưu",
            "close": "Đóng",
            "logout": "Đăng xuất",
            "anti_peeking": "Chế độ chống nhìn trộm (ZaDark)",
            "block_typing": "Chặn hiển thị 'Đang soạn tin'",
            "block_delivered": "Chặn gửi trạng thái 'Đã nhận'",
            "block_seen": "Chặn gửi trạng thái 'Đã xem'",
            "start_with_system": "Tự khởi động cùng hệ thống",
            "start_hidden": "Khởi động thu nhỏ dưới khay hệ thống",

            "welcome_title": "Chào mừng bạn đến với Zalo PC",
            "welcome_desc": "Khám phá những tiện ích hỗ trợ làm việc và trò chuyện cùng người thân, bạn bè được tối ưu hóa cho máy tính của bạn."
        },
        "en": {
            "app_title": "Zalo",
            "search": "Search",
            "priority": "Priority",
            "other": "Other",
            "classify": "Labels",
            "today": "Today",
            "yesterday": "Yesterday",
            "just_now": "Just now",
            "minutes_ago": "mins",
            "hours_ago": "hours",
            "days_ago": "days",
            "active_now": "Active now",
            "offline": "Offline",
            "you_prefix": "You: ",
            "no_conversations": "No conversations yet",
            "no_search_results": "No matching results found",
            "create_group": "Create Group",
            "add_friend": "Add Friend",
            "message_placeholder": "Enter @, message to ",
            "send_hint": "Press Enter to send, Shift + Enter for new line",
            "add_member_tooltip": "Add member to chat",
            "search_in_chat_tooltip": "Search in messages",
            "voice_call_tooltip": "Start voice call",
            "video_call_tooltip": "Start video call",
            "conversation_info_tooltip": "Conversation info",
            "e2ee_encrypted_msg": "🔒 [E2EE Encrypted Message]",
            "photo_msg": "[Photo]",
            "file_msg": "[File]",
            "sent": "Sent",
            "delivered": "Delivered",
            "seen": "Seen",

            "nav_messages": "Messages",
            "nav_contacts": "Contacts",
            "nav_cloud": "zCloud Storage",
            "nav_tools": "Business Tools",
            "nav_settings": "Zalos Settings",

            "menu_account_info": "Account Info",
            "menu_settings": "Settings",
            "menu_data": "Data Management",
            "menu_language": "Language",
            "menu_about": "About Zalo",
            "menu_logout": "Log Out",

            "contacts_title": "Contacts",
            "friend_list": "Friend List",
            "group_list": "Group List",
            "friend_requests": "Friend Requests",
            "search_contacts_placeholder": "Search contacts...",
            "chat_action": "Message",
            "accept": "Accept",
            "decline": "Decline",
            "empty_contacts_title": "No friends found",
            "empty_contacts_sub": "Click 'Add Friend' above to find and connect with friends",
            "empty_groups_title": "You have not joined any groups",
            "empty_invites_title": "No pending friend requests",
            "friend_count_label": "Friends",
            "group_count_label": "Groups",

            "settings_title": "Settings",
            "general_settings": "General",
            "appearance_settings": "Appearance",
            "language_settings": "Language",
            "privacy_settings": "Privacy",
            "notification_settings": "Notifications",
            "sound_settings": "Sounds",
            "select_language": "Select display language:",
            "dark_mode": "Dark mode",
            "light_mode": "Light mode",
            "system_mode": "System default",
            "theme_light": "Light",
            "theme_dark": "Dark",
            "theme_system": "System",
            "vietnamese": "Tiếng Việt (Vietnamese)",
            "english": "English",
            "save": "Save",
            "close": "Close",
            "logout": "Log Out",
            "anti_peeking": "Anti-peeking mode (ZaDark)",
            "block_typing": "Hide 'Typing...' indicator",
            "block_delivered": "Hide 'Delivered' receipt",
            "block_seen": "Hide 'Seen' receipt",
            "start_with_system": "Launch on system startup",
            "start_hidden": "Start minimized to tray",

            "welcome_title": "Welcome to Zalo PC",
            "welcome_desc": "Discover productivity and messaging features optimized for your desktop with family, friends, and colleagues."
        }
    })

    function tr(key: string): string {
        var langDict = translations[currentLanguage] || translations["vi"];
        return (langDict && langDict[key] !== undefined) ? langDict[key] : key;
    }

    function t(vi: string, en: string): string {
        return currentLanguage === "en" ? en : vi;
    }
}
