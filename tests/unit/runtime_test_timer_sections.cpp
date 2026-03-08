#include "runtime_test_timer_sections.h"

#include "psxrecomp/runtime/psx_system.h"
#include "psxrecomp/runtime/resource_pack.h"

#include <array>
#include <cassert>
#include <filesystem>
#include <fstream>

namespace MemoryMap = psxrecomp::MemoryMap;

void runRuntimeTimerChecks(psxrecomp::runtime::PsxSystem& system)
{
    using psxrecomp::Address;
    using psxrecomp::runtime::InterruptLine;

    // Timer0 target IRQ should fire once runFrame advances cycles.
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::INTERRUPT_MASK,
                                             static_cast<psxrecomp::u32>(InterruptLine::Timer0));
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x8, 3u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4, 0x0018u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0, 0u);
    assert((system.interrupts().readStatus() &
            static_cast<psxrecomp::u32>(InterruptLine::Timer0)) == 0);
    system.runFrame();
    assert((system.interrupts().readStatus() &
            static_cast<psxrecomp::u32>(InterruptLine::Timer0)) != 0);

    [[maybe_unused]] const psxrecomp::u16 timer0Mode =
        system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4);
    assert((timer0Mode & (1u << 11)) != 0);

    // Timer2 alternate divider mode should only advance every 8 CPU cycles.
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x20 + 0x4,
                                             0x0200u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x20 + 0x0, 0u);
    system.timers().tick(7, nullptr);
    assert(system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x20 +
                                                   0x0) == 0u);
    system.timers().tick(1, nullptr);
    assert(system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x20 +
                                                   0x0) == 1u);

    // Timer overflow should wrap counter in free-running mode.
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4, 0x0000u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0, 0xFFFEu);
    system.timers().tick(4, nullptr);
    assert(system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0) ==
           2u);

    // Reset-on-target with target=0 should behave as 0x10000 period (no divide-by-zero).
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4, 0x0008u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x8, 0x0000u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0, 0xFFFEu);
    system.timers().tick(4, nullptr);
    assert(system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0) ==
           2u);

    // Reset-on-target should not trigger early when counter starts above target.
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4, 0x0018u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x8, 3u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0, 10u);
    system.timers().tick(3, nullptr);
    assert(system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0) ==
           13u);

    // Target flag must not latch on overflow if target value was not crossed.
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4, 0x0000u);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x8, 0x7FFFu);
    system.writeMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x0, 0xFFFEu);
    system.timers().tick(2, nullptr);
    [[maybe_unused]] const psxrecomp::u16 noTargetOnOverflowMode =
        system.readMmioExplicit<psxrecomp::u16>(psxrecomp::runtime::Mmio::TIMER_BASE + 0x4);
    assert((noTargetOnOverflowMode & (1u << 11)) == 0u);
}

void runRuntimeResourcePackChecks()
{
    using psxrecomp::runtime::ResourcePack;

    auto tempRoot = std::filesystem::temp_directory_path() / "psxrecomp_runtime_test_assets";
    std::filesystem::create_directories(tempRoot / "textures");
    {
        std::ofstream file(tempRoot / "textures" / "logo.bin", std::ios::binary);
        const char bytes[] = {1, 2, 3, 4};
        file.write(bytes, sizeof(bytes));
    }

    ResourcePack pack;
    assert(!pack.loadFromDirectory(tempRoot / "missing"));
    assert(pack.loadFromDirectory(tempRoot));
    assert(pack.hasResource("textures/logo.bin"));
    auto resource = pack.readResource("textures/logo.bin");
    assert(resource.has_value());
    assert(resource->size() == 4);
    assert(pack.resourceCount() == 1);

    std::filesystem::remove(tempRoot / "textures" / "logo.bin");
    auto missingResource = pack.readResource("textures/logo.bin");
    assert(!missingResource.has_value());

    std::filesystem::remove_all(tempRoot);
}
