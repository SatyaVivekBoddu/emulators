#include "ppu.hpp"
#include "types.hpp"

#include <cstdio>

namespace gb {
    

u8 Ppu::statInterruptBitsAsByte() const {
    return static_cast<u8>(
        (statInterruptOnMode0 ? 0x08 : 0) |
        (statInterruptOnMode1 ? 0x10 : 0) |
        (statInterruptOnMode2 ? 0x20 : 0) |
        (statInterruptOnLycMatch ? 0x40 : 0));
}

void Ppu::writeStat(u8 value) {
    statInterruptOnMode0 = value & 0x08;
    statInterruptOnMode1 = value & 0x10;
    statInterruptOnMode2 = value & 0x20;
    statInterruptOnLycMatch = value & 0x40;
}

u8 Ppu::getMode() const {
    if (ly >= kScreenHeight) return 1; // VBlank
    if (scanlineCycles < 80) return 2; // OAM scan
    if (scanlineCycles < 80 + 172) return 3; // Drawing
    return 0; // HBlank
}

u8 Ppu::readStat() const {
    u8 lycMatchBit = (ly == lyc) ? 0x04 : 0x00;
    return static_cast<u8>(0x80 | statInterruptBitsAsByte() | lycMatchBit | getMode());
}


u8 Ppu::decodePixel(u8 lowByte, u8 highByte, int pixelX) {
    int bit = 7 - pixelX;
    u8 lowBit = (lowByte >> bit) & 0x01;
    u8 highBit = (highByte >> bit) & 0x01;
    return static_cast<u8>((highBit << 1) | lowBit);
}

void Ppu::renderSprites() {
    if (!(lcdc & 0x02)) return; // bit 1: sprite rendering enabled at all
    
    for (int i = 0; i < 40; ++i) {
        u8 spriteY = readOAM(static_cast<u16>(i * 4)) - 16;
        u8 spriteX = readOAM(static_cast<u16>(i * 4 + 1)) - 8;
        u8 tileIndex = readOAM(static_cast<u16>(i * 4 + 2));
        u8 attrs = readOAM(static_cast<u16>(i * 4 + 3));
        
        int rowInSprite = ly - spriteY;
        if (rowInSprite < 0 || rowInSprite >= 8) continue; // this sprite isn't on this scanline
        
        bool flipY = attrs & 0x40;
        int actualRow = flipY ? (7 - rowInSprite) : rowInSprite;
        
        u16 rowAddr = static_cast<u16>(0x8000 + tileIndex * 16 + actualRow * 2);
        u8 lowByte = readVRAM(rowAddr);
        u8 highByte = readVRAM(static_cast<u16>(rowAddr + 1));
        
        for (int px = 0; px < 8; ++px) {
            bool flipX = attrs & 0x20;
            int actualPx = flipX ? (7 - px) : px;
            u8 colorIndex = decodePixel(lowByte, highByte, actualPx);
            if (colorIndex == 0) continue; // color 0 is always transparent for sprites
            
            int screenX = spriteX + px;
            if (screenX < 0 || screenX >= kScreenWidth) continue;
            framebuffer[static_cast<u16>(ly * kScreenWidth + screenX)] = colorIndex;
        }
    }
}

void Ppu::renderScanline() {
    if (ly >= kScreenHeight) return;
    
    for (int x = 0; x < kScreenWidth; ++x) {
        u8 bgX = static_cast<u8>(x + scx);
        u8 bgY = static_cast<u8>(ly + scy);
        int tileCol = bgX / 8;
        int tileRow = bgY / 8;
        int pixelX  = bgX % 8;
        int pixelY  = bgY % 8;
        u16 mapIndex = static_cast<u16>(tileRow * 32 + tileCol);
        u8 tileIndex = readVRAM(static_cast<u16>(getBgMapBase() + mapIndex));

        u16 tileAddr;
        if (lcdc & 0x10) {
            tileAddr = static_cast<u16>(0x8000 + tileIndex * 16);
        } else {
            s8 signedIndex = static_cast<s8>(tileIndex);
            tileAddr = static_cast<u16>(0x9000 + signedIndex * 16);
        }
        // tile layout: [1,2],...,[1,2] (x8)
        u16 rowAddr = static_cast<u16>(tileAddr + pixelY * 2);
        u8 lowByte  = readVRAM(rowAddr);
        u8 highByte = readVRAM(static_cast<u16>(rowAddr + 1));
        u8 colorIndex = decodePixel(lowByte, highByte, pixelX);
        u8 shade = static_cast<u8>((bgp >> (colorIndex * 2)) & 0x03);
        framebuffer[static_cast<u16>(ly * kScreenWidth + x)] = shade;
    }
    renderSprites();
}

void Ppu::advanceScanline() {
    renderScanline();
    ++ly;
    if (ly >= 154) ly = 0;

    if (ly == lyc && statInterruptOnLycMatch) {
        lycInterruptRequested = true;
    }
}

void Ppu::tick(int cycles) {
    if (!(lcdc & 0x80)) return;
    
    scanlineCycles += cycles;
    if (scanlineCycles >= kCyclesPerScanline) {
        scanlineCycles -= kCyclesPerScanline;
        advanceScanline();
    }
}

}  // namespace gb
