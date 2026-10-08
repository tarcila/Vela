## SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
## SPDX-License-Identifier: Apache-2.0

# Every test_*.cpp here joins the suite without editing a shared CMake file.

project(vsrIntegrationTests LANGUAGES CXX)

file(GLOB VSR_INTEGRATION_TEST_SOURCES CONFIGURE_DEPENDS
  ${CMAKE_CURRENT_LIST_DIR}/test_*.cpp
)
project_add_executable(${VSR_INTEGRATION_TEST_SOURCES})
project_link_libraries(PRIVATE vsr_integration vsr_scene vsr_rendering vela_ext_catch2)
if (TARGET vsr_ui_imgui)
  project_link_libraries(PRIVATE vsr_ui_imgui)
  target_compile_definitions(${PROJECT_NAME} PRIVATE VSR_TEST_VIEWER_UI)
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
target_compile_definitions(${PROJECT_NAME} PRIVATE
  VSR_MESH_TEST_PYTHON="${Python3_EXECUTABLE}"
  VSR_MESH_TEST_SOURCE="${CMAKE_SOURCE_DIR}"
)
add_test(NAME vsr::integration COMMAND ${PROJECT_NAME} "[integration]")
add_test(NAME vsr::mesh-helper
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/test-mesh-bundle.py)

