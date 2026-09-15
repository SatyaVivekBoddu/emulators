#pragma once
#include "sample_buffer.hpp"
#include "types.hpp"
#include <cstdio>

namespace gb {

class SquareWaveChannel {

public:
    void tick(int cycles) {

        timerCounter -= cycles;
        while (timerCounter <= 0) {
            int period = (2048 - frequencyValue) * 4;
            timerCounter += (period > 0 ? period : 1);
            waveStep = (waveStep + 1) % 8;
        }
    }
    u8 currentSample() const {
        bool high = (wavePatterns[waveCycle] >> waveStep) & 1;
        return high ? volume : 0;
    }
    void writeFrequencyLow(u8 value) {
        frequencyValue = static_cast<u16>((frequencyValue & 0x0700) | value);
    }
    void writeFrequencyHigh(u8 value) {
        frequencyValue = static_cast<u16>((frequencyValue & 0x00FF) | ((value & 0x07) << 8));
    }
    void writeWaveAndLength(u8 value) { waveCycle = (value >> 6) & 0x03; }

    void writeEnvelope(u8 value) {
        envelopeInitialVolume = (value >> 4) & 0x0F;
        envelopeIncreasing = value & 0x08;
        envelopePeriod = value & 0x07;
        volume = envelopeInitialVolume;
    }

    void tickEnvelope() {
        if (envelopePeriod == 0)
            return;
        if (envelopeTimer > 0) {
            --envelopeTimer;
            return;
        }
        envelopeTimer = envelopePeriod;
        if (envelopeIncreasing && volume < 15)
            ++volume;
        else if (!envelopeIncreasing && volume > 0)
            --volume;
    }

private:
    int timerCounter = 1;
    int waveStep = 0;
    u16 frequencyValue = 0;
    u8 waveCycle = 2;

    u8 volume = 0;
    u8 envelopeInitialVolume = 0;
    bool envelopeIncreasing = false;
    u8 envelopePeriod = 0;
    int envelopeTimer = 0;
    static constexpr u8 wavePatterns[4] = {0b00000001, 0b10000001, 0b10000111, 0b01111110};
};

class Apu {
public:
    void setSampleBuffer(SampleBuffer* buf) { sampleBuffer = buf; }

    void writeNR50(u8 value) {
        leftMasterVolume = (value >> 4) & 0x07;
        rightMasterVolume = value & 0x07;
    }
    void writeNR51(u8 value) { channelMix = value; }

    void tick(int cycles) {
        channel1.tick(cycles);
        channel2.tick(cycles);

        sampleCycleCounter += cycles;
        while (sampleCycleCounter >= kCyclesPerSample) {
            sampleCycleCounter -= kCyclesPerSample;
            generatePush();
        }
        envelopeCycleCounter += cycles;
        while (envelopeCycleCounter >= kCyclesPerEnvelopeTick) {
            envelopeCycleCounter -= kCyclesPerEnvelopeTick;
            channel1.tickEnvelope();
            channel2.tickEnvelope();
        }
    }

    SquareWaveChannel channel1;
    SquareWaveChannel channel2;

private:
    SampleBuffer* sampleBuffer = nullptr;
    double sampleCycleCounter = 0.0;
    double envelopeCycleCounter = 0.0;
    static constexpr double kCyclesPerSample = 4194304.0 / 44100.0;
    static constexpr double kCyclesPerEnvelopeTick = 4194304.0 / 64.0;

    u8 leftMasterVolume = 7;
    u8 rightMasterVolume = 7;
    u8 channelMix = 0xFF;
    // bit 7: channel 4 -> left    bit 3: channel 4 -> right
    // bit 6: channel 3 -> left    bit 2: channel 3 -> right
    // bit 5: channel 2 -> left    bit 1: channel 2 -> right
    // bit 4: channel 1 -> left    bit 0: channel 1 -> right
    float applyMasterVolume(float mixed, u8 masterVolume) const {
        return mixed * (masterVolume / 7.0f);
    }
    void generatePush() {
        if (!sampleBuffer)
            return;

        float left = 0.0f;
        float right = 0.0f;
        if (channelMix & 0x10)
            left += channel1.currentSample();
        if (channelMix & 0x20)
            left += channel2.currentSample();
        if (channelMix & 0x01)
            right += channel1.currentSample();
        if (channelMix & 0x02)
            right += channel2.currentSample();

        left = left / 2.0f / 15.0f;
        right = right / 2.0f / 15.0f;

        sampleBuffer->push(applyMasterVolume(left, leftMasterVolume));
        sampleBuffer->push(applyMasterVolume(right, rightMasterVolume));
    }
};

} // namespace gb