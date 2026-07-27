#include "Hardware.h"
#include "RasterRenderer.h"
#include "DeltaTimer.h"

// This file intentionally looks like microcontroller firmware. The host
// executable entry point is kept separately in src/platform/DesktopEntry.cpp.

void setup() {
  if (!Hardware::initialize()) {
    return;
  }

  Hardware::clearLeds({0, 0, 0});
  Hardware::presentLeds();
}

JoystickState joyConvert(const Hardware::Vec2& vec) {
  JoystickState state;
  state.x = vec.x;
  state.y = vec.y;
  return state;
}

Hardware::Vec2 hardConvert(const JoystickState& state) {
  Hardware::Vec2 vec;
  vec.x = state.x;
  vec.y = state.y;
  return vec;
}

inline float smoothDamp(
    float current,
    float target,
    float& currentVelocity,
    float smoothingTime,
    float deltaSeconds,
    float maxSpeed = std::numeric_limits<float>::infinity())
{
    smoothingTime = std::max(0.0001f, smoothingTime);
    deltaSeconds = std::max(0.0f, deltaSeconds);

    const float omega = 2.0f / smoothingTime;
    const float x = omega * deltaSeconds;

    const float decay =
        1.0f /
        (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

    float displacement = current - target;
    const float originalTarget = target;

    if (std::isfinite(maxSpeed)) {
        const float maxDisplacement = maxSpeed * smoothingTime;

        displacement = std::clamp(
            displacement,
            -maxDisplacement,
            maxDisplacement);
    }

    target = current - displacement;

    const float temporary =
        (currentVelocity + omega * displacement) *
        deltaSeconds;

    currentVelocity =
        (currentVelocity - omega * temporary) *
        decay;

    float result =
        target +
        (displacement + temporary) *
        decay;

    // Prevent the value from crossing past the original target.
    const bool targetWasAhead = originalTarget > current;
    const bool resultPassedTarget = result > originalTarget;

    if (targetWasAhead == resultPassedTarget) {
        result = originalTarget;

        if (deltaSeconds > 0.000001f) {
            currentVelocity =
                (result - originalTarget) / deltaSeconds;
        } else {
            currentVelocity = 0.0f;
        }
    }

    return result;
}

RenderInputs input;
Hardware::DeltaTimer deltaTimer;
Hardware::Vec2 j1Velocity{};
Hardware::Vec2 j2Velocity{};
Hardware::Vec2 j1current{};
Hardware::Vec2 j2current{};
void loop() {
  Hardware::poll();
  if (!Hardware::isRunning()) {
    return;
  }
  const float deltaSeconds = deltaTimer.tick();
  j1current = hardConvert(input.joystick1);
  j2current = hardConvert(input.joystick2);

  auto j1target = Hardware::readJoystick(0);
  auto j2target = Hardware::readJoystick(1);

  j1target.x = roundf(j1target.x * 15.0f) / 15.0f;
  j1target.y = roundf(j1target.y * 15.0f) / 15.0f;

  j1current.x = smoothDamp(j1current.x, j1target.x, j1Velocity.x, 0.018f, deltaSeconds);
  j1current.y = smoothDamp(j1current.y, j1target.y, j1Velocity.y, 0.018f, deltaSeconds);
  j2current.x = smoothDamp(j2current.x, j2target.x, j2Velocity.x, 0.018f, deltaSeconds);
  j2current.y = smoothDamp(j2current.y, j2target.y, j2Velocity.y, 0.018f, deltaSeconds);

  input.joystick1 = joyConvert(j1current);
  input.joystick2 = joyConvert(j2current);
  input.faceButtons[0] = Hardware::readFaceButton(Hardware::FaceButton::One);
  input.faceButtons[1] = Hardware::readFaceButton(Hardware::FaceButton::Two);
  input.faceButtons[2] = Hardware::readFaceButton(Hardware::FaceButton::Three);
  input.faceButtons[3] = Hardware::readFaceButton(Hardware::FaceButton::Four);
  input.timeSeconds = static_cast<float>(Hardware::millis()) * 0.001f;

  renderFrame(input);
  Hardware::presentLeds();
}
