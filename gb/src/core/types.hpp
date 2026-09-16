#pragma once
#include <cstdint>

namespace gb {

using s8 = std::int8_t;
using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;
using u64 = std::uint64_t; // prolly bad practice
using i8 = std::int8_t;
using i16 = std::int16_t;

inline constexpr int kScreenWidth = 160;
inline constexpr int kScreenHeight = 144;
inline constexpr double kClockHz = 4194304.0;
} // namespace gb