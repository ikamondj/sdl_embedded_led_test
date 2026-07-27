#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "Hardware.h"
#include "VisualOutput.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

namespace Hardware {
namespace {

constexpr float CONTROLLER_DEADZONE = 0.15f;

SDL_GameController* controller = nullptr;
SDL_Joystick* rawJoystick = nullptr;
SDL_JoystickID activeJoystickInstanceId = -1;

bool running = false;

float clampAxis(float value) {
  return std::clamp(value, -1.0f, 1.0f);
}

Vec2 applyRadialDeadzone(Vec2 value) {
  const float magnitude = std::sqrt(value.x * value.x + value.y * value.y);
  if (magnitude <= CONTROLLER_DEADZONE) {
    return {};
  }

  const float safeMagnitude = std::max(magnitude, 0.00001f);
  const float scaledMagnitude = std::clamp(
      (magnitude - CONTROLLER_DEADZONE) / (1.0f - CONTROLLER_DEADZONE),
      0.0f,
      1.0f);

  return {
      value.x / safeMagnitude * scaledMagnitude,
      value.y / safeMagnitude * scaledMagnitude,
  };
}

float normalizeControllerAxis(Sint16 value) {
  if (value >= 0) {
    return static_cast<float>(value) / 32767.0f;
  }
  return static_cast<float>(value) / 32768.0f;
}

void closeActiveJoystick() {
  if (controller != nullptr) {
    SDL_GameControllerClose(controller);
    controller = nullptr;
  }

  if (rawJoystick != nullptr) {
    SDL_JoystickClose(rawJoystick);
    rawJoystick = nullptr;
  }

  activeJoystickInstanceId = -1;
}

bool openInputDeviceAtIndex(int deviceIndex) {
  if (controller != nullptr || rawJoystick != nullptr) {
    return false;
  }

  if (SDL_IsGameController(deviceIndex)) {
    controller = SDL_GameControllerOpen(deviceIndex);
    if (controller == nullptr) {
      std::cerr << "Could not open mapped game controller: " << SDL_GetError() << '\n';
      return false;
    }

    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
    activeJoystickInstanceId = SDL_JoystickInstanceID(joystick);

    const char* name = SDL_GameControllerName(controller);
    std::cout << "Mapped gamepad connected: "
              << (name != nullptr ? name : "Unknown controller") << '\n';
    return true;
  }

  rawJoystick = SDL_JoystickOpen(deviceIndex);
  if (rawJoystick == nullptr) {
    std::cerr << "Could not open raw joystick: " << SDL_GetError() << '\n';
    return false;
  }

  activeJoystickInstanceId = SDL_JoystickInstanceID(rawJoystick);
  const char* name = SDL_JoystickName(rawJoystick);
  std::cout << "Unmapped joystick connected using raw fallback: "
            << (name != nullptr ? name : "Unknown joystick") << '\n'
            << "  Raw mapping: axes 0/1 and 2/3; buttons 0/1/2/3.\n";
  return true;
}

void openFirstAvailableInputDevice() {
  if (controller != nullptr || rawJoystick != nullptr) {
    return;
  }

  const int joystickCount = SDL_NumJoysticks();
  for (int index = 0; index < joystickCount; ++index) {
    if (openInputDeviceAtIndex(index)) {
      return;
    }
  }

  std::cout << "No gamepad detected. Keyboard controls remain available.\n";
}

Vec2 readGamepadStick(std::size_t index) {
  Vec2 value{};

  if (controller != nullptr) {
    const SDL_GameControllerAxis axisX = index == 0
        ? SDL_CONTROLLER_AXIS_LEFTX
        : SDL_CONTROLLER_AXIS_RIGHTX;
    const SDL_GameControllerAxis axisY = index == 0
        ? SDL_CONTROLLER_AXIS_LEFTY
        : SDL_CONTROLLER_AXIS_RIGHTY;

    value = {
        normalizeControllerAxis(SDL_GameControllerGetAxis(controller, axisX)),
        -normalizeControllerAxis(SDL_GameControllerGetAxis(controller, axisY)),
    };
  } else if (rawJoystick != nullptr) {
    const int axisX = index == 0 ? 0 : 2;
    const int axisY = index == 0 ? 1 : 3;

    if (SDL_JoystickNumAxes(rawJoystick) <= axisY) {
      return {};
    }

    value = {
        normalizeControllerAxis(SDL_JoystickGetAxis(rawJoystick, axisX)),
        -normalizeControllerAxis(SDL_JoystickGetAxis(rawJoystick, axisY)),
    };
  } else {
    return {};
  }

  return applyRadialDeadzone(value);
}

Vec2 readKeyboardStick(std::size_t index) {
  const Uint8* keys = SDL_GetKeyboardState(nullptr);
  if (keys == nullptr) {
    return {};
  }

  Vec2 value{};

  if (index == 0) {
    value.x = static_cast<float>(keys[SDL_SCANCODE_D])
            - static_cast<float>(keys[SDL_SCANCODE_A]);
    value.y = static_cast<float>(keys[SDL_SCANCODE_W])
            - static_cast<float>(keys[SDL_SCANCODE_S]);
  } else {
    value.x = static_cast<float>(keys[SDL_SCANCODE_RIGHT])
            - static_cast<float>(keys[SDL_SCANCODE_LEFT]);
    value.y = static_cast<float>(keys[SDL_SCANCODE_UP])
            - static_cast<float>(keys[SDL_SCANCODE_DOWN]);
  }

  // Normalize keyboard diagonals so they have the same maximum magnitude as a
  // physical joystick at full deflection.
  const float magnitude = std::sqrt(value.x * value.x + value.y * value.y);
  if (magnitude > 1.0f) {
    value.x /= magnitude;
    value.y /= magnitude;
  }

  return value;
}

SDL_GameControllerButton controllerButtonFor(FaceButton button) {
  // SDL's standard names map naturally to common controllers:
  // One=A/Cross, Two=B/Circle, Three=X/Square, Four=Y/Triangle.
  switch (button) {
    case FaceButton::One: return SDL_CONTROLLER_BUTTON_A;
    case FaceButton::Two: return SDL_CONTROLLER_BUTTON_B;
    case FaceButton::Three: return SDL_CONTROLLER_BUTTON_X;
    case FaceButton::Four: return SDL_CONTROLLER_BUTTON_Y;
  }
  return SDL_CONTROLLER_BUTTON_INVALID;
}

SDL_Scancode keyboardButtonFor(FaceButton button) {
  switch (button) {
    case FaceButton::One: return SDL_SCANCODE_1;
    case FaceButton::Two: return SDL_SCANCODE_2;
    case FaceButton::Three: return SDL_SCANCODE_3;
    case FaceButton::Four: return SDL_SCANCODE_4;
  }
  return SDL_SCANCODE_UNKNOWN;
}

}  // namespace

bool initialize() {
  SDL_SetMainReady();

  if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_JOYSTICK |
               SDL_INIT_GAMECONTROLLER) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return false;
  }

  if (!VisualOutput::initialize()) {
    shutdown();
    return false;
  }

  clearLeds();
  openFirstAvailableInputDevice();
  running = true;

  std::cout
      << "Controls:\n"
      << "  Joystick 1: left gamepad stick or WASD\n"
      << "  Joystick 2: right gamepad stick or arrow keys\n"
      << "  Face buttons: gamepad A/B/X/Y or number keys 1/2/3/4\n"
      << "  Escape or close window: quit\n";

  return true;
}

void shutdown() {
  running = false;
  closeActiveJoystick();
  VisualOutput::shutdown();
  SDL_Quit();
}

void poll() {
  SDL_Event event;
  while (SDL_PollEvent(&event) != 0) {
    switch (event.type) {
      case SDL_QUIT:
        running = false;
        break;

      case SDL_KEYDOWN:
        if (event.key.keysym.scancode == SDL_SCANCODE_ESCAPE) {
          running = false;
        }
        break;

      case SDL_JOYDEVICEADDED:
        if (controller == nullptr && rawJoystick == nullptr) {
          openInputDeviceAtIndex(event.jdevice.which);
        }
        break;

      case SDL_JOYDEVICEREMOVED:
        if (event.jdevice.which == activeJoystickInstanceId) {
          std::cout << "Gamepad disconnected.\n";
          closeActiveJoystick();
          openFirstAvailableInputDevice();
        }
        break;

      default:
        break;
    }
  }
}

bool isRunning() {
  return running;
}

void useDesktopVisual(bool enabled) {
  VisualOutput::useDesktop(enabled);
}

void useFpsLogging(bool enabled) {
  VisualOutput::useFpsLogging(enabled);
}

void useVsync(bool enabled) {
  VisualOutput::useVsync(enabled);
}

void useWindowedVisual(bool enabled) {
  VisualOutput::useWindowed(enabled);
}

Vec2 readJoystick(std::size_t index) {
  if (index > 1) {
    return {};
  }

  const Vec2 gamepad = readGamepadStick(index);
  const Vec2 keyboard = readKeyboardStick(index);

  return {
      clampAxis(gamepad.x + keyboard.x),
      clampAxis(gamepad.y + keyboard.y),
  };
}

bool readFaceButton(FaceButton button) {
  const Uint8* keys = SDL_GetKeyboardState(nullptr);
  const SDL_Scancode key = keyboardButtonFor(button);
  const bool keyboardPressed =
      keys != nullptr && key != SDL_SCANCODE_UNKNOWN && keys[key] != 0;

  bool gamepadPressed = false;

  if (controller != nullptr) {
    const SDL_GameControllerButton gamepadButton = controllerButtonFor(button);
    gamepadPressed =
        gamepadButton != SDL_CONTROLLER_BUTTON_INVALID
        && SDL_GameControllerGetButton(controller, gamepadButton) != 0;
  } else if (rawJoystick != nullptr) {
    const int rawButtonIndex = static_cast<int>(button);
    gamepadPressed =
        rawButtonIndex < SDL_JoystickNumButtons(rawJoystick)
        && SDL_JoystickGetButton(rawJoystick, rawButtonIndex) != 0;
  }

  return keyboardPressed || gamepadPressed;
}

std::uint32_t millis() {
  return SDL_GetTicks();
}

void delayMs(std::uint32_t milliseconds) {
  SDL_Delay(milliseconds);
}

void clearLeds(Rgb color) {
  VisualOutput::clear(color);
}

void setLed(int x, int y, Rgb color) {
  VisualOutput::setPixel(x, y, color);
}

void presentLeds() {
  if (!VisualOutput::present()) {
    running = false;
  }
}

}  // namespace Hardware
