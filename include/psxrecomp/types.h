#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace psxrecomp
{

/**
 * @brief Main types used throughout the project
 */

// Basic PSX types
using u8 = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8 = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

// PSX memory addresses
using Address = u32;

// PSX register index
using Register = u8;

/**
 * @brief PSX memory map constants
 */
namespace MemoryMap
{
constexpr Address RAM_BASE = 0x00000000;
constexpr Address RAM_SIZE = 0x00200000; // 2MB
constexpr Address RAM_MIRROR = 0x00200000;

constexpr Address SCRATCHPAD_BASE = 0x1F800000;
constexpr Address SCRATCHPAD_SIZE = 0x00000400; // 1KB

constexpr Address IO_BASE = 0x1F801000;
constexpr Address IO_SIZE = 0x00001000;

constexpr Address BIOS_BASE = 0x1FC00000;
constexpr Address BIOS_SIZE = 0x00080000; // 512KB
} // namespace MemoryMap

/**
 * @brief MIPS R3000 register names
 */
namespace Registers
{
constexpr Register ZERO = 0; // Always 0
constexpr Register AT = 1;   // Assembler temporary
constexpr Register V0 = 2;   // Return values
constexpr Register V1 = 3;
constexpr Register A0 = 4; // Arguments
constexpr Register A1 = 5;
constexpr Register A2 = 6;
constexpr Register A3 = 7;
constexpr Register T0 = 8; // Temporaries
constexpr Register T1 = 9;
constexpr Register T2 = 10;
constexpr Register T3 = 11;
constexpr Register T4 = 12;
constexpr Register T5 = 13;
constexpr Register T6 = 14;
constexpr Register T7 = 15;
constexpr Register S0 = 16; // Saved
constexpr Register S1 = 17;
constexpr Register S2 = 18;
constexpr Register S3 = 19;
constexpr Register S4 = 20;
constexpr Register S5 = 21;
constexpr Register S6 = 22;
constexpr Register S7 = 23;
constexpr Register T8 = 24; // More temporaries
constexpr Register T9 = 25;
constexpr Register K0 = 26; // Kernel
constexpr Register K1 = 27;
constexpr Register GP = 28; // Global pointer
constexpr Register SP = 29; // Stack pointer
constexpr Register FP = 30; // Frame pointer
constexpr Register RA = 31; // Return address

constexpr int NUM_REGISTERS = 32;
} // namespace Registers

} // namespace psxrecomp
