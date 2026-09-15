#include "core/cartridge.hpp"
#include <fstream>

namespace gb {

bool Cartridge::load(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        return false;

    std::streamsize size = file.tellg();
    rom.resize(static_cast<size_t>(size));

    file.seekg(0, std::ios::beg);
    return static_cast<bool>(file.read(reinterpret_cast<char*>(rom.data()), size));
}

u8 Cartridge::read(u16 addr) const {
    return (addr < rom.size()) ? rom[addr] : 0xFF; // out of range undefined
}

void Cartridge::write(u16 addr, u8 value) {
    (void) addr;
    (void) value;
}

} // namespace gb
