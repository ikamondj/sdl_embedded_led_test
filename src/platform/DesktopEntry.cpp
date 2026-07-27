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
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "-d") {
      desktopVisual = true;
    } else if (argument == "-t") {
      threadedRenderer = true;
    } else if (argument == "-p") {
      precomputeMask = true;
    } else {
      std::cerr << "Usage: " << argv[0] << " [-d] [-t] [-p]\n"
                << "  -d  Use fullscreen SDL visual output.\n"
                << "  -t  Use the persistent four-core raster pool.\n"
                << "  -p  Rebuild the offline pixel visibility mask and exit.\n";
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
