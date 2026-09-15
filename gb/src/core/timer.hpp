#pragma once

#include "types.hpp"

namespace gb {

class Timer {
public:
    void tick(int cycles);

    u8 readDiv() const { return static_cast<u8>(divCounter >> 8); }
    void writeDiv() { divCounter = 0; }

    u8 tima = 0;
    u8 tma = 0;
    u8 tac = 0;
    bool interruptRequested = false;

private:
    u16 divCounter = 0;
    int timaCounter = 0;
};

} // namespace gb
