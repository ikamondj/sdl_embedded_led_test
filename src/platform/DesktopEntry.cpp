#include "Hardware.h"
#include "RasterRenderer.h"

#include <iostream>
#include <string>

void setup();
void loop();

int main(int argc, char** argv) {
  bool desktopVisual = false;
  bool threadedRenderer = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "-d") {
      desktopVisual = true;
    } else if (argument == "-t") {
      threadedRenderer = true;
    } else {
      std::cerr << "Usage: " << argv[0] << " [-d] [-t]\n"
                << "  -d  Use fullscreen SDL visual output.\n"
                << "  -t  Use multithreaded CPU rasterization.\n";
      return 2;
    }
  }

  Hardware::useDesktopVisual(desktopVisual);
  setThreadedRendering(threadedRenderer);
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
