#pragma once

#include "Hardware.h"

namespace VisualOutput {

bool initialize();
void shutdown();
void clear(Hardware::Rgb color);
void setPixel(int x, int y, Hardware::Rgb color);
bool present();
void useDesktop(bool enabled);

}  // namespace VisualOutput
