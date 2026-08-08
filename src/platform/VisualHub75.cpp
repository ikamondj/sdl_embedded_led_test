#include "VisualOutput.h"

#include <led-matrix.h>

#include <iostream>

namespace VisualHub75 {
namespace {

rgb_matrix::RGBMatrix* matrix = nullptr;
rgb_matrix::FrameCanvas* canvas = nullptr;

}  // namespace

bool initialize() {
  rgb_matrix::RGBMatrix::Options options;
  options.rows = Hardware::MATRIX_HEIGHT;
  options.cols = Hardware::MATRIX_WIDTH;
  options.chain_length = 1;
  options.parallel = 1;
  options.hardware_mapping = "regular";

  rgb_matrix::RuntimeOptions runtime;
  // Pi 4 generally needs a larger GPIO slowdown than earlier boards.
  runtime.gpio_slowdown = 4;
  // Retain the invoking user's groups so SDL controller hot-plugging through
  // /dev/input keeps working after direct GPIO access is initialized.
  runtime.drop_privileges = 0;

  matrix = rgb_matrix::RGBMatrix::CreateFromOptions(options, runtime);
  if (!matrix) {
    std::cerr
        << "Could not initialize HUB75 GPIO output. Run as root (or grant the "
           "required GPIO privileges) and verify the Adafruit bonnet wiring.\n";
    return false;
  }
  canvas = matrix->CreateFrameCanvas();
  canvas->Clear();
  std::cout << "HUB75 output: 64x32, Adafruit HAT/Bonnet mapping, GPIO slowdown 4.\n";
  return true;
}

void shutdown() {
  if (matrix) matrix->Clear();
  delete matrix;
  matrix = nullptr;
  canvas = nullptr;
}

void clear(Hardware::Rgb color) {
  if (canvas) canvas->Fill(color.r, color.g, color.b);
}

void setPixel(int x, int y, Hardware::Rgb color) {
  if (canvas && x >= 0 && x < Hardware::MATRIX_WIDTH &&
      y >= 0 && y < Hardware::MATRIX_HEIGHT) {
    canvas->SetPixel(x, y, color.r, color.g, color.b);
  }
}

bool present() {
  if (!matrix || !canvas) return false;
  canvas = matrix->SwapOnVSync(canvas);
  return true;
}

}  // namespace VisualHub75
