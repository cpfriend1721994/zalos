# SPDX-License-Identifier: BSD-3-Clause
find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Threads REQUIRED)

option(ZALOS_SYSTEM_OPUS "Link the call audio engine against the system libopus instead of the bundled 1.6.1" OFF)

if(ZALOS_SYSTEM_OPUS)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ZALOS_OPUS REQUIRED IMPORTED_TARGET opus)
    set(ZALOS_OPUS_TARGET PkgConfig::ZALOS_OPUS)
else()
    include(ExternalProject)
    set(ZALOS_OPUS_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../third_party/opus")
    set(ZALOS_OPUS_BUILD_DIR "${CMAKE_CURRENT_BINARY_DIR}/opus-build")
    set(ZALOS_OPUS_LIBRARY "${ZALOS_OPUS_BUILD_DIR}/libopus.a")
    ExternalProject_Add(zalos_opus_bundled
        SOURCE_DIR "${ZALOS_OPUS_SOURCE_DIR}"
        BINARY_DIR "${ZALOS_OPUS_BUILD_DIR}"
        INSTALL_COMMAND ""
        CMAKE_ARGS
            -DCMAKE_BUILD_TYPE=Release
            -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            -DOPUS_BUILD_SHARED_LIBRARY=OFF
            -DOPUS_BUILD_PROGRAMS=OFF
            -DOPUS_BUILD_TESTING=OFF
            -DOPUS_INSTALL_PKG_CONFIG_MODULE=OFF
            -DOPUS_INSTALL_CMAKE_CONFIG_MODULE=OFF
            -DOPUS_DRED=OFF
            -DOPUS_OSCE=OFF
        BUILD_BYPRODUCTS "${ZALOS_OPUS_LIBRARY}")
    add_library(zalos_opus_static STATIC IMPORTED GLOBAL)
    set_target_properties(zalos_opus_static PROPERTIES
        IMPORTED_LOCATION "${ZALOS_OPUS_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ZALOS_OPUS_SOURCE_DIR}/include")
    set(ZALOS_OPUS_TARGET zalos_opus_static)
endif()

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
    src/pulse_api.cpp
    src/pulse_api.h
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
target_link_libraries(zcall_audio_core PUBLIC Qt6::Core Threads::Threads ${CMAKE_DL_LIBS} ${ZALOS_OPUS_TARGET} m)
if(TARGET zalos_opus_bundled)
    add_dependencies(zcall_audio_core zalos_opus_bundled)
endif()

add_executable(zalos-call-audio src/main.cpp)
target_compile_options(zalos-call-audio PRIVATE -Wall -Wextra)
target_link_libraries(zalos-call-audio PRIVATE zcall_audio_core)
target_link_options(zalos-call-audio PRIVATE -Wl,--exclude-libs,ALL)

install(TARGETS zalos-call-audio RUNTIME DESTINATION "${ZCALL_LIBEXEC_DIR}")

if(ZCALL_BUILD_TESTS)
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../tests/audio ${CMAKE_CURRENT_BINARY_DIR}/tests)
endif()
