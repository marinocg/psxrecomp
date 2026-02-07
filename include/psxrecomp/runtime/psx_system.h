#pragma once

#include "psxrecomp/types.h"

namespace psxrecomp
{
namespace runtime
{

/**
 * @brief PSX System interface
 *
 * Provides the runtime environment for recompiled PSX code,
 * including memory management and hardware emulation.
 */
class PsxSystem
{
  public:
    PsxSystem();
    ~PsxSystem();

    /**
     * @brief Initialize the PSX system
     * @return true if successful
     */
    bool initialize();

    /**
     * @brief Run a single frame
     */
    void runFrame();

    /**
     * @brief Read from PSX memory
     * @param address Memory address
     * @return Value at address
     */
    template <typename T> T read(Address address);

    /**
     * @brief Write to PSX memory
     * @param address Memory address
     * @param value Value to write
     */
    template <typename T> void write(Address address, T value);

    /**
     * @brief Get pointer to RAM
     * @return Pointer to 2MB RAM
     */
    u8* getRam();

  private:
    u8* m_ram;        // 2MB main RAM
    u8* m_scratchpad; // 1KB scratchpad
    u8* m_bios;       // 512KB BIOS

    void initMemory();
    void cleanupMemory();
};

} // namespace runtime
} // namespace psxrecomp
