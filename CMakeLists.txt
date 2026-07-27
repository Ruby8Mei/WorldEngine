cmake_minimum_required(VERSION 3.15)
project(inop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_executable(inop
  src/inop.cpp
  src/registry.cpp
  src/pipeline.cpp
  src/rng.cpp
  src/generator.cpp
  src/main.cpp
)

if(MSVC)
  target_compile_options(inop PRIVATE /W4 /utf-8)
else()
  target_compile_options(inop PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(WIN32)
  target_link_libraries(inop PRIVATE bcrypt)
endif()

enable_testing()
add_test(NAME self_test COMMAND inop --self-test --no-color)
