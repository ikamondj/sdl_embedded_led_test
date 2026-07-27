#include "VisualOutput.h"

#include <SDL.h>

#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#if defined(__linux__)
#include <glob.h>
#include <unistd.h>
#endif

namespace VisualSDL {
namespace {

constexpr int WINDOW_SCALE = 16;
SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
SDL_Texture* texture = nullptr;
bool fpsLogging = false;
bool vsyncEnabled = false;
bool windowed = false;
std::uint64_t fpsWindowStart = 0;
std::uint32_t fpsFrameCount = 0;
std::array<std::uint32_t,
           Hardware::MATRIX_WIDTH * Hardware::MATRIX_HEIGHT> framebuffer{};

std::uint32_t packArgb(Hardware::Rgb color) {
  return 0xFF000000u |
         (static_cast<std::uint32_t>(color.r) << 16u) |
         (static_cast<std::uint32_t>(color.g) << 8u) |
         static_cast<std::uint32_t>(color.b);
}

#if defined(__linux__)
bool environmentVariableMissing(const char* name) {
  const char* value = std::getenv(name);
  return value == nullptr || *value == '\0';
}

void setIfFileExists(const char* name, const std::filesystem::path& path) {
  if (environmentVariableMissing(name) && std::filesystem::exists(path)) {
    setenv(name, path.string().c_str(), 0);
  }
}

void discoverDesktopSession() {
  const std::string runtimeDirectory =
      "/run/user/" + std::to_string(static_cast<unsigned long>(getuid()));

  if (environmentVariableMissing("XDG_RUNTIME_DIR") &&
      std::filesystem::is_directory(runtimeDirectory)) {
    setenv("XDG_RUNTIME_DIR", runtimeDirectory.c_str(), 0);
  }

  // Prefer the native Wayland session when its socket is available.
  if (environmentVariableMissing("WAYLAND_DISPLAY") &&
      std::filesystem::exists(
          std::filesystem::path(runtimeDirectory) / "wayland-0")) {
    setenv("WAYLAND_DISPLAY", "wayland-0", 0);
  }

  // SSH sessions generally omit DISPLAY even while the local X/Xwayland
  // desktop continues to run on :0.
  if (environmentVariableMissing("DISPLAY")) {
    for (int display = 0; display < 10; ++display) {
      const std::string socket =
          "/tmp/.X11-unix/X" + std::to_string(display);
      if (std::filesystem::exists(socket)) {
        const std::string value = ":" + std::to_string(display);
        setenv("DISPLAY", value.c_str(), 0);
        break;
      }
    }
  }

  if (environmentVariableMissing("XAUTHORITY")) {
    if (const char* home = std::getenv("HOME")) {
      setIfFileExists(
          "XAUTHORITY", std::filesystem::path(home) / ".Xauthority");
    }
    setIfFileExists(
        "XAUTHORITY",
        std::filesystem::path(runtimeDirectory) / "gdm" / "Xauthority");

    // Mutter/Xwayland uses a generated authority filename.
    glob_t matches{};
    const std::string pattern = runtimeDirectory + "/.mutter-Xwaylandauth.*";
    if (environmentVariableMissing("XAUTHORITY") &&
        glob(pattern.c_str(), 0, nullptr, &matches) == 0 &&
        matches.gl_pathc > 0) {
      setenv("XAUTHORITY", matches.gl_pathv[0], 0);
    }
    globfree(&matches);
  }
}
#endif

}  // namespace

bool initialize() {
#if defined(__linux__)
  discoverDesktopSession();
#endif
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL video initialization failed: " << SDL_GetError() << '\n';
#if defined(__linux__)
    std::cerr
        << "DISPLAY=" << (std::getenv("DISPLAY") ? std::getenv("DISPLAY") : "<unset>")
        << ", WAYLAND_DISPLAY="
        << (std::getenv("WAYLAND_DISPLAY")
                ? std::getenv("WAYLAND_DISPLAY")
                : "<unset>")
        << ", XDG_RUNTIME_DIR="
        << (std::getenv("XDG_RUNTIME_DIR")
                ? std::getenv("XDG_RUNTIME_DIR")
                : "<unset>")
        << "\nThe desktop may belong to another user or deny this process "
           "access. Run -d as the logged-in desktop user.\n";
#endif
    return false;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  const int windowWidth =
      windowed ? Hardware::MATRIX_WIDTH
               : Hardware::MATRIX_WIDTH * WINDOW_SCALE;
  const int windowHeight =
      windowed ? Hardware::MATRIX_HEIGHT
               : Hardware::MATRIX_HEIGHT * WINDOW_SCALE;
  Uint32 windowFlags = SDL_WINDOW_SHOWN;
  if (windowed) {
    windowFlags |= SDL_WINDOW_RESIZABLE;
#if defined(__linux__)
  } else {
    windowFlags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
#endif
  }
  window = SDL_CreateWindow(
      "HUB75 64x32 Desktop Simulator", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, windowWidth, windowHeight, windowFlags);
  if (!window) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
    return false;
  }

  const Uint32 rendererFlags =
      SDL_RENDERER_ACCELERATED |
      (vsyncEnabled ? SDL_RENDERER_PRESENTVSYNC : 0);
  renderer = SDL_CreateRenderer(window, -1, rendererFlags);
  if (!renderer) {
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  }

  if (fpsLogging) {
    SDL_RendererInfo rendererInfo{};
    if (SDL_GetRendererInfo(renderer, &rendererInfo) == 0) {
      std::cout << "SDL renderer: "
                << (rendererInfo.name ? rendererInfo.name : "unknown")
                << ", accelerated="
                << ((rendererInfo.flags & SDL_RENDERER_ACCELERATED) ? "yes" : "no")
                << ", vsync="
                << ((rendererInfo.flags & SDL_RENDERER_PRESENTVSYNC) ? "yes" : "no")
                << '\n';
    }
    const int displayIndex = SDL_GetWindowDisplayIndex(window);
    SDL_DisplayMode displayMode{};
    if (displayIndex >= 0 &&
        SDL_GetCurrentDisplayMode(displayIndex, &displayMode) == 0) {
      std::cout << "SDL display mode: " << displayMode.w << 'x'
                << displayMode.h << " @ " << displayMode.refresh_rate
                << " Hz\n";
    }
    fpsWindowStart = SDL_GetPerformanceCounter();
    fpsFrameCount = 0;
  }
  if (!renderer) {
    std::cerr << "SDL_CreateRenderer failed: " << SDL_GetError() << '\n';
    return false;
  }

  SDL_RenderSetLogicalSize(
      renderer, Hardware::MATRIX_WIDTH, Hardware::MATRIX_HEIGHT);
  SDL_RenderSetIntegerScale(renderer, SDL_TRUE);
  texture = SDL_CreateTexture(
      renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      Hardware::MATRIX_WIDTH, Hardware::MATRIX_HEIGHT);
  if (!texture) {
    std::cerr << "SDL_CreateTexture failed: " << SDL_GetError() << '\n';
    return false;
  }
  framebuffer.fill(packArgb({}));
  return true;
}

void shutdown() {
  if (texture) SDL_DestroyTexture(texture);
  if (renderer) SDL_DestroyRenderer(renderer);
  if (window) SDL_DestroyWindow(window);
  texture = nullptr;
  renderer = nullptr;
  window = nullptr;
  SDL_QuitSubSystem(SDL_INIT_VIDEO);
}

void clear(Hardware::Rgb color) {
  framebuffer.fill(packArgb(color));
}

void setPixel(int x, int y, Hardware::Rgb color) {
  if (x >= 0 && x < Hardware::MATRIX_WIDTH &&
      y >= 0 && y < Hardware::MATRIX_HEIGHT) {
    framebuffer[static_cast<std::size_t>(
        y * Hardware::MATRIX_WIDTH + x)] = packArgb(color);
  }
}

bool present() {
  if (!renderer || !texture) return false;
  if (SDL_UpdateTexture(texture, nullptr, framebuffer.data(),
                        Hardware::MATRIX_WIDTH *
                            static_cast<int>(sizeof(std::uint32_t))) != 0) {
    std::cerr << "SDL_UpdateTexture failed: " << SDL_GetError() << '\n';
    return false;
  }
  SDL_RenderClear(renderer);
  SDL_RenderCopy(renderer, texture, nullptr, nullptr);
  SDL_RenderPresent(renderer);

  if (fpsLogging) {
    ++fpsFrameCount;
    const std::uint64_t now = SDL_GetPerformanceCounter();
    const std::uint64_t frequency = SDL_GetPerformanceFrequency();
    const double elapsed =
        static_cast<double>(now - fpsWindowStart) /
        static_cast<double>(frequency);
    if (elapsed >= 1.0) {
      std::cout << "SDL FPS: "
                << static_cast<double>(fpsFrameCount) / elapsed << '\n';
      fpsWindowStart = now;
      fpsFrameCount = 0;
    }
  }
  return true;
}

void useFpsLogging(bool enabled) {
  fpsLogging = enabled;
}

void useVsync(bool enabled) {
  vsyncEnabled = enabled;
}

void useWindowed(bool enabled) {
  windowed = enabled;
}

}  // namespace VisualSDL
