cmake_minimum_required(VERSION 3.20)
project(inop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# src/ is split by role, not left flat:
#   logic/            the cipher core + prep layer (inop/registry/pipeline/
#                      rng/generator/languages) — DESIGN.md already draws
#                      this exact boundary, this just gives it a folder.
#   settings/          the machine-configuration schema (Settings struct,
#                      parse/validate/build_machine), independent of how a
#                      config was obtained.
#   interface/         every user-facing entry point: the terminal CLI
#                      (main.cpp, batch.*) and the optional GUI (gui*.*).
#   benchmark-debug/   the standalone throughput/correctness harness entry
#                      point.
# Every #include "foo.hpp" in the source stays a bare filename — no file
# was touched to make this split, only the compiler's include path grew to
# cover all four folders (see target_include_directories below).
set(INOP_SRC_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logic
  ${CMAKE_CURRENT_SOURCE_DIR}/src/settings
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interface
  ${CMAKE_CURRENT_SOURCE_DIR}/src/benchmark-debug
)

# The GUI settings panel (GLFW + raw OpenGL + stb_truetype) is optional and
# opt-in — the terminal build has zero mandatory dependencies beyond a
# compiler either way. Off by default; enable with -DINOP_WITH_GUI=ON and a
# vcpkg toolchain file (see CMakePresets.json's "gui" preset).
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

# gui.hpp exposes exactly one declaration (run_gui_settings()) with no
# GL/GLFW types in it, so main.cpp includes it unconditionally either way —
# only this source-list choice decides whether the real panel or the
# CLI-only stub provides that symbol. No #ifdef needed in main.cpp.
if(INOP_WITH_GUI)
  set(INOP_GUI_SOURCES
    src/interface/gui.cpp
    src/interface/gui_render.cpp
    src/interface/gui_widgets.cpp
    src/interface/gui_config_store.cpp
    src/interface/gui_file_tile_panel.cpp
    src/interface/gui_settings_panel.cpp
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

# inop_benchmark never links the GUI — it's a headless correctness/speed
# harness and has no reason to touch a window.
if(INOP_WITH_GUI)
  target_include_directories(inop PRIVATE ${STB_INCLUDE_DIR})
  target_link_libraries(inop PRIVATE glfw OpenGL::GL nlohmann_json::nlohmann_json)
  if(MINGW)
    # Fully static, not just libgcc/libstdc++: this MinGW is the
    # posix-threads variant, so GLFW's threading support pulls in
    # libwinpthread-1.dll too — without -static that DLL (and libgcc/
    # libstdc++'s) has to travel alongside inop.exe, which defeats the
    # point of a single portable settings-panel-enabled binary.
    target_link_options(inop PRIVATE -static)
  endif()
endif()

enable_testing()
add_test(NAME self_test COMMAND inop --self-test --no-color)
