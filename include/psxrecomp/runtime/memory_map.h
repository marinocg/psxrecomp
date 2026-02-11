#pragma once

#include "psxrecomp/types.h"

namespace psxrecomp
{
namespace runtime
{
namespace Mmio
{
constexpr Address DMA_BASE = 0x1F801080;
constexpr Address DMA_SIZE = 0x00000080;

constexpr Address INTERRUPT_STATUS = 0x1F801070;
constexpr Address INTERRUPT_MASK = 0x1F801074;

constexpr Address TIMER_BASE = 0x1F801100;
constexpr Address TIMER_SIZE = 0x00000030;

constexpr Address GPU_GP0 = 0x1F801810;
constexpr Address GPU_GP1 = 0x1F801814;

constexpr Address CDROM_BASE = 0x1F801800;
constexpr Address CDROM_SIZE = 0x00000004;

constexpr Address SPU_BASE = 0x1F801C00;
constexpr Address SPU_SIZE = 0x00000200;

constexpr Address CONTROLLER_BASE = 0x1F801040;
constexpr Address CONTROLLER_SIZE = 0x00000010;
} // namespace Mmio
} // namespace runtime
} // namespace psxrecomp
