#include "RasterRenderer.h"

#include <algorithm>
#include <cmath>


#include <cstdint>



constexpr float EYE_SPACING = 0.65f;
constexpr float EYE_HORIZONTAL_TRAVEL = 0.30f;
constexpr float EYE_VERTICAL_TRAVEL = 0.20f;

// This is the original cyan pupil size: 0.10² = 0.01.
constexpr float PUPIL_RADIUS_SQR = 0.01f;

// Fixed black inner-circle radius squared.
constexpr float INNER_PUPIL_RADIUS_SQR = 0.0001f;

constexpr ColorF BACKGROUND_COLOR{
    0.0f,
    0.0f,
    0.0f
};

constexpr ColorF EYE_COLOR{
    1.0f,
    1.0f,
    0.0f
};

constexpr ColorF PUPIL_COLOR{
    0.0f,
    1.0f,
    1.0f
};

constexpr ColorF BROW_COLOR{
    0.0f,
    1.0f,
    1.0f
};

constexpr ColorF SCLERA_COLOR{
    1.0f,
    1.0f,
    1.0f
};

constexpr ColorF INNER_PUPIL_COLOR{
    0.0f,
    0.0f,
    0.0f
};

ColorF eyeSample(
    float x,
    float y,
    const RenderInputs& input)
{
    const float time = input.timeSeconds;


    constexpr float CLOSE_TIME  = 0.085f;
    constexpr float CLOSED_TIME = 0.045f;
    constexpr float OPEN_TIME   = 0.145f;

    constexpr float TOTAL_BLINK_TIME =
        CLOSE_TIME +
        CLOSED_TIME +
        OPEN_TIME;

    /*
    * Cheap deterministic pseudo-random generator.
    *
    * This does not require <random>, and it produces the same sequence every
    * time the program starts. Make randomState non-constant to vary the sequence
    * between boots.
    */
    static std::uint32_t randomState = 0xA341316Cu;

    auto random01 = []() -> float
    {
        // xorshift32
        randomState ^= randomState << 13;
        randomState ^= randomState >> 17;
        randomState ^= randomState << 5;

        // Use the lower 24 bits to produce approximately [0, 1).
        return static_cast<float>(randomState & 0x00FFFFFFu) /
            static_cast<float>(0x01000000u);
    };

    /*
    * Average several uniform random values.
    *
    * The result is centered near zero and has a roughly bell/normal-like shape,
    * but is much cheaper than calculating a true normal distribution.
    *
    * Approximate range: [-1, +1]
    * Most results remain near zero.
    */
    auto cheapNormalish = [&random01]() -> float
    {
        const float average =
            (
                random01() +
                random01() +
                random01() +
                random01()
            ) * 0.25f;

        return (average - 0.5f) * 2.0f;
    };

    /*
    * Choose the open-eye waiting time before the next blink.
    *
    * Distribution:
    *
    *   5%  short:  around 0.25 seconds
    *   80% normal: around the current 4.20 seconds
    *   15% long:   at least 3 × 4.20 = 12.60 seconds
    */
    auto chooseBlinkInterval = [&]() -> float
    {
        constexpr float AVERAGE_INTERVAL = 4.20f;

        const float categoryRoll = random01();
        const float variation = cheapNormalish();

        if (categoryRoll < 0.05f)
        {
            /*
            * Short cluster:
            * approximately 0.18–0.32 seconds.
            */
            return std::max(
                TOTAL_BLINK_TIME,
                0.25f + variation * 0.07f);
        }

        if (categoryRoll < 0.20f)
        {
            /*
            * Long cluster:
            * starts at 3× the average and varies upward.

            * Approximately 12.6–16.8 seconds.
            * It never falls below 12.6 seconds.
            */
            return
                AVERAGE_INTERVAL * 3.0f +
                (variation + 1.0f) *
                AVERAGE_INTERVAL * 0.5f;
        }

        /*
        * Ordinary cluster:
        * approximately 3.15–5.25 seconds, concentrated around 4.20.
        */
        return
            AVERAGE_INTERVAL +
            variation * 1.05f;
    };

    /*
    * Persistent blink scheduler.
    *
    * eyeSample() is called many times per frame, but after a blink is scheduled,
    * nextBlinkStart immediately moves into the future. Therefore, all remaining
    * samples in the frame see the same blink state.
    */
    static bool blinkTimerInitialized = false;
    static float nextBlinkStart = 0.0f;
    static float activeBlinkStart = -1000.0f;
    static float previousTime = 0.0f;

    /*
    * Handle program startup or the simulation clock being reset backward.
    */
    if (!blinkTimerInitialized || time < previousTime)
    {
        blinkTimerInitialized = true;
        activeBlinkStart = -1000.0f;
        nextBlinkStart = time + chooseBlinkInterval();
    }

    previousTime = time;

    /*
    * Start a blink when its scheduled time arrives, then immediately schedule
    * the next one.
    */
    if (time >= nextBlinkStart)
    {
        activeBlinkStart = time;
        nextBlinkStart =
            activeBlinkStart +
            chooseBlinkInterval();
    }

    const float blinkTime =
        time - activeBlinkStart;

    float blink = 0.0f;

    if (
        blinkTime >= 0.0f &&
        blinkTime < CLOSE_TIME)
    {
        blink =
            easeOutCubic(
                blinkTime / CLOSE_TIME);
    }
    else if (
        blinkTime >= CLOSE_TIME &&
        blinkTime < CLOSE_TIME + CLOSED_TIME)
    {
        blink = 1.0f;
    }
    else if (
        blinkTime >= CLOSE_TIME + CLOSED_TIME &&
        blinkTime < TOTAL_BLINK_TIME)
    {
        const float openingTime =
            blinkTime -
            CLOSE_TIME -
            CLOSED_TIME;

        blink =
            1.0f -
            smoothstep01(
                openingTime / OPEN_TIME);
    }

    auto upperEye = [blink](float absoluteX) {
        if (absoluteX < 0.5f || absoluteX > 1.0f) {
            return -5.0f;
        }

        const float openUpper =
            std::cbrt(
                0.7f * absoluteX - 0.35f) -
            0.05f;

        const float closedPosition =
            1.4f * absoluteX-.75f;

        return lerpFloat(
            openUpper,
            closedPosition,
            blink);
    };

    auto lowerEye = [blink](float absoluteX) {
        if (absoluteX < 0.5f || absoluteX > 1.0f) {
            return 5.0f;
        }

        const float offset =
            absoluteX - 0.5f;

        const float openLower =
            5.6f *
                offset *
                offset *
                offset -
            0.05f;

        const float closedPosition =
            1.4f * absoluteX-.75f;

        return lerpFloat(
            openLower,
            closedPosition,
            blink);
    };

    /*
     * The original eye code operated in y=[0,2], while this renderer uses
     * y=[-0.75,+0.75]. Reconstruct the original coordinate with y+1.
     */
    auto eyePredicate =
        [upperEye, lowerEye](float sampleX, float sampleY)
    {
        const float absoluteX =
            std::fabs(sampleX);

        if (absoluteX < 0.5f || absoluteX > 1.0f) {
            return false;
        }

        const float originalY =
            sampleY + .1f;

        return
            lowerEye(absoluteX) < originalY &&
            originalY < upperEye(absoluteX);
    };

    auto browPredicate = [blink](float x, float y) {
        bool inbotbrow = inTri(x,y,
            .560f+blink*.15f, .391f-blink*.15f,
            .500f+blink*.15f, .577f-blink*.15f,
            .650f+blink*.15f, .510f-blink*.15f,
            1.0f);

        bool intopbrow = inTri(x,y,
            .713f+blink*.15f, .550f-blink*.15f,
            .680f+blink*.15f, .680f-blink*.15f,
            .840f+blink*.15f, .580f-blink*.15f,
            1.0f);

        return inbotbrow || intopbrow;
    };

    /*
     * Joystick 1 moves all concentric eye circles together.
     */
    const float pupilOffsetX =
        EYE_HORIZONTAL_TRAVEL *
        input.joystick1.x;

    const float pupilOffsetY =
        EYE_VERTICAL_TRAVEL *
        input.joystick1.y+.15f;

    const float leftCenterX =
        -EYE_SPACING + pupilOffsetX;

    const float rightCenterX =
        EYE_SPACING + pupilOffsetX;

    const float centerY =
        pupilOffsetY;

    /*
     * Shared concentric-circle predicate generator.
     *
     * The supplied value is radius squared, so no sqrt() is needed.
     * All circles are clipped to the yellow eye geometry.
     */
    auto makeEyeCirclePredicate =
        [
            leftCenterX,
            rightCenterX,
            centerY,
            eyePredicate
        ](float radiusSqr)
    {
        return
            [
                leftCenterX,
                rightCenterX,
                centerY,
                radiusSqr,
                eyePredicate
            ](float sampleX, float sampleY)
        {
            if (!eyePredicate(sampleX, sampleY)) {
                return false;
            }

            const float leftDx =
                sampleX - leftCenterX;

            const float rightDx =
                sampleX - rightCenterX;

            const float dy =
                sampleY - centerY;

            const float leftDistanceSqr =
                leftDx * leftDx +
                dy * dy;

            const float rightDistanceSqr =
                rightDx * rightDx +
                dy * dy;

            return
                leftDistanceSqr <= radiusSqr ||
                rightDistanceSqr <= radiusSqr;
        };
    };


    const float scleraRadiusSqr = .0035f;

    const auto pupilPredicate =
        makeEyeCirclePredicate(
            PUPIL_RADIUS_SQR);

    const auto scleraPredicate =
        makeEyeCirclePredicate(
            scleraRadiusSqr);

    const auto innerPupilPredicate =
        makeEyeCirclePredicate(
            INNER_PUPIL_RADIUS_SQR);

    /*
     * Component order matters because each later component replaces a
     * coverage-weighted portion of the color already in the pixel.
     */
    ColorF color = BACKGROUND_COLOR;

    color = rast(
        x,
        y,
        color,
        EYE_COLOR,
        input.antialiasingLevel,
        eyePredicate);

    color = rast(
        x,
        y,
        color,
        PUPIL_COLOR,
        input.antialiasingLevel,
        pupilPredicate);

    color = rast(
        x,
        y,
        color,
        SCLERA_COLOR,
        input.antialiasingLevel,
        scleraPredicate);

    color = rast(
        x,
        y,
        color,
        INNER_PUPIL_COLOR,
        input.antialiasingLevel,
        innerPupilPredicate);

    color = rast(
        abs(x),
        y,
        color,
        BROW_COLOR,
        input.antialiasingLevel,
        browPredicate
    );

    return color;
}

float mouthWidth(const RenderInputs& input) {
    return dlerp(.85f, .387298f, .85f, .85f, .85f, input.joystick2.x, input.joystick2.y);
}

float mouthTop(float x, const RenderInputs& input) {
    float mouthW = mouthWidth(input);

    if (abs(x) > mouthW) {
        return -5.0f;
    }
    return dlerp(
        0.7f*x*x*x*x-.55f, 
        sqrt(.15f-x*x)-.35f, 
        -.5f, 
        0.7f*x*x*x*x-.55f, 
        -.5f, 
        input.joystick2.x, input.joystick2.y);
}

float mouthBot(float x, const RenderInputs& input) {
    float mouthW = mouthWidth(input);

    if (abs(x) > mouthW) {
        return 5.0f;
    }
    return dlerp(
        1.1f*x*x*x*x-.75f, 
        -sqrt(.15f-x*x)-.35f, 
        -.5f, 
        0.7f*x*x*x*x-.65f, 
        -.5f, input.joystick2.x, input.joystick2.y);
}

bool topTooth(float x, float y, float mouthTop, const RenderInputs& input) {
    x = abs(x);
    return inTri(x,y,
        .395f, -.497f,
        .602f, -.420f,
        .500f, -.650f,
        0.1f) && y < mouthTop;
}

ColorF mouthSample(
    float x,
    float y,
    const RenderInputs& input)
{
    constexpr ColorF MOUTH_COLOR{
        0.0f,
        1.0f,
        1.0f
    };

    constexpr ColorF TOOTH_COLOR{
        1.0f,
        1.0f,
        0.0f
    };

    ColorF color = BACKGROUND_COLOR;
    float mouthTopVal = mouthTop(x, input);
    color = rast(
        x,
        y,
        color,
        MOUTH_COLOR,
        input.antialiasingLevel,
        [input, mouthTopVal](float sampleX, float sampleY) {
            return sampleY > mouthBot(sampleX, input) && sampleY < mouthTopVal;
        });

    color = rast(
        x,
        y,
        color,
        TOOTH_COLOR,
        input.antialiasingLevel,
        [input, mouthTopVal](float sampleX, float sampleY) {
            return topTooth(sampleX, sampleY, mouthTopVal, input);
        });

    return color;
}

ColorF shadeSample(
    float x,
    float y,
    const RenderInputs& input)
{
    ColorF color = eyeSample(x, y, input);

    if (color.r == 0.0f && color.g == 0.0f && color.b == 0.0f) {
        color = mouthSample(x, y, input);
    }

    return color;
}
