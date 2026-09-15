#pragma once
#include "types.hpp"
#include <string>
#include <vector>

namespace gb {

class Cartridge {
public:
    bool load(const std::string& path);

    u8 read(u16 addr) const;
    void write(u16 addr, u8 value);

private:
    std::vector<u8> rom;
};

} // namespace gb
