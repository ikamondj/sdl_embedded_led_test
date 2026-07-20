cmake_minimum_required(VERSION 3.24)

project(Hub75DesktopSimulator VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

# Build SDL2 statically so the produced executable does not require an SDL2.dll
# beside it. The source archive is downloaded automatically on first configure.
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

add_executable(Hub75Simulator
  src/main.cpp
  src/RasterRenderer.cpp
  src/DeltaTimer.cpp
  src/platform/DesktopEntry.cpp
  src/platform/HardwareSDL.cpp
  src/imagery/SongOfTheNight.cpp
)

target_include_directories(Hub75Simulator PRIVATE include)

target_compile_definitions(Hub75Simulator PRIVATE
  $<$<PLATFORM_ID:Windows>:NOMINMAX;WIN32_LEAN_AND_MEAN>
)

if(TARGET SDL2::SDL2-static)
  target_link_libraries(Hub75Simulator PRIVATE SDL2::SDL2-static)
elseif(TARGET SDL2-static)
  target_link_libraries(Hub75Simulator PRIVATE SDL2-static)
else()
  message(FATAL_ERROR "The fetched SDL2 project did not expose a static SDL2 target.")
endif()

if(MSVC)
  target_compile_options(Hub75Simulator PRIVATE /W4 /permissive-)
else()
  target_compile_options(Hub75Simulator PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Put the executable in the same location for single- and multi-config generators.
set_target_properties(Hub75Simulator PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_BINARY_DIR}/bin"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_BINARY_DIR}/bin"
)
