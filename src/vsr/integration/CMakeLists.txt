## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

# Headless agent-integration core. Deliberately free of UI, Lua, Pi and
# network dependencies. Sources, including those in module subdirectories, are
# globbed so later additions need no edit to a shared CMake file.

project(vsr_integration)

project_add_library(STATIC)

file(GLOB_RECURSE VSR_INTEGRATION_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/*.cpp
)
project_sources(PRIVATE ${VSR_INTEGRATION_SOURCES})

project_include_directories(
PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/../..>
)

find_package(Threads REQUIRED)
project_link_libraries(PUBLIC vsr_scene Threads::Threads)
# PNG deflate via the vendored stb writer; an implementation detail.
project_link_libraries(PRIVATE $<BUILD_INTERFACE:vela_ext_stb_image>
  $<BUILD_INTERFACE:vela_ext_zlib>)

# Embedded MCP server (header-only under mcp/); opt-in, no extra dependency.
if (VSR_USE_INTEGRATION_MCP)
  project_compile_definitions(PUBLIC VSR_USE_INTEGRATION_MCP)
endif()
