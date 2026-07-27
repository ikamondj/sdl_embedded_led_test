#include "Hardware.h"
#include "RasterRenderer.h"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <string>
#include <thread>

void setup();
void loop();

int main(int argc, char** argv) {
  bool desktopVisual = false;
  bool threadedRenderer = false;
  bool precomputeMask = false;
  bool fpsLogging = false;
  bool vsync = false;
  bool windowedVisual = false;
  double frameRateLimit = 0.0;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "-d") {
      desktopVisual = true;
    } else if (argument == "-t") {
      threadedRenderer = true;
    } else if (argument == "-p") {
      precomputeMask = true;
    } else if (argument == "-f") {
      fpsLogging = true;
    } else if (argument == "-v") {
      vsync = true;
    } else if (argument == "-s") {
      desktopVisual = true;
      windowedVisual = true;
    } else if (argument == "-fps") {
      if (index + 1 >= argc) {
        std::cerr << "-fps requires a positive number.\n";
        return 2;
      }
      char* end = nullptr;
      frameRateLimit = std::strtod(argv[++index], &end);
      if (end == argv[index] || *end != '\0' ||
          !std::isfinite(frameRateLimit) || frameRateLimit <= 0.0) {
        std::cerr << "Invalid frame-rate limit: " << argv[index] << '\n';
        return 2;
      }
    } else {
      std::cerr << "Usage: " << argv[0]
                << " [-d] [-s] [-v] [-t] [-p] [-f] [-fps number]\n"
                << "  -d  Use fullscreen SDL visual output.\n"
                << "  -s  Use a resizable 64x32 SDL window.\n"
                << "  -v  Enable SDL presentation VSync.\n"
                << "  -t  Use the persistent four-core raster pool.\n"
                << "  -p  Rebuild the offline pixel visibility mask and exit.\n"
                << "  -f  Report average SDL presentation FPS once per second.\n"
                << "  -fps N  Limit rendering to N frames per second.\n";
      return 2;
    }
  }

  const std::filesystem::path executable =
      std::filesystem::absolute(argv[0]);
  const std::string maskPath =
      (executable.parent_path() / "raster-mask.bin").string();

  if (precomputeMask) {
    setThreadedRendering(true);
    return generateRasterMask(maskPath) ? 0 : 1;
  }

  Hardware::useDesktopVisual(desktopVisual);
  Hardware::useFpsLogging(fpsLogging);
  Hardware::useVsync(vsync);
  Hardware::useWindowedVisual(windowedVisual);
  setThreadedRendering(threadedRenderer);
  if (loadRasterMask(maskPath)) {
    std::cout << "Using raster visibility mask: " << maskPath << '\n';
  }
  setup();

  if (!Hardware::isRunning()) {
    std::cerr << "Failed to initialize the hardware interface.\n";
    Hardware::shutdown();
    return 1;
  }

  if (frameRateLimit > 0.0) {
    std::cout << "Frame-rate limit: " << frameRateLimit << " FPS\n";
  }

  using FrameClock = std::chrono::steady_clock;
  const std::chrono::duration<double> targetFrameDuration =
      frameRateLimit > 0.0
          ? std::chrono::duration<double>(1.0 / frameRateLimit)
          : std::chrono::duration<double>::zero();

  while (Hardware::isRunning()) {
    const FrameClock::time_point frameStart = FrameClock::now();
    loop();
    if (frameRateLimit > 0.0 && Hardware::isRunning()) {
      std::this_thread::sleep_until(frameStart + targetFrameDuration);
    }
  }

  Hardware::shutdown();
  return 0;
}
