#pragma once
#include <array>
#include <atomic>
#include <cstddef>

namespace gb {

class SampleBuffer {
public:
    void push(float sample) {
        size_t w = writePos.load(std::memory_order_relaxed);
        size_t nextW = (w + 1) % buffer.size();
        if (nextW == readPos.load(std::memory_order_acquire)) {
            return; // full — drop the sample rather than overwrite unread data
        }
        buffer[w] = sample;
        writePos.store(nextW, std::memory_order_release);
    }

    float pop() {
        size_t r = readPos.load(std::memory_order_relaxed);
        if (r == writePos.load(std::memory_order_acquire)) {
            return 0.0f; // empty
        }
        float s = buffer[r];
        readPos.store((r + 1) % buffer.size(), std::memory_order_release);
        return s;
    }

    size_t remaining() {
        size_t r = readPos.load(std::memory_order_acquire); // what would happen with relaxed?
        size_t w = writePos.load(std::memory_order_acquire);
        return (w >= r) ? (w - r) : (buffer.size() - r + w);
    }

private:
    std::array<float, 16384> buffer{};
    std::atomic<size_t> writePos{0};
    std::atomic<size_t> readPos{0};
};

} // namespace gb