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
float x5Amount = 0.0f, x6Amount = 0.0f;
float x1Amount = 0.0f, x2Amount = 0.0f;
float pupilFirstAmount = 1.0f, pupilSecondAmount = 0.0f;
float x5Velocity = 0.0f, x6Velocity = 0.0f;
float x1Velocity = 0.0f, x2Velocity = 0.0f;
float pupilFirstVelocity = 0.0f, pupilSecondVelocity = 0.0f;
float pupstate = 1;
float shutdownHoldSeconds = 0.0f;
float brightness = 1.0f;
bool rendererBlackout = false;
bool blackoutWakeArmed = false;
bool blackoutClickWasPressed = false;

constexpr float SHUTDOWN_HOLD_SECONDS = 3.0f;
constexpr float BRIGHTNESS_MIN = 0.1f;
constexpr float BRIGHTNESS_MAX = 1.0f;
constexpr float BRIGHTNESS_FULL_RANGE_SECONDS = 4.0f;


template <bool CircularButtonGeometry>
void loopImpl() {
  Hardware::poll();
  if (!Hardware::isRunning()) {
    return;
  }
  const float deltaSeconds = deltaTimer.tick();

  if (rendererBlackout) {
    const bool clickPressed =
        Hardware::readFaceButton(Hardware::FaceButton::Seven);
    input.faceButtons[6] = clickPressed;

    if (!clickPressed) {
      blackoutWakeArmed = true;
    }

    if (blackoutWakeArmed
        && clickPressed
        && !blackoutClickWasPressed) {
      rendererBlackout = false;
      blackoutWakeArmed = false;
    } else {
      blackoutClickWasPressed = clickPressed;
      Hardware::delayMs(10);
      return;
    }
  }

  j1current = hardConvert(input.joystick1);
  j2current = hardConvert(input.joystick2);

  auto j1target = Hardware::readJoystick(1);
  auto j2target = Hardware::readJoystick(0);

  bool f2 = input.faceButtons[2];
  bool f3 = input.faceButtons[3];
  bool f6 = input.faceButtons[6];

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
  input.faceButtons[2] = Hardware::readFaceButton(Hardware::FaceButton::Two);
  input.faceButtons[3] = Hardware::readFaceButton(Hardware::FaceButton::One);
  input.faceButtons[4] = Hardware::readFaceButton(Hardware::FaceButton::Five);
  input.faceButtons[5] = Hardware::readFaceButton(Hardware::FaceButton::Six);
  input.faceButtons[6] = Hardware::readFaceButton(Hardware::FaceButton::Seven);
  input.timeSeconds = static_cast<float>(Hardware::millis()) * 0.001f;

  const bool shutdownChord =
      input.faceButtons[4]
      && input.faceButtons[5]
      && input.faceButtons[6];
  shutdownHoldSeconds = shutdownChord
      ? shutdownHoldSeconds + deltaSeconds
      : 0.0f;

  if (shutdownHoldSeconds >= SHUTDOWN_HOLD_SECONDS) {
    rendererBlackout = true;
    blackoutWakeArmed = false;
    blackoutClickWasPressed = input.faceButtons[6];
    shutdownHoldSeconds = 0.0f;
    Hardware::clearLeds({0, 0, 0});
    Hardware::presentLeds();
    Hardware::delayMs(10);
    return;
  }

  const bool brightnessChord =
      input.faceButtons[0]
      && input.faceButtons[1]
      && input.faceButtons[2]
      && input.faceButtons[3];
  if (brightnessChord) {
    constexpr float BRIGHTNESS_RATE =
        (BRIGHTNESS_MAX - BRIGHTNESS_MIN)
        / BRIGHTNESS_FULL_RANGE_SECONDS;
    brightness = std::clamp(
        brightness + j2target.y * BRIGHTNESS_RATE * deltaSeconds,
        BRIGHTNESS_MIN,
        BRIGHTNESS_MAX);
    Hardware::setBrightness(brightness);
  }

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


  const float _5_6_target = input.faceButtons[4] ? (input.faceButtons[5] ? 2.0f : 1.0f) : (input.faceButtons[5] ? 3.0f : 0.0f);
  const float _1_2_target = input.faceButtons[0] ? (input.faceButtons[1] ? 2.0f : 1.0f) : (input.faceButtons[1] ? 3.0f : 0.0f);

  if constexpr (CircularButtonGeometry) {
    x_5_6 = smoothDamp4(x_5_6, _5_6_target, x_5_6_v, .1f, deltaSeconds);
    x_3_4 = smoothDamp4(x_3_4, pupstate, x_3_4_v, .1f, deltaSeconds);
    x_1_2 = smoothDamp4(x_1_2, _1_2_target, x_1_2_v, .1f, deltaSeconds);
  } else {
    x5Amount = smoothDamp(x5Amount, input.faceButtons[4] ? 1.0f : 0.0f, x5Velocity, .1f, deltaSeconds);
    x6Amount = smoothDamp(x6Amount, input.faceButtons[5] ? 1.0f : 0.0f, x6Velocity, .1f, deltaSeconds);
    x1Amount = smoothDamp(x1Amount, input.faceButtons[0] ? 1.0f : 0.0f, x1Velocity, .1f, deltaSeconds);
    x2Amount = smoothDamp(x2Amount, input.faceButtons[1] ? 1.0f : 0.0f, x2Velocity, .1f, deltaSeconds);
    const bool pupilFirstTarget = pupstate == 1.0f || pupstate == 2.0f;
    const bool pupilSecondTarget = pupstate == 2.0f || pupstate == 3.0f;
    pupilFirstAmount = smoothDamp(pupilFirstAmount, pupilFirstTarget ? 1.0f : 0.0f, pupilFirstVelocity, .1f, deltaSeconds);
    pupilSecondAmount = smoothDamp(pupilSecondAmount, pupilSecondTarget ? 1.0f : 0.0f, pupilSecondVelocity, .1f, deltaSeconds);
  }

  const auto stateLerp = [](float s0, float s1, float s2, float s3,
                            [[maybe_unused]] float circular,
                            [[maybe_unused]] float first,
                            [[maybe_unused]] float second) {
    if constexpr (CircularButtonGeometry) {
      return clerp4(s0, s1, s2, s3, circular);
    } else {
      return squareLerp4(s0, s1, s2, s3, first, second);
    }
  };

  input.joystick1.x = stateLerp(0.0f, 1.0f, 0.0f, -1.0f, x_5_6, x5Amount, x6Amount);
  float rups = stateLerp(0.0f, 0.0f, 0.6f, 0.0f, x_5_6, x5Amount, x6Amount)
      * stateLerp(0.0f, 1.0f, 0.0f, 1.0f, x_1_2, x1Amount, x2Amount);
  input.joystick1.x *= stateLerp(1.0f, 0.5f, 0.5f, 0.5f, x_1_2, x1Amount, x2Amount);
  input.joystick1.x += rups;
  float ups = stateLerp(1.0f, 1.0f, 0.5f, 1.0f, x_5_6, x5Amount, x6Amount);
  
  input.joystick1.y = stateLerp(0.0f, 0.0f, -1.0f, 0.0f, x_5_6, x5Amount, x6Amount)
      + stateLerp(0.0f, ups, 0.0f, -1.0f, x_1_2, x1Amount, x2Amount);
  
  
  input.scleraRadiusSqr = stateLerp(
      .0005f, .0035f, .008f, .021f,
      x_3_4, pupilFirstAmount, pupilSecondAmount);
  input.pupilRadiusSqr = stateLerp(
      .007f, .01f, .03f, .03f,
      x_3_4, pupilFirstAmount, pupilSecondAmount);
  input.innerPupilRadiusSqr = stateLerp(
      .002f, .0001f, .009f, .001f,
      x_3_4, pupilFirstAmount, pupilSecondAmount);
  

  renderFrame(input);
  Hardware::presentLeds();
}

void loopSquareControls() {
  loopImpl<false>();
}

void loopCircularControls() {
  loopImpl<true>();
}
