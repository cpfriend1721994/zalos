# Changelog

All notable changes to **Zalos** will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

---

## [1.3.3] - 2026-11-28

### Added
- **Phone-Style Layout for Narrow Windows:** Zalos fits screen tiles such as a quarter (480×1040) or an eighth (480×520) of a 1080p screen, and the minimum window size drops from 480×440 to 360×420. With one-column layout, an open chat fills the whole window (the navigation rail hides and the back arrow returns to the list), conversation info opens full window with a back arrow, and Settings opens full window in narrow or short windows.
- **Back to the Chat List in Narrow Windows:** in the one-column layout, Esc (when no dialog or menu is open), the mouse Back button and a two-finger swipe on the touchpad close the full-window conversation info, then return to the chat list.
- **Forgot Passcode:** the lock screen has a "Quên mã khóa?" button that, after confirmation, removes the passcode, signs out and deletes the messages saved on this computer.
- **Automatic Update Checks:** Settings › About replaces the "Kiểm tra cập nhật" button with "Tự động kiểm tra cập nhật" (Off, Every launch, Daily by default, Weekly) and shows when it last checked. A new version is announced once; AppImage, .deb and .rpm installs can also turn on "Tự động tải và cài bản mới". A failed check is retried later.
- **Select Several Messages:** "Chọn nhiều tin nhắn" in the message menu enters a selection mode to copy, forward or delete (for you) up to 100 messages at once.
- **Photo Albums:** sending two or more photos together posts them as one Zalo album, and incoming albums are grouped by their album id instead of by time.
- **GIF Tab:** the emoji and sticker panel has a GIF tab with trending GIFs and search, served by Zalo.
- **Important and Urgent Messages:** the bell button in the composer marks a message as Quan trọng or Khẩn cấp; marked messages, sent or received, show a coloured label.
- **Delete for Everyone in Groups:** group owners and admins can delete a member's message for everyone, after confirmation.
- **Mark as Read:** the chat list menu has "Đánh dấu đã đọc", and the filter menu has "Đánh dấu tất cả đã đọc" (with confirmation); "Đánh dấu đã đọc" in the system tray menu marks every chat as read when there are unread messages.
- **Mentions:** conversations with an unread mention show an `@` badge, and an `@` button in the chat jumps to each unread mention.
- **Undo Deleting a Conversation:** deleting a conversation hides it at once and shows "Hoàn tác" for 5 seconds before it is deleted on the server.
- **More Menus in the Chat Header and Composer:** in narrow windows, add member / create group and conversation info move into a `⋯` menu in the chat header (search too below 420 px). Below 480 px, the composer keeps sticker, image, file and voice buttons and moves screenshot, mention, contact card, poll, schedule, quick message, formatting and expand into a `⋯` menu.
- **Keyboard Shortcuts Sheet:** `Ctrl+/` or "Phím tắt" in the account menu lists every keyboard shortcut, including the ones you changed in `shortcuts-custom.json`.
- **Result List for Search in a Chat:** when searching inside a conversation, an eye button next to the up/down arrows opens the list of all matches (newest first, with sender, avatar, text and time, the current one highlighted), like the message results of the main search; clicking a match jumps to it.
- **Floating Date While Scrolling:** scrolling up through a chat shows the day of the messages at the top ("Hôm nay", "Hôm qua" or the date), which fades out shortly after you stop.

### Changed & Improved
- **Faster Chat Lists and Messages:** message merging reuses the open conversation's index, and the chat list and contact list update through one shared routine that moves rows instead of searching for them.
- **Faster Database Startup and Deletes:** the database checks each table's columns once instead of up to 15 times at startup, and deleting a conversation, message or expired messages runs in a single transaction.
- **Lighter Call Media Path:** video and audio packet protection avoids extra copies and reallocations, incoming video frames are reassembled into one pre-sized buffer, each message from the call engine is parsed once, outgoing camera frames are written without an extra copy, and oversized video frames from the call engine are skipped correctly.
- **Smoother Message List:** recycled message rows no longer replay their fade-in while you scroll fast, a row builds its context-menu data and full-time tooltip only when you right-click or hover, the "seen by" avatars are prepared only in group chats, and the big-emoji size of each message is worked out once and reused.
- **Less Work in the Background:** the call timer, GIF and sticker animations, loading spinners and the QR placeholder pause while the window is hidden or minimised, the screen-share picker previews load off the main thread, the image viewer's thumbnail strip reuses its items, recent stickers are no longer re-read from settings on every look, and shared photos and files of conversations unloaded from memory are released with them.
- **Short Windows:** below 600 px of height, conversation rows are 60 px with 40 px avatars, and the composer input grows from 1 to 4 lines instead of 3 to 8.
- **Wider Bubbles in Narrow Chats:** message bubbles keep a fixed gap on the opposite side instead of a flat 70 % width, so narrow chats wrap less.
- **Video Call Split View:** "Chia đôi" stacks the two videos top and bottom when the window is taller than wide, and the self preview scales with the window.
- **Sharp Rendering at Interface Scale:** icons, avatars, badges and rounded image masks use mipmapped vector/DPR-scaled rendering; online/offline status dots draw their ring and fill as a single concentric antialiased vector image so they stay round and centered at fractional scales (90%, 125%); and `:v` is redrawn as a 3D glossy vector matching the original `:v`, rendered 1:1 at the exact physical pixel size for every inline and large size across 80%–150% scale and HiDPI displays without jagged edges or downscaling blur.
- **Proportional Emoji & `:v` Scaling:** inline Unicode emoji (`16 / 19 / 23 px`) and vector `:v` (`14 / 17 / 20 px`) are visually balanced to equal diameter across small, normal, and large message text, standalone messages of 1–3 emojis enlarge without a bubble (`44 / 36 / 32 px` emoji, `40 / 33 / 29 px` `:v`), and color emoji fallback applies consistently across the emoji picker, reactions, and Settings.
- **Settings Layout:** the download folder, avatar cache limit and avatar clean-up settings use the same card rows as the other settings, and the storage tab's downloaded files entry follows the chosen download folder.
- **Dialogs in Narrow Windows:** confirmation dialogs shrink to fit the window instead of overflowing.
- **Voice Messages:** voice messages have a seek bar, a 1× / 1.5× / 2× speed chip and show the playing position; the next voice message in the chat plays automatically.
- **Pick Up Where You Left Off:** reopening a chat during the same session returns to the message you were reading when there are no new unread messages.
- **Drag and Drop Into the Composer:** dropped files and photos go to the attachment tray for review instead of being sent at once, and dropped text is inserted into the message.
- **File Downloads:** file cards show download progress and a Downloaded state with a button to show the file in its folder; "Lưu thành…" in the message menu saves files and videos anywhere, and Settings › General can change the download folder.
- **Stable Media Sizes:** photos and videos reserve their final size from the size the sender declared, so the message list no longer jumps while thumbnails load.
- **Tray Shows Offline State:** the tray icon turns grey with an "Offline" tooltip when the connection has been lost for a few seconds, and the light/dark mode toggle was removed from the tray menu (theme mode remains in Settings › Appearance).
- **Clear Reply Quotes:** quotes of photos, videos, files, stickers, voice messages, contact cards, locations and calls show labels such as `[Hình ảnh]`, `[Video]` or `[Tập tin] name` instead of raw links, JSON or internal text; an empty quote is filled from the original message when it is loaded.
- **Server Info Fallback:** when the server info request fails, Zalos retries it once in the newer Zalo Web format (all parameters signed, maintenance and bucket fields included) before the usual retries.
- **Login Screen:** the Zalos logo sits next to the name, the QR card shows an animated placeholder while the code loads, and the app version appears under the card.
- **Welcome Screen:** the slides describe what Zalos actually does (group calls marked as beta, what syncs with your phone, drag-and-drop files, `/` for quick messages), two new slides cover privacy (anti-peeking, passcode, hidden chats) and keyboard shortcuts, and buttons open the shortcuts list, phone sync and appearance settings.
- **Account Menu:** the menu opens with your avatar, name and phone number; clicking them opens your profile.
- **Conversation Info Buttons:** the quick action buttons share the panel width and keep two-line labels aligned, so none of them runs off the edge.
- **Empty Lists:** the chat list, Contacts, Create group and Forward show an icon, a title and a hint (and a Reload button where useful) instead of a plain line of text.
- **Consistent Text Sizes:** all text follows one size scale; a few titles that used 17 px (chat header name, Settings, phone sync prompt) now use 16 px like the other dialogs.
- **Screen Readers:** icon buttons, menu items, checkboxes and conversation info buttons announce their names to screen readers such as Orca.
- **Consistent Text Fields:** search and text boxes in Contacts, Profile, the info panel, Kiki and the schedule dialog use the same Zalos field style in light and dark mode.
- **README and Website:** install instructions no longer list Flatpak (Zalos is not on Flathub yet), the download commands always fetch the latest release for your machine's architecture, package sizes are updated (about 21 MB for .deb/.rpm, 26 MB for the AppImage), the Snap notes say OpenH264 is bundled, and the feature lists no longer claim end-to-end encryption.
- **Message Actions Match Each Message Type:** reactions, reply, forward, copy, pin and selection only appear where Zalo supports them. Calls, polls, greeting cards, bank transfer cards, Zalo notices and system lines no longer show a reaction button, hover actions or a right-click menu; stickers, voice messages and contact cards can be replied to but not reacted to or forwarded (forwarding sent only a plain-text copy); locations can be replied to and forwarded. Unsent messages no longer offer forward. When forwarding a selection, messages that can't be forwarded are skipped with a notice.
- **Scroll Bars Everywhere:** every scrolling list or panel has a scroll bar on its right edge that you can drag, like the message list: the chat list, search results, Contacts, Cloud, the Settings tab list, labels, reactions and "seen by" lists, the sticker and GIF panels, Business tools, the call device picker and others.
- **Search Results in Tabs:** the main search shows three tabs above the results, Trò chuyện, Liên hệ & nhóm and Tin nhắn, each with its count. Clicking a tab shows only that list; the tab opens on the first list that has results.
- **Same Highlight When Opening a Found Message:** opening a message from the main search now shows it like search inside a chat (centred with a blue outline, kept while the search results are open), and every jump to a message (search results, the arrows, quotes, pins, reactions) blinks the message once instead of keeping it highlighted for almost two seconds.

### Fixed
- **Cancelled Screenshot Sent Anyway:** cancelling the system screenshot dialog no longer captures and sends the whole screen.
- **Settings Split Across Two Files:** all settings now live in one file; existing values from the old file are moved over once on first start (so the sound language and other preferences are read correctly).
- **Sign-Out Leftovers:** signing out clears the state added in this release (scroll anchors, download progress, GIF results, pending attachments, urgency and mention markers) and the phone sync state is fully reset on failure.
- **Screenshot Temporary Files:** leftover screenshot files are removed with the other temporary files.
- **Sticker Search:** each sticker search now finishes once and releases its resources.
- **Avatar Loading Race:** cancelling an avatar load can no longer let a late decode report the image twice.
- **Phone Sync Cleanup:** an unreachable message-batch branch in phone history sync was removed.
- **Late Replies After Sign-Out:** group details, shared media, pinned messages and labels that arrive after you sign out or switch accounts are ignored instead of being written into the next session.
- **Recalled Messages in Notifications:** the desktop notification of a message that is recalled is closed.
- **Dock Badge and Notification Grouping in Flatpak and AppImage:** the unread badge and notifications use the app's actual desktop file name.
- **Start With the System in Flatpak:** the option uses the system background permission and reports when it isn't granted instead of showing "on" without effect.
- **Anti-Peeking in the Chat List:** with anti-peeking on, message previews in the chat list are hidden until you hover a conversation.
- **Muted Chats Still Notifying:** mute settings sent with prefixed ids (`group_…`, `g…`, `u…`), other id fields, numbers as text or millisecond start times are read correctly, and an error or unreadable mute reply no longer unmutes every chat.
- **App Lock Skipped After Sleep:** with auto-lock on, Zalos locks when the computer goes to sleep, since the idle timer does not count suspended time.
- **App Lock During Calls:** the idle auto-lock waits until an active call ends.
- **Passcode Attempts Reset on Restart:** failed attempts and the waiting time are kept across restarts.
- **Esc and Shortcuts With Dialogs Open:** Esc closes Settings, group, label, poll, forward and business dialogs again, and app shortcuts no longer act behind an open dialog (a regression in 1.3.2).
- **Distorted Default Emoji:** the default quick emoji buttons in Settings › Chat render without the distortion of native text rendering, and each emoji sits centred in its circle.
- **32-bit ARM Build:** bundled libopus intrinsics are disabled on 32-bit ARM so the call audio engine builds there.
- **Phone History Import:** contact lookups during phone sync go one batch at a time like Zalo PC, so group messages no longer get the wrong sender, and the expiring-message check uses its index instead of scanning every message (#19, #20, thanks @nguyenphivn).
- **Zalos Name Everywhere:** the startup screen says "Zalos" instead of "Zalo", the welcome screen greets you to Zalos instead of "Zalo PC", Settings › About describes Zalos as an unofficial Zalo client, and the tray shows "Người dùng Zalo" for a caller without a name.

### Security
- **Message Links Never Reach Local Files:** links, photos and files received from other people can no longer point at files on your computer. Before this fix, a crafted message could make Zalos upload a local file (including the saved sign-in session) when you forwarded it, or copy and open a local program when you clicked it. Your own messages keep working as before.
- **Safer Link Opening:** message links, link cards, locations and pins open only web, mail and phone links. Other link types (network shares, app handlers) are refused.
- **Sign-In Cookies Over HTTPS Only:** Zalo session cookies are sent only over encrypted connections; plain `http://` Zalo addresses are upgraded to HTTPS first, and redirects to unencrypted addresses are stopped.
- **Call Video Hardening:** oversized video frames from the other side of a call are dropped instead of being able to corrupt memory in Zalos, camera size requests are capped at 1920 px, and group calls track at most 64 video streams.
- **Private Call Connection Keys:** the keys that connect Zalos to its call engine no longer appear in the process list visible to other users of the same computer, and each connection slot accepts only one client.
- **Update Verification Required:** in-app updates refuse to install when the release has no checksum file, and packages are downloaded into a private folder instead of Downloads before installation.

## [1.3.2] - 2026-11-26

### Added
- **Chat Settings & Default Quick Emoji:** Added a dedicated Settings › Chat Settings section holding chat background options and a configurable default quick emoji (default: Like `👍`), updating both the composer quick-send button and the hover quick-react button under each message.
- **System Tray Notifications Toggle & Settings Shortcut:** Added `Notifications` (toggle) and `Settings` entries to the system tray menu.
- **Interface Scale:** Added `80%`, `90%`, `100%`, `125%`, and `150%` UI scale options under Settings › Appearance (with restart banner and clean isolation from launched external browsers/file dialogs).
- **Multi-Monitor & Wayland Portal Screen Sharing:** sharing your screen in a 1-1 video call now starts immediately when only one monitor is connected; with multiple monitors on X11 (or `grim` fallback), a screen picker shows previews, monitor names, resolutions, and an "All monitors" stitched option. On Wayland, Zalos uses the system ScreenCast portal and loads `libpipewire-0.3.so.0` at runtime (falling back to helper tools when unavailable, with no new hard dependency).

### Changed & Improved
- **Bundled libopus 1.6.1 for Calls:** the call audio engine (`zalos-call-audio`) now links its own libopus 1.6.1 instead of loading the system copy or a private fallback, so voice calls use the same codec build on every install. `zalos --check-deps` reports `libopus 1.6.1`. Packagers who need the distribution's libopus can configure with `-DZALOS_SYSTEM_OPUS=ON`.
- **No Separate libopus Package:** `.deb`, `.rpm`, AppImage, Snap and Nix builds no longer recommend, bundle or require a libopus package.
- **Zstandard Linked at Build Time:** Sync v2 phone history decompression links `libzstd` when Zalos is built instead of loading it at runtime, removing the runtime fallback path.
- **Single SQLite Copy:** the local database uses the SQLite built into Qt's `QSQLITE` driver, upgraded to SQLite 3.54.1 (with FTS5). This includes the WAL-reset corruption fix (3.51.3+) that the previous 3.49.1 driver lacked. Packages no longer ship or depend on a system `libsqlite3`.
- **Polls in Chat:** pick an option directly on the poll card and press "Bình chọn" there, without opening details. The vote dialog's confirm button is also "Bình chọn". An empty vote is no longer sent; Zalos asks you to choose an option first.
- **Less Debug Logging:** debug log files written during poll and call troubleshooting are removed. Phone history sync traces stay in `logs/errors.log` and `mobile-sync.log`.
- **Sync v2 Decoding:** zstd batches are always decoded by the streaming decoder, so batches with several frames of declared size no longer fail.
- **Tray Status Updates Live:** the system tray availability follows the tray host as it appears or disappears, and the window no longer waits on the tray host while it is checked.
- **Toast Above Call Bar:** notifications no longer cover the call control bar while the call overlay is expanded.
- **Group Link Retry:** when the group link cannot be loaded, the manage panel shows an error with a retry button instead of "link disabled".
- **Confirmed Changes Only:** profile, friend, group name and forward actions report success only after the server confirms them; while offline they are refused with a message.
- **Storage Tab Loads in Background:** the storage tab shows a loading state and fills in usage when it is ready.
- **Faster Startup With Older Cache:** cached chats parsed by an older version are migrated in small steps, so startup no longer freezes.

### Fixed
- **Poll Votes Not Recorded:** choosing an option and pressing "Bình chọn" now sends the selected option; options are read by id even when the server uses another field name; a reused poll card no longer keeps selections from another poll.
- **Poll Vote Button Stays Locked:** after you vote, the "Bình chọn" / "Đổi lựa chọn" button in the poll card and in the poll dialog stays disabled until your selection differs from your saved vote, including after returning to the chat. Changing a selection works again in both places.
- **Older Messages Stuck Loading:** a stale history reply no longer leaves the loading flag set, so older messages can load again.
- **Unread Counts Reset:** unread counts of conversations whose messages are not loaded are kept on read-point sync.
- **Messages Marked Seen While Hidden:** messages arriving while the window is hidden in the tray are counted as unread; seen is sent once the window is visible again.
- **Group Read-Only After Re-add:** a group locked after you were removed becomes writable again when you are added back.
- **Duplicate Messages on Timeout:** send requests are not re-sent after a timeout that may have reached the server; connection-level failures still retry.
- **Session Lost on Proxy Errors:** a proxy or captive-portal error during session restore no longer deletes the saved session.
- **Auto Gain Stuck in Calls:** the microphone auto-gain no longer stays blocked after the first quiet window.
- **Grouped Messages After Polls:** a message after a poll, system notice or e-card shows its avatar and name again.
- **Text Formatting After Restart:** bold, italic, colour and size on plain messages come back after restarting the app.
- **Empty Chat Shown While Loading:** the "no messages" placeholder no longer appears while history loads, and its text is translated.
- **Video File Download Button:** the download button on a video file card downloads the file instead of opening the viewer.
- **Group Confirm Labels:** disband, transfer, block, leave and remove confirmations use specific button labels.
- **Sent Friend Requests:** a failed refresh keeps the existing list.
- **Malformed Packets:** a failed HMAC computation is rejected instead of reading past the buffer; the camera stop path no longer races with its encode thread.
- **Call Bridge Bounds:** an out-of-range UI frame size from the call engine is rejected instead of stalling the parser.
- **Photo Grouping Refresh:** when a message text changes, the neighbouring photos and stickers in the open chat regroup right away.
- **Search After Chat Rename:** renaming a chat re-applies the active search and stranger filter, so the list matches the new name.
- **Headset Plugged In During Calls:** plugging in or removing a microphone or speaker during a call refreshes the device list without reopening the device menu.
- **Group Input Lock After Role Change:** the message box lock of a group updates when admin or deputy rights change in group settings.
- **Storage Cleanup Off the UI Thread:** changing the avatar limit or max age runs cleanup in the background.
- **Notification Sounds:** starting a new sound waits for the previous player to exit; the "Yours" folder label follows the app language.
- **Image Decoding Limit:** the image allocation limit is set once at startup instead of from worker threads.
- **Phone History Worker Safety:** the sync worker hands results to the app thread without holding a raw pointer across threads.
- **Faster Phone History Import:** message lookups by client message id now use their SQLite partial indexes (the query repeats `cli_msg_id <> ''`), so saving each synced message no longer scans the whole conversation and large phone history imports no longer slow down quadratically (#13, thanks @nguyenphivn).
- **Phone History Sync from iOS:** Sync v2 batches compressed as zstd frames without a content size (sent by iOS) are now decompressed with a bounded streaming decoder (64 MiB cap, concatenated frames supported, truncated or trailing data rejected) instead of failing (#15 & #16, thanks @nguyenphivn).
- **Quick Message Form:** the keyword and text fields clear after saving or cancelling, and fill with the right entry when you edit another one.
- **Unfriended Users Removed:** a user removed from friends stays out of the friend list after restart.
- **Read Sync Not Starved:** read-point sync runs during a steady stream of incoming messages instead of waiting for a quiet moment.
- **Composer Lock After Deputy Change:** the "only admins can send" state updates at once when you are made or removed as deputy admin of an open group.
- **Alias Change in Group Chat:** changing a friend's alias updates sender names in an open group chat at once.
- **Pinned Poll Edits:** the poll card in a chat refreshes when a pinned poll is edited.
- **Stranger Name Retry:** a stranger's name lookup that failed is retried the next time the chat is opened.
- **Blocked List Over 100:** the blocked list loads every page, not only the first 100 entries.
- **Reminders Stable:** reminders no longer reload when an unrelated conversation changes.
- **Voice Note Re-record:** recording again right after a voice note no longer loses the first clip.
- **Mic Recovers After Unplug:** the call microphone recovers after being unplugged and plugged back in, without ending the call.
- **Call Packets From Unknown Sources:** relay and peer-to-peer call packets from unexpected sources are ignored, and replayed packets are dropped.
- **Smoother Call Volume:** the call volume slider applies changes without lag or out-of-order values.
- **Screen Share Unavailable:** when the screen cannot be captured, screen share shows "unavailable" instead of sharing the Zalos window.
- **Offline Store Warnings:** closing the offline store no longer logs duplicate connection warnings.
- **Album Spacing:** spacing between items in long media albums is consistent.
- **Screenshot Keeps Maximized / Fullscreen Window:** capturing a screenshot with the Zalos window visible no longer shrinks a maximized or fullscreen window back to the default size; restoring from minimized and hiding via shortcut also preserve the window state.
- **Video Calls & Video Playback Without Hardware Acceleration:** local camera preview, remote call video, screen-share preview, and in-app video playback now render via a native software video surface when hardware acceleration is off instead of showing a blank screen.

## [1.3.1] - 2026-11-25

### Added
- **Group Polls in Phone History:** group polls from the phone history sync are imported into the chat timeline.
- **Middle-Click Autoscroll:** press the middle mouse button over a list (chat, conversation list, contacts, settings) to scroll: hold and drag (farther = faster) or click once to scroll until the next click, `Esc` or a wheel scroll.

### Changed & Improved
- **Faster Scrolling:** mouse wheel notches move about 1.7× further (at least 5 lines), and touchpad scrolling is scaled to match.
- **Linked Devices in About:** Settings › About refreshes the linked device list every time the tab opens, and the section is labelled "Linked devices".

### Fixed
- **Text Messages Missing After Phone Sync:** text messages from phone history were skipped during sync; they are now imported.
- **Recalled Messages After Phone Sync:** messages recalled on the phone are now shown as recalled instead of as normal messages.
- **Expired Photo and Video Placeholders:** when several photos or videos have expired next to each other, each placeholder shows its icon above the text again, and the play button no longer covers "Video expired".
- **Hover Actions on Expired Media:** the reply, forward and more toolbar appears when hovering expired photos and videos, including grouped photos.
- **Reactions Reappearing After Removal:** a removed reaction no longer comes back when the same older reaction events arrive again during the session.
- **Late Call Reply Attaching to a New Call:** a delayed call-setup reply from a cancelled call is ignored when it carries a different call ID.
- **App Restart Duplicate Instance & Status Bar Icon:** Restarting Zalos (such as when toggling hardware acceleration in Settings) now forces a clean exit instead of hiding the old instance to the system tray, and unregisters the outgoing tray SNI D-Bus service before launching the replacement process so two app instances and duplicate status bar icons no longer appear.

## [1.3.0] - 2026-11-24

### Added
- **Full zBusiness Tools Integration:** Added Zalo server REST endpoints in `ZaloSession` (`/api/social/friend/get-bizacc`, `/api/prodcatalog/catalog/{list,create,update,delete,register}`, `/api/prodcatalog/product/{list,create,update,mdelete}`, `/api/autoreply/{list,create,update,delete}`) and wired `BusinessToolsModal` to display live business profile details (category, address, contact info, public catalog link), manage product catalogs & items (create, edit, delete, share to chat), and manage auto-reply rules (create, toggle enable/disable, edit scope/content, delete).
- **Location Map Previews Like Zalo PC:** Upgraded shared location messages (`chat.location.new`) with an interactive 156 px map preview card showing server thumbnails or zoom-15 OpenStreetMap tiles (with local disk/RAM caching and OSM `User-Agent`), topped with a centered Zalo-style sender avatar pin and click-to-open Google Maps link.
- **SQLite Label Cache:** Persists label definitions (`labels_cache`) in `ZaloOfflineStore` meta storage so navigation-rail folders and conversation label badges appear immediately at startup before the live refresh completes.

- **Reaction Notifications & Unseen Reaction Button:** when someone reacts to your message you now get a desktop notification ("… đã bày tỏ cảm xúc …", can be turned off in Settings › Notifications), and the chat shows a reaction button above "jump to latest" that takes you through each message with unseen reactions, oldest first; seen state syncs to Zalo on a best-effort basis.

### Fixed
- **Interface Font:** when the desktop font is not installed (e.g. KDE set to "Ubuntu" without the font), Zalos no longer picks its emoji font as the UI font. It now uses the desktop font only if it is installed and supports Vietnamese, otherwise Noto Sans (system copy, or a bundled OFL copy), and repairs previously cached wrong choices.
- **Links Open in the Browser:** links are opened with a clean environment so the browser or `kde-open` no longer crashes on the bundled Qt libraries; when no default browser exists, the link is copied and a notice explains it. Bare links such as `vt.tiktok.com/…` or `fb.watch/…`, links in shared link cards, system notices, pinned messages, profiles, polls, reminders, zBusiness texts, friend requests and Kiki are now clickable, and styled links are no longer cut in half.
- **zBusiness Product Catalogs (error 113):** catalogs are loaded 20 per page like Zalo PC instead of one oversized request.
- **Link Safety Check No Longer Blocks Opening:** a slow link safety check delays opening by at most 2.5 seconds instead of holding the link until the request times out.
- **Emoji Rendering & Bundled CBDT Fallback:** Removed `QtRendering` overrides in `ChatMessageRow` to use `NativeTextRendering` on bundled Qt 6.8.3 LTS, and bundled a compact ASCII-stripped `26px` CBDT color emoji fallback (`ZalosEmoji-CBDT.ttf`, `3.0 MiB`) registered via `QFont::insertSubstitution` so color emojis render reliably even on distros shipping vector-only `Noto-COLRv1.ttf`.
- **Call Hangup Completion, Outgoing Call Messages & Clean Engine Shutdown:** Fixed `ZrtcMedia::stop()` timer use-after-free crash (`QPointer<QTimer>` + `stopTimer()`), flushed `IpcLink::emitFrame()`/`ElectronCallUi::close()` immediately, and added `CallBridge` HTTP fallback for `/api/voicecall/endcall` & `cancel` so ending a call never leaves the remote peer stuck on "unstable connection". Fixed missing outgoing call history bubbles (`CallBridge::callFinished` now passes `isVideo` and emits a fallback on watchdog/exit; `ZaloClient::callFinished` syncs the latest server page via `requestOldMessages(isGroup, {})` instead of `loadOlderMessages()`; `MessageModel::mergeMessages` replaces nearby `local-call-*` placeholders in-place; `ZaloClient::toMessageItem` handles `recommened.calling` and `recommened.receivecall`). Fixed `zalos-call` `SIGTERM` pipe deadlock (`pipe2(O_NONBLOCK | O_CLOEXEC)`), added Linux `PR_SET_PDEATHSIG` to `zalos-call` and `zalos-call-audio`, made `ZrtcMedia::~ZrtcMedia()` reap `zalos-call-audio` synchronously (`stopAudio(true)`), and made `CallBridge::shutdown()` wait after `kill()` so child call processes exit cleanly. Also fixed call hangup/remote end-call gating (`CallBridge::handleVoipControl` applies the 60 s `ts` staleness guard only to incoming requests and arms a `1500 ms` hangup watchdog; `EngineCore` handles null `"0"` `callId`, `"endcall"`, and idle `hangup`; `ZaloSession::handleControls` parses stringified `content`) and ensured desktop/sound notifications for subsequent messages resume immediately once a call leaves ringing phases.
- **In-App Playback for Uploaded MP4 Videos & Image Viewer Polish:** Enabled LGPL `h264`/`mp3float` decoders in bundled slim FFmpeg 7.1.5, added authenticated local video caching (`ZaloClient::prepareVideoPlayback`) so `ImageViewer` plays remote MP4/MOV/WebM/MKV video uploads in-app (including `share.file` video cards with separate Play and Download actions), and fixed `ImageViewer` annotation toolbar layout, pointer isolation, and annotated image saving.
- **Wide Panoramic Image Sizing:** `ChatMessageRow` now lets wide photos expand up to the active bubble width (max `560px`, down to `48px` minimum height, `PreserveAspectFit`, `640×1280` decode target) instead of clipping horizontally at `360px`.
- **Desktop System Theme & Font Synchronization:** Fixed `ThemeManager` dark-mode black `QPalette::PlaceholderText` making input placeholders and unselected radio/checkbox borders unreadable, fixed System Light white-on-white chat/input contrast, added KDE `kdeglobals` palette/font priority over GTK portal overrides (installed fonts only), and bound `ApplicationWindow.font.family` so tooltips inherit the active font cleanly.
- **Navigation Rail Labels & Functional Fixes:** Removed `scale` distortion on `LabelFolder` hover, normalized Vietnamese/emoji initials (`NFC` + `Array.from`), improved light-mode label text contrast, fixed group "Seen" avatars/labels, emoji search reactivity, Kiki "Insert to chat" draft insertion, `ImageViewer` photo↔video ordering and annotation undo, rename group target locking, Forward modal list filtering, double-click poll creation, modal `Esc` handling & lock-screen overlay cleanup, and `effectsAvailable` guards.
- **Responsive Narrow Windows (480×440+):** Sidebar flexibly shrinks to `260–368px` (compact mode below `720px`), popups stay within margins with scrolling containers, horizontal button rows wrap cleanly, and network-sourced strings render as `Text.PlainText`.

### Changed & Improved
- **Native File Dialogs & Smaller Packages:** choosing photos, files, avatars, backgrounds and save locations now opens your desktop's own file dialog through the XDG portal (with `zenity`/`kdialog` fallback and an honest notice when none is available) instead of Qt Quick Dialogs, and the bundled SQLite driver uses the system SQLite instead of carrying its own copy — about 4 MB smaller per package (AppImage 30.6 MB → 26.9 MB).
- **Faster Releases:** each architecture is compiled once on Ubuntu 24.04; the `.deb`, AppImage and `.rpm` are all packaged from that build (RPM file names are unchanged), and the Fedora base image is no longer built in CI.
- **Native D-Bus Tray & Dropped `Qt6::Widgets`:** Replaced `QSystemTrayIcon`/`QMenu`/`QApplication` with direct D-Bus `org.kde.StatusNotifierItem` (`/StatusNotifierItem`), `com.canonical.dbusmenu` (`/MenuBar`), `org.freedesktop.Notifications`, and `QGuiApplication` (plus dynamic `ZalosEmoji-CBDT.ttf` 26 px compaction), eliminating `libQt6Widgets` (~2.9 MB compressed savings across all bundles) and shrinking packages to ~22 MB (`.deb`/`.rpm`) and ~27 MB (`AppImage`).
- **Universal Bundled Qt 6.8.3 LTS + QML AOT Across All Packages:** Migrated Fedora 43, Ubuntu 24.04, `.deb` (~22 MB), `.rpm` (~22 MB), `AppImage` (~27 MB), Flatpak, Snap, and Nix builds to Qt 6.8.3 LTS with `-DZALOS_QML_AOT=ON` (~90%+ C++ compiled bindings), `-Os` Release codegen, `xz -9` compression, slim FFmpeg 7.1.5 LGPL media playback (H.264/AAC/MP3), bundled CBDT color emoji fallback, dynamic host-ICU forwarding, and zero system-Qt dependency across distros via shared `packaging/common/bundle-qt6.sh`.
- **Reliability & Responsiveness Hardening:** Buffered up to 32 pre-auth outbound WebSocket frames until `1/1` handshake completes, moved `QMediaDevices` audio/camera enumeration off the GUI thread (`CallBridge::refreshDevicesAsync`), launched non-sandboxed local files asynchronously via detached `xdg-open` (`Fs::FileUtils::openLocalPath`), scoped Sync v2 identity keys per account, bounded concurrent sticker detail lookups (≤ 4), hardened `AvatarImageProvider::decodeScaled` against corrupt image headers, fixed the heap delta measurement scope in `testComprehensiveMemoryLifecycle`, preserved URLs when forwarding rich-link messages, and added the `cpp_invariants` CTest gate.
- **Architecture & Runtime Polish:** Split `zalo_client.cpp` into focused `zalo_client_{messages,profile,social}.cpp` units with expanded PCH (and `zcall_core` PCH), optimized `imageToI420Into` in 2×2 blocks, interned SQLite message metadata strings, eliminated duplicate `Screen.devicePixelRatio` multiplications on icons/avatars/media, capped `MediaRunRole` grouping at 16 items, virtualized member/voter lists with `ListView`, and moved clipboard PNG encoding and system-sound discovery off the GUI thread.

## [1.2.10] - 2026-11-23

### Fixed
- **Stuck on "Connecting to server…" (WebSocket error 501):** on Qt 6.4 (the AppImage's bundled Qt, the Ubuntu 24.04 `.deb` and the Snap) the realtime connection was opened without a `/` path, so Zalo's server answered HTTP 501 and messages never arrived in real time. The path is now always set (#6, #8).
- **Crash with Fractional Scaling on Wayland:** rounded avatars and images no longer crash when the window moves between screens with different scale factors (#7, thanks @QinZinn).
- **AppImage Falling Back to the Bundled Qt for No Reason:** a normal exit (`--version`, `--help`, `--check-deps`, or opening Zalos while it is already running) no longer counts as a failed start, so the next launch no longer shows "system Qt … failed to start last time" and keeps using the system Qt.
- **Chat Jumping Back to the Bottom:** scrolling up while photos are loading or new messages arrive no longer pulls the chat back to the latest message.
- **One-Way or Dropped Audio After Long Ringing or Network Switch:** while a 1-1 call is ringing, Zalos now pings every candidate relay server so the server does not drop the session before the other side answers. During a call, changing Wi-Fi/VPN or losing P2P audio now switches cleanly back to the relay server (`ChangeAddress` / `P2P stop` / 25-packet relay streak / 2 s audio stall recovery), handles SRTP rollover/reset across multiple ROC candidates, deduplicates dual P2P + relay packets, and resets the audio jitter buffer on large sequence jumps.
- **Poll Loading & Quote Replies:** `group_poll` requests now fall back to the `group` service host when `zpw_service_map_v3` omits `group_poll`, and `sendQuote` normalizes missing/`"0"` `qmsgOwner` and `qmsgCliId` fields so quoted replies are not rejected.
- **Missing Images After Viewing Messages on Phone:** opening a group (or 1-1 chat) after reading a photo + text sequence on mobile no longer shows only the latest text message; Zalos now unwraps nested/stringified `chat.photo` payloads and alternate URL keys, falls back to `tt-group-cm.api.zaloapp.com` for `group_cloud_message`, chains `/api/cm/getrecentv2` into cold-storage `/api/cm/getoldv2`, syncs recent history after startup login/reconnect and with partial local caches, and upgrades previously misparsed offline photo cache rows (`kCacheParseVersion = 6`).

### Changed & Improved
- **English by Default:** a new install starts in English until a language is chosen, and the language picked on the sign-in screen is now saved.
- **AppImage Uses the System Qt Only From 6.4 to 6.12:** older or newer system Qt versions, which Zalos is not tested against, run on the bundled Qt instead.
- **Auto / Manual Microphone Gain Toggle in Calls:** the in-call device menu now shows an **Auto / Manual** pill next to the microphone volume slider (`CMD_IN_AUTO_GAIN`); dragging the slider locks manual volume, and clicking the pill re-enables automatic clipping protection.
- **Richer Phone History Sync v2 (Beta):** Sync v2 now supports two-phase conversation discovery (`ConversationChunk` followed by batched `message` queries of up to 30 conversations with fresh ephemeral keys), wakes an idle phone with command `592` when `transfer_status` reports `status 2`, and imports quoted replies, `@mentions` (including `@All`), and message TTLs from `MetaSchema`.
- **Sidebar, Message Lookup & Resource Optimizations:** `ConversationModel` pre-computes folded names and handles multi-row reorders incrementally without resetting the sidebar list; `MessageModel` adds `O(1)` active-thread row indexing and targeted poll updates; `ImageViewer` releases its video player immediately when switching back to photos; `AvatarImageProvider` and `MobileHistorySync` eliminate redundant buffer copies while bounding Sync v2 batch/log growth; and active calls throttle interface-change polling to reduce background syscalls.

## [1.2.9] - 2026-11-20

### Added
- **Polls Like Zalo PC:** every poll event (created, voted, option added, closed, shared) now appears in the chat as an info line with the actor ("A member" for anonymous polls) and a "View" link, and the poll card sits under the latest event only. The card shows the poll settings, voter count, the top options with percentage bars, your choice and voter avatars.
- **Poll Vote Dialog:** clicking a poll opens a full dialog to vote with single or multiple choices, change or remove your vote, add options (and vote for them), see who voted for each option, close the poll (creator and group admins, with confirmation) and share it to the group.
- **Full Poll Creation Settings:** pin to top, multiple choices, members can add options, hide results until voted, hide voters and a voting deadline, with Zalo's limits (question up to 200 characters, options up to 120, 2–30 options, no duplicates). The dialog only closes once the server accepts the poll.
- **Pinned Polls:** polls pinned in a group are now recognised and can be opened from the pinned item.

### Fixed
- **Read-on-Phone Sync:** chats read on the phone or another device no longer stay unread. Read points are kept per conversation, only move forward and survive restarts, so they apply even when they arrive before or with the messages they cover (typical after a network drop or at startup) and for groups that haven't loaded yet. Zalos also fetches the latest read points of unread chats (`get-last-seen`, like Zalo PC) after reconnecting, when the window is shown and every 15 minutes, and reads them from every payload shape (`clearUnreads`, `clearUnreadInfo`, `statusMap`) and from group seen events.
- **Phone History Sync Failing Right After Confirmation:** a `transfer_error` with status 2 means the phone app went to the background, not that the sync failed; Zalos now asks you to keep Zalo open on the phone and keeps waiting instead of failing with code -1.
- **Poll Voting:** votes now send your whole selection, so multiple-choice polls no longer replace your earlier choice and votes can be removed; adding an option no longer casts a second vote on single-choice polls; voting on a locked poll shows a clear message; no more placeholder poll card with a fake id after creating a poll.
- **Image/Video Viewer Memory:** closing the viewer releases the video decoder and returns freed memory to the system, and full-size photos are no longer kept in the image cache.
- **QML Warnings on Startup:** fixed "Window.window only supports types derived from Item" in the welcome carousel, sidebar clock, typing indicator and voice-recording dot, so these timers and animations really pause while the window is hidden.

### Changed & Improved
- **Fewer Required System Packages:** `.deb`/`.rpm` now only require Qt and core libraries; libopus, PulseAudio and OpenH264 became recommendations, and every format ships a private libopus copy that is used only when the system has none. The `.deb` now recommends the GStreamer plugins needed to play voice messages and videos. Browser sign-in reads the browser key over D-Bus (Secret Service/KWallet) instead of needing `secret-tool`/`kwallet-query`.
- **No Crash Without Media Codecs:** on systems without Qt Multimedia's GStreamer/FFmpeg backend (e.g. the AppImage on a minimal distro) Zalos no longer aborts: notification sounds use the system player, voice/video playback and recording explain what is missing, and calls continue audio-only.
- **AppImage Picks Its Qt More Safely:** the system Qt is used only when it has every needed plugin (platform, multimedia, SVG, SQLite, TLS); if it crashes at startup, the next launch switches to the bundled Qt automatically and retries after a system Qt update.
- **Dependency Report:** `zalos --check-deps` (or `./Zalos-*.AppImage --check-deps`) lists which Qt, codecs, OpenH264 and audio libraries are found, to help with bug reports.
- **Software Rendering by Default:** hardware acceleration (GPU) is now off by default, and existing installs are switched to software once; it can be turned back on in Settings › General.
- **Automatic Software Fallback:** if a hardware renderer (Vulkan, OpenGL or the Qt default) crashes before its first frame or reports a scene graph error, Zalos turns acceleration off, restarts on the software renderer and explains why in Settings; after a Vulkan failure, re-enabling tries OpenGL. Settings › About now shows the renderer actually in use.

## [1.2.8] - 2026-11-19

### Fixed
- **Call Hangup Ending Remote Calls:** fixed `ElectronCallUi` leaving `m_reject` armed after `accept`, which caused post-accept `hangup` actions to invoke `reject` instead of `uiHangup()` and leave the remote peer ringing/connected; also clears callbacks when transitioning past `incoming`, sends UDP `EndCall` when cancelling pre-answer outgoing calls (`ZrtcMedia::stop`), handles `CallBridge::hangup()` cleanly before `m_uiSocket` connects, flushes `sendUi()`, guards >4 MiB video frames, and omits `undefined` keys instead of serializing `null` on group end/cancel (`12437`/`12438`).
- **Group Sidebar Sender Prefix & Notifications:** fixed group chat sidebar previews (plus desktop notifications, birthday ecards, offline `sharedMedia`, and SQLite startup backfill) omitting sender names when `ZaloClient::toMessageItem()` omits cached sender strings; adds incremental `ConversationModel::setSenderPrefix()` updates when unknown members or aliases finish resolving.
- **Image Click Preview:** fixed clicking photos in chat and the Info Panel failing to open `ImageViewer` (`openViewerRequested(msgId)` delegation, `MediaSource.toSource()`, and `sharedMedia` fallback).

### Changed & Improved
- **Performance, Media & Engine Hardening:** batched SQLite writes (`fetchFriends`, `applyGroupInfo`, `handleIncomingMessages`, deduped `handleReactions`), version-cached group sync (`m_groupVersions`), debounced contact/friend refreshes, cached `groupMembers` lookups, incremental model updates, paused hidden QML animations/timers, off-thread >32 KiB API decrypt/parse, single WebSocket reconnect timer, incremental upload MD5, streaming `MobileHistorySync` chunks, 64 MiB `fetchBytes` cap, static `.webp` detection, shared `ZConfirmDialog.qml` (removing `IncomingCallPopup.qml`), verified P2P candidate promotion, `thread_local` SRTP HMAC reuse, TWCC fixes, binary H.264 IPC framing, off-thread camera encoding (`YUV420P`/`NV12`/`YUYV`/`UYVY` fast paths), single-session PulseAudio probe, and `std::nth_element` jitter compaction.
- **Documentation & Packaging Cleanup:** aligned project descriptions, updated READMEs, website, RPM/AppStream metadata, and hardened packaging/CI scripts.

## [1.2.7] - 2026-11-18

### Added & Improved
- **Phone History Sync v2 Fallback (Beta):** implemented clean-room OpenSSL 3 metadata & message decryption (`openMetadata`, `openMessage`, `generateKeyPair`, `calculateAgreement`), Protobuf partition/message parsing (`parseSyncV2Chunk`), `cmd 590`/`591` WebSocket transport, persisted `ik` + fresh `ek` per session, and automatic V1→V2 fallback when V1 rejects with code `211` or times out after `transfer_sync2` (never mixing V1 and V2 in the same session). Added `ZALOS_FORCE_SYNC_V2=1` / `--force-sync-v2` debug flag for direct V2 testing.
- **Flexible `libzstd` Support:** links system `libzstd` at build time when available (`ZALOS_HAVE_SYSTEM_ZSTD`) and falls back to runtime `QLibrary("zstd")` loading otherwise.
- **Qt 6.4 & Threading Fixes:** fixed `Overlay.overlay` (`QtQuick.Templates as T`) and typing-dots `index` references in `LoginView.qml`, `ChatArea.qml`, `AppLinkPopup.qml`, and `HiddenPinPopup.qml` for older Qt 6.4 AppImage runtimes, and fixed `AvatarImageProvider` creating `QNetworkAccessManager` off the main thread.
- **Docs, Website & Packaging CI Sync:** updated Settings, Sync prompt, READMEs, and `docs/index.html` with beta sync wording, separate APT/DNF repo and `.deb`/`.rpm` instructions, Snap, Flatpak (Testing), and concise per-package call notes; synced Snapcraft version from `CMakeLists.txt` (`override-pull`), added Flathub `x-checker-data`, and added `publish-snap` / `sync-flatpak` CI jobs.
- **1-1 Screen Sharing, Floating PiP & Call Tray Controls:** added in-call screen sharing for both X11/Xorg and Wayland (`xdg-desktop-portal` ScreenCast + `QScreen::grabWindow` fallback, aspect-preserving OpenH264 encoding), a draggable floating PiP minimize mode to keep chatting during calls, window-hide-to-tray during active calls with a live call badge/tooltip and tray menu actions (Mute, Speaker, Camera, Screen Share, Hang Up, plus safe minimize+PiP fallback on tray-less desktops), in-call Mic/Speaker/Camera device picker, Mic volume slider (`0–100%`) with live VU meter, 50/50 split layout, adaptive 720p capture, repeat-frame on camera stall, and call-end tone.
- **Animated GIF Playback & Messaging Polish:** fixed animated GIFs freezing under GPU rounded masking (`ShaderEffectSource` `live: true` in `ZRoundedImage`), restored `animatedUrl` for cached `chat.gif` / `*_gif` messages in `ZaloOfflineStore::loadMessages` and `MessageModel::mergeMessages`, added `AnimatedImage` playback in `ImageViewer`, and added rich-text indent (`ind_10`/`ind_20`), custom GIF uploads, poll option additions/sharing, bank card messages, and startup blocklist sync.
- **IME, Typing & Network Retry Hardening:** fixed Wayland `Fcitx5-Bamboo` / `Lotus` Enter-to-send with synchronous preedit flush and duplicate-commit guard, prevented old history batches from clearing active typing indicators, and added multi-host failover + single-host transient blip retries (`5xx`/timeout/429) with WebSocket reconnect & sleep/wake reachability probes.

## [1.2.6] - 2026-11-16

### Added & Improved
- **Contacts, Groups & Safety Features:** added group invite box (list, join, delete), friend recommendations, mutual groups in user profiles (compact top placement, expandable above 3), E2EE linked devices viewer in Settings, safe link scanning (`scanURL`) before opening external URLs, abuse reporting, Community group upgrade for owners, bank card REST support, `transfer_after_login` `temp_key` forwarding for mobile sync, real My Documents avatar lookup via `getUserInfo` (with updated folder+cloud fallback), accurate gold verified badges on My Documents and Official/System accounts, and **Friends only** and **Strangers only** conversation filters.
- **UI & Text Rendering Polish:** enabled multi-insert in the emoji picker without auto-closing, replaced disappearing-message chips with a compact dropdown selector, centered the conversation info title and pin-detail modal, balanced `ConversationInfoPanel` scroll padding, renamed the group management entry to **Quản lý nhóm / Manage group**, and enabled `QtRendering` for chat message text.
- **Memory & Image Decoder Hardening:** added 64-entry `richTextCache` LRU (`MessageModel`), omitted duplicate per-message sender name/avatar copies, tightened resident thread windowing (`400` msgs budget / `100` msgs per inactive thread), disabled texture caching on large chat images (`ZRoundedImage cache: false`, `cacheBuffer: 280`), tightened `cacheBuffer` on Sticker and Contacts views (`120` / `160` px), standardized QML font weights to `Normal`/`DemiBold`, enabled lazy/as-needed ELF linking (`-Wl,--as-needed -Wl,-z,lazy`), and capped image decoder allocation (`64 MB`, `16384×16384` / `64 MP`).

## [1.2.5] - 2026-11-16

### Added & Improved
- **Redesigned 2-Column Login Screen:** updated `LoginView` with a balanced two-column card layout for QR sign-in and Chromium browser-session detection, responsive narrow-window fallback, and live language toggle.
- **Convenient per-package updates & `appimage.org` zsync:** embeds `gh-releases-zsync` update info in AppImage builds (publishing `.AppImage.zsync` assets), detects runtime package type (`AppImage`, `.deb`, `.rpm`, `Nix`), verifies `SHA256SUMS` before applying updates, supports in-place atomic AppImage self-update and `pkexec` / desktop installer upgrade for `.deb`/`.rpm` from **Settings → About**.
- **Settings → About & Hardware Acceleration:** added the **About** tab (app version, Qt & platform info, active graphics renderer, GitHub Releases changelog, and update checker) plus an inline **Restart now** prompt when toggling GPU hardware acceleration.
- **Conversation Type Filter & Sidebar Polish:** added a filter dropdown next to the Label filter to switch between **All conversations**, **Group chats only**, or **Direct chats only**, widened the conversation list, and added optional Settings toggles to show/hide **zBusiness** and **Kiki** on the navigation rail.
- **Smart Mobile History Sync Prompt:** detects when signed-in group conversations have no cached messages and surfaces the phone history sync prompt automatically.
- **UI, Window & Localization Fixes:** auto-closes the Emoji/Sticker picker on outside click or toolbar re-click, restores maximized/fullscreen window state after screenshot capture, unified **Sticker** terminology across Vietnamese and English, fixed My Documents icon rendering in conversation avatars, synced GitHub Pages screenshots (`app-ui-dark.png`, `app-ui-light.png`), and made C++ update/renderer/composer status strings update live when switching languages.
- **QML AOT ≥ 90% & Quality Gates:** extracted `ChatMessageRow.qml`, enabled `pragma ComponentBehavior: Bound` in `ChatArea.qml`, raised `ZALOS_AOT_MIN` to `90%`, and expanded `qml_style_lint` & `test_system` automated gates while keeping Qt 6.4 (Ubuntu 24.04) compatibility.

## [1.2.4] - 2026-11-15

### Calls & Packaging
- **Zero external audio runtime needed for AppImage voice calls:** replaced `pactl`/`pacat` subprocesses (`pulseaudio-utils`) in `zalos-call-audio` with direct `dlopen()` of `libpulse-simple.so.0` / `libpulse.so.0` (`PulseApi`), and bundled a `libopus.so.0` fallback inside the AppImage.
- **Terminal-safe AppImage launch:** updated `AppRun` so launching the AppImage from a terminal detaches cleanly via `setsid` and keeps running when the terminal closes.
- **Updated `.deb` and `.rpm` dependencies:** replaced `pulseaudio-utils` with `libpulse0` / `pulseaudio-libs`; only OpenH264 (`libopenh264-7` / `openh264`) remains optional for camera video.

### Added & Improved
- **Unified Emoji Picker:** expanded `EmojiCatalog` to 1,000+ Unicode emojis across 9 categories, integrated Zalo emoticons (`😎` for `b-)`), recent history, and continuous scroll-all category navigation.
- **Full Zalo Reactions & Emoticon Shortcodes:** expanded message reactions (`kReactions`) from 15 to all 54 unique official Zalo reactions and expanded `MessageModel::convertEmoticons` from 23 to 86 official Zalo shortcodes.
- **Full Sticker Store & Live Search:** upgraded `StickerStore` and `EmojiPickerPopup` to expose the full official Zalo sticker catalog (honoring `exIds`/`exCateIds`, `thumbImg` & `https://` fallback), show the active pack header, load pack icons via `MediaSource`, and debounce live sticker search (300 ms).
- **Upstream Protocol Sync:** bumped Zalo Web `API_VERSION` to `695`, switched Cloud/My Documents history pagination to `/api/cm/getrecentv2` → `/api/cm/getoldv2` with `hasMore`/`isOld`/`isFiltered`/`-69` handling and `fetchGroupHistory` fallback, added lenient `me-v2` profile parsing with `fetchUserInfo` fallback and `84...` → `0...` phone normalization, and added `sendLink` → `sendText` fallback.

---

## [1.2.3] - 2026-11-15

### Packaging & Compatibility
- **Self-contained compact AppImage (~22 MB):** bundles a surgical `qt6-fallback` runtime (Qt 6.4 core, QML `Basic` style, XCB/TLS/WebP/SQLite plugins, `libharfbuzz`/`libgraphite2`) while continuing to prefer host Qt 6 when available (`AppRun`). Compresses with `zstd -22 -b 1M`, forwards host `ICU` and `libproxy.so.1` (`LIBPROXY_0.4.16`) dynamically to save ~40 MB, and avoids auto-detaching the launcher process.
- **`glibc 2.35` floor compatibility:** adds `libzalos-glibc-compat.so` (`_ZSt21ios_base_library_initv` via constructor init, `__isoc23_*`, `arc4random*`) and scrubs `GLIBC > 2.35` / `GLIBCXX > 3.4.30` / `CXXABI > 1.3.13` symbol versions so the AppImage runs on Ubuntu 22.04+ and other modern Linux distributions.
- **Metadata & license validation:** restores canonical MIT text in `LICENSE` and removes `<url type="vcs-browser">` from AppStream metainfo to pass `appstreamcli validate-tree` (`0.15.2`).

### Fixed
- **Qt 6.4 QML compatibility:** removed read-only `sourceSize.width` bindings on `AnimatedImage` (`ZRoundedImage`, `ZSticker`) and `pragma ComponentBehavior: Bound` in files with inline `Loader { sourceComponent: ... }` (`Main`, `SettingsModal`, `NavigationRail`, `ImageViewer`), resolving `Cannot instantiate bound component outside its creation context` on Qt 6.4.2.

---

## [1.2.2] - 2026-11-15

### Performance & Memory
- **System & background memory savings:** hiding to the tray now releases Qt Quick's scene graph and GPU textures
  (`setPersistentSceneGraph(false)` / `setPersistentGraphics(false)`), marks idle anonymous heap regions with
  `MADV_COLD` alongside `malloc_trim(0)`, and enables Linux KSM (`PR_SET_MEMORY_MERGE`). `AppRun` exports
  `GLIBC_TUNABLES` before loading shared libraries; startup filters runtime Qt plugins (`setupMinimalPluginPath`
  skipping `styles/` and non-essential `platformthemes/`/`imageformats/`), and `QtMultimedia`/FFmpeg loads lazily
  (`VoiceRecorder` passive `/dev/snd/pcm*c` probe, URL-loaded `ImageViewer` and `CallOverlay`).
- **Idle CPU & network discipline:** pauses animated stickers (`ZSticker`), GIF/WebP animations (`ZRoundedImage`,
  `ImageViewer`), the `WelcomeArea` carousel, the `Sidebar` 60 s clock timer, and 45 s online-presence polling while
  the window is hidden or inactive; emits `conversationStatusChanged` only when online state changes; replaces
  polling in `MessageScheduler` with a single-shot timer targeted at the next scheduled timestamp; adds exponential
  backoff with jitter (`5 s` to `60 s`) on WebSocket reconnects; debounces contact refreshes (`2 s`) and skips
  unchanged `getGroupInfo` fetches using cached group `version`.
- **Faster startup:** `ThemeManager` uses cached values immediately and queries `xdg-desktop-portal` asynchronously
  with a `5 s` timeout; `sticker-info.json` loads lazily on first lookup; `CallBridge` defers H.264 decoder/worker
  creation until a video call starts; `RenderGuard` (`guardVersion=2`) self-heals stale crash flags once, arms only
  after `SingleInstance` succeeds and the window is visible, and clears pending state cleanly on quit.
- **Smooth chat view, lists & media cache:** `ContactModel::rebuildContacts` diffs rows in place without full model
  resets (paired with a `120 ms` search debounce in `ContactsView`); `ConversationInfoPanel` virtualizes group
  members in a `ListView` sharing one context `Menu`; `mentionCandidates` filters `@mentions` in pre-cached C++
  data; `MessageModel::refreshRunsAround` updates tight `±1` run boundaries; `AvatarImageProvider` (`8 MiB`) and
  `MediaImageProvider` (`24 MiB`) use separate RAM caches; RIFF `ANIM`/`ANMF` header probing keeps static `.webp`
  photos on cached `Image`; `ConversationModel::dataChanged` emits explicit role vectors.
- **Streaming SQLite, mobile sync & background threads:** streams `ZDB4` mobile backup decryption in `64 KiB` chunks
  (`decryptDbCrossV4FileToFile`); moves mobile sync SQLite row parsing off the GUI thread and imports batches in
  500-row transactions; caches `ownerUid` in memory, splits `messageTs` / `messageTsBatch` into indexed queries,
  updates FTS5 conditionally, and wraps group reaction/seen writes in transactions in `ZaloOfflineStore`; moves
  `getfriends` decryption/parsing and full-history search off the GUI thread.

### Changed
- **Video calls & release builds:** `CameraSender` picks webcam formats closest to `640×360` (`NV12`/`YUV420P`/`YUYV`
  preferred over high-resolution `MJPEG`) with single-pass `YUYV -> I420` conversion; camera capture/encoding starts
  only after the callee answers; group calls decode only the active video stream (`src == 0`); `zalos-call` terminates
  cleanly when a call ends; native `x86_64` Release builds enable LTO (`CMAKE_INTERPROCEDURAL_OPTIMIZATION`) and tie
  `ZCALL_BUILD_TESTS` to `BUILD_TESTING`.

### Fixed
- **Clean release metadata:** removed a duplicated stray `[1.1.1]` section block from the changelog history and aligned
  startup Vulkan guard tracking with single-instance ownership.

## [1.2.1] - 2026-11-14

### Fixed
- **Mobile history sync is more reliable.** The backup download retries transient errors (HTTP 400/404/429/5xx and
  dropped connections) with backoff, honours `Retry-After`, and no longer logs the private backup URL. The download
  cookies are sent correctly even when the cookie jar holds empty `zpw_uin` / `zpw_sek` values, and Qt can no longer
  replace them. Thanks to @daiphuc for PR #3 (`fix: retry transient HTTP 400 on mobile sync backup download`).
- **Chat no longer jumps back to the newest message** when you scroll up with the scroll bar or the mouse wheel,
  including chats with many photos or links.
- **Call status is accurate.** Declined, unanswered, unreachable and server-unreachable calls show their real reason;
  a microphone or speaker failure shows a warning for the rest of the call; an engine crash shows "lost connection"
  instead of a blank screen.
- **Reconnects are faster.** The app reconnects at once after the network comes back or the computer wakes from
  suspend, and logins or requests that stall now time out instead of hanging on "Đang kết nối...".
- **Downloads and uploads stream from disk**, so large files no longer load fully into memory. Opening a downloaded
  file no longer opens another file with the same name.
- **Forwarded files keep their name and type**, and temporary files are removed after sending.
- **Ringtone** no longer restarts the external player in a loop when PipeWire is not running.
- **Theme and fonts from KDE** (`kdeglobals`) are read correctly, and changes made while Zalos runs are picked up.
- **Message list:** the "typing" indicator no longer covers the last message, and a crash when two copies of the same
  message arrived in one history page is fixed.
- **Chat info panel** in narrow windows floats over the chat instead of hiding it.
- **QR sign-in** no longer loses the confirmation while you are still approving it on your phone.

### Changed
- **My Documents** has its own button in the left bar between Messages and Contacts, with a document icon. The
  Messages button returns to your last regular chat instead of My Documents.
- **Faster and lighter UI:** the conversation list and message list update in place (no full redraw per message);
  avatars and photos decode off the interface thread; history pages are inserted at the top without resetting the
  list; unread and read-status updates touch only the rows that changed; opening the app does no full-table database
  work.
- Avatar and photo requests time out after 30 s, API requests after 45 s, and sign-in long-polls after 3 min.

## [1.2.0] - 2026-11-11

### Added
- Regression test for starting a call (`testCallStartLaunchesEngine`); the QML lint, AOT coverage and screenshot checks
  run as C++ tests in `tests/test_qml_gates.cpp`.

### Changed
- **Calls run on the C++ call engine only.** `zalos-call` (signalling, media) and `zalos-call-audio` (microphone and
  speaker, Opus) are built by default and installed to `libexec/zalos`. The Node.js engine (vendored zcall-native),
  its Python audio helper and their runtime checks are removed.
- The call engine takes its settings as arguments: `--key-file`, `--log-dir`, `--ui-port`, `--ui-token`, `--group`.
  The `ZALOS_CALL_ENGINE`, `ZALOS_CALL_AUDIO_ENGINE`, `ZALOS_CALL_ENGINE_BIN`, `ZALOS_CALL_AUDIO`, `ZALO_MAIN_JS` and
  `ZALOS_ZCALL_DIR` variables no longer have any effect. The transport key file holds the base64 key only.
- **No Python anywhere.** The browser sign-in script fallback is removed, so the native `BrowserSessionReader` is the only
  path. `scripts/file-shim.py` became `scripts/file-shim.sh`, and the packages no longer depend on `python3`.
- Removed `third_party/zcall-native`, `scripts/sync-zcall-native.sh` and the Node-based `call-vectors` compose service.
- Lower memory: heap is returned to the OS after login and every 5 minutes while open; resident messages are budgeted
  (3 000 across open threads); unbounded ID caches are capped; the sticker info cache is capped; the chat background is
  decoded at 96 px instead of a full-window blur.
- QML pages and menus are created only when shown: chat view and welcome area, settings panes (one at a time), account,
  snip and business menus, and the hidden-chat and app-link popups.
- Chat photos, link thumbnails, topic thumbnails and profile photos load through the `image://media/` provider (HTTP/1.1,
  shared cache, decode at display size).
- The app reports its real version (from `project(VERSION)`) instead of a hard-coded `1.0.0`.

### Fixed
- Crash (SIGSEGV) when starting a voice or video call: the engine process object was not created.
- Image failures log the real reason (`HTTP 404`, `decode failed`, …) instead of `avatar unavailable`.
- Contact invite rows used `model.*` inside a delegate with required roles (`ReferenceError`) and accept/decline sent an
  empty id.
- `PillButton` in the call overlay redeclared `enabled` (Qt property-cache warning).

---

## [1.1.1] - 2026-11-10

### Added
- **Native C++ browser session reader**: `BrowserSessionReader` extracts Chromium/Brave/Edge cookies directly in C++ via SQLite and Secret Service/KWallet decryption, so browser sign-in no longer spawns Python; the bundled export script remains only as a fallback when `python3` is present.
- **Group read receipts**: your own group messages show a stacked cluster of up to four reader avatars with a `+N` badge, a hover list of reader names and a "Người đã xem (N)" popup; readers are stored per message in the offline database, so they survive history syncs and app restarts.
- **In-chat system notices & call logs**: Group call finished notice card (`Cuộc gọi nhóm đã kết thúc`) and 1-1 voice/video call messages added into conversation flow with duration and status details.

### Changed
- **Direct tab ("Nhắn tôi")**: Displays unread group mentions (@me and @all) alongside direct messages; a mention flag left over on a fully read thread no longer keeps the row in the tab.

### Fixed
- **Mention cursor positioning**: Restores cursor directly after the inserted mention and space separator.
- **Call Overlay vertical alignment**: Aligns hangup CircleButton and control PillButtons vertically.
- **Create Group member list**: Correctly filters friends avoiding empty delegate items.

## [1.1.0] - 2026-11-09

### Added
- **Group voice and video calls**: the chat header's call buttons start a group call when the open thread is a group,
  driving the engine's group commands (`voicecall/group/requestcall`, `request`, `answer`, `state`, `finish`,
  `notify`, `ping`, `broadcast`, `adduser`) and showing the participant row with per-member state in `CallOverlay`.
  Contact rows offer the same group call actions.
- **My Documents**: the `send2me_id` thread is now presented under the single name **My Documents** (matching the
  server; rows cached as "Cloud của tôi", "My Cloud" or "Truyền File" are migrated once) and `CloudView` browses its
  photos, videos, files and links with instant filters.
- **Kiki Assistant view**: a navigation-rail destination backed by the server's `features.kiki_assistant` config;
  accounts without the service see an honest explanation and four local drafting templates instead of a simulated chat.
- **zBusiness tools**: business profile, product catalog (send a product card into the open thread) and auto-reply
  configuration in one dialog, reachable from the rail and the profile modal.
- **Official accounts are read-mostly**: `isOfficialOrSystemAccount()` combines persisted OA markers with folded
  `Zalo…`, `zBusiness`, `zCloud`, `ZaloPay`, `Thời Tiết` and `Medlatec` names on non-friend threads, hiding call,
  add-group and share-contact actions in the chat header, conversation info panel and profile modal.
- **Contacts quick actions**: voice call, video call and chat buttons on every contact row, including groups.
- **Screenshot shortcut**: `Ctrl+Alt+S` is now listed in Settings › Shortcuts (`app.screenshot`).

### Changed
- **Calls always use the bundled zcall-native engine**: the Settings › Calls engine picker ("Dùng bản khác…" /
  "Dùng bản đi kèm") and its persisted `calls/zcallDir` override are gone; `ZALOS_ZCALL_DIR` remains for development
  builds that need another engine folder.
- **Sidebar category tabs** (Tất cả / Chưa đọc / Khác / Nhắn tôi) replace the single header row, with the label filter
  button kept on the right.
- **Mobile history sync** verifies the advertised checksum and file size, aborts after two minutes without download
  progress, reports disk-write failures and logs host/progress details.
- **System tray badge** is larger, anti-aliased and fits three-digit counts.
- **Labels**: wording shortened from "thẻ phân loại" to "thẻ" and the English category name is now "Labels".
- **Documentation**: README and the GitHub Pages site describe the 1.1.0 feature set, download names and refreshed
  screenshots.

### Fixed
- **zBusiness catalog overflow**: product cards clip inside the tab's scroll area instead of bleeding past the dialog
  edge, and the tab strip no longer collapses its labels.
- **My Documents row in Contacts** no longer offers call or video-call buttons.

## [1.0.8] - 2026-11-08

### Added
- **Chat background blur overlay & refined layout**: Layered dynamic blur effect (`ZChatBackgroundBlur`) for custom chat
  backgrounds with customizable opacity and darkness sliders, allowing readable text and hiding low-resolution image artifacts.
  Strict layout boundary clipping ensures background visuals never bleed into navigation rails or conversation info panels.
- **Original resolution cover & avatar assets**: Full-resolution image loading for profile cover photos and avatars
  without downscaling artifacts in avatar view and background preview modes.
- **Precompiled Headers & CI build speedup**: Added `pch.h` for core headers, suppressed redundant DWARF symbol emissions
  on aarch64, and expanded parallel compilation workers to `-j5` across RPM, .deb, AppImage, and Nix packaging.
- **Pure-bash version resolver for Nix packages**: Standalone regex-based metadata resolver in `build-nix.sh` running in
  minimal environments without external utilities.

### Fixed
- **Unread message badge resurrection**: Resolved unread counter reappearing upon app restart for previously read conversations
  via dual seen dispatch (v1 `/api/message/seen` and `/api/group/seen` with exact message IDs alongside v2 `/api/message/seenv2`)
  and local fallback read cursor advancement.
- **AppImage AppStream packaging & runtime download timeouts**: Silenced AppStream upstream metadata warnings and extracted
  the type2 runtime directly from `appimagetool` to eliminate HTTP 504 gateway timeout risks.
- **VoIP call reject status handling**: Proper call teardown and event propagation for status code 160 when the callee rejects an incoming call.
- **Cookie domain scoping**: Scoped session cookie injection strictly to service endpoints (`.zalo.me`, `.zdn.vn`, `.zadn.vn`).
- **QML AOT compilation coverage**: Enforced bound component delegates across screens and popups to ensure type safety and high AOT compilation rates.

## [1.0.7] - 2026-11-07

### Added
- **Draft attachments bar & multi-file queuing**: Pasted clipboard images (`Ctrl+V`) and selected files are held in an
  interactive draft preview bar above the composer instead of sending immediately. Multiple files can be attached,
  inspected with thumbnails/chips, removed individually (`✕`), cleared with `Esc` or "Clear all", and sent together with
  message text on Enter.
- **Image viewer drawing and annotation canvas**: `ImageViewer` includes interactive drawing tools (freehand pen,
  rectangle box, arrow pointer, text note), color selector palette, undo (`Ctrl+Z`), canvas clear, annotated clipboard
  copy (`Ctrl+C`), and print media support (`Ctrl+P`).
- **Desktop and window state integration**: Restores window geometry, dimensions, and maximized state across launches;
  dynamic tray menu hide/show toggle; Unity/KDE dock unread message counter badge; portal screen grab with auto-hide window;
  and customizable chat background opacity slider (5% to 80%).
- **Call engine enhancements**: Option to accept video calls audio-only ("Answer without camera"), automatic filtering
  out of infrared/facial authentication sensors for standard webcam binding, and outgoing ringback sound effect.
- **Packaging**: ARM64 (aarch64) packaging added across RPM, .deb, and AppImage artifacts.

### Fixed
- **Photo viewer tool button rendering**: Fixed drawing tool button background scope binding to avoid solid white icon button bug.
- **Message echo status transition**: Local echo status transitions cleanly to `"sent"` when server acknowledgement is merged.
- **Zalo video call log identification**: Correctly identifies call types across wire codes (`calltype == 1`, `3`, `6`).
- **Delivered receipt updates**: WebSocket `cmd = 502/522` events mark delivered receipts in real time.

## [1.0.6] - 2026-11-07

### Added
- **Text emoticons render as emoji**: Zalo shortcodes such as `:v`, `:)`, `=))`, `:D` and `:P` are converted inside
  `linkify` with token boundaries, so clocks (`12:30`), URLs (`http://`, `:8080/v`) and identifiers in message text
  are left untouched.

### Fixed
- **Duplicate messages after a mobile-history resync**: `MessageModel::mergeMessages` now reconciles incoming rows by
  their non-zero `cliMsgId` (not only `local-…` echoes), so a phone-imported message whose `msgId` was missing
  (`msgId == cliMsgId`) is replaced in place by the server copy instead of showing twice. `ZaloOfflineStore::saveMessage`
  deletes any sibling row of the same conversation with the same `cli_msg_id`, and the one-shot
  `dedup_cli_version=1` migration purges rows already duplicated in `offline.db`.
- **Photo viewer no longer needs a media backend**: `ImageViewer` creates its `MediaPlayer` on demand, only when a
  video is opened. Viewing a picture (chat photo, avatar, cover, chat background) never initialises `QtMultimedia`, so
  the Qt 6.4 GStreamer backend can no longer crash on systems without `gstreamer1.0-plugins-base`.

### Changed
- **Minified GitHub Pages deployment**: the site is built from the readable `docs/` source into `_site/`
  (`scripts/minify-pages.sh` + `packaging/pages/Dockerfile.pages`, `html-minifier-terser`) and deployed from there;
  the workflow now also triggers on the minifier sources.
- **Documentation**: refreshed sign-in copy, stated that the portable AppImage is published for x86_64 (ARM64 users
  are pointed to `.deb`, RPM or Nix), and kept the real `~ubuntu24.04` asset names in the `.deb` examples.

## [1.0.5] - 2026-11-06

### Added
- **In-app video playback (`ImageViewer`)**: clicking a video message or shared video tile now opens the built-in
  `ImageViewer` with a `QtMultimedia` `MediaPlayer` + `VideoOutput` and play/pause/seek controls instead of handing
  the remote URL to an external browser.
- **Full-resolution avatar, cover & background preview**: clicking any user/group avatar (`UserProfileModal`,
  `ConversationInfoPanel`), profile cover banner, or custom chat background thumbnail in `SettingsModal` opens it in
  the in-app `ImageViewer`.
- **Click-to-copy phone number**: clicking a user's phone number in `UserProfileModal` copies it to the clipboard and
  shows a localized confirmation toast.
- **Background memory trim when hidden to tray (`ZaloClient::trimMemoryForBackground`)**: 30 seconds after the main
  window is hidden to the system tray, Zalos evicts cold resident conversations down to the active thread, clears the
  in-RAM avatar LRU cache, `QPixmapCache`, QML component/JS heaps, releases scene-graph GPU textures
  (`QQuickWindow::releaseResources()`), and returns freed heap pages to the OS via `malloc_trim(0)`.

### Fixed
- **Emulated `aarch64` Nix CI OOM crash (`exit code 137` / `SIGKILL`) & compiler warning**: disabled QML AOT C++
  code generation on `aarch64` in `default.nix` (matching RPM, `.deb` and AppImage, cutting 46 heavy `_qml.cpp`
  translation units under QEMU), capped emulated parallel compile jobs to `-j2` (`cores = 2` in `nix.conf`,
  `zalos.spec`, `build-deb.sh` and `build-appimage.sh`), and checked the return value of `std::freopen(..., stderr)`
  in `main.cpp` to silence `-Wunused-result` under `_FORTIFY_SOURCE=3`.
- **Duplicate CI workflow triggers**: removed `branches: [ main ]` and `pull_request` triggers from `ci.yml` so
  normal pushes to `main` no longer queue redundant skipped CD runs, and scoped `base-images.yml` `push` to
  `branches: [ main ]` so pushing a `v*` release tag never triggers a duplicate base-image build.
- **Mobile history sync cleanup & 64-bit ID resolution**: called `delete_snapshot_mobile_msg` (`cleanMobileSync`)
  immediately after decrypting a mobile backup archive to remove temporary snapshots on Zalo's servers, and extended
  `/api/znoise` plain-to-noised UID resolution to 64-bit `MentionData::uid` and `quote.ownerId` fields so `@mentions`
  and quoted replies imported from phone backups remain clickable.
- **Clickable sender names & mentions after mobile sync**: added on-demand `fetchGroupMembersInfo` fallback in
  `ZaloClient::viewProfile` and a `"Người dùng Zalo"` / `"Zalo user"` sender-name fallback in `ChatArea.qml` so
  clicking sender headers, `@mentions` or quoted messages always opens the profile modal even before group rosters
  finish resolving.
- **Avatar-mode chat background reactivity**: connected `currentConversationChanged` to
  `currentConversationBackgroundChanged` and resolved fallback conversation avatars in `selectConversation`, fixing a
  bug where the `"Theo ảnh đại diện"` (`"avatar"`) chat background did not update when switching conversations.

### Performance
- **Single-buffer `MessageModel` (zero dual-buffering & zero CoW detach)**: the active conversation now lives
  exclusively in `m_items` while open and swaps into/out of `m_store` via `std::move` on conversation switch,
  eliminating duplicate `QVector<MessageItem>` storage and copy-on-write detachments on every message mutation.
- **Zero-RAM `m_rawMessages` duplication when `offline.db` is active**: incoming messages write their compressed raw
  JSON directly to SQLite without keeping a duplicate `QJsonObject` in `m_rawMessages` RAM; rare operations (pin,
  forward, quote) read `raw_json` on demand from `offline.db`.
- **Scaled-on-decode images (`AvatarImageProvider::decodeScaled`)**: local files, disk-cached files and network buffers
  now use `QImageReader::setScaledSize` during decode instead of allocating a full-resolution 2K/4K `RGBA8888` buffer
  before downscaling.
- **Lazy `ConversationInfoPanel` instantiation**: wrapped `ConversationInfoPanel` in a `Loader` bound to
  `ZaloClient.showConversationInfo` in `ChatArea.qml` so its 1,200-line QML subtree is only created when the right
  sidebar is open and destroyed when closed.

## [1.0.4] - 2026-11-06

First published multi-architecture release (supersedes the unpublished `v1.0.1`–`v1.0.3` tags). Ships the full
native Qt6 C++17/QML client for `x86_64` and `aarch64` across AppImage, `.deb`, RPM and Nix, along with call
engine, file upload, attachment naming, mute sync, and build/packaging improvements over `v1.0.3`.

### Fixed (since v1.0.3)
- **1-1 outgoing video calls (`zcall-native` `2b93852`)**: preserved the caller's `camReported` state across
  `_camMic()` and re-sent camera status 2 s after `cmd 408` (`connect`), fixing a bug where the callee's phone
  displayed our camera as off for the entire call even while H.264 video frames were streaming.
- **Fast file & voice upload race (`file_done`)**: buffered early WebSocket `601` `act_type: "file_done"` events
  (`m_earlyFileDone`, 120 s TTL) so small single-chunk files or voice notes whose `file_done` push arrives a few
  milliseconds before the final HTTP `asyncfile/upload` response never miss their waiter or time out.
- **Attachment filenames & extensions (`share.file`)**: appended `params.fileExt` when `content.title` omits the
  file extension and avoided saving files as `"renewMedia"` when served through `/renewMedia` URLs, ensuring file
  type badges and `~/Downloads/Zalos/` desktop opens work reliably.
- **Bidirectional mute state sync (`getmute` & realtime `mute`)**: reconciled `m_mutedIds` against the full
  `/api/social/profile/getmute` snapshot (respecting server `systemTime` and `muteMode` `2`/`3` indefinite mutes)
  so unmuting a conversation on mobile clears the mute indicator on Zalos after reconnect/resync.
- **Nix `aarch64` packaging under QEMU**: disabled the Nix syscall sandbox when `ZALOS_EMULATED=1` (fixes
  `unable to load seccomp BPF program: Invalid argument` on emulated ARM64 builds) and skipped duplicate
  `nix flake update` runs under emulation.

### Performance
- **Faster offline start-up**: the cached conversation list is loaded in a single batched model update
  (one sort/reset instead of one per conversation), and the pre-`QApplication` input-method D-Bus probe issues
  at most one `ListNames` query instead of up to four sequential `dbus-send` calls.
- **Smoother message-list scrolling**: each `MessageItem` caches its local calendar day (`dayCache`), so the
  grouping and date-separator roles no longer repeat timezone-aware `QDateTime` conversions on every row, per frame.
- **Smaller binaries & faster multi-arch builds**: enabled `-fvisibility=hidden`, `-fvisibility-inlines-hidden`,
  `-ffunction-sections -fdata-sections` and `-Wl,--gc-sections`; shared a single `zalos_test_core` `OBJECT` library
  across `test_e2e` and `test_system` (−38 % compile steps); disabled redundant RPM debuginfo/SRPM extraction; and
  reused a single Ubuntu 24.04 Release build for both `.deb` and `AppImage` (−50 % Ubuntu compile time on `x86_64`
  and `aarch64`).

### Changed
- Release CI packages without host bind mounts: the source is baked into a throwaway image (`Dockerfile.app`)
  and artifacts are copied out with `docker cp`, so packaging works on runners whose Docker daemon cannot
  see the job checkout (fixes the exit 127 failure in `v1.0.3`).
- Redesigned the project documentation site (`https://cpfriend1721994.github.io/zalos/`) with native Light/Dark
  themes, real application screenshots, architecture deep-dive, and per-distro installation tabs.
- Repointed all metadata, homepages, download links and docs to `github.com/cpfriend1721994/zalos` and updated the
  AppStream component id to `io.github.cpfriend1721994.zalos`.

## [1.0.0] - 2026-10-31

### Added
- Native Zalo desktop client for Linux — light on memory and quick to start.
- Wayland and X11 support, with native window controls (including on GNOME Wayland).
- Automatic light/dark theme that follows the system colour scheme.
- Messaging: one-to-one and group chats, animated stickers, emoji, reactions, replies, forwards,
  pinned messages, labels, and full-history search.
- Voice and video calls.
- System tray icon with an unread-count badge.
- Offline access to your conversation history.
- Vietnamese interface.
- Packaging for `.deb`, `.rpm`, Arch, AppImage and Nix.
