#pragma once
#include "scheduler.hpp"
#include "types.hpp"

namespace gb {

class TimeKeeper {
public:
    void reset(u64 now) {
        startTime = now;
        cyclesExecuted = 0;
    }

    // How many cycles SHOULD have run by now, at current speed — clamped so a real-time
    // gap (window drag, sleep, breakpoint) can't demand an unbounded catch-up burst.
    long long targetCycles(u64 now, Cycle currentCycle) const {
        double realSecondsElapsed = static_cast<double>(now - startTime) / 1000.0;
        long long rawTarget =
            static_cast<long long>(realSecondsElapsed * kClockHz * speedMultiplier);
        long long maxAllowed = currentCycle + maxCatchUpCycles;
        return (rawTarget > maxAllowed) ? maxAllowed : rawTarget;
    }

    void setSpeedMultiplier(double mult) { speedMultiplier = mult; }
    void setPaused(bool p) { paused = p; }
    bool isPaused() const { return paused; }

private:
    u64 startTime = 0;
    long long cyclesExecuted = 0;
    double speedMultiplier = 1.0;
    bool paused = false;
    static constexpr long long maxCatchUpCycles =
        100000; // ~24ms worth — beyond this, we just fall behind
};
} // namespace gb