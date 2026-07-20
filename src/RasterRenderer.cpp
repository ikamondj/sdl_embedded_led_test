#include "RasterRenderer.h"

#include <algorithm>
#include <cmath>


constexpr int SUPERSAMPLE_GRID = 3;




ColorF shadeSample(float x, float y, const RenderInputs& input);


void renderFrame(const RenderInputs& input) {
  constexpr float worldWidth = 3.0f;
  constexpr float worldHeight = 1.5f;
  constexpr float pixelWidth = worldWidth / static_cast<float>(Hardware::MATRIX_WIDTH);
  constexpr float pixelHeight = worldHeight / static_cast<float>(Hardware::MATRIX_HEIGHT);
  constexpr int sampleCount = SUPERSAMPLE_GRID * SUPERSAMPLE_GRID;

  for (int pixelY = 0; pixelY < Hardware::MATRIX_HEIGHT; ++pixelY) {
    for (int pixelX = 0; pixelX < Hardware::MATRIX_WIDTH; ++pixelX) {
      ColorF accumulated{};

      for (int sampleY = 0; sampleY < SUPERSAMPLE_GRID; ++sampleY) {
        for (int sampleX = 0; sampleX < SUPERSAMPLE_GRID; ++sampleX) {
          const float subpixelX =
              (static_cast<float>(sampleX) + 0.5f) / static_cast<float>(SUPERSAMPLE_GRID);
          const float subpixelY =
              (static_cast<float>(sampleY) + 0.5f) / static_cast<float>(SUPERSAMPLE_GRID);

          // Pixel-center raster coordinates: X in [-2,+2], Y in [-1,+1], with
          // positive Y upward to match a conventional shader coordinate system.
          const float x = -1.5f + (static_cast<float>(pixelX) + subpixelX) * pixelWidth;
          const float y = 0.75f - (static_cast<float>(pixelY) + subpixelY) * pixelHeight;

          accumulated = add(accumulated, shadeSample(x, y, input));
        }
      }

      Hardware::setLed(pixelX, pixelY, toRgb8(scale(accumulated, 1.0f / sampleCount)));
    }
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