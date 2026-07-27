cmake_minimum_required(VERSION 3.16)

project(Hub75Visualizer VERSION 1.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  # Ubuntu on Raspberry Pi supplies SDL through libsdl2-dev. Using the system
  # package avoids building unused X11/Wayland dependencies from source.
  find_package(SDL2 REQUIRED)
else()
  include(FetchContent)
  set(SDL_SHARED OFF CACHE BOOL "" FORCE)
  set(SDL_STATIC ON CACHE BOOL "" FORCE)
  set(SDL_TEST OFF CACHE BOOL "" FORCE)
  set(SDL_TESTS OFF CACHE BOOL "" FORCE)
  set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
  set(SDL_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
  FetchContent_Declare(
    SDL2
    URL https://github.com/libsdl-org/SDL/releases/download/release-2.30.12/SDL2-2.30.12.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(SDL2)
endif()

set(PLATFORM_VISUAL_SOURCES src/platform/VisualSDL.cpp)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  include(ExternalProject)
  find_program(MAKE_EXECUTABLE NAMES make gmake REQUIRED)
  set(RGB_MATRIX_SOURCE_DIR "${CMAKE_BINARY_DIR}/_deps/rgb-matrix-src")
  file(MAKE_DIRECTORY "${RGB_MATRIX_SOURCE_DIR}/include")
  ExternalProject_Add(rgb_matrix_external
    GIT_REPOSITORY https://github.com/hzeller/rpi-rgb-led-matrix.git
    GIT_TAG master
    GIT_SHALLOW TRUE
    SOURCE_DIR "${RGB_MATRIX_SOURCE_DIR}"
    CONFIGURE_COMMAND ""
    BUILD_COMMAND "${MAKE_EXECUTABLE}" -C <SOURCE_DIR>/lib
                  "CXX=${CMAKE_CXX_COMPILER}"
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${RGB_MATRIX_SOURCE_DIR}/lib/librgbmatrix.a"
  )
  add_library(rgb_matrix STATIC IMPORTED GLOBAL)
  set_target_properties(rgb_matrix PROPERTIES
    IMPORTED_LOCATION "${RGB_MATRIX_SOURCE_DIR}/lib/librgbmatrix.a"
    INTERFACE_INCLUDE_DIRECTORIES "${RGB_MATRIX_SOURCE_DIR}/include"
  )
  add_dependencies(rgb_matrix rgb_matrix_external)
  list(APPEND PLATFORM_VISUAL_SOURCES src/platform/VisualHub75.cpp)
endif()

add_executable(Hub75Simulator
  src/main.cpp
  src/RasterRenderer.cpp
  src/DeltaTimer.cpp
  src/platform/DesktopEntry.cpp
  src/platform/Hardware.cpp
  src/platform/VisualOutput.cpp
  ${PLATFORM_VISUAL_SOURCES}
  src/imagery/SongOfTheNight.cpp
)

target_include_directories(Hub75Simulator PRIVATE include src/platform)
target_compile_definitions(Hub75Simulator PRIVATE
  $<$<PLATFORM_ID:Windows>:NOMINMAX;WIN32_LEAN_AND_MEAN>
  $<$<PLATFORM_ID:Linux>:HAVE_HUB75_OUTPUT=1>
)

if(TARGET SDL2::SDL2-static)
  target_link_libraries(Hub75Simulator PRIVATE SDL2::SDL2-static)
elseif(TARGET SDL2::SDL2)
  target_link_libraries(Hub75Simulator PRIVATE SDL2::SDL2)
elseif(TARGET SDL2-static)
  target_link_libraries(Hub75Simulator PRIVATE SDL2-static)
else()
  message(FATAL_ERROR "SDL2 did not expose a supported CMake target.")
endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(Threads REQUIRED)
  target_link_libraries(Hub75Simulator PRIVATE rgb_matrix Threads::Threads rt m)
endif()

if(MSVC)
  target_compile_options(Hub75Simulator PRIVATE /W4 /permissive-)
else()
  target_compile_options(Hub75Simulator PRIVATE -Wall -Wextra -Wpedantic)
endif()

set_target_properties(Hub75Simulator PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
)
