#include "Hardware.h"
#include "RasterRenderer.h"
#include "DeltaTimer.h"
#include "ControlHelpers.h"

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
float x_5_6, x_3_4, x_1_2;
float x_5_6_v, x_3_4_v, x_1_2_v;
float pupstate = 1;


void loop() {
  Hardware::poll();
  if (!Hardware::isRunning()) {
    return;
  }
  const float deltaSeconds = deltaTimer.tick();
  j1current = hardConvert(input.joystick1);
  j2current = hardConvert(input.joystick2);

  auto j1target = Hardware::readJoystick(1);
  auto j2target = Hardware::readJoystick(0);

  j1target.x = roundf(j1target.x * 15.0f) / 15.0f;
  j1target.y = roundf(j1target.y * 15.0f) / 15.0f;

  j1current.x = smoothDamp(j1current.x, j1target.x, j1Velocity.x, 0.018f, deltaSeconds);
  j1current.y = smoothDamp(j1current.y, j1target.y, j1Velocity.y, 0.018f, deltaSeconds);
  j2current.x = smoothDamp(j2current.x, j2target.x, j2Velocity.x, 0.018f, deltaSeconds);
  j2current.y = smoothDamp(j2current.y, j2target.y, j2Velocity.y, 0.018f, deltaSeconds);

  //input.joystick1 = joyConvert(j1current);
  input.joystick2 = joyConvert(j2current);
  input.faceButtons[0] = Hardware::readFaceButton(Hardware::FaceButton::Four);
  input.faceButtons[1] = Hardware::readFaceButton(Hardware::FaceButton::Three);
  bool f2 = input.faceButtons[2];
  bool f3 = input.faceButtons[3];
  bool f6 = input.faceButtons[6];
  input.faceButtons[2] = Hardware::readFaceButton(Hardware::FaceButton::Two);
  input.faceButtons[3] = Hardware::readFaceButton(Hardware::FaceButton::One);
  input.faceButtons[6] = Hardware::readFaceButton(Hardware::FaceButton::Seven);
  if (input.faceButtons[2] && !f2) {
    if (input.faceButtons[3]) {
        pupstate = 1.0f;
    } else {
        pupstate = 2.0f;
    }
  }
  if (input.faceButtons[3] && !f3) {
    if (input.faceButtons[2]) {
        pupstate = 1.0f;
    } else {
        pupstate = 0.0f;
    }
  }
  if (input.faceButtons[6] && !f6) {
    pupstate = 3.0f;
  }
  input.faceButtons[4] = Hardware::readFaceButton(Hardware::FaceButton::Five);
  input.faceButtons[5] = Hardware::readFaceButton(Hardware::FaceButton::Six);
  input.timeSeconds = static_cast<float>(Hardware::millis()) * 0.001f;


  float _5_6_target = input.faceButtons[4] ? (input.faceButtons[5] ? 2.0f : 1.0f) : (input.faceButtons[5] ? 3.0f : 0.0f);
  //float _3_4_target = input.faceButtons[2] ? (input.faceButtons[3] ? 2.0f : 1.0f) : (input.faceButtons[3] ? 3.0f : 0.0f);
  float _1_2_target = input.faceButtons[0] ? (input.faceButtons[1] ? 2.0f : 1.0f) : (input.faceButtons[1] ? 3.0f : 0.0f);
  x_5_6 = smoothDamp4(x_5_6, _5_6_target, x_5_6_v, .1f, deltaSeconds);
  x_3_4 = smoothDamp4(x_3_4, pupstate, x_3_4_v, .1f, deltaSeconds);
  x_1_2 = smoothDamp4(x_1_2, _1_2_target, x_1_2_v, .1f, deltaSeconds);
  input.joystick1.x = clerp4(0.0f, 1.0f, 0.0f, -1.0f, x_5_6);
  input.joystick1.x *= clerp4(1.0f, 0.5f, 0.5f, 0.5f, x_1_2);
  input.joystick1.y = clerp4(0.0f, 0.0f, -1.0f, 0.0f, x_5_6) + clerp4(0.0f, 1.0f, 0.0f, -1.0f, x_1_2);
  
  
  input.pupState = x_3_4;
  

  renderFrame(input);
  Hardware::presentLeds();
}
