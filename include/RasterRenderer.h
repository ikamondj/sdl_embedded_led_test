#pragma once

#include <array>
#include <algorithm>
#include <cmath>
#include <string>

#include "Hardware.h"



struct ColorF {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
};

struct JoystickState {
  float x = 0.0f;
  float y = 0.0f;
};

struct RenderInputs {
  float timeSeconds = 0.0f;

  JoystickState joystick1{};
  JoystickState joystick2{};

  std::array<bool, 4> faceButtons{};
  int antialiasingLevel = 1;    
};

void renderFrame(const RenderInputs& input);
// Enables the multicore pixel renderer. Disabled by default.
void setThreadedRendering(bool enabled);
bool loadRasterMask(const std::string& path);
bool generateRasterMask(const std::string& path);

inline float clamp01(float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

inline float smoothstep(float edge0, float edge1, float value) {
  if (edge0 == edge1) {
    return value < edge0 ? 0.0f : 1.0f;
  }

  const float t = clamp01((value - edge0) / (edge1 - edge0));
  return t * t * (3.0f - 2.0f * t);
}

inline float length(float x, float y) {
  return std::sqrt(x * x + y * y);
}

inline ColorF mix(const ColorF& a, const ColorF& b, float amount) {
  const float t = clamp01(amount);
  return {
      a.r + (b.r - a.r) * t,
      a.g + (b.g - a.g) * t,
      a.b + (b.b - a.b) * t,
  };
}

inline ColorF add(const ColorF& a, const ColorF& b) {
  return {a.r + b.r, a.g + b.g, a.b + b.b};
}

inline ColorF scale(const ColorF& color, float amount) {
  return {color.r * amount, color.g * amount, color.b * amount};
}

inline Hardware::Rgb toRgb8(const ColorF& color) {
  return {
      static_cast<std::uint8_t>(std::lround(clamp01(color.r) * 255.0f)),
      static_cast<std::uint8_t>(std::lround(clamp01(color.g) * 255.0f)),
      static_cast<std::uint8_t>(std::lround(clamp01(color.b) * 255.0f)),
  };
}

inline ColorF hsvToRgb(float hue, float saturation, float value) {
  hue -= std::floor(hue);
  saturation = clamp01(saturation);
  value = clamp01(value);

  const float h = hue * 6.0f;
  const int sector = static_cast<int>(std::floor(h));
  const float fraction = h - std::floor(h);
  const float p = value * (1.0f - saturation);
  const float q = value * (1.0f - saturation * fraction);
  const float t = value * (1.0f - saturation * (1.0f - fraction));

  switch (sector % 6) {
    case 0: return {value, t, p};
    case 1: return {q, value, p};
    case 2: return {p, value, t};
    case 3: return {p, q, value};
    case 4: return {t, p, value};
    default: return {value, p, q};
  }
}

// Signed-distance approximation for an ellipse. Negative values are inside.
inline float ellipseDistance(float x, float y, float radiusX, float radiusY) {
  const float nx = x / radiusX;
  const float ny = y / radiusY;
  return (std::sqrt(nx * nx + ny * ny) - 1.0f) * std::min(radiusX, radiusY);
}

constexpr inline float WORLD_WIDTH = 3.0f;
constexpr inline float WORLD_HEIGHT = 1.5f;

constexpr inline float PIXEL_WIDTH =
    WORLD_WIDTH / static_cast<float>(Hardware::MATRIX_WIDTH);

constexpr inline float PIXEL_HEIGHT =
    WORLD_HEIGHT / static_cast<float>(Hardware::MATRIX_HEIGHT);

bool inTri(
    float x,
    float y,

    float vertex1X,
    float vertex1Y,

    float vertex2X,
    float vertex2Y,

    float vertex3X,
    float vertex3Y,

    float bulge);

/*
 * Counterclockwise convex polygon test using a flat x, y argument list.
 * Convexity allows an immediate rejection at the first outside edge.
 */
template<typename... Coordinates>
inline bool inConvexPolygon(float x, float y, Coordinates... coordinates)
{
    static_assert(
        sizeof...(Coordinates) >= 6 && sizeof...(Coordinates) % 2 == 0,
        "A convex polygon requires at least three x/y coordinate pairs");

    const float vertices[] = {static_cast<float>(coordinates)...};
    constexpr int coordinateCount = static_cast<int>(sizeof...(Coordinates));

    int previous = coordinateCount - 2;
    for (int current = 0; current < coordinateCount; current += 2)
    {
        const float edgeX = vertices[current] - vertices[previous];
        const float edgeY = vertices[current + 1] - vertices[previous + 1];
        const float pointX = x - vertices[previous];
        const float pointY = y - vertices[previous + 1];

        if (edgeX * pointY - edgeY * pointX < -1e-6f) {
            return false;
        }

        previous = current;
    }

    return true;
}

float saturate(float value);

float lerpFloat(float a, float b, float amount);
float smoothstep01(float value);
float easeOutCubic(float value);

float applyAntialiasingCoverageFalloff(float coverage, int gridSize);


float saturate(float value);

float lerpFloat(float a, float b, float amount);

float smoothstep01(float value);

float easeOutCubic(float value);


inline float dlerp(
    float center,
    float left,
    float right,
    float up,
    float down,
    float joystickX,
    float joystickY)
{
    const float x =
        std::clamp(joystickX, -1.0f, 1.0f);

    const float y =
        std::clamp(joystickY, -1.0f, 1.0f);

    /*
     * First evaluate the vertical line at x = 0.
     */
    const float verticalValue =
        y < 0.0f
            ? lerpFloat(center, down, -y)
            : lerpFloat(center, up, y);

    /*
     * Then move from that vertical-line value toward the selected
     * horizontal endpoint.
     */
    const float horizontalValue =
        x < 0.0f
            ? left
            : right;

    return lerpFloat(
        verticalValue,
        horizontalValue,
        std::fabs(x));
}

/*
 * Draw one predicate-based component into one output pixel.
 *
 * antialiasingLevel:
 *   1 -> 1 sample
 *   2 -> 4 samples
 *   3 -> 9 samples
 *
 * A predicate hit replaces that fraction of the existing color with
 * the component's color.
 */
template<typename Predicate>
ColorF rast(
    float pixelCenterX,
    float pixelCenterY,
    const ColorF& existingColor,
    const ColorF& componentColor,
    int antialiasingLevel,
    const Predicate& predicate)
{
    const int gridSize =
        std::max(1, std::min(3, antialiasingLevel));

    const int sampleCount = gridSize * gridSize;
    int hitCount = 0;

    const float pixelMinX =
        pixelCenterX - PIXEL_WIDTH * 0.5f;

    const float pixelMinY =
        pixelCenterY - PIXEL_HEIGHT * 0.5f;

    for (int sampleY = 0; sampleY < gridSize; ++sampleY) {
        const float subpixelY =
            (static_cast<float>(sampleY) + 0.5f) /
            static_cast<float>(gridSize);

        const float samplePositionY =
            pixelMinY + subpixelY * PIXEL_HEIGHT;

        for (int sampleX = 0; sampleX < gridSize; ++sampleX) {
            const float subpixelX =
                (static_cast<float>(sampleX) + 0.5f) /
                static_cast<float>(gridSize);

            const float samplePositionX =
                pixelMinX + subpixelX * PIXEL_WIDTH;

            if (predicate(samplePositionX, samplePositionY)) {
                ++hitCount;
            }
        }
    }

    if (hitCount == 0) {
        return existingColor;
    }

    const float coverage = applyAntialiasingCoverageFalloff(
        static_cast<float>(hitCount) /
            static_cast<float>(sampleCount),
        gridSize);

    return mix(
        existingColor,
        componentColor,
        coverage);
}

