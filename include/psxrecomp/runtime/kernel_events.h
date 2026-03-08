#pragma once

#include "psxrecomp/runtime/interrupt_controller.h"
#include "psxrecomp/types.h"

#include <array>
#include <cstddef>
#include <functional>
#include <vector>

namespace psxrecomp
{
namespace runtime
{

/**
 * @brief PSX kernel event class identifiers (matches BIOS convention).
 *
 * The class determines which hardware interrupt or software condition can
 * trigger the event.  On real hardware the upper 4 bits encode the IRQ
 * source; here we use the canonical constants from PSX-SPX.
 */
namespace EventClass
{
constexpr u32 VBlank = 0xF0000001; ///< Vertical-blank interrupt
constexpr u32 VBlankAlt =
    0xF2000002;                   ///< Alternate VBlank callback class used by some BIOS/libetc code
constexpr u32 Gpu = 0xF0000002;   ///< GPU interrupt
constexpr u32 Cdrom = 0xF0000003; ///< CD-ROM interrupt
constexpr u32 Dma = 0xF0000004;   ///< DMA interrupt
constexpr u32 Timer0 = 0xF0000005;     ///< Root counter 0
constexpr u32 Timer1 = 0xF0000006;     ///< Root counter 1
constexpr u32 Timer2 = 0xF0000006;     ///< Root counter 2 (shares class with Timer1 on BIOS)
constexpr u32 Controller = 0xF0000008; ///< Joypad / memory-card
constexpr u32 Spu = 0xF0000009;        ///< SPU interrupt
constexpr u32 Pio = 0xF000000A;        ///< Parallel I/O
constexpr u32 Sio = 0xF000000B;        ///< Serial I/O
constexpr u32 Card = 0xF0000011;       ///< Memory-card BIOS events
} // namespace EventClass

/**
 * @brief PSX kernel event spec identifiers (matches BIOS convention).
 */
namespace EventSpec
{
constexpr u16 Counter = 0x0001;       ///< Counter becomes zero
constexpr u16 Interrupted = 0x0002;   ///< Interrupt occurred
constexpr u16 EndOfIO = 0x0004;       ///< End of I/O
constexpr u16 FileClosed = 0x0008;    ///< File closed
constexpr u16 CommandAck = 0x0010;    ///< Command acknowledged
constexpr u16 CommandDone = 0x0020;   ///< Command completed
constexpr u16 DataReady = 0x0040;     ///< Data ready
constexpr u16 DataEnd = 0x0080;       ///< Data end
constexpr u16 Timeout = 0x0100;       ///< Timeout
constexpr u16 UnknownCmd = 0x0200;    ///< Unknown command
constexpr u16 EndOfRead = 0x0400;     ///< End of read
constexpr u16 NewDevice = 0x0800;     ///< New device detected
constexpr u16 SysReq = 0x1000;        ///< System request
constexpr u16 Error = 0x2000;         ///< Error
constexpr u16 BusError = 0x4000;      ///< Prev-write bus error
constexpr u16 CardNewDevice = 0x0004; ///< Memory-card: new device
constexpr u16 CardIoError = 0x8000;   ///< Memory-card: I/O error
} // namespace EventSpec

/**
 * @brief Event mode (callback vs. polling).
 */
enum class EventMode : u32
{
    Callback = 0x1000,  ///< Invoke a callback on delivery
    NoCallback = 0x2000 ///< Polling only (tested with TestEvent)
};

/**
 * @brief Event status flags.
 */
enum class EventStatus : u32
{
    Free = 0x0000,     ///< Slot is unused
    Disabled = 0x1000, ///< Allocated but disabled
    Enabled = 0x2000,  ///< Enabled, waiting for delivery
    Delivered = 0x4000 ///< Event has been delivered (ready for test)
};

/**
 * @brief A single kernel event descriptor.
 */
struct KernelEvent
{
    u32 classId = 0;
    u16 spec = 0;
    EventMode mode = EventMode::NoCallback;
    EventStatus status = EventStatus::Free;
    u32 callbackAddress = 0;
};

/**
 * @brief Manages the PSX BIOS kernel event table.
 *
 * Provides the semantics of OpenEvent, CloseEvent, EnableEvent,
 * DisableEvent, TestEvent, DeliverEvent and UnDeliverEvent as
 * documented in PSX-SPX.
 */
class KernelEventTable
{
  public:
    /// Maximum number of event handles (matches PSX BIOS).
    static constexpr size_t MAX_EVENTS = 32;

    /// PSX event handles are ORed with 0xF1000000 + slot<<4.
    static constexpr u32 HANDLE_BASE = 0xF1000000u;

    KernelEventTable();

    /**
     * @brief Reset all event slots to free.
     */
    void reset();

    /**
     * @brief Allocate an event (BIOS B0:08 OpenEvent).
     * @param classId  Event class (e.g. EventClass::VBlank).
     * @param spec     Event spec (e.g. EventSpec::Counter).
     * @param mode     Callback or polling mode.
     * @param callbackAddress  PSX address of callback function (0 = none).
     * @return Event handle, or 0xFFFFFFFF on failure.
     */
    u32 openEvent(u32 classId, u16 spec, EventMode mode, u32 callbackAddress);

    /**
     * @brief Free an event slot (BIOS B0:09 CloseEvent).
     * @return true if the handle was valid.
     */
    bool closeEvent(u32 handle);

    /**
     * @brief Enable an event (BIOS B0:0C EnableEvent).
     * @return true if the handle was valid and the event was disabled or already enabled.
     */
    bool enableEvent(u32 handle);

    /**
     * @brief Disable an event (BIOS B0:0D DisableEvent).
     * @return true if the handle was valid.
     */
    bool disableEvent(u32 handle);

    /**
     * @brief Test if an event has been delivered (BIOS B0:0B TestEvent).
     * @return 1 if the event was delivered, 0 otherwise.
     *
     * For delivered events in NoCallback mode, this clears the delivered
     * flag and resets to Enabled.
     */
    u32 testEvent(u32 handle) const;

    /**
     * @brief Mark event as delivered (BIOS B0:07 DeliverEvent).
     *
     * NoCallback events transition to Delivered.
     * Callback events remain Enabled and are invoked by dispatcher/BIOS paths.
     */
    void deliverEvent(u32 handle);

    /**
     * @brief Deliver to all enabled events matching a class/spec pair.
     *
     * This is the primary path used by the interrupt dispatcher when a
     * hardware IRQ fires. Returns the list of callback addresses that
     * need invocation.
     */
    std::vector<u32> deliverByClassSpec(u32 classId, u16 spec);

    /**
     * @brief Undeliver an event (BIOS B0:20 UnDeliverEvent).
     *
     * Resets the Delivered flag back to Enabled.
     */
    void undeliverEvent(u32 handle);

    /**
     * @brief Read-only access to an event slot for inspection/testing.
     * @return Pointer to the event, or nullptr if handle invalid.
     */
    const KernelEvent* getEvent(u32 handle) const;

    /**
     * @brief Check whether an event has been delivered (non-mutating).
     *
     * Unlike testEvent(), this does NOT reset the delivered flag.
     * Used by the WaitEvent busy-loop to poll without consuming delivery.
     * @return true if the event status is Delivered.
     */
    bool isEventDelivered(u32 handle) const;

    /**
     * @brief Map an InterruptLine to the corresponding event class.
     */
    static u32 interruptLineToEventClass(InterruptLine line);

  private:
    std::array<KernelEvent, MAX_EVENTS> m_events;

    /**
     * @brief Decode a handle into a slot index.
     * @return Slot index, or MAX_EVENTS on invalid handle.
     */
    size_t handleToIndex(u32 handle) const;
};

} // namespace runtime
} // namespace psxrecomp
