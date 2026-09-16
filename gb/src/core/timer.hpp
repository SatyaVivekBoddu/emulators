#pragma once
#include "scheduler.hpp"
#include "types.hpp"

namespace gb {

class Timer {
public:
    explicit Timer(Scheduler& scheduler) : scheduler(scheduler) {}

    void tick(int cycles);
    void start() { scheduleNextOverflow(scheduler.now()); }

    u8 readDiv() const { return static_cast<u8>(((scheduler.now() - divResetCycle) >> 8) & 0xFF); }
    void writeDiv() { divResetCycle = scheduler.now(); }

    void writeTima(u8 value) {
        tima = value;
        reschedule();
    }
    void writeTma(u8 value) { tma = value; }
    void writeTac(u8 value) {
        tac = value;
        reschedule();
    }
    u8 readTima() const { return tima; }
    u8 readTma() const { return tma; }
    u8 readTac() const { return tac; }

    bool interruptRequested = false;

private:
    Scheduler& scheduler;
    u16 divCounter = 0;
    int timaCounter = 0;
    u8 tima = 0;
    u8 tma = 0;
    u8 tac = 0;
    Cycle divResetCycle{0};
    std::uint64_t scheduleGeneration = 0;

    static constexpr int kThresholds[4] = {1024, 16, 64, 256};

    int currentPeriod() const { return kThresholds[tac & 0x03]; }
    bool enabled() const { return (tac & 0x04) != 0; }

    void reschedule() {
        ++scheduleGeneration; // invalidates any in-flight scheduled callback
        if (enabled()) {
            scheduleNextOverflow(scheduler.now());
        }
    }

    void scheduleNextOverflow(Cycle from) {
        if (!enabled())
            return;

        int stepsUntilOverflow = 256 - tima;
        Cycle when = from + static_cast<Cycle>(stepsUntilOverflow) * currentPeriod();
        std::uint64_t generation = scheduleGeneration;

        scheduler.schedule(when, [this, generation](Cycle firedAt, Cycle) {
            if (generation != scheduleGeneration)
                return; // stale: config changed since scheduling
            tima = tma;
            interruptRequested = true;
            scheduleNextOverflow(firedAt);
        });
    }
};

} // namespace gb
