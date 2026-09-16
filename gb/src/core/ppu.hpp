#pragma once
#include "scheduler.hpp"
#include "types.hpp"
#include <array>

namespace gb {

class Ppu {
public:
    explicit Ppu(Scheduler& scheduler) : scheduler(scheduler) {}

    void start() { scheduleNextScanline(scheduler.now()); }

    u8 getMode() const {
        if (ly >= kScreenHeight)
            return 1; // VBlank
        Cycle intoLine = scheduler.now() - scanlineStartCycle;
        if (intoLine < 80)
            return 2; // OAM scan
        if (intoLine < 80 + 172)
            return 3; // Drawing
        return 0;     // HBlank
    }

    bool consumeFrameReady() {
        if (!frameReady)
            return false;
        frameReady = false;
        return true;
    }

    u8 readVRAM(u16 addr) const { return vram[addr - 0x8000]; } // 0x8000 hardcoded
    void writeVRAM(u16 addr, u8 value) { vram[addr - 0x8000] = value; }
    u8 lcdc = 0;
    u8 scy = 0;
    u8 scx = 0;
    u8 ly = 0;
    u8 bgp = 0xE4;
    int frameCount = 0;
    std::array<u8, kScreenWidth * kScreenHeight> framebuffer{}; // color index 0-3 per pixel
    u8 readOAM(u16 index) const { return oam[index]; }
    void writeOAM(u16 index, u8 value) { oam[index] = value; }
    void resetOAM() { oam = {}; }

    u8 lyc = 0;
    bool statInterruptOnLycMatch = false;
    bool statInterruptOnMode0 = false;
    bool statInterruptOnMode1 = false;
    bool statInterruptOnMode2 = false;
    bool lycInterruptRequested = false;
    bool vblankRequested = false;

    u8 readStat() const;
    void writeStat(u8 value);
    static constexpr int kCyclesPerScanline =
        456; // hardware constant (456 x 154 scanlines per full frame)
    static constexpr int kScanlinesPerFrame = 154;

    Cycle scanlineStartCycle = 0;

private:
    Scheduler& scheduler;
    std::array<u8, 0x2000> vram{};
    static constexpr u16 kTileDataBase = 0x8000;
    u16 getBgMapBase() const { return static_cast<u16>((lcdc & 0x08) ? 0x9C00 : 0x9800); }
    std::array<u8, 0xA0> oam{};

    u8 statInterruptBitsAsByte() const;

    void renderScanline();
    void renderSprites();
    void advanceScanline();
    static u8 decodePixel(u8 lowByte, u8 highByte, int pixelX);
    bool frameReady = false;
    void scheduleNextScanline(Cycle from) {
        scanlineStartCycle = from;
        scheduler.schedule(from + kCyclesPerScanline, [this](Cycle firedAt, Cycle) {
            advanceScanline();
            scheduleNextScanline(firedAt);
        });
    };
};

} // namespace gb
