#include "VisualOutput.h"

#include <SDL.h>

#include <array>
#include <cstdint>
#include <iostream>

namespace VisualOutput {
namespace {

constexpr int WINDOW_SCALE = 16;
SDL_Window* window = nullptr;
SDL_Renderer* renderer = nullptr;
SDL_Texture* texture = nullptr;
std::array<std::uint32_t,
           Hardware::MATRIX_WIDTH * Hardware::MATRIX_HEIGHT> framebuffer{};

std::uint32_t packArgb(Hardware::Rgb color) {
  return 0xFF000000u |
         (static_cast<std::uint32_t>(color.r) << 16u) |
         (static_cast<std::uint32_t>(color.g) << 8u) |
         static_cast<std::uint32_t>(color.b);
}

}  // namespace

bool initialize() {
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    std::cerr << "SDL video initialization failed: " << SDL_GetError() << '\n';
    return false;
  }
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
  window = SDL_CreateWindow(
      "HUB75 64x32 Desktop Simulator", SDL_WINDOWPOS_CENTERED,
      SDL_WINDOWPOS_CENTERED, Hardware::MATRIX_WIDTH * WINDOW_SCALE,
      Hardware::MATRIX_HEIGHT * WINDOW_SCALE,
      SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
  if (!window) {
    std::cerr << "SDL_CreateWindow failed: " << SDL_GetError() << '\n';
    return false;
  }

  renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer) {
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
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
  clear({});
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
  return true;
}

}  // namespace VisualOutput
