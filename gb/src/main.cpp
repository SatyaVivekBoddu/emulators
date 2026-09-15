#include "core/bus.hpp"
#include "core/cartridge.hpp"
#include "core/cpu.hpp"
#include "core/sample_buffer.hpp"

#include <SDL3/SDL.h>
#include <cstdio>

namespace {
constexpr int kScale = 4;
constexpr double kCyclesPerFrame = 70224.0;
constexpr double kClockHz = 4194304.0;
constexpr double kFrameDurationMs = (kCyclesPerFrame / kClockHz) * 1000.0; // ≈ 16.74 ms


SDL_Color shadeFor(gb::u8 colorIndex) {
    switch (colorIndex) {
        case 0: return SDL_Color{224, 248, 208, 255}; // lightest
        case 1: return SDL_Color{136, 192, 112, 255};
        case 2: return SDL_Color{52, 104, 86, 255};
        default: return SDL_Color{8, 24, 32, 255};    // darkest
    }
}

void audioCallback(void* userdata, SDL_AudioStream* stream, int additionalAmount, int totalAmount) {
    auto* buf = static_cast<gb::SampleBuffer*>(userdata);
    constexpr int kMaxChunk = 512;
    float chunk[kMaxChunk];
    while (additionalAmount > 0) {
        int samplesToWrite = std::min(additionalAmount / static_cast<int>(sizeof(float)), kMaxChunk);
        for (int i = 0; i < samplesToWrite; ++i) {
            chunk[i] = buf->pop();
        }
        SDL_PutAudioStreamData(stream, chunk, samplesToWrite * static_cast<int>(sizeof(float)));
        additionalAmount -= samplesToWrite * static_cast<int>(sizeof(float));
    }
}
} // namespace

Uint64 lastFrameTime = SDL_GetTicks();


int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("Usage: %s <rom_path>\n", argv[0]);
        return 1;
    }

    gb::Cartridge cartridge;
    if (!cartridge.load(argv[1])) {
        std::printf("Failed to load ROM: %s\n", argv[1]);
        return 1;
    }
    gb::SampleBuffer sampleBuffer;

    gb::Bus bus(cartridge);
    gb::Cpu cpu(bus);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        std::printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    bus.getApu().setSampleBuffer(&sampleBuffer);

    SDL_AudioSpec spec{};
    spec.freq = 44100;
    spec.format = SDL_AUDIO_F32;
    spec.channels = 2;
    SDL_AudioStream* audioStream = SDL_OpenAudioDeviceStream(
    SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, audioCallback, &sampleBuffer);
    SDL_ResumeAudioStreamDevice(audioStream);
    if (!audioStream) {
        std::printf("SDL_OpenAudioDeviceStream failed: %s\n", SDL_GetError());
    }

    SDL_Window* window = SDL_CreateWindow(
        "Game Boy Emulator",
        gb::kScreenWidth * kScale,
        gb::kScreenHeight * kScale,
        0);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    SDL_Texture* texture = SDL_CreateTexture(
        renderer,
        SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_STREAMING,
        gb::kScreenWidth, gb::kScreenHeight);

    bool running = true;
    gb::u8 prevLy = 0;

    while (running) {
        int cycles = cpu.step();
        bus.tick(cycles);

        gb::u8 currentLy = bus.getPpu().ly;
        if (currentLy < prevLy) { // full frame just finished
            // RENDER
            void* pixels = nullptr;
            int pitch = 0;
            SDL_LockTexture(texture, nullptr, &pixels, &pitch);

            auto* dst = static_cast<uint32_t*>(pixels);
            const auto& fb = bus.getPpu().framebuffer;
            for (int y = 0; y < gb::kScreenHeight; ++y) {
                for (int x = 0; x < gb::kScreenWidth; ++x) {
                    SDL_Color c = shadeFor(fb[static_cast<size_t>(y * gb::kScreenWidth + x)]);
                    dst[y * (pitch / 4) + x] =
                        static_cast<uint32_t>((c.r << 24) | (c.g << 16) | (c.b << 8) | c.a);
                }
            }
            SDL_UnlockTexture(texture);

            SDL_RenderClear(renderer);
            SDL_RenderTexture(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
            // TIMING
            Uint64 now = SDL_GetTicks();
            Uint64 elapsed = now - lastFrameTime;
            if (elapsed < static_cast<Uint64>(kFrameDurationMs)) {
                SDL_Delay(static_cast<Uint32>(kFrameDurationMs - elapsed));
            }
            lastFrameTime = SDL_GetTicks();
            // BUTTONPOLL
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                if (event.type == SDL_EVENT_QUIT) running = false;
                if (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) {
                    bool pressed = (event.type == SDL_EVENT_KEY_DOWN);
                    gb::u8 bit = 0;
                    gb::u8* group = nullptr;
                    switch (event.key.key) {
                        case SDLK_RIGHT: group = &bus.getJoypad().directionState; bit = 0x01; break;
                        case SDLK_LEFT: group = &bus.getJoypad().directionState; bit = 0x02; break;
                        case SDLK_UP: group = &bus.getJoypad().directionState; bit = 0x04; break;
                        case SDLK_DOWN: group = &bus.getJoypad().directionState; bit = 0x08; break;
                        case SDLK_Z: group = &bus.getJoypad().actionState; bit = 0x01; break; // A
                        case SDLK_X: group = &bus.getJoypad().actionState; bit = 0x02; break; // B
                        case SDLK_BACKSPACE: group = &bus.getJoypad().actionState; bit = 0x04; break; // Select
                        case SDLK_RETURN: group = &bus.getJoypad().actionState; bit = 0x08; break; // Start
                    }
                    if (group) {
                        if (pressed) *group &= static_cast<gb::u8>(~bit); // pressed = bit cleared (active-low)
                        else *group |= bit; // released = bit set
                    }
                }

            }
        }
        prevLy = currentLy;
    }

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}