#pragma once
#include "cartridge.hpp"
#include "timer.hpp"
#include "types.hpp"
#include "joypad.hpp"
#include "ppu.hpp"
#include "apu.hpp"
#include <array>

namespace gb {

class Bus {
public:
    explicit Bus(Cartridge& cart) : cartridge(cart) {}

    u8 read8(u16 addr) const;
    void write8(u16 addr, u8 value);
    void tick(int cycles) {
        timer.tick(cycles);
        if (timer.interruptRequested) {
            interruptFlag |= 0x04;
            timer.interruptRequested = false;
        }
        u8 prevLy = ppu.ly;
        ppu.tick(cycles);
        if (ppu.ly == 0 && prevLy != 0) {
            interruptFlag |= 0x01; // bit 0 = VBlank
        }
        apu.tick(cycles);
    }
    const Ppu& getPpu() const { return ppu; }
    Apu& getApu() { return apu; }
    Joypad& getJoypad() { return joypad; }
private:
    u8 serialControl = 0;
    Joypad joypad;
    Cartridge& cartridge;
    Ppu ppu;
    Apu apu;
    std::array<u8, 0x2000> wram{};
    std::array<u8, 0x7F> hram{};
    u8 interruptFlag = 0; // IF, 0xFF0F
    u8 interruptEnable = 0; // IE, 0xFFFF
    Timer timer;
    u8 serialByte = 0; // holds SB (0xFF01) for the test-ROM output hack below
};

}  // namespace gb
