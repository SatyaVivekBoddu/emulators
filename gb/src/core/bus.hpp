#pragma once
#include "apu.hpp"
#include "cartridge.hpp"
#include "joypad.hpp"
#include "ppu.hpp"
#include "scheduler.hpp"
#include "timer.hpp"
#include "types.hpp"
#include <array>

namespace gb {

class Bus {
public:
    Bus(Cartridge& cart, Scheduler& scheduler)
        : scheduler(scheduler), cartridge(cart), ppu(scheduler), apu(scheduler), timer(scheduler) {}

    void start() {
        timer.start();
        ppu.start();
        apu.start();
    }
    void collectInterrupts() {
        if (timer.interruptRequested) {
            interruptFlag |= 0x04;
            timer.interruptRequested = false;
        }
        if (ppu.lycInterruptRequested) {
            interruptFlag |= 0x02;
            ppu.lycInterruptRequested = false;
        }
        if (ppu.vblankRequested) {
            interruptFlag |= 0x01;
            ppu.vblankRequested = false;
        }
    }

    u8 read8(u16 addr) const;
    void write8(u16 addr, u8 value);
    Ppu& getPpu() { return ppu; }
    Apu& getApu() { return apu; }
    Joypad& getJoypad() { return joypad; }

private:
    Scheduler& scheduler;
    u8 serialControl = 0;
    Joypad joypad;
    Cartridge& cartridge;
    Ppu ppu;
    Apu apu;
    std::array<u8, 0x2000> wram{};
    std::array<u8, 0x7F> hram{};
    u8 interruptFlag = 0;   // IF, 0xFF0F
    u8 interruptEnable = 0; // IE, 0xFFFF
    Timer timer;
    u8 serialByte = 0; // holds SB (0xFF01) for the test-ROM output hack below
};

} // namespace gb
