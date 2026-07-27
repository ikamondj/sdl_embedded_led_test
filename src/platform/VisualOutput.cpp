#include "VisualOutput.h"

namespace VisualSDL {
bool initialize();
void shutdown();
void clear(Hardware::Rgb color);
void setPixel(int x, int y, Hardware::Rgb color);
bool present();
void useFpsLogging(bool enabled);
}  // namespace VisualSDL

#if defined(HAVE_HUB75_OUTPUT)
namespace VisualHub75 {
bool initialize();
void shutdown();
void clear(Hardware::Rgb color);
void setPixel(int x, int y, Hardware::Rgb color);
bool present();
}  // namespace VisualHub75
#endif

namespace VisualOutput {
namespace {

#if defined(HAVE_HUB75_OUTPUT)
bool desktop = false;
#else
bool desktop = true;
#endif

}  // namespace

void useDesktop(bool enabled) {
#if defined(HAVE_HUB75_OUTPUT)
  desktop = enabled;
#else
  (void)enabled;
  desktop = true;
#endif
}

void useFpsLogging(bool enabled) {
  VisualSDL::useFpsLogging(enabled);
}

bool initialize() {
#if defined(HAVE_HUB75_OUTPUT)
  return desktop ? VisualSDL::initialize() : VisualHub75::initialize();
#else
  return VisualSDL::initialize();
#endif
}

void shutdown() {
#if defined(HAVE_HUB75_OUTPUT)
  if (desktop) VisualSDL::shutdown();
  else VisualHub75::shutdown();
#else
  VisualSDL::shutdown();
#endif
}

void clear(Hardware::Rgb color) {
#if defined(HAVE_HUB75_OUTPUT)
  if (desktop) VisualSDL::clear(color);
  else VisualHub75::clear(color);
#else
  VisualSDL::clear(color);
#endif
}

void setPixel(int x, int y, Hardware::Rgb color) {
#if defined(HAVE_HUB75_OUTPUT)
  if (desktop) VisualSDL::setPixel(x, y, color);
  else VisualHub75::setPixel(x, y, color);
#else
  VisualSDL::setPixel(x, y, color);
#endif
}

bool present() {
#if defined(HAVE_HUB75_OUTPUT)
  return desktop ? VisualSDL::present() : VisualHub75::present();
#else
  return VisualSDL::present();
#endif
}

}  // namespace VisualOutput
