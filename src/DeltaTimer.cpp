#include "DeltaTimer.h"

namespace Hardware {

float DeltaTimer::tick() {
#ifdef ARDUINO

    const uint32_t nowUs = micros();

    if (!initialized_) {
        previousUs_ = nowUs;
        initialized_ = true;
        return 0.0f;
    }

    // Unsigned subtraction automatically handles micros() wraparound.
    const uint32_t elapsedUs = nowUs - previousUs_;
    previousUs_ = nowUs;

    return static_cast<float>(elapsedUs) * 1.0e-6f;

#else

    const Clock::time_point now = Clock::now();

    if (!initialized_) {
        previousTime_ = now;
        initialized_ = true;
        return 0.0f;
    }

    const float elapsed =
        std::chrono::duration<float>(now - previousTime_).count();

    previousTime_ = now;

    return elapsed;

#endif
}

void DeltaTimer::reset() {
    initialized_ = false;

#ifdef ARDUINO
    previousUs_ = 0;
#else
    previousTime_ = Clock::time_point{};
#endif
}

} // namespace Hardware