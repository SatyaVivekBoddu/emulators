#include "timer.hpp"

namespace gb {

void Timer::tick(int cycles) {
    divCounter = static_cast<u16>(divCounter + cycles);
    if (tac & 0x04) {
        static constexpr int thresholds[4] = {1024, 16, 64, 256};
        int threshold = thresholds[tac & 0x03];

        timaCounter += cycles;
        while (timaCounter >= threshold) {
            timaCounter -= threshold;
            if (tima == 0xFF) {
                tima = tma;
                interruptRequested = true;
            } else {
                ++tima;
            }
        }
    }
}

} // namespace gb
