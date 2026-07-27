#include "Hardware.h"

#include <iostream>
#include <string>

void setup();
void loop();

int main(int argc, char** argv) {
  bool desktopVisual = false;
  for (int index = 1; index < argc; ++index) {
    const std::string argument = argv[index];
    if (argument == "-d") {
      desktopVisual = true;
    } else {
      std::cerr << "Usage: " << argv[0] << " [-d]\n"
                << "  -d  Use fullscreen SDL visual output.\n";
      return 2;
    }
  }

  Hardware::useDesktopVisual(desktopVisual);
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
