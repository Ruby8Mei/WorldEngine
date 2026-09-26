cmake_minimum_required(VERSION 3.27)
project(inop VERSION 2.3.1 LANGUAGES CXX)

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
#   bombe/             the offline cryptanalysis harness. An attack tool,
#                      filed apart from the correctness harness because it
#                      is a different kind of thing, and never linked into
#                      anything that carries a message.
# Every #include "foo.hpp" in the source stays a bare filename — no file
# was touched to make this split, only the compiler's include path grew to
# cover all four folders (see target_include_directories below).
set(INOP_SRC_DIRS
  ${CMAKE_CURRENT_SOURCE_DIR}/src/logic
  ${CMAKE_CURRENT_SOURCE_DIR}/src/settings
  ${CMAKE_CURRENT_SOURCE_DIR}/src/interface
  ${CMAKE_CURRENT_SOURCE_DIR}/src/benchmark-debug
  ${CMAKE_CURRENT_SOURCE_DIR}/src/bombe
  ${CMAKE_CURRENT_SOURCE_DIR}/tests
)

# The optional GUI (GLFW + raw OpenGL + stb_truetype) is opt-in. Off by
# default; enable with -DINOP_WITH_GUI=ON and a vcpkg toolchain file (see
# CMakePresets.json's "gui" preset).
#
# NOTE: the terminal build no longer has zero mandatory dependencies. The
# data files (wheels, settings, key sheets) are JSON as of 2.3.0, and the
# reader lives in src/logic and src/settings, which compile into all four
# executables — so nlohmann-json is required by every build, not just the
# GUI. That trade was made deliberately: one file format across both
# interfaces was worth more than the zero-dependency property.
option(INOP_WITH_GUI "Build the optional GLFW/OpenGL settings-panel GUI" OFF)

# Warnings as errors, off by default and on in CI. Deliberately not the
# default: a compiler upgrade that introduces one new warning would
# otherwise stop the build on a machine where somebody is trying to send a
# message, and that trade is wrong for this project. CI has no such
# constraint and runs with it on.
option(INOP_WERROR "Treat compiler warnings as errors" OFF)
option(INOP_BUILD_GUI_HEADLESS_TESTS "Build the independent GUI headless test target" ON)

# Needed by every target, GUI or not: the data-file format is JSON and the
# reader is in the logic layer.
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)

if(INOP_WITH_GUI OR INOP_BUILD_GUI_HEADLESS_TESTS)
  find_package(glfw3 CONFIG REQUIRED)
  find_package(OpenGL REQUIRED)
  find_path(STB_INCLUDE_DIR NAMES stb_truetype.h)
  find_path(MINIAUDIO_INCLUDE_DIR NAMES miniaudio.h)
  if(NOT STB_INCLUDE_DIR)
    message(FATAL_ERROR "stb_truetype.h not found — is vcpkg's 'stb' port installed via the toolchain file?")
  endif()
  if(NOT MINIAUDIO_INCLUDE_DIR)
    message(FATAL_ERROR "miniaudio.h not found")
  endif()
endif()

set(INOP_CORE_SOURCES
  src/logic/inop.cpp
  src/logic/registry.cpp
  src/logic/pipeline.cpp
  src/logic/rng.cpp
  src/logic/generator.cpp
  src/logic/languages.cpp
  src/logic/utf8.cpp
  src/logic/transform.cpp
  src/logic/transform_greek.cpp
  src/logic/transform_hangul.cpp
  src/settings/settings.cpp
  src/interface/batch.cpp
  src/interface/cli_compat.cpp
  src/interface/cli_text.cpp
)

# gui.hpp exposes exactly one declaration (run_gui_settings()) with no
# GL/GLFW types in it, so main.cpp includes it unconditionally either way —
# only this source-list choice decides whether the real panel or the
# CLI-only stub provides that symbol. No #ifdef needed in main.cpp.
set(INOP_REAL_GUI_SOURCES
  src/interface/audio_manager.cpp
  src/interface/gui.cpp
  src/interface/gui_render.cpp
  src/interface/gui_widgets.cpp
  src/interface/gui_text_edit.cpp
  src/interface/gui_plaintext.cpp
  src/interface/gui_anim.cpp
  src/interface/gui_form.cpp
  src/interface/gui_script.cpp
  src/interface/gui_config_store.cpp
  src/interface/gui_file_tile_panel.cpp
  src/interface/gui_setup_panel.cpp
  src/interface/gui_main_menu.cpp
  src/interface/gui_bombe_panel.cpp
  src/interface/gui_enciphering_panel.cpp
  src/interface/gui_prefs.cpp
  src/interface/gui_settings_panel.cpp
  src/interface/gui_legal_panel.cpp
  src/interface/gui_maintenance_panel.cpp
  src/interface/gui_tutorial.cpp
)

set(INOP_GUI_CHECK_SOURCES
  tests/gui_aggregate_checks.cpp
  tests/gui_widget_checks.cpp
  tests/audio_checks.cpp
  tests/gui_panel_checks.cpp
)

if(INOP_WITH_GUI)
  set(INOP_GUI_SOURCES ${INOP_REAL_GUI_SOURCES} ${INOP_GUI_CHECK_SOURCES})
else()
  set(INOP_GUI_SOURCES src/interface/gui_stub.cpp)
endif()

# The core sources are identical in all four executables, and passing the
# same list to four add_executable() calls compiled every one of them four
# times. registry.cpp, settings.cpp and generator.cpp each pull in
# nlohmann/json.hpp, which is most of the cost, so the three redundant
# copies were close to half of a clean build. Compiled once here instead.
# All four targets already used byte-identical flags, so nothing about the
# resulting objects changes.
add_library(inop_core OBJECT ${INOP_CORE_SOURCES})

add_library(inop_bombe_engine OBJECT
  src/bombe/bombe_engine.cpp
)

add_executable(inop
  ${INOP_GUI_SOURCES}
  src/interface/main.cpp
  tests/cli_self_test.cpp
  $<TARGET_OBJECTS:inop_bombe_engine>
)

add_executable(inop_benchmark
  src/benchmark-debug/benchmark_main.cpp
)

# inop_langprobe dumps the folded symbol stream and the declared mark
# grammar for offline analysis of the diacritic scheme. Same standing as
# inop_benchmark: headless, offline, never linked into the live pipeline.
add_executable(inop_langprobe
  src/benchmark-debug/langprobe_main.cpp
)

# inop_bombe attacks the machine. Same standing as the other harnesses:
# offline, headless, manufactures its own key material and never reads or
# writes any, and is never linked into the live message pipeline.
add_executable(inop_bombe
  src/bombe/bombe_main.cpp
  tests/bombe_engine_checks.cpp
  $<TARGET_OBJECTS:inop_bombe_engine>
)

add_executable(inop_bombe_engine_tests
  tests/bombe_engine_checks_main.cpp
  tests/bombe_engine_checks.cpp
  src/logic/inop.cpp
  src/logic/registry.cpp
  $<TARGET_OBJECTS:inop_bombe_engine>
)

if(INOP_BUILD_GUI_HEADLESS_TESTS)
  add_executable(inop_gui_headless_tests
    tests/gui_headless_main.cpp
    tests/compatibility_fixtures.cpp
    ${INOP_REAL_GUI_SOURCES}
    ${INOP_GUI_CHECK_SOURCES}
    $<TARGET_OBJECTS:inop_bombe_engine>
  )
  add_executable(inop_audio_tests
    tests/audio_checks_main.cpp
    tests/audio_checks.cpp
    src/interface/audio_manager.cpp
  )
endif()

foreach(tgt inop inop_benchmark inop_langprobe inop_bombe)
  target_link_libraries(${tgt} PRIVATE inop_core)
endforeach()

if(INOP_BUILD_GUI_HEADLESS_TESTS)
  target_link_libraries(inop_gui_headless_tests PRIVATE inop_core)
endif()

set(INOP_CONFIGURED_TARGETS inop inop_benchmark inop_langprobe inop_bombe inop_bombe_engine_tests inop_core inop_bombe_engine)
if(INOP_BUILD_GUI_HEADLESS_TESTS)
  list(APPEND INOP_CONFIGURED_TARGETS inop_gui_headless_tests)
endif()

foreach(tgt IN LISTS INOP_CONFIGURED_TARGETS)
  target_include_directories(${tgt} PRIVATE ${INOP_SRC_DIRS})
  target_link_libraries(${tgt} PRIVATE nlohmann_json::nlohmann_json)

  if(MSVC)
    target_compile_options(${tgt} PRIVATE /W4 /utf-8)
    if(INOP_WERROR)
      target_compile_options(${tgt} PRIVATE /WX)
    endif()
  else()
    target_compile_options(${tgt} PRIVATE -Wall -Wextra -Wpedantic)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL 13)
      # -Wdangling-reference fires on every `const Suite& s = suite("38")`
      # in the tree. suite() returns a reference into a function-local
      # static map and keeps nothing, so none of them dangle; the GCC 13
      # heuristic looks at the call expression, sees a temporary argument,
      # and warns anyway. Each call site could be rewritten to bind
      # the literal to a named local first, which would silence a compiler
      # heuristic by making the code worse. Turned off here instead.
      #
      # Outside the INOP_WERROR block on purpose. It used to sit inside it,
      # which meant CI got the quiet build and every ordinary build got
      # fourteen false warnings, hiding the one real warning among them.
      target_compile_options(${tgt} PRIVATE -Wno-dangling-reference)
    endif()
    if(INOP_WERROR)
      target_compile_options(${tgt} PRIVATE -Werror)
    endif()
  endif()

  if(WIN32)
    target_link_libraries(${tgt} PRIVATE bcrypt)
  endif()
endforeach()

# inop_benchmark never links the GUI — it's a headless correctness/speed
# harness and has no reason to touch a window.
if(INOP_WITH_GUI)
  target_include_directories(inop PRIVATE ${STB_INCLUDE_DIR} ${MINIAUDIO_INCLUDE_DIR})
  target_link_libraries(inop PRIVATE glfw OpenGL::GL)
  add_custom_command(TARGET inop POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/fonts
            $<TARGET_FILE_DIR:inop>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/audio
            $<TARGET_FILE_DIR:inop>/audio
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/images
            $<TARGET_FILE_DIR:inop>/images
    VERBATIM
  )
  if(MINGW)
    # Fully static, not just libgcc/libstdc++: this MinGW is the
    # posix-threads variant, so GLFW's threading support pulls in
    # libwinpthread-1.dll too — without -static that DLL (and libgcc/
    # libstdc++'s) has to travel alongside inop.exe, which defeats the
    # point of a single portable settings-panel-enabled binary.
    target_link_options(inop PRIVATE -static)
  endif()
endif()

if(INOP_BUILD_GUI_HEADLESS_TESTS)
  target_include_directories(inop_gui_headless_tests PRIVATE ${STB_INCLUDE_DIR} ${MINIAUDIO_INCLUDE_DIR})
  target_link_libraries(inop_gui_headless_tests PRIVATE glfw OpenGL::GL)
  add_custom_command(TARGET inop_gui_headless_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/fonts
            $<TARGET_FILE_DIR:inop_gui_headless_tests>/fonts
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/audio
            $<TARGET_FILE_DIR:inop_gui_headless_tests>/audio
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/images
            $<TARGET_FILE_DIR:inop_gui_headless_tests>/images
    VERBATIM
  )
  if(MINGW)
    target_link_options(inop_gui_headless_tests PRIVATE -static)
  endif()

  target_include_directories(inop_audio_tests PRIVATE ${INOP_SRC_DIRS} ${MINIAUDIO_INCLUDE_DIR})
  if(MSVC)
    target_compile_options(inop_audio_tests PRIVATE /W4 /utf-8)
    if(INOP_WERROR)
      target_compile_options(inop_audio_tests PRIVATE /WX)
    endif()
  else()
    target_compile_options(inop_audio_tests PRIVATE -Wall -Wextra -Wpedantic)
    if(INOP_WERROR)
      target_compile_options(inop_audio_tests PRIVATE -Werror)
    endif()
  endif()
  if(MINGW)
    target_link_options(inop_audio_tests PRIVATE -static)
  endif()
endif()

# ── source rules enforced at build time ─────────────────────────────────
# DESIGN section 1 (the cipher core is a rotor machine and nothing else)
# and the no-weak-randomness rule were both prose and nothing else. They
# are checked here rather than in the test suite so that not running the
# tests cannot skip them: every build of every target runs this first, and
# a violation is a build failure naming the rule and quoting the line.
add_custom_target(inop_source_rules ALL
  COMMAND ${CMAKE_COMMAND}
          -DINOP_SRC_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/src
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_source_rules.cmake
  COMMENT "Checking INOP source rules (core purity, RNG purity)"
  VERBATIM
)
foreach(tgt IN LISTS INOP_CONFIGURED_TARGETS)
  add_dependencies(${tgt} inop_source_rules)
endforeach()
if(INOP_BUILD_GUI_HEADLESS_TESTS)
  add_dependencies(inop_audio_tests inop_source_rules)
endif()

enable_testing()
add_test(NAME self_test COMMAND inop --self-test --no-color)
if(INOP_BUILD_GUI_HEADLESS_TESTS)
  add_test(NAME gui_headless_self_test COMMAND inop_gui_headless_tests)
  add_test(NAME gui_aggregate_checks COMMAND inop_gui_headless_tests --group aggregate)
  add_test(NAME gui_widget_checks COMMAND inop_gui_headless_tests --group widgets)
  add_test(NAME gui_panel_checks COMMAND inop_gui_headless_tests --group panels)
  add_test(NAME audio_fake_backend_checks COMMAND inop_audio_tests)
  set_tests_properties(self_test gui_headless_self_test gui_aggregate_checks
                       gui_widget_checks gui_panel_checks audio_fake_backend_checks
                       PROPERTIES RUN_SERIAL TRUE)
endif()
if(WIN32)
  find_program(INOP_POWERSHELL NAMES pwsh powershell)
  if(INOP_POWERSHELL)
    add_test(NAME gui_script_manifest_validation
      COMMAND ${INOP_POWERSHELL} -NoProfile -File
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/run-gui-suite.ps1 -ValidateOnly)
    add_test(NAME gui_script_runner_self_test
      COMMAND ${INOP_POWERSHELL} -NoProfile -File
              ${CMAKE_CURRENT_SOURCE_DIR}/tools/run-gui-suite.ps1 -SelfTest)
  endif()
endif()
add_test(
  NAME generated_transform_table
  COMMAND ${CMAKE_COMMAND}
          -DPYTHON_EXECUTABLE=${Python3_EXECUTABLE}
          -DGENERATOR=${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_transform_table.py
          -DTRACKED_TABLE=${CMAKE_CURRENT_SOURCE_DIR}/src/logic/transform_table.inc
          -DCHECK_ROOT=${CMAKE_CURRENT_BINARY_DIR}
          -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/check_transform_table.cmake
)

install(TARGETS inop RUNTIME DESTINATION . COMPONENT Runtime)
if(INOP_WITH_GUI)
  install(DIRECTORY fonts DESTINATION . COMPONENT Assets)
  install(DIRECTORY audio DESTINATION . COMPONENT Assets)
  install(DIRECTORY images DESTINATION . COMPONENT Assets)
endif()
install(FILES LICENSE README.md DESTINATION . COMPONENT Documentation)

set(CPACK_GENERATOR "INNOSETUP")
set(CPACK_PACKAGE_NAME "INOP")
set(CPACK_PACKAGE_VENDOR "INOP")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "INOP rotor cipher machine")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "INOP")
set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "INOP")
set(CPACK_PACKAGE_FILE_NAME "INOP-${PROJECT_VERSION}-Windows-x64")
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_BINARY_DIR}/package")
set(CPACK_MONOLITHIC_INSTALL ON)
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
set(CPACK_VERBATIM_VARIABLES ON)
set(CPACK_INNOSETUP_ARCHITECTURE "x64")
set(CPACK_INNOSETUP_INSTALL_ROOT "{localappdata}\\Programs")
set(CPACK_INNOSETUP_ALLOW_CUSTOM_DIRECTORY ON)
set(CPACK_INNOSETUP_CREATE_UNINSTALL_LINK ON)
set(CPACK_INNOSETUP_EXTRA_SCRIPTS "${CMAKE_CURRENT_SOURCE_DIR}/cmake/inop_installer.iss")
set(CPACK_INNOSETUP_USE_CMAKE_BOOL_FORMAT ON)
set(CPACK_INNOSETUP_USE_MODERN_WIZARD ON)
set(CPACK_INNOSETUP_SETUP_AppId "INOP-Desktop")
set(CPACK_INNOSETUP_SETUP_PrivilegesRequired "lowest")
set(CPACK_INNOSETUP_SETUP_CloseApplications ON)
set(CPACK_INNOSETUP_SETUP_RestartApplications OFF)
set(CPACK_INNOSETUP_SETUP_UsePreviousAppDir ON)
find_program(INOP_INNOSETUP_COMPILER
  NAMES ISCC ISCC.exe
  HINTS
    "$ENV{LOCALAPPDATA}/Programs/Inno Setup 7"
    "$ENV{LOCALAPPDATA}/Programs/Inno Setup 6"
    "$ENV{ProgramFiles}/Inno Setup 7"
    "$ENV{ProgramFiles}/Inno Setup 6"
)
if(INOP_INNOSETUP_COMPILER)
  set(CPACK_INNOSETUP_EXECUTABLE "${INOP_INNOSETUP_COMPILER}")
endif()
include(CPack)
add_test(NAME bombe_self_check COMMAND inop_bombe --self-check)
add_test(NAME bombe_engine_checks COMMAND inop_bombe_engine_tests)

get_property(INOP_CTEST_INVENTORY DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY TESTS)
set(INOP_REQUIRED_TESTS self_test generated_transform_table bombe_self_check bombe_engine_checks)
if(INOP_BUILD_GUI_HEADLESS_TESTS)
  list(APPEND INOP_REQUIRED_TESTS gui_headless_self_test gui_aggregate_checks
       gui_widget_checks gui_panel_checks audio_fake_backend_checks)
endif()
foreach(required_test IN LISTS INOP_REQUIRED_TESTS)
  if(NOT required_test IN_LIST INOP_CTEST_INVENTORY)
    message(FATAL_ERROR "Required CTest group is missing: ${required_test}")
  endif()
endforeach()
list(SORT INOP_CTEST_INVENTORY)
message(STATUS "INOP CTest inventory: ${INOP_CTEST_INVENTORY}")
