#pragma once

#include "Hardware.h"

namespace VisualOutput {

bool initialize();
void shutdown();
void clear(Hardware::Rgb color);
void setPixel(int x, int y, Hardware::Rgb color);
bool present();
void useDesktop(bool enabled);
void useFpsLogging(bool enabled);
void useVsync(bool enabled);
void useWindowed(bool enabled);

}  // namespace VisualOutput
