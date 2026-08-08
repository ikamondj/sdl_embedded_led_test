#pragma once

#include <cstddef>
#include <cstdint>

namespace Hardware {

constexpr int MATRIX_WIDTH = 64;
constexpr int MATRIX_HEIGHT = 32;

struct Vec2 {
  float x = 0.0f;
  float y = 0.0f;
};

struct Rgb {
  std::uint8_t r = 0;
  std::uint8_t g = 0;
  std::uint8_t b = 0;
};

enum class FaceButton : std::uint8_t {
  One = 0,
  Two,
  Three,
  Four,
  Five,
  Six,
  Seven
};

// Platform lifetime -----------------------------------------------------------
// SDL supplies input on desktop and Raspberry Pi. The visual output is selected
// at build time: an SDL window on non-Linux hosts and HUB75 GPIO on Linux.
bool initialize();
void shutdown();
void poll();
bool isRunning();
// Call before initialize(). On Linux, true selects fullscreen SDL output;
// false keeps the default HUB75 GPIO output.
void useDesktopVisual(bool enabled);
void useFpsLogging(bool enabled);
void useInputLogging(bool enabled);
void useVsync(bool enabled);
void useWindowedVisual(bool enabled);

// Inputs ----------------------------------------------------------------------
// Joystick indices: 0 = left/first stick, 1 = right/second stick.
// Returned axes are normalized to [-1, +1], with +Y meaning upward.
Vec2 readJoystick(std::size_t index);
bool readFaceButton(FaceButton button);

// Time ------------------------------------------------------------------------
std::uint32_t millis();
void delayMs(std::uint32_t milliseconds);

// HUB75-like framebuffer ------------------------------------------------------
// Drawing modifies a 64x32 logical framebuffer. presentLeds() pushes the whole
// frame to the simulated panel, analogous to finishing a HUB75 frame update.
void clearLeds(Rgb color = {});
void setLed(int x, int y, Rgb color);
void presentLeds();

}  // namespace Hardware
