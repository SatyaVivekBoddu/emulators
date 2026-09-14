#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "core/bus.hpp"
#include "core/cartridge.hpp"
#include "core/cpu.hpp"
#include "core/ppu.hpp"

#include <filesystem>
#include <fstream>
#include <vector>

using namespace gb;

namespace {

std::filesystem::path writeTempRom(const std::vector<u8>& bytes) {
    auto path = std::filesystem::temp_directory_path() / "gb_test_rom.gb";
    std::ofstream file(path, std::ios::binary);
    file.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
    return path;
}

// Owns a Cartridge/Bus/Cpu with matching lifetimes (Bus and Cpu hold references to
// their predecessors) and places `opcodes` at 0x100, where the CPU starts executing.
struct System {
    Cartridge cart;
    Bus bus{cart};
    Cpu cpu{bus};

    explicit System(const std::vector<u8>& opcodes) {
        std::vector<u8> rom(0x100 + opcodes.size(), 0x00);
        std::copy(opcodes.begin(), opcodes.end(), rom.begin() + 0x100);
        REQUIRE(cart.load(writeTempRom(rom).string()));
    }
};

// Maps the CPU's 3-bit register-field encoding (used throughout the unprefixed
// and CB-prefixed opcode tables) to/from the actual register or memory-through-HL.
u8 getRegValue(System& sys, u8 index) {
    switch (index) {
        case 0: return sys.cpu.bc.hi;
        case 1: return sys.cpu.bc.lo;
        case 2: return sys.cpu.de.hi;
        case 3: return sys.cpu.de.lo;
        case 4: return sys.cpu.hl.hi;
        case 5: return sys.cpu.hl.lo;
        case 6: return sys.bus.read8(sys.cpu.hl.get());
        default: return sys.cpu.a; // 7
    }
}

void setRegValue(System& sys, u8 index, u8 value) {
    switch (index) {
        case 0: sys.cpu.bc.hi = value; break;
        case 1: sys.cpu.bc.lo = value; break;
        case 2: sys.cpu.de.hi = value; break;
        case 3: sys.cpu.de.lo = value; break;
        case 4: sys.cpu.hl.hi = value; break;
        case 5: sys.cpu.hl.lo = value; break;
        case 6: sys.bus.write8(sys.cpu.hl.get(), value); break;
        default: sys.cpu.a = value; break; // 7
    }
}

} // namespace

// --- Cartridge ---------------------------------------------------------

TEST_CASE("Cartridge fails to load a nonexistent file") {
    Cartridge cart;
    CHECK_FALSE(cart.load("this_file_does_not_exist.gb"));
}

TEST_CASE("Cartridge reads back loaded bytes; out-of-range reads return 0xFF") {
    Cartridge cart;
    REQUIRE(cart.load(writeTempRom({0x00, 0xC3, 0x50, 0x01}).string()));

    CHECK(cart.read(0) == 0x00);
    CHECK(cart.read(1) == 0xC3);
    CHECK(cart.read(3) == 0x01);
    CHECK(cart.read(0x1234) == 0xFF);
}

TEST_CASE("Cartridge writes are ignored for ROM-only cartridges") {
    Cartridge cart;
    REQUIRE(cart.load(writeTempRom({0xAA, 0xBB}).string()));

    cart.write(0, 0xFF);
    CHECK(cart.read(0) == 0xAA);
}

// --- Bus -----------------------------------------------------------------

TEST_CASE("Bus routes each address range to the right backing store") {
    System sys({0x42}); // ROM byte at 0x100 doubles as a read target below

    CHECK(sys.bus.read8(0x0100) == 0x42); // ROM

    sys.bus.write8(0xC010, 0x99); // work RAM
    CHECK(sys.bus.read8(0xC010) == 0x99);

    sys.bus.write8(0xC005, 0x77); // echo RAM mirrors work RAM
    CHECK(sys.bus.read8(0xE005) == 0x77);

    sys.bus.write8(0xFF90, 0x55); // high RAM
    CHECK(sys.bus.read8(0xFF90) == 0x55);
}

// --- Cpu: basic load/store -------------------------------------------------

TEST_CASE("Cpu executes NOP: advances PC by 1, costs 4 cycles") {
    System sys({0x00});
    u16 startPc = sys.cpu.pc;

    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.pc == startPc + 1);
}

TEST_CASE("Cpu executes LD B,C: register-to-register copy, 4 cycles") {
    System sys({0x41}); // LD B, C
    sys.cpu.bc.lo = 0x99;

    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.bc.hi == 0x99);
    CHECK(sys.cpu.bc.lo == 0x99); // source is unchanged by a load
}

TEST_CASE("Cpu executes LD (HL),A and LD A,(HL): through-memory loads, 8 cycles") {
    System sys({0x77, 0x00, 0x7E}); // LD (HL),A ; NOP ; LD A,(HL)
    sys.cpu.a = 0x5A;
    sys.cpu.hl.set(0xC010);

    CHECK(sys.cpu.step() == 8);
    CHECK(sys.bus.read8(0xC010) == 0x5A);

    sys.cpu.a = 0;
    sys.cpu.step(); // NOP
    CHECK(sys.cpu.step() == 8);
    CHECK(sys.cpu.a == 0x5A);
}

TEST_CASE("Cpu executes LD r,n for all 8 destinations, including (HL)") {
    struct Case { u8 opcode; int cycles; };
    static constexpr Case cases[] = {
        {0x06, 8}, {0x0E, 8}, {0x16, 8}, {0x1E, 8},
        {0x26, 8}, {0x2E, 8}, {0x36, 12}, {0x3E, 8},
    };

    for (auto [opcode, cycles] : cases) {
        System sys({opcode, 0x42});
        sys.cpu.hl.set(0xC050); // only relevant for the (HL) case (0x36)

        CHECK(sys.cpu.step() == cycles);
        switch (opcode) {
            case 0x06: CHECK(sys.cpu.bc.hi == 0x42); break;
            case 0x0E: CHECK(sys.cpu.bc.lo == 0x42); break;
            case 0x16: CHECK(sys.cpu.de.hi == 0x42); break;
            case 0x1E: CHECK(sys.cpu.de.lo == 0x42); break;
            case 0x26: CHECK(sys.cpu.hl.hi == 0x42); break;
            case 0x2E: CHECK(sys.cpu.hl.lo == 0x42); break;
            case 0x36: CHECK(sys.bus.read8(0xC050) == 0x42); break;
            case 0x3E: CHECK(sys.cpu.a == 0x42); break; // regression: was landing in B
        }
    }
}

TEST_CASE("Cpu executes LD rr,nn for all 4 register pairs") {
    struct Case { u8 opcode; u16 expected; };
    static constexpr Case cases[] = {
        {0x01, 0x1234}, {0x11, 0x5678}, {0x21, 0x9ABC}, {0x31, 0xDEF0},
    };

    for (auto [opcode, expected] : cases) {
        System sys({opcode, static_cast<u8>(expected & 0xFF), static_cast<u8>(expected >> 8)});

        CHECK(sys.cpu.step() == 12);
        switch (opcode) {
            case 0x01: CHECK(sys.cpu.bc.get() == expected); break;
            case 0x11: CHECK(sys.cpu.de.get() == expected); break;
            case 0x21: CHECK(sys.cpu.hl.get() == expected); break;
            case 0x31: CHECK(sys.cpu.sp == expected); break;
        }
        // regression: a broken index calc used to smear this write across all 4 pairs
        if (opcode != 0x01) CHECK(sys.cpu.bc.get() == 0);
        if (opcode != 0x11) CHECK(sys.cpu.de.get() == 0);
        if (opcode != 0x21) CHECK(sys.cpu.hl.get() == 0);
    }
}

// --- Cpu: 16-bit and 8-bit inc/dec -----------------------------------------

TEST_CASE("Cpu executes 16-bit INC/DEC for all 4 pairs, leaving the others untouched") {
    struct Case { u8 opcode; int delta; };
    static constexpr Case cases[] = {
        {0x03, +1}, {0x0B, -1}, // BC
        {0x13, +1}, {0x1B, -1}, // DE
        {0x23, +1}, {0x2B, -1}, // HL
        {0x33, +1}, {0x3B, -1}, // SP
    };

    for (auto [opcode, delta] : cases) {
        System sys({opcode});
        sys.cpu.bc.set(0x1000);
        sys.cpu.de.set(0x2000);
        sys.cpu.hl.set(0x3000);
        sys.cpu.sp = 0x4000;

        CHECK(sys.cpu.step() == 8);
        u8 pair = static_cast<u8>(opcode >> 4);
        CHECK(sys.cpu.bc.get() == 0x1000 + (pair == 0 ? delta : 0));
        CHECK(sys.cpu.de.get() == 0x2000 + (pair == 1 ? delta : 0));
        CHECK(sys.cpu.hl.get() == 0x3000 + (pair == 2 ? delta : 0));
        CHECK(sys.cpu.sp       == 0x4000 + (pair == 3 ? delta : 0));
    }
}

TEST_CASE("Cpu executes 8-bit INC/DEC: zero flag, half-carry/borrow, carry untouched") {
    System sys({0x3C}); // INC A
    sys.cpu.a = 0xFF;
    sys.cpu.f.carry = true;
    sys.cpu.step();
    CHECK(sys.cpu.a == 0x00);
    CHECK(sys.cpu.f.zero == true);
    CHECK(sys.cpu.f.halfCarry == true);
    CHECK(sys.cpu.f.carry == true); // INC/DEC never touch carry

    System sys2({0x05}); // DEC B
    sys2.cpu.bc.hi = 0x00;
    sys2.cpu.step();
    CHECK(sys2.cpu.bc.hi == 0xFF); // borrow wraps
    CHECK(sys2.cpu.f.subtract == true);
    CHECK(sys2.cpu.f.halfCarry == true);
}

// --- Cpu: indirect A loads ---------------------------------------------

TEST_CASE("Cpu executes indirect A loads/stores via BC, DE, HL+, HL-") {
    struct Case { u8 opcode; bool isLoad; int hlDelta; };
    static constexpr Case cases[] = {
        {0x02, false, 0}, {0x0A, true, 0},  // (BC),A / A,(BC)
        {0x12, false, 0}, {0x1A, true, 0},  // (DE),A / A,(DE)
        {0x22, false, +1}, {0x2A, true, +1}, // (HL+),A / A,(HL+)
        {0x32, false, -1}, {0x3A, true, -1}, // (HL-),A / A,(HL-)
    };

    for (auto [opcode, isLoad, hlDelta] : cases) {
        System sys({opcode});
        sys.cpu.bc.set(0xC100);
        sys.cpu.de.set(0xC200);
        sys.cpu.hl.set(0xC300);
        sys.cpu.a = 0x5A;

        u16 targetAddr = (opcode == 0x02 || opcode == 0x0A) ? 0xC100
                        : (opcode == 0x12 || opcode == 0x1A) ? 0xC200
                        : 0xC300;

        if (isLoad) sys.bus.write8(targetAddr, 0x77);

        CHECK(sys.cpu.step() == 8);
        if (isLoad) CHECK(sys.cpu.a == 0x77);
        else CHECK(sys.bus.read8(targetAddr) == 0x5A);

        if (hlDelta != 0) CHECK(sys.cpu.hl.get() == static_cast<u16>(0xC300 + hlDelta));
        else CHECK(sys.cpu.hl.get() == 0xC300); // untouched for BC/DE opcodes
    }
}

// --- Cpu: arithmetic -------------------------------------------------------

TEST_CASE("Cpu ADD/ADC: half-carry and full-carry") {
    System sys({0x80}); // ADD A,B
    sys.cpu.a = 0x3C;
    sys.cpu.bc.hi = 0x0F;
    sys.cpu.step();
    CHECK(sys.cpu.a == 0x4B);
    CHECK(sys.cpu.f.halfCarry == true);
    CHECK(sys.cpu.f.carry == false);

    System sys2({0x80});
    sys2.cpu.a = 0xF0;
    sys2.cpu.bc.hi = 0x20;
    sys2.cpu.step();
    CHECK(sys2.cpu.a == 0x10);
    CHECK(sys2.cpu.f.carry == true);
}

TEST_CASE("Cpu CP: sets flags like SUB but does not modify A") {
    System sys({0xB8}); // CP B
    sys.cpu.a = 0x05;
    sys.cpu.bc.hi = 0x05;
    sys.cpu.step();
    CHECK(sys.cpu.a == 0x05);
    CHECK(sys.cpu.f.zero == true);
}

TEST_CASE("Cpu AND: always sets half-carry, clears carry") {
    System sys({0xA0}); // AND B
    sys.cpu.a = 0xFF;
    sys.cpu.bc.hi = 0x0F;
    sys.cpu.f.carry = true;
    sys.cpu.step();
    CHECK(sys.cpu.a == 0x0F);
    CHECK(sys.cpu.f.halfCarry == true);
    CHECK(sys.cpu.f.carry == false);
}

TEST_CASE("Cpu executes ADD A,n immediate") {
    System sys({0xC6, 0x05}); // ADD A, 5
    sys.cpu.a = 0x02;
    CHECK(sys.cpu.step() == 8);
    CHECK(sys.cpu.a == 0x07);
}

// --- Cpu: control flow -------------------------------------------------

TEST_CASE("Cpu executes JP nn: unconditional jump") {
    System sys({0xC3, 0x00, 0x80}); // JP 0x8000
    CHECK(sys.cpu.step() == 16);
    CHECK(sys.cpu.pc == 0x8000);
}

TEST_CASE("Cpu executes JP cc,nn: taken and not-taken cycle counts") {
    System taken({0xCA, 0x00, 0x90}); // JP Z, 0x9000
    taken.cpu.f.zero = true;
    CHECK(taken.cpu.step() == 16);
    CHECK(taken.cpu.pc == 0x9000);

    System notTaken({0xC2, 0x00, 0x90}); // JP NZ, 0x9000
    notTaken.cpu.f.zero = true;
    u16 startPc = notTaken.cpu.pc;
    CHECK(notTaken.cpu.step() == 12);
    CHECK(notTaken.cpu.pc == startPc + 3);
}

TEST_CASE("Cpu executes JR cc,e8: taken and not-taken") {
    System taken({0x28, 0x05}); // JR Z, +5
    taken.cpu.f.zero = true;
    CHECK(taken.cpu.step() == 12);
    CHECK(taken.cpu.pc == 0x107);

    System notTaken({0x28, 0x05});
    notTaken.cpu.f.zero = false;
    CHECK(notTaken.cpu.step() == 8);
    CHECK(notTaken.cpu.pc == 0x102);
}

TEST_CASE("Cpu executes PUSH/POP round trips for all 4 pairs, including AF's flag byte") {
    System sys({0xC5, 0xD1}); // PUSH BC / POP DE
    sys.cpu.bc.set(0x1234);
    u16 initialSp = sys.cpu.sp;
    CHECK(sys.cpu.step() == 16);
    CHECK(sys.cpu.sp == initialSp - 2);
    CHECK(sys.bus.read8(sys.cpu.sp)     == 0x34); // low byte at the lower address
    CHECK(sys.bus.read8(sys.cpu.sp + 1) == 0x12);
    CHECK(sys.cpu.step() == 12);
    CHECK(sys.cpu.de.get() == 0x1234);
    CHECK(sys.cpu.sp == initialSp);

    System afSys({0xF5, 0xC1}); // PUSH AF / POP BC
    afSys.cpu.a = 0x80;
    afSys.cpu.f.zero = true;
    afSys.cpu.f.carry = true;
    afSys.cpu.step();
    afSys.cpu.step();
    CHECK(afSys.cpu.bc.hi == 0x80);       // A
    CHECK(afSys.cpu.bc.lo == 0x90);       // F byte: zero(0x80) | carry(0x10)
}

TEST_CASE("Cpu executes CALL then RET: round trips PC through the stack") {
    System sys({0xCD, 0x00, 0x90}); // CALL 0x9000
    CHECK(sys.cpu.step() == 24);
    CHECK(sys.cpu.pc == 0x9000);
    CHECK(sys.cpu.sp == 0xFFFC); // return address (0x103) pushed
    CHECK(sys.bus.read8(0xFFFC) == 0x03);
    CHECK(sys.bus.read8(0xFFFD) == 0x01);
}

TEST_CASE("Cpu executes RET cc: taken and not-taken") {
    System taken({0xC0}); // RET NZ
    taken.cpu.f.zero = false; // NZ condition holds
    taken.cpu.sp = 0xFFFC;
    taken.bus.write8(0xFFFC, 0x34); // low byte of return address
    taken.bus.write8(0xFFFD, 0x12); // high byte
    CHECK(taken.cpu.step() == 20);
    CHECK(taken.cpu.pc == 0x1234);
    CHECK(taken.cpu.sp == 0xFFFE); // popped, sp moved back up

    System notTaken({0xC0});
    notTaken.cpu.f.zero = true; // NZ condition fails
    notTaken.cpu.sp = 0xFFFC;
    notTaken.bus.write8(0xFFFC, 0x34);
    notTaken.bus.write8(0xFFFD, 0x12);
    CHECK(notTaken.cpu.step() == 8);
    CHECK(notTaken.cpu.pc == 0x101); // fell through, just past the 1-byte opcode
    CHECK(notTaken.cpu.sp == 0xFFFC); // untouched — nothing was popped
}
TEST_CASE("Cpu executes JR e8: unconditional, both directions") {
    System forward({0x18, 0x05}); // jump forward by 5
    CHECK(forward.cpu.step() == 12);
    CHECK(forward.cpu.pc == 0x107);

    System backward({0x00, 0x00, 0x00, 0x00, 0x00, 0x18, static_cast<u8>(-4)}); // jump backward by 4
    backward.cpu.pc = 0x105; // start execution at the JR instruction directly
    CHECK(backward.cpu.step() == 12);
    CHECK(backward.cpu.pc == 0x103); // 0x105 + 2 (past the instruction) - 4 = 0x103
}
TEST_CASE("Cpu executes RLCA/RRCA/RLA/RRA: Z always forced false, never computed") {
    System sys({0x07}); // RLCA
    sys.cpu.a = 0x00; // a value that WOULD make Z true if it were computed
    sys.cpu.step();
    CHECK(sys.cpu.a == 0x00);
    CHECK(sys.cpu.f.zero == false); // forced false, even though the result is genuinely zero
}
// --- Cpu: interrupts ---------------------------------------------------

TEST_CASE("Cpu services a pending interrupt: pushes PC, jumps to vector, clears IF only") {
    System sys({0xFB, 0x00}); // EI ; NOP
    sys.bus.write8(0xFFFF, 0x01); // IE: VBlank enabled
    sys.bus.write8(0xFF0F, 0x01); // IF: VBlank pending

    sys.cpu.step(); // EI (IME takes effect immediately in this implementation)
    CHECK(sys.cpu.step() == 20); // interrupt dispatch instead of the NOP

    CHECK(sys.cpu.pc == 0x0040); // VBlank vector
    CHECK(sys.bus.read8(0xFF0F) == 0x00); // pending flag cleared...
    CHECK(sys.bus.read8(0xFFFF) == 0x01); // ...without touching the enable register
    CHECK(sys.bus.read8(sys.cpu.sp) == 0x01);      // low byte of return address 0x0101
    CHECK(sys.bus.read8(sys.cpu.sp + 1) == 0x01);  // high byte
}

// --- Cpu: LD r,r full opcode matrix ----------------------------------------

TEST_CASE("Cpu executes LD r,r for all 64 src/dest combinations (0x40-0x7F, excluding HALT)") {
    for (u8 dest = 0; dest < 8; ++dest) {
        for (u8 src = 0; src < 8; ++src) {
            u8 opcode = static_cast<u8>(0x40 | (dest << 3) | src);
            if (opcode == 0x76) continue; // HALT, not a load

            System sys({opcode});
            sys.cpu.bc.set(0x1122);
            sys.cpu.de.set(0x3344);
            sys.cpu.hl.set(0xC050);
            sys.cpu.a = 0x77;
            sys.bus.write8(0xC050, 0x66);

            u8 expected = getRegValue(sys, src);
            int expectedCycles = (dest == 6 || src == 6) ? 8 : 4;

            CHECK(sys.cpu.step() == expectedCycles);
            CHECK(getRegValue(sys, dest) == expected);
        }
    }
}

// --- Cpu: 8-bit INC/DEC full opcode matrix ----------------------------------

TEST_CASE("Cpu executes INC r for all 8 targets, including (HL)") {
    for (u8 index = 0; index < 8; ++index) {
        u8 opcode = static_cast<u8>(0x04 | (index << 3));
        System sys({opcode});
        sys.cpu.hl.set(0xC050);
        sys.bus.write8(0xC050, 0x10);
        setRegValue(sys, index, 0x10);

        int expectedCycles = (index == 6) ? 12 : 4;
        CHECK(sys.cpu.step() == expectedCycles);
        CHECK(getRegValue(sys, index) == 0x11);
    }
}

TEST_CASE("Cpu executes DEC r for all 8 targets, including (HL)") {
    for (u8 index = 0; index < 8; ++index) {
        u8 opcode = static_cast<u8>(0x05 | (index << 3));
        System sys({opcode});
        sys.cpu.hl.set(0xC050);
        sys.bus.write8(0xC050, 0x10);
        setRegValue(sys, index, 0x10);

        int expectedCycles = (index == 6) ? 12 : 4;
        CHECK(sys.cpu.step() == expectedCycles);
        CHECK(getRegValue(sys, index) == 0x0F);
    }
}

// --- Cpu: register-register arithmetic (0x80-0xBF) --------------------------

TEST_CASE("Cpu executes ADC A,r: adds register plus carry-in") {
    System sys({0x89}); // ADC A,C
    sys.cpu.a = 0x3D;
    sys.cpu.bc.lo = 0x42;
    sys.cpu.f.carry = true;
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.a == 0x80);
    CHECK(sys.cpu.f.carry == false);
}

TEST_CASE("Cpu executes SUB r: sets borrow flags, stores result in A") {
    System sys({0x90}); // SUB B
    sys.cpu.a = 0x10;
    sys.cpu.bc.hi = 0x11;
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.a == 0xFF);
    CHECK(sys.cpu.f.subtract == true);
    CHECK(sys.cpu.f.carry == true);
    CHECK(sys.cpu.f.halfCarry == true);
}

TEST_CASE("Cpu executes SBC A,r: subtracts register plus borrow-in") {
    System sys({0x98}); // SBC A,B
    sys.cpu.a = 0x10;
    sys.cpu.bc.hi = 0x0F;
    sys.cpu.f.carry = true; // borrow-in
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.a == 0x00);
    CHECK(sys.cpu.f.zero == true);
    CHECK(sys.cpu.f.carry == false);
}

TEST_CASE("Cpu executes XOR r: clears N/H/C, sets Z from result") {
    System sys({0xA8}); // XOR B
    sys.cpu.a = 0xFF;
    sys.cpu.bc.hi = 0xFF;
    sys.cpu.f.carry = true;
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.a == 0x00);
    CHECK(sys.cpu.f.zero == true);
    CHECK(sys.cpu.f.carry == false);
    CHECK(sys.cpu.f.halfCarry == false);
}

TEST_CASE("Cpu executes OR r: clears N/H/C, sets Z from result") {
    System sys({0xB0}); // OR B
    sys.cpu.a = 0x00;
    sys.cpu.bc.hi = 0x00;
    sys.cpu.f.carry = true;
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.a == 0x00);
    CHECK(sys.cpu.f.zero == true);
    CHECK(sys.cpu.f.carry == false);
}

TEST_CASE("Cpu executes CP r: borrow flags without modifying A") {
    System sys({0xB8}); // CP B
    sys.cpu.a = 0x02;
    sys.cpu.bc.hi = 0x05;
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.a == 0x02); // unchanged
    CHECK(sys.cpu.f.carry == true); // borrow: 2 < 5
    CHECK(sys.cpu.f.zero == false);
}

TEST_CASE("Cpu executes every register-register arithmetic opcode (0x80-0xBF): all 64 combinations run at the right cycle cost") {
    for (u8 opIndex = 0; opIndex < 8; ++opIndex) {
        for (u8 srcIndex = 0; srcIndex < 8; ++srcIndex) {
            u8 opcode = static_cast<u8>(0x80 | (opIndex << 3) | srcIndex);
            System sys({opcode});
            sys.cpu.hl.set(0xC050);
            sys.cpu.a = 0x10;
            setRegValue(sys, srcIndex, 0x01);

            int expectedCycles = (srcIndex == 6) ? 8 : 4;
            CHECK(sys.cpu.step() == expectedCycles);
        }
    }
}

// --- Cpu: arithmetic immediate (0xC6-0xFE) -----------------------------------

TEST_CASE("Cpu executes arithmetic-immediate for all 8 operations, 8 cycles each") {
    { // ADC A,n
        System sys({0xCE, 0x01});
        sys.cpu.a = 0x01;
        sys.cpu.f.carry = true;
        CHECK(sys.cpu.step() == 8);
        CHECK(sys.cpu.a == 0x03);
    }
    { // SUB n
        System sys({0xD6, 0x01});
        sys.cpu.a = 0x00;
        CHECK(sys.cpu.step() == 8);
        CHECK(sys.cpu.a == 0xFF);
        CHECK(sys.cpu.f.subtract == true);
        CHECK(sys.cpu.f.carry == true);
    }
    { // SBC A,n
        System sys({0xDE, 0x01});
        sys.cpu.a = 0x01;
        sys.cpu.f.carry = true; // borrow-in
        CHECK(sys.cpu.step() == 8);
        CHECK(sys.cpu.a == 0xFF);
        CHECK(sys.cpu.f.carry == true);
    }
    { // AND n
        System sys({0xE6, 0x0F});
        sys.cpu.a = 0xFF;
        CHECK(sys.cpu.step() == 8);
        CHECK(sys.cpu.a == 0x0F);
        CHECK(sys.cpu.f.halfCarry == true);
        CHECK(sys.cpu.f.carry == false);
    }
    { // XOR n
        System sys({0xEE, 0xFF});
        sys.cpu.a = 0xFF;
        CHECK(sys.cpu.step() == 8);
        CHECK(sys.cpu.a == 0x00);
        CHECK(sys.cpu.f.zero == true);
    }
    { // OR n
        System sys({0xF6, 0x0F});
        sys.cpu.a = 0xF0;
        CHECK(sys.cpu.step() == 8);
        CHECK(sys.cpu.a == 0xFF);
    }
    { // CP n
        System sys({0xFE, 0x10});
        sys.cpu.a = 0x05;
        CHECK(sys.cpu.step() == 8);
        CHECK(sys.cpu.a == 0x05); // unchanged
        CHECK(sys.cpu.f.carry == true); // borrow: 5 < 0x10
    }
}

// --- Cpu: PUSH/POP full matrix -----------------------------------------------

TEST_CASE("Cpu executes PUSH/POP symmetric round trip for all 4 pairs") {
    struct Case { u8 pushOp; u8 popOp; };
    static constexpr Case cases[] = {
        {0xC5, 0xC1}, {0xD5, 0xD1}, {0xE5, 0xE1}, {0xF5, 0xF1},
    };
    for (auto [pushOp, popOp] : cases) {
        System sys({pushOp, popOp});
        u16 testValue = 0x1234;
        switch (pushOp) {
            case 0xC5: sys.cpu.bc.set(testValue); break;
            case 0xD5: sys.cpu.de.set(testValue); break;
            case 0xE5: sys.cpu.hl.set(testValue); break;
            case 0xF5: sys.cpu.a = 0x12; sys.cpu.f.fromByte(0xF0); break;
        }
        u16 initialSp = sys.cpu.sp;
        CHECK(sys.cpu.step() == 16); // PUSH
        CHECK(sys.cpu.sp == initialSp - 2);
        CHECK(sys.cpu.step() == 12); // POP

        switch (popOp) {
            case 0xC1: CHECK(sys.cpu.bc.get() == testValue); break;
            case 0xD1: CHECK(sys.cpu.de.get() == testValue); break;
            case 0xE1: CHECK(sys.cpu.hl.get() == testValue); break;
            case 0xF1:
                CHECK(sys.cpu.a == 0x12);
                CHECK(sys.cpu.f.toByte() == 0xF0);
                break;
        }
        CHECK(sys.cpu.sp == initialSp);
    }
}

// --- Cpu: HALT / DI ----------------------------------------------------------

TEST_CASE("Cpu executes HALT: halts and burns 4 cycles per step until an interrupt is pending") {
    System sys({0x76, 0x00}); // HALT ; NOP
    CHECK(sys.cpu.step() == 4);
    u16 pcAfterHalt = sys.cpu.pc;
    CHECK(sys.cpu.step() == 4); // still halted, burns cycles without advancing PC
    CHECK(sys.cpu.pc == pcAfterHalt);

    sys.bus.write8(0xFFFF, 0x01); // IE
    sys.bus.write8(0xFF0F, 0x01); // IF
    // IME is still false (never enabled), so this wakes up but falls through to the NOP,
    // rather than dispatching the interrupt.
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.pc == static_cast<u16>(pcAfterHalt + 1));
}

TEST_CASE("Cpu executes DI: leaves a pending interrupt undispatched") {
    System sys({0xF3, 0x00}); // DI ; NOP
    sys.bus.write8(0xFFFF, 0x01);
    sys.bus.write8(0xFF0F, 0x01);
    CHECK(sys.cpu.step() == 4); // DI
    CHECK(sys.cpu.step() == 4); // NOP, not a 20-cycle interrupt dispatch
    CHECK(sys.cpu.pc == 0x0102);
}

// --- Cpu: LDH / high-page and direct A loads --------------------------------

TEST_CASE("Cpu executes LDH [a8],A and LDH A,[a8]") {
    System sys({0xE0, 0x80}); // LDH [0xFF80], A
    sys.cpu.a = 0x42;
    CHECK(sys.cpu.step() == 12);
    CHECK(sys.bus.read8(0xFF80) == 0x42);

    System sys2({0xF0, 0x80}); // LDH A, [0xFF80]
    sys2.bus.write8(0xFF80, 0x99);
    CHECK(sys2.cpu.step() == 12);
    CHECK(sys2.cpu.a == 0x99);
}

TEST_CASE("Cpu executes LD [a16],A and LD A,[a16]") {
    System sys({0xEA, 0x00, 0xC0}); // LD [0xC000], A
    sys.cpu.a = 0x42;
    CHECK(sys.cpu.step() == 16);
    CHECK(sys.bus.read8(0xC000) == 0x42);

    System sys2({0xFA, 0x00, 0xC0}); // LD A, [0xC000]
    sys2.bus.write8(0xC000, 0x99);
    CHECK(sys2.cpu.step() == 16);
    CHECK(sys2.cpu.a == 0x99);
}

TEST_CASE("Cpu executes LDH [C],A and LDH A,[C]") {
    System sys({0xE2}); // LDH [C], A
    sys.cpu.bc.lo = 0x80;
    sys.cpu.a = 0x42;
    CHECK(sys.cpu.step() == 8);
    CHECK(sys.bus.read8(0xFF80) == 0x42);

    System sys2({0xF2}); // LDH A, [C]
    sys2.cpu.bc.lo = 0x80;
    sys2.bus.write8(0xFF80, 0x99);
    CHECK(sys2.cpu.step() == 8);
    CHECK(sys2.cpu.a == 0x99);
}

// --- Cpu: conditional control flow, full condition matrix -------------------

TEST_CASE("Cpu executes JP cc,nn for all 4 conditions, taken and not taken") {
    struct Case { u8 opcode; int condIndex; };
    static constexpr Case cases[] = {
        {0xC2, 0}, {0xCA, 1}, {0xD2, 2}, {0xDA, 3}, // NZ, Z, NC, C
    };
    for (auto [opcode, condIndex] : cases) {
        for (bool takenExpected : {true, false}) {
            System sys({opcode, 0x00, 0x90});
            switch (condIndex) {
                case 0: sys.cpu.f.zero = !takenExpected; break;
                case 1: sys.cpu.f.zero = takenExpected; break;
                case 2: sys.cpu.f.carry = !takenExpected; break;
                case 3: sys.cpu.f.carry = takenExpected; break;
            }
            u16 startPc = sys.cpu.pc;
            int cycles = sys.cpu.step();
            if (takenExpected) {
                CHECK(cycles == 16);
                CHECK(sys.cpu.pc == 0x9000);
            } else {
                CHECK(cycles == 12);
                CHECK(sys.cpu.pc == static_cast<u16>(startPc + 3));
            }
        }
    }
}

TEST_CASE("Cpu executes JR cc,e8 for all 4 conditions, taken and not taken") {
    struct Case { u8 opcode; int condIndex; };
    static constexpr Case cases[] = {
        {0x20, 0}, {0x28, 1}, {0x30, 2}, {0x38, 3}, // NZ, Z, NC, C
    };
    for (auto [opcode, condIndex] : cases) {
        for (bool takenExpected : {true, false}) {
            System sys({opcode, 0x05});
            switch (condIndex) {
                case 0: sys.cpu.f.zero = !takenExpected; break;
                case 1: sys.cpu.f.zero = takenExpected; break;
                case 2: sys.cpu.f.carry = !takenExpected; break;
                case 3: sys.cpu.f.carry = takenExpected; break;
            }
            u16 startPc = sys.cpu.pc;
            int cycles = sys.cpu.step();
            if (takenExpected) {
                CHECK(cycles == 12);
                CHECK(sys.cpu.pc == static_cast<u16>(startPc + 2 + 5));
            } else {
                CHECK(cycles == 8);
                CHECK(sys.cpu.pc == static_cast<u16>(startPc + 2));
            }
        }
    }
}

TEST_CASE("Cpu executes CALL cc,a16 for all 4 conditions, taken and not taken") {
    struct Case { u8 opcode; int condIndex; };
    static constexpr Case cases[] = {
        {0xC4, 0}, {0xCC, 1}, {0xD4, 2}, {0xDC, 3}, // NZ, Z, NC, C
    };
    for (auto [opcode, condIndex] : cases) {
        for (bool takenExpected : {true, false}) {
            System sys({opcode, 0x00, 0x90});
            switch (condIndex) {
                case 0: sys.cpu.f.zero = !takenExpected; break;
                case 1: sys.cpu.f.zero = takenExpected; break;
                case 2: sys.cpu.f.carry = !takenExpected; break;
                case 3: sys.cpu.f.carry = takenExpected; break;
            }
            u16 initialSp = sys.cpu.sp;
            int cycles = sys.cpu.step();
            if (takenExpected) {
                CHECK(cycles == 24);
                CHECK(sys.cpu.pc == 0x9000);
                CHECK(sys.cpu.sp == static_cast<u16>(initialSp - 2));
                CHECK(sys.bus.read8(sys.cpu.sp) == 0x03); // return address 0x0103
                CHECK(sys.bus.read8(static_cast<u16>(sys.cpu.sp + 1)) == 0x01);
            } else {
                CHECK(cycles == 12);
                CHECK(sys.cpu.pc == 0x0103);
                CHECK(sys.cpu.sp == initialSp); // nothing pushed
            }
        }
    }
}

TEST_CASE("Cpu executes RET cc for all 4 conditions, taken and not taken") {
    struct Case { u8 opcode; int condIndex; };
    static constexpr Case cases[] = {
        {0xC0, 0}, {0xC8, 1}, {0xD0, 2}, {0xD8, 3}, // NZ, Z, NC, C
    };
    for (auto [opcode, condIndex] : cases) {
        for (bool takenExpected : {true, false}) {
            System sys({opcode});
            switch (condIndex) {
                case 0: sys.cpu.f.zero = !takenExpected; break;
                case 1: sys.cpu.f.zero = takenExpected; break;
                case 2: sys.cpu.f.carry = !takenExpected; break;
                case 3: sys.cpu.f.carry = takenExpected; break;
            }
            sys.cpu.sp = 0xFFFC;
            sys.bus.write8(0xFFFC, 0x34);
            sys.bus.write8(0xFFFD, 0x12);

            int cycles = sys.cpu.step();
            if (takenExpected) {
                CHECK(cycles == 20);
                CHECK(sys.cpu.pc == 0x1234);
                CHECK(sys.cpu.sp == 0xFFFE);
            } else {
                CHECK(cycles == 8);
                CHECK(sys.cpu.pc == 0x0101);
                CHECK(sys.cpu.sp == 0xFFFC);
            }
        }
    }
}

TEST_CASE("Cpu executes RET: pops PC unconditionally") {
    System sys({0xC9});
    sys.cpu.sp = 0xFFFC;
    sys.bus.write8(0xFFFC, 0x34);
    sys.bus.write8(0xFFFD, 0x12);
    CHECK(sys.cpu.step() == 16);
    CHECK(sys.cpu.pc == 0x1234);
    CHECK(sys.cpu.sp == 0xFFFE);
}

TEST_CASE("Cpu executes RETI: pops PC and immediately re-enables interrupts") {
    System sys({0xD9}); // RETI
    sys.cpu.sp = 0xFFFC;
    sys.bus.write8(0xFFFC, 0x00);
    sys.bus.write8(0xFFFD, 0x90); // return to 0x9000
    CHECK(sys.cpu.step() == 16);
    CHECK(sys.cpu.pc == 0x9000);
    CHECK(sys.cpu.sp == 0xFFFE);

    sys.bus.write8(0xFFFF, 0x01); // IE: VBlank
    sys.bus.write8(0xFF0F, 0x01); // IF: VBlank pending
    sys.bus.write8(0x9000, 0x00); // NOP at the return target — should be preempted
    CHECK(sys.cpu.step() == 20); // interrupt dispatched because RETI set IME
    CHECK(sys.cpu.pc == 0x0040);
}

// --- Cpu: flag/accumulator opcodes -------------------------------------------

TEST_CASE("Cpu executes CPL: complements A, sets N and H") {
    System sys({0x2F});
    sys.cpu.a = 0x35;
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.a == 0xCA);
    CHECK(sys.cpu.f.subtract == true);
    CHECK(sys.cpu.f.halfCarry == true);
}

TEST_CASE("Cpu executes CCF: flips carry, clears N and H") {
    System sys({0x3F});
    sys.cpu.f.carry = false;
    sys.cpu.f.subtract = true;
    sys.cpu.f.halfCarry = true;
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.f.carry == true);
    CHECK(sys.cpu.f.subtract == false);
    CHECK(sys.cpu.f.halfCarry == false);
}

TEST_CASE("Cpu executes SCF: sets carry, clears N and H") {
    System sys({0x37});
    sys.cpu.f.carry = false;
    sys.cpu.f.subtract = true;
    sys.cpu.f.halfCarry = true;
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.f.carry == true);
    CHECK(sys.cpu.f.subtract == false);
    CHECK(sys.cpu.f.halfCarry == false);
}

TEST_CASE("Cpu executes RRCA: rotates A right through bit 0 into carry and bit 7") {
    System sys({0x0F});
    sys.cpu.a = 0x01;
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.a == 0x80);
    CHECK(sys.cpu.f.carry == true);
    CHECK(sys.cpu.f.zero == false); // forced false, unlike RRC A
}

TEST_CASE("Cpu executes RLA: rotates A left through the carry flag") {
    System sys({0x17});
    sys.cpu.a = 0x80;
    sys.cpu.f.carry = true;
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.a == 0x01); // old carry shifted into bit 0
    CHECK(sys.cpu.f.carry == true); // old bit 7 shifted out
}

TEST_CASE("Cpu executes RRA: rotates A right through the carry flag") {
    System sys({0x1F});
    sys.cpu.a = 0x01;
    sys.cpu.f.carry = true;
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.a == 0x80); // old carry shifted into bit 7
    CHECK(sys.cpu.f.carry == true); // old bit 0 shifted out
}

TEST_CASE("Cpu executes DAA: corrects a binary sum into valid BCD (45 + 38 = 83)") {
    System sys({0x80, 0x27}); // ADD A,B ; DAA
    sys.cpu.a = 0x45;
    sys.cpu.bc.hi = 0x38;
    sys.cpu.step(); // ADD -> a = 0x7D
    CHECK(sys.cpu.step() == 4); // DAA
    CHECK(sys.cpu.a == 0x83);
    CHECK(sys.cpu.f.carry == false);
}

TEST_CASE("Cpu executes DAA: corrects a binary difference into valid BCD (50 - 18 = 32)") {
    System sys({0x90, 0x27}); // SUB B ; DAA
    sys.cpu.a = 0x50;
    sys.cpu.bc.hi = 0x18;
    sys.cpu.step(); // SUB -> a = 0x38, H set from the nibble borrow
    CHECK(sys.cpu.step() == 4); // DAA
    CHECK(sys.cpu.a == 0x32);
    CHECK(sys.cpu.f.carry == false);
}

// --- Cpu: 16-bit stack/HL arithmetic -----------------------------------------

TEST_CASE("Cpu executes JP HL: jumps without touching the stack, 4 cycles") {
    System sys({0xE9});
    sys.cpu.hl.set(0x8000);
    u16 initialSp = sys.cpu.sp;
    CHECK(sys.cpu.step() == 4);
    CHECK(sys.cpu.pc == 0x8000);
    CHECK(sys.cpu.sp == initialSp);
}

TEST_CASE("Cpu executes LD [a16],SP: writes SP little-endian to memory") {
    System sys({0x08, 0x00, 0xC0}); // LD [0xC000], SP
    sys.cpu.sp = 0x1234;
    CHECK(sys.cpu.step() == 20);
    CHECK(sys.bus.read8(0xC000) == 0x34);
    CHECK(sys.bus.read8(0xC001) == 0x12);
}

TEST_CASE("Cpu executes LD HL,SP+e8: adds a signed offset, sets H/C from the low-byte add") {
    System sys({0xF8, 0x02}); // LD HL, SP+2
    sys.cpu.sp = 0xFFF8;
    CHECK(sys.cpu.step() == 12);
    CHECK(sys.cpu.hl.get() == 0xFFFA);
    CHECK(sys.cpu.f.zero == false);
    CHECK(sys.cpu.f.subtract == false);

    System neg({0xF8, static_cast<u8>(-1)}); // LD HL, SP-1
    neg.cpu.sp = 0xFFF8;
    CHECK(neg.cpu.step() == 12);
    CHECK(neg.cpu.hl.get() == 0xFFF7);

    System carrySys({0xF8, 0x01});
    carrySys.cpu.sp = 0x00FF; // low byte 0xFF + 0x01 carries out of bit 3 and bit 7
    CHECK(carrySys.cpu.step() == 12);
    CHECK(carrySys.cpu.hl.get() == 0x0100);
    CHECK(carrySys.cpu.f.carry == true);
    CHECK(carrySys.cpu.f.halfCarry == true);
}

TEST_CASE("Cpu executes ADD SP,e8: same flag math as LD HL,SP+e8 but stores into SP, and Z is always forced false") {
    System sys({0xE8, 0x10});
    sys.cpu.sp = 0xFFF0;
    CHECK(sys.cpu.step() == 16);
    CHECK(sys.cpu.sp == 0x0000); // wraps
    CHECK(sys.cpu.f.carry == true); // 0xF0 + 0x10 carries
    CHECK(sys.cpu.f.zero == false); // forced false even though the numeric result is 0
}

TEST_CASE("Cpu executes LD SP,HL") {
    System sys({0xF9});
    sys.cpu.hl.set(0xC123);
    CHECK(sys.cpu.step() == 8);
    CHECK(sys.cpu.sp == 0xC123);
}

TEST_CASE("Cpu executes ADD HL,rr for all 4 sources, including ADD HL,HL") {
    struct Case { u8 opcode; };
    static constexpr Case cases[] = { {0x09}, {0x19}, {0x29}, {0x39} };

    for (auto [opcode] : cases) {
        System sys({opcode});
        sys.cpu.hl.set(0x0FFF);
        sys.cpu.bc.set(0x0001);
        sys.cpu.de.set(0x0001);
        sys.cpu.sp = 0x0001;

        CHECK(sys.cpu.step() == 8);
        u16 expected = (opcode == 0x29) ? 0x1FFE : 0x1000; // ADD HL,HL doubles instead of adding 1
        CHECK(sys.cpu.hl.get() == expected);
        CHECK(sys.cpu.f.halfCarry == true);
        CHECK(sys.cpu.f.subtract == false);
    }
}

TEST_CASE("Cpu executes ADD HL,BC: full carry out of bit 15") {
    System sys({0x09});
    sys.cpu.hl.set(0xFFFF);
    sys.cpu.bc.set(0x0001);
    CHECK(sys.cpu.step() == 8);
    CHECK(sys.cpu.hl.get() == 0x0000);
    CHECK(sys.cpu.f.carry == true);
}

// --- Cpu: RST -----------------------------------------------------------------

TEST_CASE("Cpu executes RST for all 8 vectors, pushing the return address") {
    struct Case { u8 opcode; u16 vector; };
    static constexpr Case cases[] = {
        {0xC7, 0x0000}, {0xCF, 0x0008}, {0xD7, 0x0010}, {0xDF, 0x0018},
        {0xE7, 0x0020}, {0xEF, 0x0028}, {0xF7, 0x0030}, {0xFF, 0x0038},
    };
    for (auto [opcode, vector] : cases) {
        System sys({opcode});
        u16 initialSp = sys.cpu.sp;
        CHECK(sys.cpu.step() == 16);
        CHECK(sys.cpu.pc == vector);
        CHECK(sys.cpu.sp == static_cast<u16>(initialSp - 2));
        CHECK(sys.bus.read8(sys.cpu.sp) == 0x01); // low byte of return address 0x0101
        CHECK(sys.bus.read8(static_cast<u16>(sys.cpu.sp + 1)) == 0x01);
    }
}

// --- Cpu: CB-prefixed opcodes -------------------------------------------------

TEST_CASE("Cpu executes CB rotate/shift group for all 8 operations and all 8 registers") {
    struct Case { u8 opIndex; u8 input; u8 expected; bool expectCarry; };
    static constexpr Case cases[] = {
        {0, 0x80, 0x01, true},  // RLC: bit7 -> bit0 and carry
        {1, 0x01, 0x80, true},  // RRC: bit0 -> bit7 and carry
        {2, 0x80, 0x00, true},  // RL:  carry-in 0 shifted in, bit7 out
        {3, 0x01, 0x00, true},  // RR:  carry-in 0 shifted in, bit0 out
        {4, 0x80, 0x00, true},  // SLA: bit7 out, 0 shifted in
        {5, 0x81, 0xC0, true},  // SRA: bit0 out, bit7 preserved (arithmetic)
        {6, 0x1A, 0xA1, false}, // SWAP: nibble swap, carry always false
        {7, 0x01, 0x00, true},  // SRL: bit0 out, 0 shifted in
    };

    for (auto [opIndex, input, expected, expectCarry] : cases) {
        for (u8 regIndex = 0; regIndex < 8; ++regIndex) {
            u8 cbOpcode = static_cast<u8>((opIndex << 3) | regIndex);
            System sys({0xCB, cbOpcode});
            sys.cpu.hl.set(0xC050); // valid target for regIndex == 6
            sys.cpu.f.carry = false; // carry-in for RL/RR
            setRegValue(sys, regIndex, input);

            int expectedCycles = (regIndex == 6) ? 16 : 8;
            CHECK(sys.cpu.step() == expectedCycles);
            CHECK(getRegValue(sys, regIndex) == expected);
            CHECK(sys.cpu.f.carry == expectCarry);
        }
    }
}

TEST_CASE("Cpu executes CB BIT for all 8 bits and all 8 registers") {
    for (u8 bitNum = 0; bitNum < 8; ++bitNum) {
        for (u8 regIndex = 0; regIndex < 8; ++regIndex) {
            u8 cbOpcode = static_cast<u8>(0x40 | (bitNum << 3) | regIndex);
            int expectedCycles = (regIndex == 6) ? 12 : 8;

            System setSys({0xCB, cbOpcode});
            setSys.cpu.hl.set(0xC050);
            setRegValue(setSys, regIndex, static_cast<u8>(1 << bitNum));
            CHECK(setSys.cpu.step() == expectedCycles);
            CHECK(setSys.cpu.f.zero == false);
            CHECK(setSys.cpu.f.subtract == false);
            CHECK(setSys.cpu.f.halfCarry == true);
            CHECK(getRegValue(setSys, regIndex) == static_cast<u8>(1 << bitNum)); // BIT never modifies the value

            System clearSys({0xCB, cbOpcode});
            clearSys.cpu.hl.set(0xC050);
            setRegValue(clearSys, regIndex, static_cast<u8>(~(1 << bitNum)));
            CHECK(clearSys.cpu.step() == expectedCycles);
            CHECK(clearSys.cpu.f.zero == true);
        }
    }
}

TEST_CASE("Cpu executes CB RES and SET for all 8 bits and all 8 registers") {
    for (u8 bitNum = 0; bitNum < 8; ++bitNum) {
        for (u8 regIndex = 0; regIndex < 8; ++regIndex) {
            int expectedCycles = (regIndex == 6) ? 16 : 8;

            u8 resOpcode = static_cast<u8>(0x80 | (bitNum << 3) | regIndex);
            System resSys({0xCB, resOpcode});
            resSys.cpu.hl.set(0xC050);
            setRegValue(resSys, regIndex, 0xFF);
            CHECK(resSys.cpu.step() == expectedCycles);
            CHECK(getRegValue(resSys, regIndex) == static_cast<u8>(~(1 << bitNum) & 0xFF));

            u8 setOpcode = static_cast<u8>(0xC0 | (bitNum << 3) | regIndex);
            System setSys({0xCB, setOpcode});
            setSys.cpu.hl.set(0xC050);
            setRegValue(setSys, regIndex, 0x00);
            CHECK(setSys.cpu.step() == expectedCycles);
            CHECK(getRegValue(setSys, regIndex) == static_cast<u8>(1 << bitNum));
        }
    }
}

// --- Ppu ----------------------------------------------------------------

TEST_CASE("Ppu renders a scanline through BGP: raw color index is remapped to a shade") {
    Ppu ppu;
    ppu.lcdc = 0x10; // unsigned tile addressing from 0x8000
    ppu.bgp = 0x1B;  // 00 01 10 11: color 1 remaps to shade 2

    ppu.writeVRAM(0x8000, 0xFF); // tile 0, row 0, low byte: all bits set
    ppu.writeVRAM(0x8001, 0x00); // high byte: all clear -> raw color index 1 per pixel
    ppu.writeVRAM(0x9800, 0x00); // background map (0,0) -> tile 0

    ppu.tick(456); // exactly one scanline

    CHECK(ppu.ly == 1);
    for (int x = 0; x < 8; ++x) {
        CHECK(ppu.framebuffer[static_cast<size_t>(x)] == 2);
    }
}
