#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#else
#include <chrono>
#endif

namespace Hardware {

class DeltaTimer {
public:
    DeltaTimer() = default;

    float tick();

    void reset();

private:
    bool initialized_ = false;

#ifdef ARDUINO
    uint32_t previousUs_ = 0;
#else
    using Clock = std::chrono::steady_clock;
    Clock::time_point previousTime_{};
#endif
};

} // namespace Hardware