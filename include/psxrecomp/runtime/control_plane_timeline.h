#pragma once

#include "psxrecomp/types.h"

#include <array>
#include <cstddef>

namespace psxrecomp
{
namespace runtime
{

/**
 * @brief Event kinds recorded in the control-plane timeline ring buffer.
 *
 * Each kind maps to a specific hardware or emulator control-plane transition.
 * The compact encoding ensures the ring buffer stays cache-friendly.
 */
enum class CpEventKind : u8
{
    // I_STAT / I_MASK
    IStatSet,    ///< I_STAT bit(s) raised by a device edge
    IStatClear,  ///< I_STAT written by software (game ack or chain handler)
    IMaskWrite,  ///< I_MASK register written

    // DMA DICR
    DicrWrite,   ///< Software wrote DICR (bits 16-23 enable; W1C flag bits 24-30)
    DicrFlagSet, ///< Channel completion flag latched (bit 24+N) after transfer

    // DMA DPCR
    DpcrWrite,   ///< Software wrote DPCR (channel master-enable bits)

    // DMA CHCR
    ChcrWrite,        ///< Software wrote CHCR (arm/start)
    ChcrTriggerClear, ///< CHCR bit 28 cleared by hardware when transfer begins
    ChcrBusyClear,    ///< CHCR bit 24 cleared by hardware when transfer completes

    // DMA transfer lifecycle
    DmaTransferStart,    ///< DMA transfer begins (port + base address)
    DmaTransferDeferred, ///< DMA transfer deferred (CDROM DRQSTS not ready)
    DmaTransferDone,     ///< DMA transfer complete (words moved)

    // Device IRQ edges
    DeviceIrqRise, ///< Device IRQ line rose (false→true edge)
    DeviceIrqFall, ///< Device IRQ line fell (true→false edge)

    // Dispatcher
    DispatcherEnter, ///< InterruptDispatcher::serviceInterrupts entered
    DispatcherAck,   ///< Dispatcher acknowledged an IRQ line (wrote I_STAT)
    DispatcherExit,  ///< Dispatcher returned
};

/**
 * @brief A single entry in the control-plane timeline ring buffer.
 *
 * Kept to 16 bytes so entries pack tightly into cache lines.
 */
struct CpEvent
{
    CpEventKind kind = CpEventKind::IStatSet;
    u8 aux = 0;   ///< Port index for DMA events; line bit for IRQ events
    u16 pad = 0;
    u32 value = 0;  ///< Primary payload (register value, word count, etc.)
    u32 before = 0; ///< State snapshot before the operation (I_STAT, CHCR, …)
    u32 after = 0;  ///< State snapshot after the operation
};

static_assert(sizeof(CpEvent) == 16, "CpEvent must be 16 bytes");

/**
 * @brief Lock-free, power-of-two ring buffer recording control-plane events.
 *
 * Intended as a post-mortem diagnostic tool: when a game stalls or the IRQ
 * pipeline diverges from expected behaviour, dump the last N events to
 * understand the sequence of hardware register changes that led to the stall.
 *
 * The buffer overwrites the oldest entry when full (no blocking).
 *
 * @tparam N  Capacity in events; must be a power of two.
 */
template <size_t N>
class ControlPlaneTimeline
{
    static_assert((N & (N - 1)) == 0, "ControlPlaneTimeline capacity must be a power of two");

  public:
    static constexpr size_t Capacity = N;

    void reset()
    {
        m_head = 0;
        m_count = 0;
    }

    /// Record a new event.  O(1), no allocation, no locks.
    void push(CpEventKind kind, u8 aux, u32 value, u32 before, u32 after)
    {
        const size_t slot = m_head & (N - 1);
        m_entries[slot] = CpEvent{kind, aux, 0, value, before, after};
        m_head++;
        if (m_count < N)
        {
            m_count++;
        }
    }

    /// Number of valid entries (≤ N).
    size_t count() const
    {
        return m_count;
    }

    /**
     * @brief Read the i-th oldest entry (0 = oldest still in buffer).
     *
     * Returns a zero-initialised CpEvent if i ≥ count().
     */
    CpEvent at(size_t i) const
    {
        if (i >= m_count)
        {
            return CpEvent{};
        }
        const size_t oldest = (m_count < N) ? 0 : (m_head & (N - 1));
        const size_t slot = (oldest + i) & (N - 1);
        return m_entries[slot];
    }

    /// Read the most-recent entry (equivalent to at(count()-1)).
    CpEvent latest() const
    {
        if (m_count == 0)
        {
            return CpEvent{};
        }
        return at(m_count - 1);
    }

  private:
    std::array<CpEvent, N> m_entries{};
    size_t m_head = 0;
    size_t m_count = 0;
};

/// Concrete timeline used throughout the runtime (256 events ≈ 4 KB).
using RuntimeControlPlaneTimeline = ControlPlaneTimeline<256>;

/// Human-readable label for a CpEventKind.
const char* cpEventKindLabel(CpEventKind kind);

} // namespace runtime
} // namespace psxrecomp
