#pragma once
#include "bus.hpp"
#include "types.hpp"

namespace gb {

// A, F, BC, DE, HL
struct RegisterPair { // BC, DE, HL
    u8 hi = 0;
    u8 lo = 0;

    u16 get() const { return static_cast<u16>((static_cast<u16>(hi) << 8) | lo); }
    void set(u16 value) {
        hi = static_cast<u8>(value >> 8);
        lo = static_cast<u8>(value & 0xFF);
    }
};

struct Flags {
    bool zero = false;
    bool subtract = false;
    bool halfCarry = false;
    bool carry = false;

    u8 toByte() const {
        return (zero ? 0x80 : 0) | (subtract ? 0x40 : 0) | (halfCarry ? 0x20 : 0) |
               (carry ? 0x10 : 0);
        // lower nibble is always 0, matching real hardware
    }
    void fromByte(u8 value) {
        zero = value & 0x80;
        subtract = value & 0x40;
        halfCarry = value & 0x20;
        carry = value & 0x10;
    }
};

class Cpu {

public:
    explicit Cpu(Bus& bus) : bus(bus) {} // explain a bit more detailed

    int step();

    u8 a = 0;
    Flags f;
    RegisterPair bc, de, hl;
    u16 pc = 0x0100; // Power on states
    u16 sp = 0xFFFE;
    u8 lastOpcode = 0x00;

    bool getIme() const { return imeFlag; }
    bool isHalted() const { return halted; }

private:
    Bus& bus;

    bool imeFlag = false;
    static constexpr u16 interruptVector[5] = {0x0040, 0x0048, 0x0050, 0x0058, 0x0060};
    bool halted = false;
    u8 fetch8();
    u16 fetch16();
    u8 readReg8(u8 index) const;
    void writeReg8(u8, u8);
    int executeLoadRegToReg(u8 oppcode);

    int executeArithmetic(u8 opcode);
    void addToA(u8 value, bool useCarry);
    void subFromA(u8 value, bool useCarry, bool storeResult);
    void andWithA(u8 value);
    void xorWithA(u8 value);
    void orWithA(u8 value);

    bool checkCondition(u8 index) const;

    u16 getAF() const;
    void setAF(u16 value);
    void push16(u16 value);
    u16 pop16();

    int executeLoad16Immediate(u8 opcode);
    int executeLoad8Immediate(u8 opcode);
    int executeIndirectA(u8 opcode);
    int incDecReg8(u8 opcode, bool isIncrement);
    int executeArithmeticImmediate(u8 opIndex);
    int incDecReg16(u8 opcode);
    void addToHL(u16 value);

    u8 applyRotateFlags(u8 result, u8 outBit);
    u8 rlc(u8 value);
    u8 rrc(u8 value);
    u8 rl(u8 value);
    u8 rr(u8 value);
    u8 sla(u8 value);
    u8 sra(u8 value);
    u8 srl(u8 value);
    u8 swap(u8 value);
    void testBit(u8 value, u8 bitNum);
    u8 resetBit(u8 value, u8 bitNum);
    u8 setBit(u8 value, u8 bitNum);
    int executeCbOpcode(u8 cbOpcode);
    bool handleInterrupts();
};

} // namespace gb
