#include <cstdio>
#include "bus.hpp"

namespace gb {

u8 Bus::read8(u16 addr) const {
    if (addr <= 0x7FFF) return cartridge.read(addr);       // ROM
    if (addr <= 0x9FFF) return ppu.readVRAM(addr);         // VRAM
    if (addr <= 0xBFFF) return cartridge.read(addr);       // cart RAM
    if (addr <= 0xDFFF) return wram[addr - 0xC000];        // work RAM
    if (addr <= 0xFDFF) return wram[addr - 0xE000];        // echo RAM
    if (addr <= 0xFE9F) return 0xFF;                       // OAM (stub)
    if (addr <= 0xFEFF) return 0xFF;                       // unusable region
    if (addr <= 0xFF7F) {  // I/O (stub)
        if (addr == 0xFF00) return joypad.read();
        if (addr == 0xFF01) return serialByte;
        if (addr == 0xFF02) return serialControl; // track the last written value explicitly
        if (addr == 0xFF04) return timer.readDiv();
        if (addr == 0xFF05) return timer.tima;
        if (addr == 0xFF06) return timer.tma;
        if (addr == 0xFF07) return timer.tac;
        if (addr == 0xFF0F) return interruptFlag;
        if (addr == 0xFF40) return ppu.lcdc;
        if (addr == 0xFF41) return ppu.readStat();
        if (addr == 0xFF42) return ppu.scy;
        if (addr == 0xFF43) return ppu.scx;
        if (addr == 0xFF44) return ppu.ly;
        if (addr == 0xFF47) return ppu.bgp;
        return 0xFF;
    }
    if (addr <= 0xFFFE) return hram[addr - 0xFF80];        // high RAM
    return interruptEnable;                                // 0xFFFF
}

void Bus::write8(u16 addr, u8 value) {
    if (addr <= 0x7FFF) { cartridge.write(addr, value); return; }
    if (addr <= 0x9FFF) {
        // static int vramWriteCount = 0;
        // if (vramWriteCount >= 8200 && vramWriteCount < 8210) {
        //     std::printf("VRAM write: addr=0x%04X value=0x%02X\n", addr, value);
        // }
        // ++vramWriteCount;
        ppu.writeVRAM(addr, value);
        return;
    }
    if (addr <= 0xBFFF) { cartridge.write(addr, value); return; }
    if (addr <= 0xDFFF) { wram[addr - 0xC000] = value; return; }
    if (addr <= 0xFDFF) { wram[addr - 0xE000] = value; return; }
    if (addr <= 0xFE9F) { return; }
    if (addr <= 0xFEFF) { return; }
    if (addr <= 0xFF7F) {
        if (addr == 0xFF00) { joypad.write(value); return; }
        if (addr == 0xFF01) { serialByte = value; return; }
        if (addr == 0xFF02) {
            serialControl = value;
            if (value & 0x80) {
                std::putchar(static_cast<char>(serialByte));
                std::fflush(stdout);
                // interruptFlag |= 0x08;
            }
            return;
        }
        if (addr == 0xFF04) { timer.writeDiv(); return; }
        if (addr == 0xFF05) { timer.tima = value; return; }
        if (addr == 0xFF06) { timer.tma = value; return; }
        if (addr == 0xFF07) { timer.tac = value; return; }
        if (addr == 0xFF0F) { interruptFlag = value; return; }
        if (addr == 0xFF40) { ppu.lcdc = value; return; }
        if (addr == 0xFF42) { ppu.scy = value; return; }
        if (addr == 0xFF43) { ppu.scx = value; return; }
        if (addr == 0xFF46) {
            u16 sourceBase = static_cast<u16>(value << 8);
            static int dmaCount = 0;
            ++dmaCount;
            if (dmaCount <= 5) {
                std::printf("DMA #%d, first 8 bytes at 0x%04X: ", dmaCount, sourceBase);
                for (int i = 0; i < 8; ++i) std::printf("%02X ", read8(static_cast<u16>(sourceBase + i)));
                std::printf("\n");
            }

            // ppu.resetOAM();
            for (u16 i = 0; i < 0xA0; ++i) {
                u8 byte = read8(static_cast<u16>(sourceBase + i));
                ppu.writeOAM(static_cast<u16>(i), byte);
            }
            return;
        }
        if (addr == 0xFF47) { ppu.bgp = value; return; }
        return;
    }
    if (addr <= 0xFFFE) { hram[addr - 0xFF80] = value; return; }
    interruptEnable = value;
}

}  // namespace gb
