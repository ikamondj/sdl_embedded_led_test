#include "RasterRenderer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <thread>
#include <vector>


constexpr int SUPERSAMPLE_GRID = 2;

// Applied to predicate coverage when rast() uses a 2x2 or larger grid.
// 0.0 is ordinary linear antialiasing.  Higher values increasingly suppress
// low-coverage samples while preserving both zero and full coverage.
constexpr float ANTIALIASING_COVERAGE_FALLOFF = 1.3f;


float applyAntialiasingCoverageFalloff(float coverage, int gridSize)
{
    coverage = std::clamp(coverage, 0.0f, 1.0f);

    if (gridSize < 2 || ANTIALIASING_COVERAGE_FALLOFF <= 0.0f) {
        return coverage;
    }

    return std::pow(
        coverage,
        1.0f + ANTIALIASING_COVERAGE_FALLOFF);
}




ColorF shadeSample(float x, float y, const RenderInputs& input);

namespace {
bool threadedRendering = false;
}

void setThreadedRendering(bool enabled) {
  threadedRendering = enabled;
}

void renderFrame(const RenderInputs& input) {
  constexpr float worldWidth = 3.0f;
  constexpr float worldHeight = 1.5f;
  constexpr float pixelWidth = worldWidth / static_cast<float>(Hardware::MATRIX_WIDTH);
  constexpr float pixelHeight = worldHeight / static_cast<float>(Hardware::MATRIX_HEIGHT);
  constexpr int sampleCount = SUPERSAMPLE_GRID * SUPERSAMPLE_GRID;

  constexpr int pixelCount =
      Hardware::MATRIX_WIDTH * Hardware::MATRIX_HEIGHT;
  std::array<Hardware::Rgb, pixelCount> frame{};

  auto renderPixel = [&](int pixelIndex) {
    const int pixelX = pixelIndex % Hardware::MATRIX_WIDTH;
    const int pixelY = pixelIndex / Hardware::MATRIX_WIDTH;
    ColorF accumulated{};

    for (int sampleY = 0; sampleY < SUPERSAMPLE_GRID; ++sampleY) {
      for (int sampleX = 0; sampleX < SUPERSAMPLE_GRID; ++sampleX) {
        const float subpixelX =
            (static_cast<float>(sampleX) + 0.5f) /
            static_cast<float>(SUPERSAMPLE_GRID);
        const float subpixelY =
            (static_cast<float>(sampleY) + 0.5f) /
            static_cast<float>(SUPERSAMPLE_GRID);

        // Pixel-center raster coordinates: X in [-1.5,+1.5],
        // Y in [-0.75,+0.75], with positive Y upward.
        const float x =
            -1.5f + (static_cast<float>(pixelX) + subpixelX) * pixelWidth;
        const float y =
            0.75f - (static_cast<float>(pixelY) + subpixelY) * pixelHeight;

        accumulated = add(accumulated, shadeSample(x, y, input));
      }
    }

    frame[static_cast<std::size_t>(pixelIndex)] =
        toRgb8(scale(accumulated, 1.0f / sampleCount));
  };

  // The imagery owns stateful blink/gaze schedulers. Rendering the first pixel
  // primes that state once; every remaining sample is then read-only and can
  // safely be evaluated in parallel without changing the resulting image.
  renderPixel(0);

  const unsigned detectedThreads =
      threadedRendering ? std::thread::hardware_concurrency() : 1;
  const int workerCount = std::clamp(
      static_cast<int>(detectedThreads == 0 ? 1 : detectedThreads), 1, 4);

  std::vector<std::thread> workers;
  workers.reserve(static_cast<std::size_t>(workerCount - 1));
  for (int worker = 1; worker < workerCount; ++worker) {
    const int begin = 1 + ((pixelCount - 1) * worker) / workerCount;
    const int end = 1 + ((pixelCount - 1) * (worker + 1)) / workerCount;
    workers.emplace_back([&, begin, end]() {
      for (int pixel = begin; pixel < end; ++pixel) {
        renderPixel(pixel);
      }
    });
  }

  const int mainEnd = 1 + (pixelCount - 1) / workerCount;
  for (int pixel = 1; pixel < mainEnd; ++pixel) {
    renderPixel(pixel);
  }
  for (auto& worker : workers) {
    worker.join();
  }

  for (int pixel = 0; pixel < pixelCount; ++pixel) {
    Hardware::setLed(
        pixel % Hardware::MATRIX_WIDTH,
        pixel / Hardware::MATRIX_WIDTH,
        frame[static_cast<std::size_t>(pixel)]);
  }
}

bool inTri(
    float x,
    float y,

    float vertex1X,
    float vertex1Y,

    float vertex2X,
    float vertex2Y,

    float vertex3X,
    float vertex3Y,

    float bulge)
{
    constexpr float epsilon = 1e-6f;

    bulge = std::clamp(bulge, 0.0f, 1.0f);

    auto distance = [](float x1, float y1, float x2, float y2)
    {
        const float dx = x2 - x1;
        const float dy = y2 - y1;

        return std::sqrt(dx * dx + dy * dy);
    };

    auto cross = [](
        float vector1X,
        float vector1Y,
        float vector2X,
        float vector2Y)
    {
        return vector1X * vector2Y - vector1Y * vector2X;
    };

    // Determine whether the vertices are clockwise or counterclockwise.
    const float triangleWinding = cross(
        vertex2X - vertex1X,
        vertex2Y - vertex1Y,
        vertex3X - vertex1X,
        vertex3Y - vertex1Y);

    // All three vertices lie on one line.
    if (std::abs(triangleWinding) < epsilon)
        return false;

    const float orientation =
        triangleWinding > 0.0f ? 1.0f : -1.0f;

    auto satisfiesSide = [&](
        float sideStartX,
        float sideStartY,
        float sideEndX,
        float sideEndY,
        float oppositeVertexX,
        float oppositeVertexY)
    {
        const float edgeX = sideEndX - sideStartX;
        const float edgeY = sideEndY - sideStartY;

        const float edgeLength =
            std::sqrt(edgeX * edgeX + edgeY * edgeY);

        if (edgeLength < epsilon)
            return false;

        /*
            Signed distance from the straight triangle edge.

            Negative values are inside the triangle.
        */
        const float lineConstraint =
            -orientation *
            cross(
                edgeX,
                edgeY,
                x - sideStartX,
                y - sideStartY) /
            edgeLength;

        /*
            At full bulge, each straight edge is replaced by an arc
            belonging to a circle centered at the opposite vertex.

            For an equilateral triangle, this radius equals the common
            side length.
        */
        const float radius =
            0.5f *
            (
                distance(
                    oppositeVertexX,
                    oppositeVertexY,
                    sideStartX,
                    sideStartY) +
                distance(
                    oppositeVertexX,
                    oppositeVertexY,
                    sideEndX,
                    sideEndY)
            );

        if (radius < epsilon)
            return false;

        /*
            Negative values are inside the circle centered at the
            opposite vertex.
        */
        const float circleConstraint =
            distance(
                x,
                y,
                oppositeVertexX,
                oppositeVertexY) -
            radius;

        const float blendedConstraint =
            lineConstraint +
            (circleConstraint - lineConstraint) * bulge;

        return blendedConstraint <= epsilon;
    };

    return
        satisfiesSide(
            vertex1X, vertex1Y,
            vertex2X, vertex2Y,
            vertex3X, vertex3Y) &&

        satisfiesSide(
            vertex2X, vertex2Y,
            vertex3X, vertex3Y,
            vertex1X, vertex1Y) &&

        satisfiesSide(
            vertex3X, vertex3Y,
            vertex1X, vertex1Y,
            vertex2X, vertex2Y);
}


float saturate(float value)
{
    return std::clamp(value, 0.0f, 1.0f);
}

float lerpFloat(float a, float b, float amount)
{
    return a + (b - a) * amount;
}

float smoothstep01(float value)
{
    const float t = saturate(value);
    return t * t * (3.0f - 2.0f * t);
}

float easeOutCubic(float value)
{
    const float t = saturate(value);
    const float inverse = 1.0f - t;

    return 1.0f - inverse * inverse * inverse;
}
