#define SDL_MAIN_HANDLED
#include <SDL.h>

#include "Hardware.h"
#include "VisualOutput.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <iostream>

namespace Hardware {
namespace {

constexpr float CONTROLLER_DEADZONE = 0.15f;

SDL_GameController* controller = nullptr;
SDL_Joystick* rawJoystick = nullptr;
SDL_JoystickID activeJoystickInstanceId = -1;

bool running = false;
bool inputLoggingEnabled = false;
bool joystickLoggingEnabled = false;
bool activeUsesAdaptiveLinuxProfile = false;
std::uint16_t brightnessScale = 65535;

Rgb applyBrightness(Rgb color) {
  const auto scaleChannel = [](std::uint8_t channel) {
    return static_cast<std::uint8_t>(
        (static_cast<std::uint32_t>(channel) * brightnessScale + 32767u)
        / 65535u);
  };
  return {
      scaleChannel(color.r),
      scaleChannel(color.g),
      scaleChannel(color.b),
  };
}

bool deviceMatchesAdaptiveLinuxProfile(int deviceIndex) {
#if defined(__linux__)
  char guid[33]{};
  SDL_JoystickGetGUIDString(
      SDL_JoystickGetDeviceGUID(deviceIndex), guid, sizeof(guid));
  if (std::strcmp(guid, "030000005e0400001a0b000000010000") != 0) {
    return false;
  }

  SDL_Joystick* joystick = SDL_JoystickOpen(deviceIndex);
  if (joystick == nullptr) {
    return false;
  }
  const bool matches =
      SDL_JoystickGetVendor(joystick) == 1118
      && SDL_JoystickGetProduct(joystick) == 2842
      && SDL_JoystickNumAxes(joystick) == 6
      && SDL_JoystickNumButtons(joystick) == 11
      && SDL_JoystickNumHats(joystick) == 1;
  SDL_JoystickClose(joystick);
  return matches;
#else
  static_cast<void>(deviceIndex);
  return false;
#endif
}

void logJoystickIdentity(SDL_Joystick* joystick) {
  if (joystick == nullptr
      || (!inputLoggingEnabled && !joystickLoggingEnabled)) {
    return;
  }

  char guid[33]{};
  SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), guid, sizeof(guid));
  std::cout << "Input device identity:\n"
            << "  name: "
            << (SDL_JoystickName(joystick) != nullptr
                    ? SDL_JoystickName(joystick)
                    : "Unknown joystick") << '\n'
            << "  GUID: " << guid << '\n'
            << "  vendor/product/version: "
            << SDL_JoystickGetVendor(joystick) << '/'
            << SDL_JoystickGetProduct(joystick) << '/'
            << SDL_JoystickGetProductVersion(joystick) << '\n'
            << "  axes/buttons/hats: "
            << SDL_JoystickNumAxes(joystick) << '/'
            << SDL_JoystickNumButtons(joystick) << '/'
            << SDL_JoystickNumHats(joystick) << '\n';
}

void logAllInputDevices() {
  if (!inputLoggingEnabled && !joystickLoggingEnabled) {
    return;
  }

  SDL_version linkedVersion{};
  SDL_GetVersion(&linkedVersion);
  std::cout << "SDL input diagnostics:\n"
            << "  compiled SDL: " << SDL_MAJOR_VERSION << '.'
            << SDL_MINOR_VERSION << '.' << SDL_PATCHLEVEL << '\n'
            << "  linked SDL: " << static_cast<int>(linkedVersion.major) << '.'
            << static_cast<int>(linkedVersion.minor) << '.'
            << static_cast<int>(linkedVersion.patch) << '\n';

  const int joystickCount = SDL_NumJoysticks();
  std::cout << "  detected joystick interfaces: " << joystickCount << '\n';

  for (int index = 0; index < joystickCount; ++index) {
    char guid[33]{};
    SDL_JoystickGetGUIDString(
        SDL_JoystickGetDeviceGUID(index), guid, sizeof(guid));
    const char* deviceName = SDL_JoystickNameForIndex(index);
    std::cout << "Joystick interface " << index << ":\n"
              << "  name: "
              << (deviceName != nullptr ? deviceName : "Unknown joystick") << '\n'
              << "  GUID: " << guid << '\n'
              << "  mapped game controller: "
              << (SDL_IsGameController(index) ? "yes" : "no") << '\n';

#if SDL_VERSION_ATLEAST(2, 24, 0)
    const char* devicePath = SDL_JoystickPathForIndex(index);
    std::cout << "  path: "
              << (devicePath != nullptr ? devicePath : "unavailable") << '\n';
#endif

    SDL_Joystick* joystick = SDL_JoystickOpen(index);
    if (joystick == nullptr) {
      std::cout << "  open failed: " << SDL_GetError() << '\n';
      continue;
    }

    std::cout << "  instance ID: " << SDL_JoystickInstanceID(joystick) << '\n'
              << "  vendor/product/version: "
              << SDL_JoystickGetVendor(joystick) << '/'
              << SDL_JoystickGetProduct(joystick) << '/'
              << SDL_JoystickGetProductVersion(joystick) << '\n'
              << "  axes/buttons/hats/trackballs: "
              << SDL_JoystickNumAxes(joystick) << '/'
              << SDL_JoystickNumButtons(joystick) << '/'
              << SDL_JoystickNumHats(joystick) << '/'
              << SDL_JoystickNumBalls(joystick) << '\n';

#if SDL_VERSION_ATLEAST(2, 0, 14)
    const char* serial = SDL_JoystickGetSerial(joystick);
    std::cout << "  serial: "
              << (serial != nullptr && serial[0] != '\0'
                      ? serial
                      : "unavailable") << '\n';
#endif

#if SDL_VERSION_ATLEAST(2, 0, 9)
    char* mapping = SDL_GameControllerMappingForDeviceIndex(index);
    std::cout << "  SDL mapping: "
              << (mapping != nullptr ? mapping : "unavailable") << '\n';
    SDL_free(mapping);
#endif

    SDL_JoystickClose(joystick);
  }
}

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
  activeUsesAdaptiveLinuxProfile = false;
}

bool openInputDeviceAtIndex(int deviceIndex) {
  if (controller != nullptr || rawJoystick != nullptr) {
    return false;
  }

  const bool usesAdaptiveLinuxProfile =
      deviceMatchesAdaptiveLinuxProfile(deviceIndex);

  if (SDL_IsGameController(deviceIndex)) {
    controller = SDL_GameControllerOpen(deviceIndex);
    if (controller == nullptr) {
      std::cerr << "Could not open mapped game controller: " << SDL_GetError() << '\n';
      return false;
    }

    SDL_Joystick* joystick = SDL_GameControllerGetJoystick(controller);
    activeJoystickInstanceId = SDL_JoystickInstanceID(joystick);
    activeUsesAdaptiveLinuxProfile = usesAdaptiveLinuxProfile;

    const char* name = SDL_GameControllerName(controller);
    std::cout << "Mapped gamepad connected: "
              << (name != nullptr ? name : "Unknown controller") << '\n';
    if (activeUsesAdaptiveLinuxProfile) {
      std::cout << "  Using stable Linux Adaptive Joystick profile.\n";
    }
    logJoystickIdentity(joystick);
    return true;
  }

  rawJoystick = SDL_JoystickOpen(deviceIndex);
  if (rawJoystick == nullptr) {
    std::cerr << "Could not open raw joystick: " << SDL_GetError() << '\n';
    return false;
  }

  activeJoystickInstanceId = SDL_JoystickInstanceID(rawJoystick);
  activeUsesAdaptiveLinuxProfile = usesAdaptiveLinuxProfile;
  const char* name = SDL_JoystickName(rawJoystick);
  std::cout << "Unmapped joystick connected using raw fallback: "
            << (name != nullptr ? name : "Unknown joystick") << '\n';
  if (activeUsesAdaptiveLinuxProfile) {
    std::cout << "  Using stable Linux Adaptive Joystick profile.\n";
  }
#if defined(__linux__)
  std::cout << "  Raw mapping: axes 0/1 for the mouth; axis 2=X6.\n";
#else
  std::cout << "  Raw mapping: axes 0/1 for the mouth; axis 4=X6.\n";
#endif
  logJoystickIdentity(rawJoystick);
  return true;
}

void openFirstAvailableInputDevice() {
  if (controller != nullptr || rawJoystick != nullptr) {
    return;
  }

  const int joystickCount = SDL_NumJoysticks();
#if defined(__linux__)
  for (int index = 0; index < joystickCount; ++index) {
    if (deviceMatchesAdaptiveLinuxProfile(index)
        && openInputDeviceAtIndex(index)) {
      return;
    }
  }
#endif
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
    case FaceButton::Five: return SDL_CONTROLLER_BUTTON_RIGHTSHOULDER;
    case FaceButton::Six: return SDL_CONTROLLER_BUTTON_INVALID;
    case FaceButton::Seven: return SDL_CONTROLLER_BUTTON_RIGHTSTICK;
  }
  return SDL_CONTROLLER_BUTTON_INVALID;
}

SDL_Scancode keyboardButtonFor(FaceButton button) {
  switch (button) {
    case FaceButton::One: return SDL_SCANCODE_1;
    case FaceButton::Two: return SDL_SCANCODE_2;
    case FaceButton::Three: return SDL_SCANCODE_3;
    case FaceButton::Four: return SDL_SCANCODE_4;
    case FaceButton::Five: return SDL_SCANCODE_5;
    case FaceButton::Six: return SDL_SCANCODE_6;
    case FaceButton::Seven: return SDL_SCANCODE_7;
  }
  return SDL_SCANCODE_UNKNOWN;
}

int rawButtonIndexFor(FaceButton button) {
#if defined(__linux__)
  if (activeUsesAdaptiveLinuxProfile) {
    switch (button) {
      case FaceButton::One: return 0;    // X3
      case FaceButton::Two: return 1;    // X4
      case FaceButton::Three: return 2;  // X1
      case FaceButton::Four: return 3;   // X2
      case FaceButton::Five: return 4;   // X5
      case FaceButton::Six: return -1;   // X6 is axis 2
      case FaceButton::Seven: return 9;  // Joystick click
    }
  }
  if (button == FaceButton::Six) {
    return -1;
  }
  return button == FaceButton::Seven
      ? 8
      : static_cast<int>(button);
#else
  if (button == FaceButton::Six) {
    return -1;
  }
  return button == FaceButton::Seven
      ? 8
      : static_cast<int>(button);
#endif
}

int rawAxisIndexFor(FaceButton button) {
  if (button != FaceButton::Six) {
    return -1;
  }
#if defined(__linux__)
  return activeUsesAdaptiveLinuxProfile ? 2 : 4;
#else
  return 4;
#endif
}

}  // namespace

bool initialize() {
  SDL_SetMainReady();

  if (SDL_Init(SDL_INIT_EVENTS | SDL_INIT_TIMER | SDL_INIT_JOYSTICK |
               SDL_INIT_GAMECONTROLLER) != 0) {
    std::cerr << "SDL_Init failed: " << SDL_GetError() << '\n';
    return false;
  }

  logAllInputDevices();

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
      << "  Buttons X1-X5: raw controller buttons 1-5 or keys 1-5\n"
      << "  Button X6: raw controller axis 4 or key 6\n"
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
        if (deviceMatchesAdaptiveLinuxProfile(event.jdevice.which)
            && !activeUsesAdaptiveLinuxProfile) {
          if (controller != nullptr || rawJoystick != nullptr) {
            std::cout << "Preferred Adaptive Joystick connected; switching input device.\n";
            closeActiveJoystick();
          }
          openInputDeviceAtIndex(event.jdevice.which);
        } else if (controller == nullptr && rawJoystick == nullptr) {
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

      case SDL_JOYBUTTONDOWN:
      case SDL_JOYBUTTONUP:
        if (inputLoggingEnabled
            && event.jbutton.which == activeJoystickInstanceId) {
          std::cout << "Raw button "
                    << static_cast<unsigned int>(event.jbutton.button)
                    << (event.type == SDL_JOYBUTTONDOWN
                            ? ": pressed\n"
                            : ": released\n");
        }
        break;

      case SDL_JOYAXISMOTION:
        if (event.jaxis.which == activeJoystickInstanceId
            && ((event.jaxis.axis < 4 && joystickLoggingEnabled)
                || (event.jaxis.axis >= 4 && inputLoggingEnabled))) {
          std::cout << "Raw axis "
                    << static_cast<unsigned int>(event.jaxis.axis)
                    << " changed: " << event.jaxis.value
                    << " (normalized "
                    << normalizeControllerAxis(event.jaxis.value) << ")\n";
        }
        break;

      case SDL_JOYHATMOTION:
        if (inputLoggingEnabled
            && event.jhat.which == activeJoystickInstanceId) {
          std::cout << "Raw hat "
                    << static_cast<unsigned int>(event.jhat.hat)
                    << " changed: "
                    << static_cast<unsigned int>(event.jhat.value) << '\n';
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

void useInputLogging(bool enabled) {
  inputLoggingEnabled = enabled;
}

void useJoystickLogging(bool enabled) {
  joystickLoggingEnabled = enabled;
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

  SDL_Joystick* joystick = controller != nullptr
      ? SDL_GameControllerGetJoystick(controller)
      : rawJoystick;

  const int rawAxisIndex = rawAxisIndexFor(button);
  if (rawAxisIndex >= 0) {
    // X6 is a click-like raw axis: positive is pressed and negative is
    // released. Its raw index differs between Linux and Windows.
    gamepadPressed = joystick != nullptr
        && SDL_JoystickNumAxes(joystick) > rawAxisIndex
        && SDL_JoystickGetAxis(joystick, rawAxisIndex) > 0;
  } else if (joystick != nullptr) {
    const int rawButtonIndex = rawButtonIndexFor(button);
    if (rawButtonIndex >= 0
        && rawButtonIndex < SDL_JoystickNumButtons(joystick)) {
      gamepadPressed = SDL_JoystickGetButton(joystick, rawButtonIndex) != 0;
    } else if (controller != nullptr) {
      const SDL_GameControllerButton gamepadButton = controllerButtonFor(button);
      gamepadPressed =
          gamepadButton != SDL_CONTROLLER_BUTTON_INVALID
          && SDL_GameControllerGetButton(controller, gamepadButton) != 0;
    }
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
  VisualOutput::clear(applyBrightness(color));
}

void setLed(int x, int y, Rgb color) {
  VisualOutput::setPixel(x, y, applyBrightness(color));
}

void setBrightness(float scale) {
  const float clamped = std::clamp(scale, 0.1f, 1.0f);
  brightnessScale = static_cast<std::uint16_t>(
      std::lround(clamped * 65535.0f));
}

void presentLeds() {
  if (!VisualOutput::present()) {
    running = false;
  }
}

}  // namespace Hardware
