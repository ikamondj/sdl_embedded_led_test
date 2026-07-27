#include "Hardware.h"
#include "RasterRenderer.h"

#include <iostream>
#include <filesystem>
#include <string>

void setup();
void loop();

int main(int argc, char** argv) {
  bool desktopVisual = false;
  bool threadedRenderer = false;
  bool precomputeMask = false;
  bool fpsLogging = false;
  bool vsync = false;
  bool windowedVisual = false;
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
    } else {
      std::cerr << "Usage: " << argv[0]
                << " [-d] [-s] [-v] [-t] [-p] [-f]\n"
                << "  -d  Use fullscreen SDL visual output.\n"
                << "  -s  Use a resizable 64x32 SDL window.\n"
                << "  -v  Enable SDL presentation VSync.\n"
                << "  -t  Use the persistent four-core raster pool.\n"
                << "  -p  Rebuild the offline pixel visibility mask and exit.\n"
                << "  -f  Report average SDL presentation FPS once per second.\n";
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

  while (Hardware::isRunning()) {
    loop();
  }

  Hardware::shutdown();
  return 0;
}
