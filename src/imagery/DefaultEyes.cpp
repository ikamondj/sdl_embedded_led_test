#include "RasterRenderer.h"

namespace RasterRenderer {
    ColorF shadeSample(float x, float y, const RenderInputs& input) {
    const bool button1 = input.faceButtons[0];
    const bool button2 = input.faceButtons[1];
    const bool button3 = input.faceButtons[2];
    const bool button4 = input.faceButtons[3];

    const float time = input.timeSeconds;
    const float eyeSpacing = 0.70f + 0.16f * input.joystick2.x;
    const float pupilRadius = 0.10f + 0.055f * (input.joystick2.y + 1.0f);
    const float squint = button3 ? 0.42f : 1.0f;
    const float eyeRadiusX = 0.55f;
    const float eyeRadiusY = 0.36f * squint;

    // Joystick 1 moves both pupils while preserving their separation.
    const float pupilOffsetX = input.joystick1.x * 0.20f;
    const float pupilOffsetY = input.joystick1.y * 0.15f * squint;

    const float leftEyeX = x + eyeSpacing;
    const float rightEyeX = x - eyeSpacing;
    const float eyeY = y;

    const float leftEyeDistance = ellipseDistance(leftEyeX, eyeY, eyeRadiusX, eyeRadiusY);
    const float rightEyeDistance = ellipseDistance(rightEyeX, eyeY, eyeRadiusX, eyeRadiusY);
    const float eyeDistance = std::min(leftEyeDistance, rightEyeDistance);
    const float eyeCoverage = 1.0f - smoothstep(-0.018f, 0.018f, eyeDistance);

    const float leftPupilDistance = length(
        leftEyeX - pupilOffsetX,
        eyeY - pupilOffsetY) - pupilRadius;
    const float rightPupilDistance = length(
        rightEyeX - pupilOffsetX,
        eyeY - pupilOffsetY) - pupilRadius;
    const float pupilDistance = std::min(leftPupilDistance, rightPupilDistance);
    const float pupilCoverage =
        (1.0f - smoothstep(-0.018f, 0.018f, pupilDistance)) * eyeCoverage;

    const float hue = 0.50f + 0.12f * std::sin(time * 0.55f) + 0.08f * input.joystick2.x;
    const ColorF irisColor = button1
        ? hsvToRgb(hue + 0.35f, 0.90f, 1.0f)
        : hsvToRgb(hue, 0.82f, 1.0f);

    // A subtle animated background gives the renderer a shader-like field while
    // remaining cheap enough to translate directly to embedded CPU raster math.
    const float radial = length(x * 0.55f, y);
    const float backgroundWave = 0.5f + 0.5f * std::sin(9.0f * radial - time * 2.2f);
    const float backgroundAmount = 0.015f + 0.025f * backgroundWave;
    ColorF color = scale(hsvToRgb(hue + radial * 0.10f, 0.75f, 1.0f), backgroundAmount);

    const ColorF eyeWhite = button4 ? ColorF{0.12f, 0.12f, 0.12f}
                                    : ColorF{0.95f, 0.92f, 0.72f};
    color = mix(color, eyeWhite, eyeCoverage);

    // Iris glow is constrained to the eye predicate.
    const float irisRadius = pupilRadius * 2.15f;
    const float leftIris = length(leftEyeX - pupilOffsetX, eyeY - pupilOffsetY);
    const float rightIris = length(rightEyeX - pupilOffsetX, eyeY - pupilOffsetY);
    const float irisDistance = std::min(leftIris, rightIris) - irisRadius;
    const float irisCoverage =
        (1.0f - smoothstep(-0.035f, 0.035f, irisDistance)) * eyeCoverage;
    color = mix(color, irisColor, irisCoverage * 0.88f);

    const ColorF pupilColor = button4 ? ColorF{1.0f, 1.0f, 1.0f}
                                        : ColorF{0.008f, 0.008f, 0.012f};
    color = mix(color, pupilColor, pupilCoverage);

    // Button 2 adds a moving ring evaluated purely from pixel position.
    if (button2) {
        const float ringRadius = 1.18f + 0.10f * std::sin(time * 2.7f);
        const float ringDistance = std::fabs(length(x * 0.62f, y) - ringRadius);
        const float ringCoverage = 1.0f - smoothstep(0.015f, 0.060f, ringDistance);
        color = add(color, scale(hsvToRgb(hue + 0.18f, 0.95f, 1.0f), ringCoverage * 0.75f));
    }

    // Thin center glint to make subpixel movement visible on a low-resolution
    // matrix without requiring any vector drawing API.
    const float glint = (1.0f - smoothstep(0.0f, 0.028f, std::fabs(y)))
                        * (1.0f - smoothstep(0.0f, 1.65f, std::fabs(x)))
                        * 0.12f;
    color = add(color, {glint, glint, glint});

    return color;
    }
}