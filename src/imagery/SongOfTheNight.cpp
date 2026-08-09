#include "RasterRenderer.h"
#include "ControlHelpers.h"

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

constexpr ColorF TOUNGUE_COLOR {
    1.0f,
    0.0f,
    1.0f
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

constexpr ColorF TOP_BANGS_COLOR{
    1.0f,
    0.0f,
    1.0f
};

constexpr ColorF BOTTOM_BANGS_COLOR{
    1.0f,
    1.0f,
    0.0f
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
    const bool newBlinkFrame =
        !blinkTimerInitialized || time != previousTime;

    if (!blinkTimerInitialized || time < previousTime)
    {
        blinkTimerInitialized = true;
        activeBlinkStart = -1000.0f;
        nextBlinkStart = time + chooseBlinkInterval();
    }

    /*
    * Start a blink when its scheduled time arrives, then immediately schedule
    * the next one. Rendering calls eyeSample() thousands of times with the same
    * timestamp, so mutate animation state only on the first sample of a frame.
    */
    if (newBlinkFrame && time >= nextBlinkStart)
    {
        activeBlinkStart = time;
        nextBlinkStart =
            activeBlinkStart +
            chooseBlinkInterval();
    }

    if (newBlinkFrame) {
        previousTime = time;
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

    if (input.disableBlink) {
        blink = 0.0f;
    }

    float blinkability = dlerp(
            1.0f, 
            0.0f, 
            1.0f, 
            0.0f, 
            0.0f, 
            input.joystick1.x, input.joystick1.y);

    auto upperEye = [blink, input, blinkability](float absoluteX) {
        
        if (absoluteX < 0.4f || absoluteX > 1.0f) {
            return -5.0f;
        }

        float defOpen = std::cbrt(
                0.7f * absoluteX - 0.35f) -
            0.05f;

        float contenta = 1.97 * (absoluteX - .4f) * (absoluteX - .4f) - 0.05f;

        const float sloffset = absoluteX - 0.7f;
        const float sleepy = -3.0f * sqrtf(.09 - sloffset * sloffset) + .91f;

        const float foffset = absoluteX - .75;
        const float fullopen = 3.7f * sqrtf(.36-foffset*foffset) - 1.6f;

        const float openUpper = dlerp(
            defOpen, 
            sleepy, 
            defOpen, 
            fullopen, 
            contenta, input.joystick1.x, input.joystick1.y);
            

        const float flatBlink =
            1.4f * absoluteX-.75f;

        

        float nonblinkables = openUpper;

        float blinkables = lerpFloat(
            openUpper,
            flatBlink,
            blink);

        return lerpFloat(
            nonblinkables,
            blinkables,
            blinkability);
    };

    auto lowerEye = [blink, input, blinkability](float absoluteX) {
        if (absoluteX > 1.0f) {
            return 5.0f;
        }
        if (absoluteX < 0.4f) {
            return -.05f;
        }

        const float offset =
            absoluteX - 0.5f;

        const float defopen =
            5.6f *
                offset *
                offset *
                offset -
            0.05f;

        const float sloffset = absoluteX - 0.7f;
        const float sleepy = -2.3f * sqrtf(.09 - sloffset * sloffset) + .65f;

        const float foffset = absoluteX - .63;
        const float fullopen = -2.0f * sqrtf(.16-foffset*foffset) + 0.71f;

        const float closedPosition =
            1.4f * absoluteX-.75f;
        const float slimopen = lerpFloat(defopen, closedPosition, .77f);
        

        const float openLower = dlerp(
            defopen, 
            sleepy, 
            slimopen,
            fullopen, 
            defopen, input.joystick1.x, input.joystick1.y);

        

        float blinkables = lerpFloat(
            openLower,
            closedPosition,
            blink);

        float nonblinkables = openLower;

        return lerpFloat(
            nonblinkables,
            blinkables,
            blinkability);
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

    auto browPredicate = [blink, blinkability, input](float x, float y) {
        float blinkz = blink * blinkability;
        float bigEyeOffsetX = dlerp(0.0f, 0.0f, -0.17f, -.1f, 0.081f, input.joystick1.x, input.joystick1.y);
        float bigEyeOffsetY = dlerp(0.0f, -0.091f, -0.14f, -.0315f, 0.0f, input.joystick1.x, input.joystick1.y);
        bool inbotbrow = inTri(x,y,
            .560f+blinkz*.15f+bigEyeOffsetX * (1-blinkz), .391f-blinkz*.15f+bigEyeOffsetY * (1-blinkz),
            .500f+blinkz*.15f+bigEyeOffsetX * (1-blinkz), .577f-blinkz*.15f+bigEyeOffsetY * (1-blinkz),
            .650f+blinkz*.15f+bigEyeOffsetX * (1-blinkz), .510f-blinkz*.15f+bigEyeOffsetY * (1-blinkz),
            1.0f);

        bool intopbrow = inTri(x,y,
            .713f+blinkz*.15f+bigEyeOffsetX * (1-blinkz), .550f-blinkz*.15f+bigEyeOffsetY * (1-blinkz),
            .680f+blinkz*.15f+bigEyeOffsetX * (1-blinkz), .680f-blinkz*.15f+bigEyeOffsetY * (1-blinkz),
            .840f+blinkz*.15f+bigEyeOffsetX * (1-blinkz), .580f-blinkz*.15f+bigEyeOffsetY * (1-blinkz),
            1.0f);

        return inbotbrow || intopbrow;
    };

    /*
     * Autonomous gaze.  Most of the time the eyes fixate near their neutral
     * center, with occasional larger looks.  Movement between fixations is a
     * short saccade rather than a continuous, mechanical wander.
     */
    static bool pupilMotionInitialized = false;
    static bool pupilWasBlinking = false;
    static float pupilPreviousTime = 0.0f;
    static float pupilNextFixation = 0.0f;
    static float pupilSaccadeStart = 0.0f;
    static float pupilSaccadeEnd = 0.0f;
    static float pupilStartX = 0.0f;
    static float pupilStartY = 0.15f;
    static float pupilTargetX = 0.0f;
    static float pupilTargetY = 0.15f;
    static float pupilGazeX = 0.0f;
    static float pupilGazeY = 0.15f;

    const bool newPupilFrame =
        !pupilMotionInitialized || time != pupilPreviousTime;

    if (!pupilMotionInitialized || time < pupilPreviousTime)
    {
        pupilMotionInitialized = true;
        pupilWasBlinking = false;
        pupilNextFixation = time + 0.7f;
        pupilSaccadeStart = time;
        pupilSaccadeEnd = time;
        pupilStartX = pupilTargetX = pupilGazeX = 0.0f;
        pupilStartY = pupilTargetY = pupilGazeY = 0.15f;
    }

    if (newPupilFrame)
    {
        const bool blinking = blink > 0.10f;
        const bool blinkStarted = blinking && !pupilWasBlinking;

        if (time < pupilSaccadeEnd)
        {
            const float progress = smoothstep01(
                (time - pupilSaccadeStart) /
                std::max(0.001f, pupilSaccadeEnd - pupilSaccadeStart));
            pupilGazeX = lerpFloat(pupilStartX, pupilTargetX, progress);
            pupilGazeY = lerpFloat(pupilStartY, pupilTargetY, progress);
        }
        else
        {
            pupilGazeX = pupilTargetX;
            pupilGazeY = pupilTargetY;
        }

        // A blink often hides a larger gaze reset; otherwise hold fixations.
        if (time >= pupilNextFixation || (blinkStarted && random01() < 0.72f))
        {
            pupilStartX = pupilGazeX;
            pupilStartY = pupilGazeY;

            if (random01() < 0.78f)
            {
                // Four averaged samples strongly favor the neutral center.
                pupilTargetX = cheapNormalish() * 0.075f;
                pupilTargetY = 0.15f + cheapNormalish() * 0.095f;
            }
            else
            {
                // Less common deliberate look toward the usable perimeter.
                const float angle = random01() * 6.28318530718f;
                const float reach = 0.72f + random01() * 0.28f;
                pupilTargetX = std::cos(angle) * 0.10f * reach;
                pupilTargetY = 0.15f + std::sin(angle) * 0.15f * reach;
            }

            pupilSaccadeStart = time;
            const float distance = std::sqrt(
                (pupilTargetX - pupilStartX) * (pupilTargetX - pupilStartX) +
                (pupilTargetY - pupilStartY) * (pupilTargetY - pupilStartY));
            pupilSaccadeEnd = time + 0.028f + distance * 0.22f;
            pupilNextFixation = time + 0.45f + random01() * 1.9f;
        }

        pupilWasBlinking = blinking;
        pupilPreviousTime = time;
    }

    /*
     * Keep both pupils within the intersection of their current eye regions.
     * A 0.04 inset leaves approximately three quarters of a radius-0.10 circle
     * visible even where a curved eyelid is locally close to a straight edge.
     */
    constexpr float PUPIL_VISIBLE_INSET = 0.04f;
    const float safeOffsetX = std::clamp(pupilGazeX, -0.10f, 0.10f);
    const float leftAbsoluteX = EYE_SPACING - safeOffsetX;
    const float rightAbsoluteX = EYE_SPACING + safeOffsetX;
    const float safeLowerY = std::max(
        lowerEye(leftAbsoluteX), lowerEye(rightAbsoluteX)) - 0.1f +
        PUPIL_VISIBLE_INSET;
    const float safeUpperY = std::min(
        upperEye(leftAbsoluteX), upperEye(rightAbsoluteX)) - 0.1f -
        PUPIL_VISIBLE_INSET;

    const float contentaAmount = std::max(-input.joystick1.x, 0.0f);
    const float sleepyAmount =
        std::max(-input.joystick1.y, 0.0f) *
        (1.0f - std::fabs(input.joystick1.x));
    const float hiddenEyeAmount = std::max(contentaAmount, sleepyAmount);
    const float exitAmount = smoothstep01(
        (hiddenEyeAmount - 0.80f) / 0.20f);

    const float pupilOffsetX =
        hiddenEyeAmount > 0.0f ? 0.0f : safeOffsetX;
    const float pupilOffsetY =
        hiddenEyeAmount > 0.0f
            ? 0.15f + 3.0f * exitAmount
            : (safeLowerY <= safeUpperY
                ? std::clamp(pupilGazeY, safeLowerY, safeUpperY)
                : 0.5f * (safeLowerY + safeUpperY));

    const float leftCenterX =
        -EYE_SPACING + pupilOffsetX - .03f * std::fabs(pupilOffsetX);

    const float rightCenterX =
        EYE_SPACING + pupilOffsetX  + .03f * std::fabs(pupilOffsetX);

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


    const float scleraRadiusSqr = input.scleraRadiusSqr;
    const float pupilRadiusSqr = input.pupilRadiusSqr;
    const float innerPupilRadiusSqr = input.innerPupilRadiusSqr;


    const auto pupilPredicate =
        makeEyeCirclePredicate(
            pupilRadiusSqr);

    const auto scleraPredicate =
        makeEyeCirclePredicate(
            scleraRadiusSqr);

    const auto innerPupilPredicate =
        makeEyeCirclePredicate(
            innerPupilRadiusSqr);

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
        RasterPass::Eye,
        eyePredicate);

    color = rast(
        x,
        y,
        color,
        PUPIL_COLOR,
        input.antialiasingLevel,
        RasterPass::Pupil,
        pupilPredicate);

    color = rast(
        x,
        y,
        color,
        SCLERA_COLOR,
        input.antialiasingLevel,
        RasterPass::PupilHighlight,
        scleraPredicate);

    color = rast(
        x,
        y,
        color,
        INNER_PUPIL_COLOR,
        input.antialiasingLevel,
        RasterPass::InnerPupil,
        innerPupilPredicate);

    color = rast(
        std::fabs(x),
        y,
        color,
        BROW_COLOR,
        input.antialiasingLevel,
        RasterPass::Brow,
        browPredicate
    );

    return color;
}

float mouthWidth(const RenderInputs& input) {
    return .85f;
    //return dlerp(.85f, .387298f, .85f, .85f, .85f, input.joystick2.x, input.joystick2.y);
}

float mouthTop(float x, const RenderInputs& input) {
    float mouthW = mouthWidth(input);

    if (std::fabs(x) > mouthW) {
        return -5.0f;
    }
    float openSmile = 0.7f*x*x*x*x-.55f;
    float leftness = std::clamp(-input.joystick2.x, 0.0f, 1.0f);
    float cx = x * (1-0.544355294118f*(1-leftness));
    float circleMouth = sqrt(.15f-cx*cx)-.35f;
    float uwumouth = 0.3f*x*x-0.344f+0.1f*cosf(5*x);
    constexpr float UP_TOP_LIFT = 0.5f * PIXEL_HEIGHT;
    constexpr float RIGHT_TOP_LIFT = 0.75f * PIXEL_HEIGHT;
    
    return dlerp(
        openSmile, 
        circleMouth, 
        -.5f + RIGHT_TOP_LIFT,
        openSmile + UP_TOP_LIFT,
        uwumouth, 
        input.joystick2.x, input.joystick2.y);
}

float mouthBot(float x, const RenderInputs& input) {
    float mouthW = mouthWidth(input);

    if (std::fabs(x) > mouthW) {
        return 5.0f;
    }
    
    float closeSmile = 0.7f*x*x*x*x-.55f;
    float openSmile = 1.1f*x*x*x*x-.75f;
    float leftness = std::clamp(-input.joystick2.x, 0.0f, 1.0f);
    float cx = x * (1-0.544355294118f*(1-leftness));
    float circleMouth = -sqrt(.15f-cx*cx)-.35f;
    float uwumouth = sqrtf(x*x+0.1f)-1.08f;
    return dlerp(
        openSmile, 
        circleMouth, 
        -.5f, 
        closeSmile, 
        uwumouth, input.joystick2.x, input.joystick2.y);
}

bool topTooth(float x, float y, float mouthTop, const RenderInputs& input) {
    x = std::fabs(x);
    float topLeftX = dlerp(.395f, .25f, .395f, .395f, .39f, input.joystick2.x, input.joystick2.y);
    float topLeftY = dlerp(-.497f, -.018f, -.497f, -.497f, -.283f, input.joystick2.x, input.joystick2.y);
    float topRightX = dlerp(.602f, .373f, .602f, .602f, .55f, input.joystick2.x, input.joystick2.y);
    float topRightY = dlerp(-.420f, -.164f, -.420f, -.420f, -.3f, input.joystick2.x, input.joystick2.y);
    float bottomX = dlerp(.500f, .174f, .500f, .500f, .418f, input.joystick2.x, input.joystick2.y);
    float bottomY = dlerp(-.650f, -.251f, -.650f, -.650f, -.496f, input.joystick2.x, input.joystick2.y);
    return inTri(x,y,
        topLeftX, topLeftY,
        topRightX, topRightY,
        bottomX, bottomY,
        0.1f) && y <= mouthTop;
}

bool bottomTooth(float x, float y, float mouthBot, float mouthTop, const RenderInputs& input) {
    x = std::fabs(x);
    float botLeftX = dlerp(.22f, .173f, .22f, .22f, .242f, input.joystick2.x, input.joystick2.y);
    float botLeftY = dlerp(-.81f, -.73f, -.81f, -.81f, -.716f, input.joystick2.x, input.joystick2.y);
    float botRightX = dlerp(.412f, .337f, .412f, .412f, .4f, input.joystick2.x, input.joystick2.y);
    float botRightY = dlerp(-.805f, -.6f, -.805f, -.805f, -.618f, input.joystick2.x, input.joystick2.y);
    float topX = dlerp(.318f, .172f, .318f, .318f, .255f, input.joystick2.x, input.joystick2.y);
    float topY = dlerp(-.6, -.513f, -.6f, -.6f, -.473f, input.joystick2.x, input.joystick2.y);
    return inTri(x,y,
        botLeftX, botLeftY,
        botRightX, botRightY,
        topX, topY,
        0.1f) && y >= mouthBot && y <= mouthTop;
}

bool tongue(float x, float y, float mouthTop, const RenderInputs& input) {
    float top = std::min(mouthTop,dlerp(1, 1, 1, 1, 1, input.joystick2.x, input.joystick2.y));
    float bottomdef = -.25f*sqrtf(.04-x*x)-.7f;
    float bottom = dlerp(1.0f, 1.0f, bottomdef, 1.0f, 1.0f, input.joystick2.x, input.joystick2.y);
    return y <= top && y >= bottom;
}

ColorF mouthSample(
    float x,
    float y,
    const RenderInputs& input, ColorF color)
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

    float mouthTopVal = mouthTop(x, input);
    float mouthBotVal = mouthBot(x, input);
    color = rast(
        x,
        y,
        color,
        MOUTH_COLOR,
        input.antialiasingLevel,
        RasterPass::Mouth,
        [input, mouthTopVal, mouthBotVal](float sampleX, float sampleY) {
            return sampleY > mouthBotVal && sampleY < mouthTopVal;
        });

    color = rast(
        x,
        y,
        color,
        TOUNGUE_COLOR,
        input.antialiasingLevel,
        RasterPass::Tongue,
        [input, mouthTopVal](float sampleX, float sampleY) {
            return tongue(sampleX, sampleY, mouthTopVal, input);
        }
    );

    color = rast(
        x,
        y,
        color,
        TOOTH_COLOR,
        input.antialiasingLevel,
        RasterPass::Teeth,
        [input, mouthTopVal, mouthBotVal](float sampleX, float sampleY) {
            return topTooth(sampleX, sampleY, mouthTopVal, input) ||
                   bottomTooth(
                       sampleX, sampleY, mouthBotVal, mouthTopVal, input);
        });

    return color;
}

ColorF topbangs(
    float x,
    float y,
    const RenderInputs& input,
    const ColorF& existingColor)
{
    return rast(
        x,
        y,
        existingColor,
        TOP_BANGS_COLOR,
        input.antialiasingLevel,
        RasterPass::TopBangs,
        [](float sampleX, float sampleY) {
            sampleX += 0.05f;
            return inConvexPolygon(
                sampleX, sampleY,
                -0.54f, 1.54f,
                -0.455f, 0.74f,
                -0.234f, 0.366f,
                0.42f, 0.656f,
                0.758f, 1.55f);
        });
}

ColorF bottombangs(
    float x,
    float y,
    const RenderInputs& input,
    const ColorF& existingColor)
{
    return rast(
        x,
        y,
        existingColor,
        BOTTOM_BANGS_COLOR,
        input.antialiasingLevel,
        RasterPass::BottomBangs,
        [](float sampleX, float sampleY) {
            sampleX += 0.05f;
            return
                inConvexPolygon(
                    sampleX, sampleY,
                    -0.11f, 0.575f,
                    -0.234f, 0.37f,
                    -0.063f, 0.162f,
                    0.238f, 0.336f,
                    0.188f, 0.63f) ||
                inConvexPolygon(
                    sampleX, sampleY,
                    -0.11f, 0.575f,
                    -0.234f, 0.37f,
                    0.412f, 0.64f,
                    0.258f, 0.657f,
                    0.046f, 0.638f) ||
                inConvexPolygon(
                    sampleX, sampleY,
                    -0.063f, 0.162f,
                    0.318f, 0.132f,
                    0.456f, 0.366f,
                    0.238f, 0.336f) ||
                inConvexPolygon(
                    sampleX, sampleY,
                    0.456f, 0.366f,
                    0.417f, 0.513f,
                    0.373f, 0.513f,
                    0.379f, 0.32f) ||
                inConvexPolygon(
                    sampleX, sampleY,
                    0.373f, 0.513f,
                    0.325f, 0.455f,
                    0.41f, 0.465f);
        });
}

ColorF shadeSample(
    float x,
    float y,
    const RenderInputs& input)
{
    ColorF color = eyeSample(x, y, input);

    color = mouthSample(x, y, input, color);

    color = topbangs(x, y, input, color);

    color = bottombangs(x, y, input, color);

    return color;
}
