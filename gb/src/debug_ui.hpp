#pragma once
#include "core/bus.hpp"
#include "core/cpu.hpp"
#include "core/time_keeper.hpp"

#include <SDL3/SDL.h>

namespace gb {

// Dear ImGui-based debug panel (CPU/PPU state, VRAM tile viewer, background map viewer,
// pause/step controls), docked as a fixed-width strip along the right edge of the window
// so it never overlaps the game view. Sections are collapsible so it stays uncluttered.
class DebugUi {
public:
    static constexpr int kPanelWidth = 420;

    void init(SDL_Window* window, SDL_Renderer* renderer);
    void shutdown();

    void processEvent(const SDL_Event& event);

    bool visible = true;
    void toggleVisible() { visible = !visible; }

    // Renders ImGui's draw data into `renderer`. Caller is responsible for
    // SDL_RenderClear/RenderTexture(game framebuffer) before this and SDL_RenderPresent after.
    void draw(Bus& bus, Cpu& cpu, TimeKeeper& timeKeeper, SDL_Renderer* renderer);

    // Set by the "Step" button while paused; main.cpp should consume this once per loop
    // iteration and, if true, execute exactly one CPU instruction.
    bool consumeStepRequest() {
        bool requested = stepRequested;
        stepRequested = false;
        return requested;
    }

private:
    SDL_Window* window = nullptr;
    SDL_Texture* tileTexture = nullptr;  // 16x24 tiles of 8x8px = 128x192
    SDL_Texture* bgMapTexture = nullptr; // 32x32 tiles of 8x8px = 256x256
    bool stepRequested = false;

    void drawCpuSection(const Cpu& cpu, TimeKeeper& timeKeeper);
    void drawPpuSection(const Ppu& ppu);
    void drawTileSection(const Ppu& ppu);
    void drawBgMapSection(const Ppu& ppu);
};

} // namespace gb
