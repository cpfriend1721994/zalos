# SPDX-License-Identifier: BSD-3-Clause
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Threads REQUIRED)

add_library(zcall_audio_core STATIC
    src/clip_guard.cpp
    src/clip_guard.h
    src/devices.cpp
    src/devices.h
    src/diag.cpp
    src/diag.h
    src/dsp.cpp
    src/dsp.h
    src/dump.cpp
    src/dump.h
    src/jitter.cpp
    src/jitter.h
    src/log.cpp
    src/log.h
    src/opus_api.cpp
    src/opus_api.h
    src/options.cpp
    src/options.h
    src/playout.cpp
    src/playout.h
    src/sink.cpp
    src/sink.h
    src/spsc_ring.h
    src/subprocess.cpp
    src/subprocess.h
    src/wire.cpp
    src/wire.h
)
target_include_directories(zcall_audio_core PUBLIC src)
target_compile_features(zcall_audio_core PUBLIC cxx_std_17)
target_compile_options(zcall_audio_core PRIVATE -Wall -Wextra)
target_link_libraries(zcall_audio_core PUBLIC Qt6::Core Threads::Threads ${CMAKE_DL_LIBS})

add_executable(zalos-call-audio src/main.cpp)
target_compile_options(zalos-call-audio PRIVATE -Wall -Wextra)
target_link_libraries(zalos-call-audio PRIVATE zcall_audio_core)

install(TARGETS zalos-call-audio RUNTIME DESTINATION "${ZCALL_LIBEXEC_DIR}")

if(ZCALL_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tests/audio ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
