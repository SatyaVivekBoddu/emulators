#include "cpu.hpp"

namespace gb {

u8 Cpu::fetch8() {
    u8 value = bus.read8(pc);
    ++pc;
    return value;
}

u16 Cpu::fetch16() {
    u8 low = fetch8();
    u8 high = fetch8();
    return static_cast<u16>((static_cast<u16>(high) << 8) | low);
}

u8 Cpu::readReg8(u8 index) const {
    switch (index) {
        case 0:
            return bc.hi;
        case 1:
            return bc.lo;
        case 2:
            return de.hi;
        case 3:
            return de.lo;
        case 4:
            return hl.hi;
        case 5:
            return hl.lo;
        case 6:
            return bus.read8(hl.get()); // memory through HL
        case 7:
            return a;
        default:
            return 0xFF; // unreachable — index is always 3 bits
    }
}

void Cpu::writeReg8(u8 index, u8 value) {
    switch (index) {
        case 0:
            bc.hi = value;
            return;
        case 1:
            bc.lo = value;
            return;
        case 2:
            de.hi = value;
            return;
        case 3:
            de.lo = value;
            return;
        case 4:
            hl.hi = value;
            return;
        case 5:
            hl.lo = value;
            return;
        case 6:
            bus.write8(hl.get(), value);
            return;
        case 7:
            a = value;
            return;
    }
}

int Cpu::executeLoadRegToReg(u8 opcode) {
    u8 destIndex = (opcode >> 3) & 0x07;
    u8 srcIndex = opcode & 0x07;

    u8 value = readReg8(srcIndex);
    writeReg8(destIndex, value);

    bool touchesMemory = (destIndex == 6) || (srcIndex == 6);
    return touchesMemory ? 8 : 4;
}

void Cpu::addToA(u8 value, bool useCarry) {
    u8 carryIn = (useCarry && f.carry) ? 1 : 0;
    u16 result = static_cast<u16>(a) + value + carryIn;

    bool halfCarry = ((a & 0x0F) + (value & 0x0F) + carryIn) > 0x0F;
    bool carry = result > 0xFF;

    a = static_cast<u8>(result);
    f.zero = (a == 0);
    f.subtract = false;
    f.halfCarry = halfCarry;
    f.carry = carry;
}

int Cpu::incDecReg8(u8 opcode, bool isIncrement) {
    u8 index = (opcode >> 3) & 0x07;
    u8 value = readReg8(index);
    u8 result = static_cast<u8>(value + (isIncrement ? 1 : -1));
    writeReg8(index, result);
    f.zero = (result == 0);
    f.subtract = !isIncrement;
    f.halfCarry = ((value & 0x0F) == (isIncrement ? 0x0F : 0x00));

    return (index == 6) ? 12 : 4;
}
int Cpu::incDecReg16(u8 opcode) {
    u8 index = (opcode >> 4) & 0x03;
    bool isIncrement = !(opcode & 0x08);
    switch (index) {
        case 0:
            bc.set(static_cast<u16>(bc.get() + (isIncrement ? 1 : -1)));
            break;
        case 1:
            de.set(static_cast<u16>(de.get() + (isIncrement ? 1 : -1)));
            break;
        case 2:
            hl.set(static_cast<u16>(hl.get() + (isIncrement ? 1 : -1)));
            break;
        case 3:
            sp = static_cast<u16>(sp + (isIncrement ? 1 : -1));
            break;
    }
    return 8;
}

void Cpu::subFromA(u8 value, bool useCarry, bool storeResult) {
    u8 carryIn = (useCarry && f.carry) ? 1 : 0;
    int result = static_cast<int>(a) - value - carryIn;

    bool halfCarry = (static_cast<int>(a & 0x0F) - static_cast<int>(value & 0x0F) - carryIn) < 0;
    bool carry = result < 0;

    u8 finalResult = static_cast<u8>(result);
    f.zero = (finalResult == 0);
    f.subtract = true;
    f.halfCarry = halfCarry;
    f.carry = carry;

    if (storeResult)
        a = finalResult;
}

void Cpu::andWithA(u8 value) {
    a &= value;
    f.zero = (a == 0);
    f.subtract = false;
    f.halfCarry = true; // AND always sets H, a fixed hardware behavior — not derived from the math
    f.carry = false;
}

void Cpu::xorWithA(u8 value) {
    a ^= value;
    f.zero = (a == 0);
    f.subtract = false;
    f.halfCarry = false;
    f.carry = false;
}

void Cpu::orWithA(u8 value) {
    a |= value;
    f.zero = (a == 0);
    f.subtract = false;
    f.halfCarry = false;
    f.carry = false;
}

int Cpu::executeArithmetic(u8 opcode) {
    u8 opIndex = (opcode >> 3) & 0x07;
    u8 srcIndex = opcode & 0x07;
    u8 value = readReg8(srcIndex);

    switch (opIndex) {
        case 0:
            addToA(value, false);
            break; // ADD
        case 1:
            addToA(value, true);
            break; // ADC
        case 2:
            subFromA(value, false, true);
            break; // SUB
        case 3:
            subFromA(value, true, true);
            break; // SBC
        case 4:
            andWithA(value);
            break; // AND
        case 5:
            xorWithA(value);
            break; // XOR
        case 6:
            orWithA(value);
            break; // OR
        case 7:
            subFromA(value, false, false);
            break; // CP — same math as SUB, discard result
    }

    return (srcIndex == 6) ? 8 : 4;
}

bool Cpu::checkCondition(u8 index) const {
    switch (index) {
        case 0:
            return !f.zero; // NZ
        case 1:
            return f.zero; // Z
        case 2:
            return !f.carry; // NC
        case 3:
            return f.carry; // C
        default:
            return false;
    }
}

u16 Cpu::getAF() const {
    return static_cast<u16>((static_cast<u16>(a) << 8) | f.toByte());
}
void Cpu::setAF(u16 value) {
    a = static_cast<u8>(value >> 8);
    f.fromByte(static_cast<u8>(value & 0xFF));
}

void Cpu::push16(u16 value) {
    sp -= 2;
    bus.write8(sp, static_cast<u8>(value & 0xFF));   // low byte at the lower address
    bus.write8(sp + 1, static_cast<u8>(value >> 8)); // high byte at the higher address
}

u16 Cpu::pop16() {
    u8 low = bus.read8(sp);
    u8 high = bus.read8(sp + 1);
    sp += 2;
    return static_cast<u16>((static_cast<u16>(high) << 8) | low);
}

int Cpu::executeLoad16Immediate(u8 opcode) {
    u8 index = (opcode >> 4) & 0x03;
    u16 value = fetch16();
    switch (index) {
        case 0:
            bc.set(value);
            break;
        case 1:
            de.set(value);
            break;
        case 2:
            hl.set(value);
            break;
        case 3:
            sp = value;
            break;
    }
    return 12;
}

int Cpu::executeLoad8Immediate(u8 opcode) {
    u8 index = (opcode >> 3) & 0x07;
    u8 value = fetch8();
    writeReg8(index, value);
    return (index == 6) ? 12 : 8;
}

int Cpu::executeIndirectA(u8 opcode) {
    u8 field = (opcode >> 3) & 0x07;
    bool isLoad = field & 0x01;
    u8 index = field >> 1;
    u16 addr;
    switch (index) {
        case 0:
            addr = bc.get();
            break;
        case 1:
            addr = de.get();
            break;
        default:
            addr = hl.get();
            break;
    }
    if (isLoad)
        a = bus.read8(addr);
    else
        bus.write8(addr, a);

    if (index == 2)
        hl.set(static_cast<u16>(addr + 1));
    if (index == 3)
        hl.set(static_cast<u16>(addr - 1));
    return 8;
}

int Cpu::executeArithmeticImmediate(u8 opcode) {
    u8 opIndex = (opcode >> 3) & 0x07;
    u8 value = fetch8();

    switch (opIndex) {
        case 0:
            addToA(value, false);
            break; // ADD
        case 1:
            addToA(value, true);
            break; // ADC
        case 2:
            subFromA(value, false, true);
            break; // SUB
        case 3:
            subFromA(value, true, true);
            break; // SBC
        case 4:
            andWithA(value);
            break; // AND
        case 5:
            xorWithA(value);
            break; // XOR
        case 6:
            orWithA(value);
            break; // OR
        case 7:
            subFromA(value, false, false);
            break; // CP — same math as SUB, discard result
    }

    return 8;
}

void Cpu::addToHL(u16 value) {
    u16 hlValue = hl.get();
    u32 result = static_cast<u32>(hlValue) + value;

    bool halfCarry = ((hlValue & 0x0FFF) + (value & 0x0FFF)) > 0x0FFF;
    bool carry = result > 0xFFFF;

    hl.set(static_cast<u16>(result));
    f.subtract = false;
    f.halfCarry = halfCarry;
    f.carry = carry;
}

bool Cpu::handleInterrupts() {
    if (!imeFlag)
        return false;
    u8 pending = bus.read8(0xFFFF) & bus.read8(0xFF0F) & 0x1F;
    if (pending == 0)
        return false;

    for (u8 bit = 0; bit < 5; ++bit) {
        if (pending & (1 << bit)) {
            u8 currentIF = bus.read8(0xFF0F);
            bus.write8(0xFF0F, static_cast<u8>(currentIF & ~(1 << bit)));
            push16(pc);
            pc = interruptVector[bit];
            return true;
        }
    }
    return false;
}

u8 Cpu::applyRotateFlags(u8 result, u8 outBit) {
    f.zero = (result == 0);
    f.subtract = false;
    f.halfCarry = false;
    f.carry = outBit;
    return result;
}

u8 Cpu::rlc(u8 value) {
    u8 bit7 = (value >> 7) & 0x01;
    u8 result = static_cast<u8>((value << 1) | bit7);
    return applyRotateFlags(result, bit7);
}

u8 Cpu::rrc(u8 value) {
    u8 bit0 = value & 0x01;
    u8 result = static_cast<u8>((value >> 1) | (bit0 << 7));
    return applyRotateFlags(result, bit0);
}

u8 Cpu::rl(u8 value) {
    u8 bit7 = (value >> 7) & 0x01;
    u8 result = static_cast<u8>((value << 1) | (f.carry ? 1 : 0));
    return applyRotateFlags(result, bit7);
}

u8 Cpu::rr(u8 value) {
    u8 bit0 = value & 0x01;
    u8 result = static_cast<u8>((value >> 1) | (f.carry ? 0x80 : 0));
    return applyRotateFlags(result, bit0);
}

u8 Cpu::sla(u8 value) {
    u8 bit7 = (value >> 7) & 0x01;
    u8 result = static_cast<u8>(value << 1);
    return applyRotateFlags(result, bit7);
}

u8 Cpu::sra(u8 value) {
    u8 bit0 = value & 0x01;
    u8 result = static_cast<u8>((value >> 1) | (value & 0x80));
    return applyRotateFlags(result, bit0);
}

u8 Cpu::srl(u8 value) {
    u8 bit0 = value & 0x01;
    u8 result = static_cast<u8>(value >> 1);
    return applyRotateFlags(result, bit0);
}

u8 Cpu::swap(u8 value) {
    u8 result = static_cast<u8>(((value >> 4) & 0x0F) | ((value << 4) & 0xF0));
    f.zero = (result == 0);
    f.subtract = false;
    f.halfCarry = false;
    f.carry =
        false; // SWAP always forces carry false — doesn't fit applyRotateFlags's "outBit" shape
    return result;
}
void Cpu::testBit(u8 value, u8 bitNum) {
    bool isSet = value & (1 << bitNum);
    f.zero = !isSet;
    f.subtract = false;
    f.halfCarry = true;
}
u8 Cpu::resetBit(u8 value, u8 bitNum) {
    return static_cast<u8>(value & ~(1 << bitNum));
}
u8 Cpu::setBit(u8 value, u8 bitNum) {
    return static_cast<u8>(value | (1 << bitNum));
}
int Cpu::executeCbOpcode(u8 cbOpcode) {
    u8 regIndex = cbOpcode & 0x07;
    u8 value = readReg8(regIndex);
    u8 bitNum = (cbOpcode >> 3) & 0x07;
    int cost = (regIndex == 6) ? 16 : 8;

    if (cbOpcode <= 0x3F) {
        u8 opIndex = (cbOpcode >> 3) & 0x07;
        u8 result = value;
        switch (opIndex) {
            case 0:
                result = rlc(value);
                break;
            case 1:
                result = rrc(value);
                break;
            case 2:
                result = rl(value);
                break;
            case 3:
                result = rr(value);
                break;
            case 4:
                result = sla(value);
                break;
            case 5:
                result = sra(value);
                break;
            case 6:
                result = swap(value);
                break;
            case 7:
                result = srl(value);
                break;
        }
        writeReg8(regIndex, result);
        return cost;
    }
    u8 category = (cbOpcode >> 6) & 0x03;
    switch (category) {
        case 1: // BIT
            testBit(value, bitNum);
            return (regIndex == 6) ? 12 : 8; // BIT's memory-access cost differs from RES/SET's
        case 2:                              // RES
            writeReg8(regIndex, resetBit(value, bitNum));
            return cost;
        case 3: // SET
            writeReg8(regIndex, setBit(value, bitNum));
            return cost;
    }
    return cost;
}

int Cpu::step() {
    if (handleInterrupts()) {
        return 20; // TODO: understand better why interrupts have same cycle cost
    }
    if (halted) {
        u8 pending = bus.read8(0xFFFF) & bus.read8(0xFF0F) & 0x1F;
        if (pending != 0) {
            halted = false;
            // fall through to normal execution/interrupt handling next call
        } else {
            return 4; // still halted, burn 4 cycles, let timer/ppu keep advancing
        }
    }
    u8 opcode = fetch8();
    // static int debugCount = 0;
    // if (debugCount < 200) {
    //     std::printf("PC=0x%04X OP=0x%02X SP=0x%04X\n",
    //                 static_cast<unsigned>(pc - 1), opcode, sp);
    //     ++debugCount;
    // }

    if (opcode == 0x76) { // HALT
        halted = true;
        // Note: real hardware has a documented "HALT bug" when imeFlag is false
        // and an interrupt is already pending at this exact moment — not modeled here.
        return 4;

        std::fprintf(stderr, "HALT not yet implemented at PC=0x%04X\n",
                     static_cast<unsigned>(pc - 1));
        std::exit(1);
    }

    if (opcode >= 0x40 && opcode <= 0x7F) {
        return executeLoadRegToReg(opcode);
    }
    if (opcode >= 0x80 && opcode <= 0xBF) {
        return executeArithmetic(opcode);
    }

    switch (opcode) {
        case 0x00:
            return 4; // NOP
        case 0xF3:
            imeFlag = false;
            return 4; // DI
        case 0xFB:
            imeFlag = true;
            return 4; // EI
        // case 0x06: bc.hi = fetch8(); return 8; // LD B, n
        case 0xC3: { // JP nn — unconditional
            u16 addr = fetch16();
            pc = addr;
            return 16;
        }
        case 0xE0: { // LDH [a8], A
            u8 lowByte = fetch8();
            bus.write8(static_cast<u16>(0xFF00 | lowByte), a);
            return 12;
        }
        case 0xEA: { // LDH [a8], A
            u16 addr = fetch16();
            bus.write8(addr, a);
            return 16;
        }
        case 0xFA: { // LD A, [a16]
            u16 addr = fetch16();
            a = bus.read8(addr);
            return 16;
        }
        case 0xF0: { // LDH A, [a8]
            u8 lowByte = fetch8();
            a = bus.read8(static_cast<u16>(0xFF00 | lowByte));
            return 12;
        }
        case 0xE2: { // LDH [C], A
            bus.write8(static_cast<u16>(0xFF00 | bc.lo), a);
            return 8;
        }
        case 0xF2: { // LDH A, [C]
            a = bus.read8(static_cast<u16>(0xFF00 | bc.lo));
            return 8;
        }
        case 0xC2:
        case 0xCA:
        case 0xD2:
        case 0xDA: { // JP cc, nn
            u16 addr = fetch16();
            u8 condIndex = (opcode >> 3) & 0x03;
            if (checkCondition(condIndex)) {
                pc = addr;
                return 16;
            }
            return 12;
        }
        case 0x20:
        case 0x30:
        case 0x28:
        case 0x38: { // JR cc, e8
            u16 delta = fetch8();
            u8 condIndex = (opcode >> 3) & 0x03;
            if (checkCondition(condIndex)) {
                pc = static_cast<u16>(pc + static_cast<s8>(delta));
                return 12;
            }
            return 8;
        }
        case 0xC5:
        case 0xD5:
        case 0xE5:
        case 0xF5: { // PUSH rr
            u8 index = (opcode >> 4) & 0x03;
            u16 value = (index == 3)   ? getAF()
                        : (index == 0) ? bc.get()
                        : (index == 1) ? de.get()
                                       : hl.get();
            push16(value);
            return 16;
        }
        case 0xC1:
        case 0xD1:
        case 0xE1:
        case 0xF1: { // POP rr
            u8 index = (opcode >> 4) & 0x03;
            u16 value = pop16();
            if (index == 3)
                setAF(value);
            else if (index == 0)
                bc.set(value);
            else if (index == 1)
                de.set(value);
            else
                hl.set(value);
            return 12;
        }
        case 0xC4:
        case 0xCC:
        case 0xD4:
        case 0xDC: { // CALL cc, a16
            u16 addr = fetch16();
            u8 condIndex = (opcode >> 3) & 0x03;
            if (checkCondition(condIndex)) {
                push16(pc);
                pc = addr;
                return 24;
            }
            return 12;
        }
        case 0xCD: { // CALL nn
            u16 addr = fetch16();
            push16(pc); // pc already points past this instruction
            pc = addr;
            return 24;
        }
        case 0xC9: { // RET
            pc = pop16();
            return 16;
        }
        case 0xD9: { // RETI
            pc = pop16();
            imeFlag = true;
            return 16;
        }
        case 0xC0:
        case 0xC8:
        case 0xD0:
        case 0xD8: { // RET cc
            u8 condIndex = (opcode >> 3) & 0x03;
            if (checkCondition(condIndex)) {
                pc = pop16();
                return 20;
            }
            return 8;
        }
        case 0x2F: { // CPL
            a = static_cast<u8>(~a);
            f.subtract = true;
            f.halfCarry = true;
            return 4;
        }
        case 0x3F: { // CCF
            f.carry = !f.carry;
            f.subtract = false;
            f.halfCarry = false;
            return 4;
        }
        case 0x37: { // SCF
            f.subtract = false;
            f.halfCarry = false;
            f.carry = true;
            return 4;
        }
        case 0xCB: {
            u8 cbOpcode = fetch8();
            return executeCbOpcode(cbOpcode);
        }
        case 0xE9: { // JP HL
            pc = hl.get();
            return 4;
        }
        case 0x18: { // JR e8
            s8 offset = static_cast<s8>(fetch8());
            pc = static_cast<u16>(pc + offset);
            return 12;
        }
        case 0x08: { // LD [a16], SP
            u16 addr = fetch16();
            bus.write8(addr, static_cast<u8>(sp & 0xFF));
            bus.write8(static_cast<u16>(addr + 1), static_cast<u8>(sp >> 8));
            return 20;
        }
        case 0x07: { // RLCA
            u8 bit7 = (a >> 7) & 0x01;
            a = static_cast<u8>((a << 1) | bit7);
            f.zero = false; // RLCA always clears Z, unlike RLC A which computes it
            f.subtract = false;
            f.halfCarry = false;
            f.carry = bit7;
            return 4;
        }
        case 0x0F: { // RRCA
            u8 bit0 = a & 0x01;
            a = static_cast<u8>((a >> 1) | (bit0 << 7));
            f.zero = false;
            f.subtract = false;
            f.halfCarry = false;
            f.carry = bit0;
            return 4;
        }
        case 0x17: { // RLA
            u8 bit7 = (a >> 7) & 0x01;
            a = static_cast<u8>((a << 1) | (f.carry ? 1 : 0));
            f.zero = false;
            f.subtract = false;
            f.halfCarry = false;
            f.carry = bit7;
            return 4;
        }
        case 0x1F: { // RRA
            u8 bit0 = a & 0x01;
            a = static_cast<u8>((a >> 1) | (f.carry ? 0x80 : 0));
            f.zero = false;
            f.subtract = false;
            f.halfCarry = false;
            f.carry = bit0;
            return 4;
        }
        case 0x27: { // DAA TODO: UNDERSTAND better
            u8 correction = 0;
            bool setCarry = false;

            if (!f.subtract) {
                if (f.halfCarry || (a & 0x0F) > 0x09)
                    correction |= 0x06;
                if (f.carry || a > 0x99) {
                    correction |= 0x60;
                    setCarry = true;
                }
                a = static_cast<u8>(a + correction);
            } else {
                if (f.halfCarry)
                    correction |= 0x06;
                if (f.carry)
                    correction |= 0x60;
                a = static_cast<u8>(a - correction);
                setCarry =
                    f.carry; // carry never gets newly SET on the subtraction path, only preserved
            }

            f.zero = (a == 0);
            f.halfCarry = false;
            f.carry = setCarry;
            return 4;
        }
        case 0xF8: { // LD HL, SP+e8
            u8 rawByte = fetch8();
            s8 offset = static_cast<s8>(rawByte);

            bool halfCarry = ((sp & 0x0F) + (rawByte & 0x0F)) > 0x0F;
            bool carry = ((sp & 0xFF) + rawByte) > 0xFF;

            hl.set(static_cast<u16>(sp + offset));

            f.zero = false;
            f.subtract = false;
            f.halfCarry = halfCarry;
            f.carry = carry;

            return 12;
        }
        case 0xE8: { // ADD SP, e8
            u8 rawByte = fetch8();
            s8 offset = static_cast<s8>(rawByte);

            bool halfCarry = ((sp & 0x0F) + (rawByte & 0x0F)) > 0x0F;
            bool carry = ((sp & 0xFF) + rawByte) > 0xFF;

            sp = static_cast<u16>(sp + offset);

            f.zero = false;
            f.subtract = false;
            f.halfCarry = halfCarry;
            f.carry = carry;

            return 16;
        }
        case 0xF9: { // LD SP, HL
            sp = hl.get();
            return 8;
        }
        case 0x09:
            addToHL(bc.get());
            return 8;
        case 0x19:
            addToHL(de.get());
            return 8;
        case 0x29:
            addToHL(hl.get());
            return 8;
        case 0x39:
            addToHL(sp);
            return 8;

        case 0x01:
        case 0x11:
        case 0x21:
        case 0x31:
            return executeLoad16Immediate(opcode);
        case 0x06:
        case 0x0E:
        case 0x16:
        case 0x1E:
        case 0x26:
        case 0x2E:
        case 0x36:
        case 0x3E:
            return executeLoad8Immediate(opcode);
        case 0x02:
        case 0x0A:
        case 0x12:
        case 0x1A:
        case 0x22:
        case 0x2A:
        case 0x32:
        case 0x3A:
            return executeIndirectA(opcode);
        case 0x04:
        case 0x0C:
        case 0x14:
        case 0x1C:
        case 0x24:
        case 0x2C:
        case 0x34:
        case 0x3C:
            return incDecReg8(opcode, true);
        case 0x05:
        case 0x0D:
        case 0x15:
        case 0x1D:
        case 0x25:
        case 0x2D:
        case 0x35:
        case 0x3D:
            return incDecReg8(opcode, false);
        case 0xC6:
        case 0xCE:
        case 0xD6:
        case 0xDE:
        case 0xE6:
        case 0xEE:
        case 0xF6:
        case 0xFE:
            return executeArithmeticImmediate(opcode);
        case 0x03:
        case 0x0B:
        case 0x13:
        case 0x1B:
        case 0x23:
        case 0x2B:
        case 0x33:
        case 0x3B:
            return incDecReg16(opcode);
        case 0xC7:
        case 0xCF:
        case 0xD7:
        case 0xDF:
        case 0xE7:
        case 0xEF:
        case 0xF7:
        case 0xFF: {
            u8 index = (opcode >> 3) & 0x07;
            push16(pc);
            pc = static_cast<u16>(index * 8);
            return 16;
        }
        default:
            std::fprintf(stderr, "Unimplemented opcode: 0x%02X at PC=0x%04X\n", opcode,
                         static_cast<unsigned>(pc - 1));
            std::exit(1);
    }
}

} // namespace gb
