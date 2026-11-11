# SPDX-License-Identifier: BSD-3-Clause
find_package(Qt6 REQUIRED COMPONENTS Core Test)

foreach(name wire jitter dsp opus)
    set(target test_call_audio_${name})
    add_executable(${target} test_call_audio_${name}.cpp fake_decoder.h)
    set_target_properties(${target} PROPERTIES AUTOMOC ON)
    target_link_libraries(${target} PRIVATE zcall_audio_core Qt6::Test)
    target_compile_definitions(${target} PRIVATE
        ZCALL_AUDIO_BIN="$<TARGET_FILE:zalos-call-audio>")
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endforeach()
add_dependencies(test_call_audio_opus zalos-call-audio)
