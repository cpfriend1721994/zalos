# SPDX-License-Identifier: BSD-3-Clause
find_package(Qt6 REQUIRED COMPONENTS Core Network)
find_package(OpenSSL REQUIRED)

add_library(zcall_core STATIC
    src/js.h
    src/json_cpp.h
    src/json_cpp.cpp
    src/log.h
    src/log.cpp
    src/send_rate.h
    src/send_rate.cpp
    src/wire/bytes.h
    src/wire/ipc_crypto.h
    src/wire/ipc_crypto.cpp
    src/wire/rtcp.h
    src/wire/rtcp.cpp
    src/wire/rtp.h
    src/wire/rtp.cpp
    src/wire/srtp.h
    src/wire/srtp.cpp
    src/wire/video.h
    src/wire/video.cpp
    src/wire/zrtp_packet.h
    src/wire/zrtp_packet.cpp
    src/media.h
    src/call_ui.h
    src/call_window.h
    src/call_window.cpp
    src/ui_client.h
    src/ui_client.cpp
    src/stub_media.h
    src/stub_media.cpp
    src/zrtc_media.h
    src/zrtc_media.cpp
    src/engine_core.h
    src/engine_core.cpp
    src/ipc_link.h
    src/ipc_link.cpp
)
target_include_directories(zcall_core PUBLIC src)
target_link_libraries(zcall_core PUBLIC Qt6::Core Qt6::Network OpenSSL::Crypto)
set_target_properties(zcall_core PROPERTIES AUTOMOC ON)

add_executable(zalos-call src/main.cpp)
target_link_libraries(zalos-call PRIVATE zcall_core)
set_target_properties(zalos-call PROPERTIES AUTOMOC ON)
install(TARGETS zalos-call RUNTIME DESTINATION "${ZCALL_LIBEXEC_DIR}")

if(ZCALL_BUILD_TESTS)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    set(ZCALL_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../tests")

    add_executable(test_call_wire "${ZCALL_TEST_DIR}/test_call_wire.cpp")
    target_link_libraries(test_call_wire PRIVATE zcall_core Qt6::Test)
    target_include_directories(test_call_wire PRIVATE "${ZCALL_TEST_DIR}")
    target_compile_definitions(test_call_wire PRIVATE ZCALL_VECTORS_DIR="${ZCALL_TEST_DIR}/vectors")
    set_target_properties(test_call_wire PROPERTIES AUTOMOC ON)
    add_test(NAME test_call_wire COMMAND test_call_wire)

    add_executable(test_call_engine "${ZCALL_TEST_DIR}/test_call_engine.cpp")
    target_link_libraries(test_call_engine PRIVATE zcall_core Qt6::Test)
    target_include_directories(test_call_engine PRIVATE "${ZCALL_TEST_DIR}")
    target_compile_definitions(test_call_engine PRIVATE ZCALL_ENGINE_BIN="$<TARGET_FILE:zalos-call>")
    set_target_properties(test_call_engine PROPERTIES AUTOMOC ON)
    add_test(NAME test_call_engine COMMAND test_call_engine)
    set_tests_properties(test_call_engine PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
