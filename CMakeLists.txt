cmake_minimum_required(VERSION 3.22)
project(zalos VERSION 1.2.0 LANGUAGES CXX)
option(BUILD_TESTING "Build unit tests" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(-ffunction-sections -fdata-sections)
    add_link_options(-Wl,--gc-sections)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS
    Core
    Gui
    Widgets
    Qml
    Quick
    QuickControls2
    Network
    DBus
    Svg
    Sql
    WebSockets
    Multimedia
)
qt_standard_project_setup()

find_package(OpenSSL REQUIRED)
find_package(ZLIB REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LZMA REQUIRED liblzma)

# OpenH264 headers only: libopenh264 is dlopen()ed from the user's system at run time and never linked or
# bundled (see src/core/call/h264_codec.h). Without the headers calls stay audio-only.
include(CheckIncludeFileCXX)
check_include_file_cxx(wels/codec_api.h ZALOS_HAVE_OPENH264)
if(ZALOS_HAVE_OPENH264)
    add_compile_definitions(ZALOS_HAVE_OPENH264)
else()
    message(WARNING "OpenH264 headers (wels/codec_api.h) not found: call video will be unavailable")
endif()

include(GNUInstallDirs)

set(ZALOS_SOURCES
    src/main.cpp
    src/core/crypto/db_cross.h
    src/core/crypto/db_cross.cpp
    src/core/fs/file_utils.h
    src/core/fs/file_utils.cpp
    src/core/media/media_utils.h
    src/core/media/media_utils.cpp
    src/core/call/zcall_engine.h
    src/core/call/zcall_engine.cpp
    src/core/call/call_bridge.h
    src/core/call/camera_sender.h
    src/core/call/call_bridge.cpp
    src/core/call/camera_sender.cpp
    src/core/call/h264_codec.h
    src/core/call/h264_codec.cpp
    src/core/call/peer_video.h
    src/core/call/peer_video.cpp
    src/core/platform/desktop_notifications.h
    src/core/platform/desktop_notifications.cpp
    src/core/platform/single_instance.h
    src/core/platform/single_instance.cpp
    src/core/network/message_scheduler.h
    src/core/network/message_scheduler.cpp
    src/core/media/voice_recorder.h
    src/core/media/voice_recorder.cpp
    src/core/platform/shortcut_map.h
    src/core/platform/shortcut_map.cpp
    src/core/platform/app_lock.h
    src/core/platform/app_lock.cpp
    src/core/platform/storage_manager.h
    src/core/platform/storage_manager.cpp
    src/core/network/group_admin.h
    src/core/network/error_catalog.h
    src/core/network/error_catalog.cpp
    src/core/network/group_admin.cpp
    src/core/network/label_manager.h
    src/core/network/label_manager.cpp
    src/core/network/sticker_store.h
    src/core/network/sticker_store.cpp
    src/core/network/pin_board.h
    src/core/models/message_search_model.h
    src/core/network/pin_board.cpp
    src/core/models/message_search_model.cpp
    src/core/models/rich_composer.h
    src/core/models/rich_composer.cpp
    src/core/sync/mobile_history_sync.cpp
    src/core/sync/v2/sync_v2_crypto.cpp
    src/core/sync/v2/sync_v2_frames.cpp
    src/core/offline/offline_store.h
    src/core/offline/offline_store.cpp
    src/core/network/zalo_api_client.h
    src/core/network/zalo_api_client.cpp
    src/core/network/zalo_crypto.h
    src/core/network/zalo_crypto.cpp
    src/core/network/zalo_session.h
    src/core/network/zalo_session.cpp
    src/core/network/zalo_client.h
    src/core/network/zalo_client.cpp
    src/core/network/qrcodegen.hpp
    src/core/network/qrcodegen.cpp
    src/core/models/conversation_model.h
    src/core/models/conversation_model.cpp
    src/core/models/message_model.h
    src/core/models/message_model.cpp
    src/core/models/contact_model.h
    src/core/models/contact_model.cpp
    src/core/platform/theme_manager.h
    src/core/platform/theme_manager.cpp
    src/core/platform/system_tray.h
    src/core/platform/system_tray.cpp
    src/core/platform/avatar_image_provider.h
    src/core/platform/avatar_image_provider.cpp
    src/core/platform/icon_image_provider.h
    src/core/platform/icon_image_provider.cpp
    src/core/platform/render_profile.cpp
    src/core/platform/browser_session_reader.h
    src/core/platform/browser_session_reader.cpp
    src/core/platform/qml_singletons.h
    src/core/platform/sound_engine.h
    src/core/platform/sound_engine.cpp
)

qt_add_executable(zalos ${ZALOS_SOURCES})

set_source_files_properties(src/qml/Theme.qml src/qml/I18n.qml src/qml/ImageViewerState.qml
    PROPERTIES
        QT_QML_SINGLETON_TYPE TRUE
)

qt_add_qml_module(zalos
    URI Zalos
    VERSION 1.0
    # Lets qmlcachegen resolve QColor/QFont/... properties of the C++ singletons (QtQuick value types).
    DEPENDENCIES QtQuick
    QML_FILES
        src/qml/Main.qml
        src/qml/NavigationRail.qml
        src/qml/Sidebar.qml
        src/qml/WelcomeArea.qml
        src/qml/ChatArea.qml
        src/qml/Theme.qml
        src/qml/LoginView.qml
        src/qml/SettingsModal.qml
        src/qml/UserProfileModal.qml
        src/qml/IncomingCallPopup.qml
        src/qml/AddFriendModal.qml
        src/qml/CreateGroupModal.qml
        src/qml/ConversationInfoPanel.qml
        src/qml/EmojiPickerPopup.qml
        src/qml/ContactsView.qml
        src/qml/ZIcon.qml
        src/qml/ZAvatar.qml
        src/qml/CallOverlay.qml
        src/qml/ZMaskEffect.qml
        src/qml/ZBlurEffect.qml
        src/qml/ChatJumpDownButton.qml
        src/qml/ChatUnreadBar.qml
        src/qml/LockScreen.qml
        src/qml/ZButton.qml
        src/qml/ZTextField.qml
        src/qml/ZComboBox.qml
        src/qml/SyncPromptDialog.qml
        src/qml/ForwardMessageModal.qml
        src/qml/CreatePollModal.qml
        src/qml/GroupManageModal.qml
        src/qml/GroupJoinModal.qml
        src/qml/LabelManageModal.qml
        src/qml/I18n.qml
        src/qml/ZMenuRow.qml
        src/qml/ImageViewer.qml
        src/qml/ImageViewerState.qml
        src/qml/ZSwitch.qml
        src/qml/ZCheckBox.qml
        src/qml/ZRadio.qml
        src/qml/ZRoundedImage.qml
        src/qml/MediaSource.js
        src/qml/ZSticker.qml
        src/qml/PopupHost.qml
        src/qml/PinDetailHost.qml
        src/qml/AppLinkPopup.qml
        src/qml/HiddenPinPopup.qml
        src/qml/PinMenuHost.qml
        src/qml/CloudView.qml
        src/qml/KikiView.qml
        src/qml/BusinessToolsModal.qml
    RESOURCES
        assets/icons/zalos.svg
        assets/icons/pacman.png
)

target_include_directories(zalos PRIVATE
    # qmltyperegistrar includes QML-registered headers by file name
    src/core/models src/core/call src/core/offline src/core/network src/core/platform src/core/media
    src
    src/core
    ${LZMA_INCLUDE_DIRS}
)
target_compile_definitions(zalos PRIVATE ZALOS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    ZALOS_APP_VERSION="${PROJECT_VERSION}")
if(BUILD_TESTING)
    target_compile_definitions(zalos PRIVATE ZALOS_CALL_BUILD_PATH="$<TARGET_FILE:zalos-call>")
endif()
target_precompile_headers(zalos PRIVATE src/core/pch.h)

# QML compiled to C++ (qmlcachegen AOT) calls QQmlPrivate::AOTCompiledContext, private Qt API versioned per minor
# release (e.g. Qt_6.10_PRIVATE_API): such a binary only runs on the exact Qt minor it was built with. Packages
# that must run on several Qt versions (RPM, .deb, AppImage, prebuilt Arch) pass -DZALOS_QML_AOT=OFF and ship
# bytecode only; development/test builds and Nix (pinned Qt) keep it ON (tests gate the AOT share).
option(ZALOS_QML_AOT "Compile QML bindings to C++ (ties the binary to the build's Qt minor version)" ON)
if(NOT ZALOS_QML_AOT)
    set_target_properties(zalos PROPERTIES QT_QMLCACHEGEN_ARGUMENTS "--only-bytecode")
endif()

target_link_libraries(zalos PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::DBus
    Qt6::Svg
    Qt6::Sql
    Qt6::WebSockets
    Qt6::Multimedia
    OpenSSL::Crypto
    ZLIB::ZLIB
    SQLite::SQLite3
    ${LZMA_LIBRARIES}
    ${CMAKE_DL_LIBS}
)

# Installation targets for packaging (RPM, Flatpak, AppImage)
install(TARGETS zalos
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Call engine (C++, engines/zcall-cpp): zalos-call (signalling and media) and zalos-call-audio (microphone and speaker),
# installed to libexec/zalos. CallBridge runs them from there; there is no script runtime in the call path.
add_subdirectory(engines/zcall-cpp)
# The engine finds zalos-call-audio next to itself; in a build tree the two live in different directories.
add_custom_target(zalos_call_audio_beside_engine ALL
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:zalos-call-audio> $<TARGET_FILE_DIR:zalos-call>
    DEPENDS zalos-call zalos-call-audio
    VERBATIM)

install(FILES assets/zalos.desktop
    DESTINATION ${CMAKE_INSTALL_DATADIR}/applications
)

install(FILES assets/metainfo/io.github.cpfriend1721994.zalos.metainfo.xml
    DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo
)

install(FILES assets/icons/zalos.svg
    DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/scalable/apps
)

# Testing configuration
if(BUILD_TESTING)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    
    add_executable(test_crypto
        tests/test_crypto.cpp
        src/core/crypto/db_cross.cpp
    )
    target_include_directories(test_crypto PRIVATE src src/core ${LZMA_INCLUDE_DIRS})
    target_link_libraries(test_crypto PRIVATE
        Qt6::Core
        Qt6::Test
        OpenSSL::Crypto
        SQLite::SQLite3
        ${LZMA_LIBRARIES}
        ${CMAKE_DL_LIBS}
    )
    add_test(NAME test_crypto COMMAND test_crypto)

    add_executable(test_models
        tests/test_models.cpp
        src/core/models/conversation_model.cpp
        src/core/models/message_model.cpp
        src/core/models/contact_model.cpp
    )
    target_include_directories(test_models PRIVATE src src/core)
    target_link_libraries(test_models PRIVATE
        Qt6::Core
        Qt6::Qml
        Qt6::Test
    )
    add_test(NAME test_models COMMAND test_models)

    add_executable(test_protocol
        tests/test_protocol.cpp
        src/core/network/zalo_crypto.cpp
    )
    target_include_directories(test_protocol PRIVATE src src/core)
    target_link_libraries(test_protocol PRIVATE
        Qt6::Core
        Qt6::Test
        OpenSSL::Crypto
        ZLIB::ZLIB
    )
    add_test(NAME test_protocol COMMAND test_protocol)

    add_executable(test_errors
        tests/test_errors.cpp
        src/core/network/error_catalog.cpp
    )
    target_include_directories(test_errors PRIVATE src src/core)
    target_link_libraries(test_errors PRIVATE
        Qt6::Core
        Qt6::Test
    )
    add_test(NAME test_errors COMMAND test_errors)

    add_executable(test_sync_v2
        tests/test_sync_v2.cpp
        src/core/sync/v2/sync_v2_crypto.cpp
        src/core/sync/v2/sync_v2_frames.cpp
    )
    target_compile_definitions(test_sync_v2 PRIVATE ZALOS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
    target_include_directories(test_sync_v2 PRIVATE src src/core)
    target_link_libraries(test_sync_v2 PRIVATE
        Qt6::Core
        Qt6::Test
        OpenSSL::Crypto
    )
    add_test(NAME test_sync_v2 COMMAND test_sync_v2)

    add_library(zalos_test_core OBJECT
        src/core/crypto/db_cross.cpp
        src/core/fs/file_utils.cpp
        src/core/media/media_utils.cpp
        src/core/call/zcall_engine.cpp
        src/core/call/call_bridge.cpp
        src/core/call/camera_sender.cpp
        src/core/call/h264_codec.cpp
        src/core/call/peer_video.cpp
        src/core/platform/desktop_notifications.cpp
        src/core/platform/single_instance.cpp
        src/core/network/message_scheduler.cpp
        src/core/media/voice_recorder.cpp
        src/core/platform/shortcut_map.cpp
        src/core/platform/app_lock.cpp
        src/core/platform/storage_manager.cpp
        src/core/network/error_catalog.h
        src/core/network/error_catalog.cpp
        src/core/network/group_admin.cpp
        src/core/network/label_manager.cpp
        src/core/network/sticker_store.cpp
        src/core/network/pin_board.cpp
        src/core/models/message_search_model.cpp
        src/core/models/rich_composer.cpp
        src/core/sync/mobile_history_sync.cpp
        src/core/sync/v2/sync_v2_crypto.cpp
        src/core/sync/v2/sync_v2_frames.cpp
        src/core/offline/offline_store.cpp
        src/core/network/zalo_crypto.cpp
        src/core/network/zalo_session.cpp
        src/core/network/zalo_api_client.cpp
        src/core/network/zalo_client.cpp
        src/core/network/qrcodegen.cpp
        src/core/models/conversation_model.cpp
        src/core/models/message_model.cpp
        src/core/models/contact_model.cpp
        src/core/platform/theme_manager.cpp
        src/core/platform/browser_session_reader.cpp
        src/core/platform/system_tray.cpp
        src/core/platform/sound_engine.cpp
    )
    target_compile_definitions(zalos_test_core PUBLIC ZALOS_TESTING ZALOS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        ZALOS_CALL_BUILD_PATH="$<TARGET_FILE:zalos-call>")
    target_precompile_headers(zalos_test_core PRIVATE src/core/pch.h)
    target_include_directories(zalos_test_core PUBLIC . src src/core src/core/models src/core/call src/core/offline src/core/network src/core/platform src/core/media ${LZMA_INCLUDE_DIRS})
    target_link_libraries(zalos_test_core PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::Network
        Qt6::DBus
        Qt6::Sql
        Qt6::Test
        Qt6::WebSockets
        Qt6::Multimedia
        Qt6::Svg
        OpenSSL::Crypto
        ZLIB::ZLIB
        SQLite::SQLite3
        ${LZMA_LIBRARIES}
        ${CMAKE_DL_LIBS}
    )

    add_executable(test_e2e
        tests/test_e2e.cpp
        $<TARGET_OBJECTS:zalos_test_core>
    )
    target_link_libraries(test_e2e PRIVATE zalos_test_core)
    add_test(NAME test_e2e COMMAND test_e2e)

    # qt_add_qml_module's generated resources: .rcc/ up to Qt 6.4 (Ubuntu 24.04), .qt/rcc/ from Qt 6.5.
    if(Qt6_VERSION VERSION_LESS 6.5)
        set(ZALOS_QT_RCC_DIR ${CMAKE_CURRENT_BINARY_DIR}/.rcc)
    else()
        set(ZALOS_QT_RCC_DIR ${CMAKE_CURRENT_BINARY_DIR}/.qt/rcc)
    endif()
    add_executable(test_system
        tests/test_system.cpp
        $<TARGET_OBJECTS:zalos_test_core>
        src/core/platform/avatar_image_provider.cpp
        src/core/platform/icon_image_provider.cpp
        src/core/platform/render_profile.cpp
        src/core/platform/qml_singletons.h
        ${ZALOS_QT_RCC_DIR}/qrc_zalos_raw_qml_0.cpp
        ${ZALOS_QT_RCC_DIR}/qrc_qmake_Zalos.cpp
        # C++ QML types of the Zalos module (ZaloClient/ThemeManager/TrayController singletons, ...)
        ${CMAKE_CURRENT_BINARY_DIR}/zalos_qmltyperegistrations.cpp
    )
    set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/zalos_qmltyperegistrations.cpp PROPERTIES GENERATED TRUE)
    add_dependencies(test_system zalos)
    target_link_libraries(test_system PRIVATE zalos_test_core)
    add_test(NAME test_system COMMAND test_system)
    # QML gates in plain C++ (tests/test_qml_gates.cpp): theme/component lint (skill 03), AOT coverage floor (skill 02,
    # Qt 6.5+ only, qmlcachegen writes .aotstats) and the UI screenshot sanity check. The visual check reads the
    # screenshots test_system writes, so it is registered after it.
    add_executable(test_qml_gates "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_qml_gates.cpp")
    set_target_properties(test_qml_gates PROPERTIES AUTOMOC ON)
    target_link_libraries(test_qml_gates PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
    target_compile_definitions(test_qml_gates PRIVATE
        ZALOS_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
        ZALOS_BUILD_DIR="${CMAKE_CURRENT_BINARY_DIR}"
        ZALOS_AOT_MIN=85)
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.5)
        target_compile_definitions(test_qml_gates PRIVATE ZALOS_AOT_GATE=1)
    endif()
    add_dependencies(test_qml_gates zalos)
    add_test(NAME qml_style_lint COMMAND test_qml_gates qmlStyle)
    add_test(NAME qml_aot_coverage COMMAND test_qml_gates qmlAotCoverage)
    add_test(NAME ui_visual_diff COMMAND test_qml_gates uiVisualDiff)
endif()

