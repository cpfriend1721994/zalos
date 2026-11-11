# SPDX-License-Identifier: BSD-3-Clause
cmake_minimum_required(VERSION 3.21)
project(zcall_cpp VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOUIC OFF)

option(ZCALL_BUILD_ENGINE "Build the zalos-call signalling/media engine" ON)
option(ZCALL_BUILD_AUDIO "Build the zalos-call-audio microphone/speaker engine" ON)
option(ZCALL_BUILD_TESTS "Build the call engine unit tests" ON)

include(GNUInstallDirs)
set(ZCALL_LIBEXEC_DIR "${CMAKE_INSTALL_LIBEXECDIR}/zalos" CACHE STRING "Install directory of the call engine binaries")

if(ZCALL_BUILD_TESTS)
    enable_testing()
endif()

if(ZCALL_BUILD_ENGINE)
    add_subdirectory(engine)
endif()

if(ZCALL_BUILD_AUDIO)
    add_subdirectory(audio)
endif()

install(FILES LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/zalos" RENAME zcall-cpp.LICENSE)
