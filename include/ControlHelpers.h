#include <algorithm>
#include <cmath>

// Maps any value into [0, 4).
inline float wrap4(float x) noexcept
{
    x = std::fmod(x, 4.0f);
    return x < 0.0f ? x + 4.0f : x;
}

// Signed shortest displacement from current to target, in [-2, 2).
inline float delta4(float current, float target) noexcept
{
    float delta = wrap4(target - current + 2.0f) - 2.0f;

    // Resolve the exactly-opposite case consistently.
    // Remove this if either direction is acceptable.
    if (delta == -2.0f && target - current > 0.0f)
        delta = 2.0f;

    return delta;
}

// Unity-style SmoothDamp operating on the circular space [0, 4).
//
// velocity is measured in circle-units per second.
// maxSpeed is also measured in circle-units per second.
inline float smoothDamp4(
    float current,
    float target,
    float& velocity,
    float smoothTime,
    float deltaTime,
    float maxSpeed = INFINITY) noexcept
{
    smoothTime = std::max(0.0001f, smoothTime);

    const float omega = 2.0f / smoothTime;
    const float x = omega * deltaTime;

    // Unity's inexpensive approximation of exp(-x).
    const float decay =
        1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);

    // Work in an unwrapped local coordinate system where target lies along
    // the shortest path from current.
    const float displacement = -delta4(current, target);

    const float maxDisplacement = maxSpeed * smoothTime;
    const float change = std::clamp(
        displacement,
        -maxDisplacement,
        maxDisplacement
    );

    const float localTarget = current - change;
    const float temp = (velocity + omega * change) * deltaTime;

    velocity = (velocity - omega * temp) * decay;

    float output = localTarget + (change + temp) * decay;

    // Prevent overshooting, matching Unity SmoothDamp behavior.
    const bool targetAhead = localTarget > current;
    const bool overshot = targetAhead
        ? output > localTarget
        : output < localTarget;

    if (overshot)
    {
        output = localTarget;
        velocity = 0.0f;
    }

    return wrap4(output);
}

// Periodic Catmull-Rom interpolation through four scalar values.
//
// alpha is a circular coordinate in [0, 4):
//
//   alpha = 0.0 -> _0
//   alpha = 1.0 -> _1
//   alpha = 2.0 -> _2
//   alpha = 3.0 -> _3
//   alpha = 3.5 -> halfway through the _3 -> _0 segment
//
// Values themselves are ordinary scalar values; alpha is the circular part.
inline float clerp4(
    float _0,
    float _1,
    float _2,
    float _3,
    float alpha) noexcept
{
    alpha = wrap4(alpha);

    const int segment = static_cast<int>(alpha);
    const float t = alpha - static_cast<float>(segment);

    const float values[4] = { _0, _1, _2, _3 };

    const float p0 = values[(segment + 3) & 3];
    const float p1 = values[ segment      ];
    const float p2 = values[(segment + 1) & 3];
    const float p3 = values[(segment + 2) & 3];

    // Catmull-Rom polynomial in Horner form.
    return 0.5f * (
        (2.0f * p1) +
        t * (
            (-p0 + p2) +
            t * (
                (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) +
                t * (-p0 + 3.0f * p1 - 3.0f * p2 + p3)
            )
        )
    );
}

// Bilinear interpolation over the same four logical states as clerp4:
// 0 = neither, 1 = first only, 2 = both, 3 = second only. Each button has
// its own independently smoothed [0, 1] coordinate.
inline float squareLerp4(
    float _0,
    float _1,
    float _2,
    float _3,
    float first,
    float second) noexcept
{
    first = std::clamp(first, 0.0f, 1.0f);
    second = std::clamp(second, 0.0f, 1.0f);
    const float withoutSecond = _0 + (_1 - _0) * first;
    const float withSecond = _3 + (_2 - _3) * first;
    return withoutSecond + (withSecond - withoutSecond) * second;
}
