#pragma once

#include "psxrecomp/types.h"

#include <functional>

namespace psxrecomp
{
namespace runtime
{

class InputController; // forward declaration
class Scheduler;       // forward declaration

/**
 * @brief SIO0 (Controller / Memory Card serial interface) register block.
 *
 * Maps to the PSX registers at 0x1F801040..0x1F80104F.
 *
 * PSX-SPX defines the following registers:
 *   0x1F801040  JOY_TX_DATA (W) / JOY_RX_DATA (R)   8-bit
 *   0x1F801044  JOY_STAT                             32-bit (R)
 *   0x1F801048  JOY_MODE                             16-bit (R/W)
 *   0x1F80104A  JOY_CTRL                             16-bit (R/W)
 *   0x1F80104E  JOY_BAUD                             16-bit (R/W)
 *
 * This implementation models SIO0 as a bus with device routing.
 * The first TX byte selects the target device:
 *   0x01 — Controller (digital pad, type 0x41, command 0x42).
 *   0x81 — Memory Card (not implemented; cleanly returns no-ACK).
 * JOY_CTRL.13 selects the physical port (port 2 is disconnected).
 * ACK/IRQ delivery uses the scheduler for timing accuracy.
 */
class Sio0
{
  public:
    /// Callback signature for raising InterruptLine::Controller.
    using IrqCallback = std::function<void()>;

    /// Register offsets relative to CONTROLLER_BASE (0x1F801040).
    enum Register : Address
    {
        Data = 0x00, ///< JOY_TX_DATA (W) / JOY_RX_DATA (R)
        Stat = 0x04, ///< JOY_STAT (R)
        Mode = 0x08, ///< JOY_MODE (R/W)
        Ctrl = 0x0A, ///< JOY_CTRL (R/W)
        Baud = 0x0E, ///< JOY_BAUD (R/W)
    };

    /// Approximate CPU cycles between TX write and ACK/IRQ assertion.
    static constexpr u32 ACK_DELAY_CYCLES = 100;

    void reset();

    /// Wire up the host-side input source.  Must be called before any
    /// transfer can produce meaningful button data.
    void setInputController(InputController* input);

    /// Wire up the scheduler for ACK-delay timing.
    void setScheduler(Scheduler* scheduler);

    /// Install the callback invoked when the controller IRQ should fire.
    void setIrqCallback(IrqCallback callback);

    // ----------------------------------------------------------------
    //  Sized register access used by the MMIO router.
    // ----------------------------------------------------------------

    /// 8-bit read  (address is offset from CONTROLLER_BASE).
    u8 read8(Address offset);

    /// 8-bit write (address is offset from CONTROLLER_BASE).
    void write8(Address offset, u8 value);

    /// 16-bit read.
    u16 read16(Address offset);

    /// 16-bit write.
    void write16(Address offset, u16 value);

    /// 32-bit read (JOY_STAT is the only meaningful 32-bit readable register).
    u32 read32(Address offset);

    /// 32-bit write (no 32-bit writeable registers on real HW, but accept gracefully).
    void write32(Address offset, u32 value);

    // ----------------------------------------------------------------
    //  Direct accessors – useful for tests and future protocol layer.
    // ----------------------------------------------------------------

    u32 stat() const;
    u16 mode() const;
    u16 ctrl() const;
    u16 baud() const;
    u8 rxData() const;

  private:
    /// Apply the side-effects of a JOY_CTRL write (acknowledge, reset, etc.).
    void applyCtrlSideEffects(u16 value);

    /// Process one TX byte through the digital-pad state machine.
    /// Updates m_rxData and JOY_STAT bits (RX Not Empty, /ACK).
    void processTransferByte(u8 txByte);

    /// Reset the protocol state machine (called on /CS deassert / reset).
    void resetProtocol();

    /// Schedule the ACK-delay event that clears STAT.7 and, when CTRL.12
    /// (IRQ enable) is set, also asserts STAT.9 and raises the IRQ.
    void scheduleAckIrq();

    /// Execute the ACK pulse completion: always clear STAT.7; only assert
    /// STAT.9 and fire the IRQ callback when CTRL.12 is set.
    /// @param generation  The protocol generation captured at schedule time;
    ///                    if it no longer matches m_ackGeneration the transfer
    ///                    was reset and this callback is stale.
    void fireAckIrqNow(u32 generation);

    // ----- Device routing -----
    /// Identifies the device selected by the first address byte on the bus.
    enum class DeviceType : u8
    {
        None,       ///< No device addressed yet / unknown address.
        Controller, ///< Standard controller (address 0x01).
        MemoryCard, ///< Memory card (address 0x81) — not yet implemented.
    };

    // ----- Protocol state machine -----
    enum class ProtoState : u8
    {
        Idle,         ///< Waiting for address byte.
        Selected,     ///< Address matched, waiting for command byte.
        Transferring, ///< Sending response data bytes.
        Deselected,   ///< Unrecognised/unimplemented device addressed; absorb
                      ///< remaining bytes until /CS (/JOY) is deasserted.
    };

    DeviceType m_activeDevice = DeviceType::None;
    ProtoState m_protoState = ProtoState::Idle;
    u8 m_protoByteIndex = 0;      ///< Current data byte within the transfer.
    u16 m_protoButtons = 0xFFFFu; ///< Latched button word for current transfer.
    u32 m_ackGeneration = 0;      ///< Incremented on every resetProtocol(); used to
                                  ///< invalidate stale scheduled ACK callbacks.
    InputController* m_input = nullptr;
    Scheduler* m_scheduler = nullptr;
    IrqCallback m_irqCallback;

    // ----- Register latches -----
    u8 m_rxData = 0xFF;  ///< Last received byte (read via JOY_RX_DATA).
    u32 m_stat = 0x0005; ///< JOY_STAT reset default: TX Ready 1 & 2.
    u16 m_mode = 0x000D; ///< JOY_MODE reset default: 8-bit, no parity, MUL1.
    u16 m_ctrl = 0x0000; ///< JOY_CTRL reset default: everything off.
    u16 m_baud = 0x0088; ///< JOY_BAUD reset default: ~250 kHz with MUL1.
};

} // namespace runtime
} // namespace psxrecomp
