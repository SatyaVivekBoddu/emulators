#include "core/bus.hpp"
#include "core/cartridge.hpp"
#include "core/cpu.hpp"
#include "core/ppu.hpp"
#include "core/sample_buffer.hpp"
#include "core/time_keeper.hpp"
#include "core/types.hpp"
#include "debug_ui.hpp"

#include <SDL3/SDL.h>
#include <cstdio>
#include <imgui.h>
#include <print>

namespace {
constexpr int kScale = 4;

SDL_Color shadeFor(gb::u8 colorIndex) {
    switch (colorIndex) {
        case 0:
            return SDL_Color{224, 248, 208, 255}; // lightest
        case 1:
            return SDL_Color{136, 192, 112, 255};
        case 2:
            return SDL_Color{52, 104, 86, 255};
        default:
            return SDL_Color{8, 24, 32, 255}; // darkest
    }
}

void audioCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int) {
    auto* buf = static_cast<gb::SampleBuffer*>(userdata);
    constexpr int kMaxChunk = 512;
    float chunk[kMaxChunk];
    while (additionalAmount > 0) {
        int samplesToWrite =
            std::min(additionalAmount / static_cast<int>(sizeof(float)), kMaxChunk);
        for (int i = 0; i < samplesToWrite; ++i) {
            chunk[i] = buf->pop();
        }
        SDL_PutAudioStreamData(stream, chunk, samplesToWrite * static_cast<int>(sizeof(float)));
        additionalAmount -= samplesToWrite * static_cast<int>(sizeof(float));
    }
}

void pollInput(gb::Bus& bus, gb::DebugUi& debugUi, bool& running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        debugUi.processEvent(event);

        if (event.type == SDL_EVENT_QUIT) { // does this include ctrl+c
            running = false;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_F1 && !event.key.repeat) {
            debugUi.toggleVisible();
        }
        if (ImGui::GetIO().WantTextInput) {
            continue; // a debug-UI text field has focus — don't feed this key to the joypad
        }
        if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
            gb::u8 bit = 0;
            gb::u8* group = nullptr;
            switch (event.key.key) {
                case SDLK_RIGHT:
                    group = &bus.getJoypad().directionState;
                    bit = 0x01;
                    break;
                case SDLK_LEFT:
                    group = &bus.getJoypad().directionState;
                    bit = 0x02;
                    break;
                case SDLK_UP:
                    group = &bus.getJoypad().directionState;
                    bit = 0x04;
                    break;
                case SDLK_DOWN:
                    group = &bus.getJoypad().directionState;
                    bit = 0x08;
                    break;
                case SDLK_Z:
                    group = &bus.getJoypad().actionState;
                    bit = 0x01;
                    break; // A
                case SDLK_X:
                    group = &bus.getJoypad().actionState;
                    bit = 0x02;
                    break; // B
                case SDLK_BACKSPACE:
                    group = &bus.getJoypad().actionState;
                    bit = 0x04;
                    break; // Select
                case SDLK_RETURN:
                    group = &bus.getJoypad().actionState;
                    bit = 0x08;
                    break; // Start
            }
            if (group != nullptr) {
                if (event.type == SDL_EVENT_KEY_DOWN) {  // pressed
                    *group &= static_cast<gb::u8>(~bit); // pressed = bit cleared (active-low)
                } else {
                    *group |= bit; // released = bit set
                }
            }
        }
    }
}

// Uploads the PPU's completed framebuffer into `texture`. Call only when a new frame is
// ready — the actual clear/present happens every loop iteration in main() so the window
// (and the debug UI) stay responsive even while paused or between GB frames.
void updateGbTexture(const gb::Ppu& ppu, SDL_Texture* texture) {
    void* pixels = nullptr;
    int pitch = 0;
    SDL_LockTexture(texture, nullptr, &pixels, &pitch);

    auto* dst = static_cast<gb::u32*>(pixels);
    const auto& fb = ppu.framebuffer;
    for (int y = 0; y < gb::kScreenHeight; ++y) {
        for (int x = 0; x < gb::kScreenWidth; ++x) {
            SDL_Color c = shadeFor(fb[static_cast<size_t>(y * gb::kScreenWidth + x)]);
            dst[y * (pitch / 4) + x] =
                static_cast<gb::u32>((c.r << 24) | (c.g << 16) | (c.b << 8) | c.a);
        }
    }
    SDL_UnlockTexture(texture);
}

} // namespace

Uint64 lastFrameTime = SDL_GetTicks();

int main(int argc, char** argv) {
    if (argc < 2) {
        std::println("Usage: {} <rom_path>", argv[0]);
        return 1;
    }

    gb::Cartridge cartridge;
    if (!cartridge.load(argv[1])) {
        std::println("Failed to load ROM: %s\n", argv[1]);
        return 1;
    }
    gb::Scheduler scheduler;
    gb::Bus bus(cartridge, scheduler);
    gb::Cpu cpu(bus);
    bus.start(); // arms the PPU/timer/APU scheduler chain — without this, advanceScanline()
                 // never fires and the PPU stays frozen at LY=0 forever.

    gb::SampleBuffer sampleBuffer;
    gb::TimeKeeper tk;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::println("SDL_Init failed: {}", SDL_GetError());
        return 1;
    }
    bus.getApu().setSampleBuffer(&sampleBuffer);

    SDL_AudioSpec spec{};
    spec.freq = 44100;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    SDL_AudioStream* audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                             &spec, audioCallback, &sampleBuffer);
    if (audioStream == nullptr) {
        std::println("SDL_OpenAudioDeviceStream failed: {}", SDL_GetError());
    } else {
        SDL_ResumeAudioStreamDevice(audioStream);
    }

    constexpr int kGameWidth = gb::kScreenWidth * kScale;
    constexpr int kGameHeight = gb::kScreenHeight * kScale;
    SDL_Window* window =
        SDL_CreateWindow("Game Boy Emulator", kGameWidth + gb::DebugUi::kPanelWidth, kGameHeight,
                         SDL_WINDOW_RESIZABLE);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_Texture* texture =
        SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_STREAMING,
                          gb::kScreenWidth, gb::kScreenHeight);

    gb::DebugUi debugUi;
    debugUi.init(window, renderer);

    tk.reset(SDL_GetTicks());

    bool running = true;
    while (running) {
        if (debugUi.consumeStepRequest()) {
            gb::Cycle executed = scheduler.now() + cpu.step();
            bus.collectInterrupts();
            scheduler.advanceTo(executed);
        }

        if (!tk.isPaused()) {
            gb::Cycle target = tk.targetCycles(SDL_GetTicks(), scheduler.now());
            while (scheduler.now() < target) {
                gb::Cycle budget = std::min(scheduler.cycleBudget(), target - scheduler.now());
                gb::Cycle batchEnd = scheduler.now() + budget;

                gb::Cycle executed = scheduler.now();
                while (executed < batchEnd) {
                    executed += cpu.step();
                    bus.collectInterrupts();
                }
                scheduler.advanceTo(executed);
            }
        }

        if (bus.getPpu().consumeFrameReady()) {
            updateGbTexture(bus.getPpu(), texture);
        }

        // Present every loop tick (not just when a new GB frame is ready) so the window
        // stays responsive and the debug UI keeps updating even while paused.
        SDL_RenderClear(renderer);
        SDL_FRect gameDst{0, 0, static_cast<float>(kGameWidth), static_cast<float>(kGameHeight)};
        SDL_RenderTexture(renderer, texture, nullptr, &gameDst);
        debugUi.draw(bus, cpu, tk, renderer);
        SDL_RenderPresent(renderer);

        pollInput(bus, debugUi, running);
        SDL_Delay(1);
    }

    debugUi.shutdown();
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}