#include "debug_ui.hpp"

#include "core/ppu.hpp"
#include "core/types.hpp"

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

#include <array>

namespace gb {

namespace {

constexpr int kTileCols = 16;
constexpr int kTileRows = 24; // 16 * 24 = 384 tiles
constexpr int kTilePx = 8;
constexpr int kTileScale = 2;
constexpr int kTileTexW = kTileCols * kTilePx; // 128
constexpr int kTileTexH = kTileRows * kTilePx; // 192
constexpr int kBgMapTiles = 32;
constexpr int kBgMapScale = 1;
constexpr int kBgMapTexW = kBgMapTiles * kTilePx; // 256
constexpr int kBgMapTexH = kBgMapTiles * kTilePx; // 256

u32 shadeToRgba(u8 shade) {
    // Same four-tone palette as the main display (shadeFor in main.cpp), duplicated here
    // so the debug views read as the same "colors" the player sees.
    switch (shade) {
        case 0:
            return 0xE0F8D0FFu; // lightest
        case 1:
            return 0x88C070FFu;
        case 2:
            return 0x346856FFu;
        default:
            return 0x081820FFu; // darkest
    }
}

u8 decodeColorIndex(u8 lowByte, u8 highByte, int pixelX) {
    int bit = 7 - pixelX;
    u8 lowBit = (lowByte >> bit) & 0x01;
    u8 highBit = (highByte >> bit) & 0x01;
    return static_cast<u8>((highBit << 1) | lowBit);
}

void paintTilePixels(std::array<u32, kTileTexW * kTileTexH>& pixels, const Ppu& ppu, int tileX,
                     int tileY, u16 tileDataAddr) {
    for (int row = 0; row < kTilePx; ++row) {
        u16 rowAddr = static_cast<u16>(tileDataAddr + row * 2);
        u8 lowByte = ppu.readVRAM(rowAddr);
        u8 highByte = ppu.readVRAM(static_cast<u16>(rowAddr + 1));
        for (int col = 0; col < kTilePx; ++col) {
            u8 colorIndex = decodeColorIndex(lowByte, highByte, col);
            u8 shade = static_cast<u8>((ppu.bgp >> (colorIndex * 2)) & 0x03);
            int px = tileX * kTilePx + col;
            int py = tileY * kTilePx + row;
            pixels[static_cast<size_t>(py * kTileTexW + px)] = shadeToRgba(shade);
        }
    }
}

} // namespace

void DebugUi::init(SDL_Window* sdlWindow, SDL_Renderer* renderer) {
    window = sdlWindow;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    tileTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
                                    kTileTexW, kTileTexH);
    bgMapTexture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888,
                                     SDL_TEXTUREACCESS_STREAMING, kBgMapTexW, kBgMapTexH);
}

void DebugUi::shutdown() {
    SDL_DestroyTexture(tileTexture);
    SDL_DestroyTexture(bgMapTexture);
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
}

void DebugUi::processEvent(const SDL_Event& event) {
    ImGui_ImplSDL3_ProcessEvent(&event);
}

void DebugUi::drawCpuSection(const Cpu& cpu, TimeKeeper& timeKeeper) {
    bool paused = timeKeeper.isPaused();
    if (ImGui::Checkbox("Paused", &paused)) {
        timeKeeper.setPaused(paused);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!paused);
    if (ImGui::Button("Step")) {
        stepRequested = true;
    }
    ImGui::EndDisabled();

    if (ImGui::BeginTable("registers", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_SizingFixedFit)) {
        auto row = [](const char* name, u16 value) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(name);
            ImGui::TableNextColumn();
            ImGui::Text("0x%04X", value);
        };
        row("AF", static_cast<u16>((cpu.a << 8) | cpu.f.toByte()));
        row("BC", cpu.bc.get());
        row("DE", cpu.de.get());
        row("HL", cpu.hl.get());
        row("SP", cpu.sp);
        row("PC", cpu.pc);
        row("OP", cpu.lastOpcode);
        ImGui::EndTable();
    }

    ImGui::Text("Flags:  Z=%d  N=%d  H=%d  C=%d", cpu.f.zero, cpu.f.subtract, cpu.f.halfCarry,
                cpu.f.carry);
    ImGui::Text("IME=%d  HALT=%d", cpu.getIme(), cpu.isHalted());
}

void DebugUi::drawPpuSection(const Ppu& ppu) {
    static constexpr const char* kModeNames[4] = {"0 HBlank", "1 VBlank", "2 OAM", "3 Draw"};
    ImGui::Text("LY=%d  LYC=%d  Mode=%s", ppu.ly, ppu.lyc, kModeNames[ppu.getMode() & 0x03]);
    ImGui::Text("SCX=%d  SCY=%d  BGP=0x%02X", ppu.scx, ppu.scy, ppu.bgp);
    ImGui::Text("Frame count: %d", ppu.frameCount);

    ImGui::Spacing();
    ImGui::Text("LCDC = 0x%02X", ppu.lcdc);
    ImGui::BulletText("LCD enable:        %d", (ppu.lcdc & 0x80) != 0);
    ImGui::BulletText("Window map 9C00:   %d", (ppu.lcdc & 0x40) != 0);
    ImGui::BulletText("Window enable:     %d", (ppu.lcdc & 0x20) != 0);
    ImGui::BulletText("BG/win tiles 8000: %d", (ppu.lcdc & 0x10) != 0);
    ImGui::BulletText("BG map 9C00:       %d", (ppu.lcdc & 0x08) != 0);
    ImGui::BulletText("Obj size 8x16:     %d", (ppu.lcdc & 0x04) != 0);
    ImGui::BulletText("Obj enable:        %d", (ppu.lcdc & 0x02) != 0);
    ImGui::BulletText("BG/win enable:     %d", (ppu.lcdc & 0x01) != 0);

    ImGui::Spacing();
    ImGui::Text("STAT = 0x%02X", ppu.readStat());
    ImGui::BulletText("LYC=LY int enable: %d", ppu.statInterruptOnLycMatch);
    ImGui::BulletText("Mode2 int enable:  %d", ppu.statInterruptOnMode2);
    ImGui::BulletText("Mode1 int enable:  %d", ppu.statInterruptOnMode1);
    ImGui::BulletText("Mode0 int enable:  %d", ppu.statInterruptOnMode0);
    ImGui::BulletText("LYC match pending: %d", ppu.lycInterruptRequested);
    ImGui::BulletText("VBlank pending:    %d", ppu.vblankRequested);
    ImGui::BulletText("scanlineStartCycle:    %lld", ppu.scanlineStartCycle);
}

void DebugUi::drawTileSection(const Ppu& ppu) {
    static std::array<u32, kTileTexW * kTileTexH> pixels{};
    for (int tileY = 0; tileY < kTileRows; ++tileY) {
        for (int tileX = 0; tileX < kTileCols; ++tileX) {
            int tileIndex = tileY * kTileCols + tileX;
            u16 tileDataAddr = static_cast<u16>(0x8000 + tileIndex * 16);
            paintTilePixels(pixels, ppu, tileX, tileY, tileDataAddr);
        }
    }
    SDL_UpdateTexture(tileTexture, nullptr, pixels.data(),
                      kTileTexW * static_cast<int>(sizeof(u32)));

    ImGui::TextUnformatted("0x8000-0x97FF, 384 tiles");
    ImGui::Image(reinterpret_cast<ImTextureID>(tileTexture),
                 ImVec2(kTileTexW * kTileScale, kTileTexH * kTileScale));
}

void DebugUi::drawBgMapSection(const Ppu& ppu) {
    static std::array<u32, kBgMapTexW * kBgMapTexH> pixels{};
    u16 mapBase = (ppu.lcdc & 0x08) ? 0x9C00 : 0x9800;
    bool unsignedAddressing = (ppu.lcdc & 0x10) != 0;

    for (int mapRow = 0; mapRow < kBgMapTiles; ++mapRow) {
        for (int mapCol = 0; mapCol < kBgMapTiles; ++mapCol) {
            u16 mapIndex = static_cast<u16>(mapRow * kBgMapTiles + mapCol);
            u8 tileIndexByte = ppu.readVRAM(static_cast<u16>(mapBase + mapIndex));

            u16 tileDataAddr;
            if (unsignedAddressing) {
                tileDataAddr = static_cast<u16>(0x8000 + tileIndexByte * 16);
            } else {
                s8 signedIndex = static_cast<s8>(tileIndexByte);
                tileDataAddr = static_cast<u16>(0x9000 + signedIndex * 16);
            }

            for (int row = 0; row < kTilePx; ++row) {
                u16 rowAddr = static_cast<u16>(tileDataAddr + row * 2);
                u8 lowByte = ppu.readVRAM(rowAddr);
                u8 highByte = ppu.readVRAM(static_cast<u16>(rowAddr + 1));
                for (int col = 0; col < kTilePx; ++col) {
                    u8 colorIndex = decodeColorIndex(lowByte, highByte, col);
                    u8 shade = static_cast<u8>((ppu.bgp >> (colorIndex * 2)) & 0x03);
                    int px = mapCol * kTilePx + col;
                    int py = mapRow * kTilePx + row;
                    pixels[static_cast<size_t>(py * kBgMapTexW + px)] = shadeToRgba(shade);
                }
            }
        }
    }
    SDL_UpdateTexture(bgMapTexture, nullptr, pixels.data(),
                      kBgMapTexW * static_cast<int>(sizeof(u32)));

    ImGui::Text("Map base: 0x%04X   Tile data: %s", mapBase,
                unsignedAddressing ? "0x8000 (unsigned)" : "0x9000 (signed)");
    ImGui::Image(reinterpret_cast<ImTextureID>(bgMapTexture),
                 ImVec2(kBgMapTexW * kBgMapScale, kBgMapTexH * kBgMapScale));
}

void DebugUi::draw(Bus& bus, Cpu& cpu, TimeKeeper& timeKeeper, SDL_Renderer* renderer) {
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();

    if (visible) {
        int windowW = 0;
        int windowH = 0;
        SDL_GetWindowSize(window, &windowW, &windowH);

        // A fixed-width strip docked to the right edge, full height — the game view (drawn
        // separately, pinned to the top-left) never has this panel over it.
        ImGui::SetNextWindowPos(ImVec2(static_cast<float>(windowW - kPanelWidth), 0));
        ImGui::SetNextWindowSize(
            ImVec2(static_cast<float>(kPanelWidth), static_cast<float>(windowH)));
        ImGuiWindowFlags flags = ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar;
        ImGui::Begin("Debug", nullptr, flags);
        ImGui::TextUnformatted("Debug (F1 to hide)");
        ImGui::Separator();

        if (ImGui::CollapsingHeader("CPU", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawCpuSection(cpu, timeKeeper);
        }
        if (ImGui::CollapsingHeader("PPU", ImGuiTreeNodeFlags_DefaultOpen)) {
            drawPpuSection(bus.getPpu());
        }
        if (ImGui::CollapsingHeader("VRAM Tiles")) {
            drawTileSection(bus.getPpu());
        }
        if (ImGui::CollapsingHeader("Background Map")) {
            drawBgMapSection(bus.getPpu());
        }

        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
}

} // namespace gb
