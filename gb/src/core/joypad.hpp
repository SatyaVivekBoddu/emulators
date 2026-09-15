#pragma once
#include "types.hpp"
#include <cstdio>

namespace gb {

class Joypad {
public:
    u8 read() const {
        u8 result = 0xC0 | selectBits | 0x0F;
        if (!(selectBits & 0x10)) {
            result &= static_cast<u8>(0xF0 | selectBits | (directionState & 0x0F));
        }
        if (!(selectBits & 0x20)) {
            result &= static_cast<u8>(0xF0 | selectBits | (actionState & 0x0F));
        }
        return result;
    }

    void write(u8 value) { selectBits = value & 0x30; }

    u8 directionState = 0x0F;
    u8 actionState = 0x0F;

private:
    u8 selectBits = 0x30;
};

} // namespace gb