#include "Hardware.h"

#include <iostream>

void setup();
void loop();

int main() {
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
