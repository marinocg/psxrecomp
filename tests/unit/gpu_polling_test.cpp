#include "psxrecomp/runtime/psx_system.h"

#include <stdexcept>

int main()
{
    using psxrecomp::runtime::PsxSystem;

    PsxSystem system;
    if (!system.initialize())
    {
        throw std::runtime_error("failed to initialize GPU polling test system");
    }

    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0, 0xE2001357u);
    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x10000002u);

    bool observedExpectedLatch = false;
    for (int iteration = 0; iteration < 4; ++iteration)
    {
        const auto gpustat =
            system.readMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1);
        const auto gpuread =
            system.readMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0);
        if ((gpustat & (1u << 27)) == 0 && gpuread == 0x00001357u)
        {
            observedExpectedLatch = true;
            break;
        }
    }
    if (!observedExpectedLatch)
    {
        throw std::runtime_error("GP1(10h) result was not visible immediately via GPUREAD");
    }

    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x10000007u);
    const auto versionLatch =
        system.readMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0);
    if (versionLatch != 0x00000002u)
    {
        throw std::runtime_error("unexpected GPU version latch value");
    }

    system.writeMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x10000009u);
    const auto preservedLatch =
        system.readMmioExplicit<psxrecomp::u32>(psxrecomp::runtime::Mmio::GPU_GP0);
    if (preservedLatch != versionLatch)
    {
        throw std::runtime_error("unsupported GPU internal register clobbered GPUREAD");
    }

    return 0;
}
