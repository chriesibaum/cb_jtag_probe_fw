# Copyright (c) 2021 Teslabs Engineering S.L.
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# add our custom zephyr BOARD_ROOT directory
list(APPEND BOARD_ROOT
	${CMAKE_CURRENT_SOURCE_DIR}/../
)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cb_jtag_probe)

# Setup git watcher, extracts git infos and writes them to POST_CONFIGURE_FILE(s) based on
# of PRE_CONFIGURE_FILE(s) template(s).
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/../build_helpers/cmake/")
set(PRE_CONFIGURE_FILE "${CMAKE_SOURCE_DIR}/../build_helpers/cmake/fw_version.h.in")
set(POST_CONFIGURE_FILE "${CMAKE_BINARY_DIR}/fw_version.h")
include(git_watcher)

target_include_directories(app PRIVATE ${CMAKE_BINARY_DIR})


FILE(GLOB app_sources CONFIGURE_DEPENDS src/*.c)
target_sources(app PRIVATE ${app_sources})
