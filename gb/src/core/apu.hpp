#pragma once
#include "sample_buffer.hpp"
#include "scheduler.hpp"
#include "types.hpp"
#include <array>

namespace gb {

class SquareWaveChannel {

public:
    // The duty position is a pure function of how long the channel has been running.
    u8 currentSample(Cycle now) const {
        int period = (2048 - frequencyValue) * 4;
        if (period <= 0) {
            return 0;
        }
        Cycle elapsed = now - lastTriggerCycle;
        if (elapsed < 0) {
            return 0;
        }
        int steps = static_cast<int>((elapsed / period) % 8);
        bool high = (wavePatterns[waveCycle] >> steps) & 1;
        return high ? volume : 0;
    }

    void writeFrequencyLow(u8 value, Cycle now) {
        rebaseTo(now);
        frequencyValue = static_cast<u16>((frequencyValue & 0x0700) | value);
    }
    void writeFrequencyHigh(u8 value, Cycle now) {
        rebaseTo(now);
        frequencyValue = static_cast<u16>((frequencyValue & 0x00FF) | ((value & 0x07) << 8));
    }
    void writeWaveAndLength(u8 value) { waveCycle = (value >> 6) & 0x03; }

    void writeEnvelope(u8 value) {
        envelopeInitialVolume = (value >> 4) & 0x0F;
        envelopeIncreasing = ((value & 0x08) != 0);
        envelopePeriod = value & 0x07;
        volume = envelopeInitialVolume;
    }

    void tickEnvelope() {
        if (envelopePeriod == 0) {
            return;
        }
        if (envelopeTimer > 0) {
            --envelopeTimer;
            return;
        }
        envelopeTimer = envelopePeriod;
        if (envelopeIncreasing && volume < 15) {
            ++volume;
        } else if (!envelopeIncreasing && volume > 0) {
            --volume;
        }
    }
    void trigger(Cycle now) {
        lastTriggerCycle = now;
        volume = envelopeInitialVolume;
        envelopeTimer = envelopePeriod;
    }

private:
    void rebaseTo(Cycle now) { // TODO: explain better
        int period = (2048 - frequencyValue) * 4;
        if (period <= 0) { // TODO: explain this case
            lastTriggerCycle = now;
            return;
        }
        Cycle elapsed = now - lastTriggerCycle;
        if (elapsed < 0) {
            lastTriggerCycle = now;
            return;
        }
        Cycle phaseWithinStep = elapsed % period;
        int step = static_cast<int>((elapsed / period) % 8);
        lastTriggerCycle =
            now - (static_cast<Cycle>(step) * period + phaseWithinStep); // TODO: explain better
    }

    Cycle lastTriggerCycle = 0;
    u16 frequencyValue = 0;
    u8 waveCycle = 2;
    u8 volume = 0;
    u8 envelopeInitialVolume = 0;
    bool envelopeIncreasing = false;
    u8 envelopePeriod = 0;
    int envelopeTimer = 0;

    static constexpr std::array<u8, 4> wavePatterns = {0b00000001, 0b10000001, 0b10000111,
                                                       0b01111110};
};

class Apu {
public:
    explicit Apu(Scheduler& scheduler) : scheduler(scheduler) {}

    void start() {
        scheduleNextSample(scheduler.now());
        scheduleNextEnvelopeTick(scheduler.now());
    }

    void setSampleBuffer(SampleBuffer* buf) { sampleBuffer = buf; }

    void writeNR50(u8 value) {
        leftMasterVolume = (value >> 4) & 0x07;
        rightMasterVolume = value & 0x07;
    }
    void writeNR51(u8 value) { channelMix = value; }

    Cycle now() const { return scheduler.now(); }

    SquareWaveChannel channel1;
    SquareWaveChannel channel2;

    long long totalSamplesPushed = 0;

private:
    static constexpr double kCyclesPerSample = kClockHz / 44100.0;
    static constexpr double kCyclesPerEnvelopeTick = kClockHz / 64.0;
    Scheduler& scheduler;
    double sampleFraction = 0.0;
    double envelopeFraction = 0.0;

    void scheduleNextSample(Cycle from) {
        sampleFraction += kCyclesPerSample;
        Cycle delta = static_cast<Cycle>(sampleFraction);
        sampleFraction -= static_cast<double>(delta);
        if (delta <= 0) { // huh how?
            delta = 1;
        }
        scheduler.schedule(from + delta, [this](Cycle firedAt, Cycle) {
            generatePush(firedAt);
            scheduleNextSample(firedAt);
        });
    }

    void scheduleNextEnvelopeTick(Cycle from) {
        envelopeFraction += kCyclesPerEnvelopeTick;
        Cycle delta = static_cast<Cycle>(envelopeFraction);
        envelopeFraction -= static_cast<double>(delta);
        if (delta <= 0) {
            delta = 1;
        }
        scheduler.schedule(from + delta, [this](Cycle firedAt, Cycle) {
            channel1.tickEnvelope();
            channel2.tickEnvelope();
            scheduleNextEnvelopeTick(firedAt);
        });
    }
    SampleBuffer* sampleBuffer = nullptr;

    u8 leftMasterVolume = 7;
    u8 rightMasterVolume = 7;
    u8 channelMix = 0xFF;
    // bit 7: channel 4 -> left    bit 3: channel 4 -> right
    // bit 6: channel 3 -> left    bit 2: channel 3 -> right
    // bit 5: channel 2 -> left    bit 1: channel 2 -> right
    // bit 4: channel 1 -> left    bit 0: channel 1 -> right

    static float applyMasterVolume(float mixed, u8 masterVolume) {
        return mixed * static_cast<float>(masterVolume) / 7.0F;
    }
    void generatePush(Cycle at) {
        if (sampleBuffer == nullptr) {
            return;
        }

        float left = 0.0F;
        float right = 0.0F;
        if (channelMix & 0x10)
            left += channel1.currentSample(at);
        if (channelMix & 0x20)
            left += channel2.currentSample(at);
        if (channelMix & 0x01)
            right += channel1.currentSample(at);
        if (channelMix & 0x02)
            right += channel2.currentSample(at);

        left = left / 2.0F / 15.0F;
        right = right / 2.0F / 15.0F;

        sampleBuffer->push(applyMasterVolume(left, leftMasterVolume));
        sampleBuffer->push(applyMasterVolume(right, rightMasterVolume));
        ++totalSamplesPushed;
    }
};

} // namespace gb