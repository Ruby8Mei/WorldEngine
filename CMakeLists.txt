cmake_minimum_required(VERSION 3.20)
project(inop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set(INOP_SRC_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logic
  ${CMAKE_CURRENT_SOURCE_DIR}/src/settings
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interface
  ${CMAKE_CURRENT_SOURCE_DIR}/src/benchmark-debug
)

option(INOP_WITH_GUI "Build the optional GLFW/OpenGL settings-panel GUI" OFF)

if(INOP_WITH_GUI)
  find_package(glfw3 CONFIG REQUIRED)
  find_package(OpenGL REQUIRED)
  find_path(STB_INCLUDE_DIR NAMES stb_truetype.h)
  if(NOT STB_INCLUDE_DIR)
    message(FATAL_ERROR "stb_truetype.h not found — is vcpkg's 'stb' port installed via the toolchain file?")
  endif()
  find_package(nlohmann_json CONFIG REQUIRED)
endif()

set(INOP_CORE_SOURCES
  src/logic/inop.cpp
  src/logic/registry.cpp
  src/logic/pipeline.cpp
  src/logic/rng.cpp
  src/logic/generator.cpp
  src/logic/languages.cpp
  src/settings/settings.cpp
  src/interface/batch.cpp
)

if(INOP_WITH_GUI)
  set(INOP_GUI_SOURCES
    src/interface/gui.cpp
    src/interface/gui_render.cpp
    src/interface/gui_widgets.cpp
    src/interface/gui_config_store.cpp
    src/interface/gui_file_tile_panel.cpp
    src/interface/gui_setup_panel.cpp
    src/interface/gui_main_menu.cpp
  )
else()
  set(INOP_GUI_SOURCES src/interface/gui_stub.cpp)
endif()

add_executable(inop
  ${INOP_CORE_SOURCES}
  ${INOP_GUI_SOURCES}
  src/interface/main.cpp
)

add_executable(inop_benchmark
  ${INOP_CORE_SOURCES}
  src/benchmark-debug/benchmark_main.cpp
)

foreach(tgt inop inop_benchmark)
  target_include_directories(${tgt} PRIVATE ${INOP_SRC_DIRS})

  if(MSVC)
    target_compile_options(${tgt} PRIVATE /W4 /utf-8)
  else()
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  if(WIN32)
    target_link_libraries(${tgt} PRIVATE bcrypt)
  endif()
endforeach()

if(INOP_WITH_GUI)
  target_include_directories(inop PRIVATE ${STB_INCLUDE_DIR})
  target_link_libraries(inop PRIVATE glfw OpenGL::GL nlohmann_json::nlohmann_json)
  if(MINGW)
    target_link_options(inop PRIVATE -static)
  endif()
endif()

enable_testing()
add_test(NAME self_test COMMAND inop --self-test --no-color)

