#include "psxrecomp/runtime/sio0.h"

#include "psxrecomp/runtime/input.h"
#include "psxrecomp/runtime/scheduler.h"

namespace psxrecomp
{
namespace runtime
{

// PSX-SPX reset defaults:
//   JOY_STAT : TX Ready 1 (bit 0) and TX Ready 2 (bit 2) are set.
//   JOY_MODE : 0x000D  (8-bit, no parity, MUL1).
//   JOY_CTRL : 0x0000  (all features off).
//   JOY_BAUD : 0x0088  (~250 kHz at MUL1).
void Sio0::reset()
{
    m_rxData = 0xFF;
    m_stat = 0x0005; // TX Ready 1 | TX Ready 2
    m_mode = 0x000D;
    m_ctrl = 0x0000;
    m_baud = 0x0088;
    resetProtocol();
}

void Sio0::setInputController(InputController* input)
{
    m_input = input;
}

void Sio0::setScheduler(Scheduler* scheduler)
{
    m_scheduler = scheduler;
}

void Sio0::setIrqCallback(IrqCallback callback)
{
    m_irqCallback = std::move(callback);
}

// ----------------------------------------------------------------
//  Protocol helpers
// ----------------------------------------------------------------

void Sio0::resetProtocol()
{
    m_activeDevice = DeviceType::None;
    m_protoState = ProtoState::Idle;
    m_protoByteIndex = 0;
    m_protoButtons = 0xFFFFu;
    // Increment generation to invalidate any pending scheduler ACK callbacks.
    ++m_ackGeneration;
}

/// JOY_STAT bit definitions used by the protocol layer.
static constexpr u32 STAT_TX_READY1 = 1u << 0;
static constexpr u32 STAT_RX_NOT_EMPTY = 1u << 1;
static constexpr u32 STAT_TX_READY2 = 1u << 2;
static constexpr u32 STAT_ACK_LEVEL = 1u << 7;
static constexpr u32 STAT_IRQ_REQUEST = 1u << 9;

/// JOY_CTRL bit masks.
static constexpr u16 CTRL_IRQ_ENABLE = 1u << 12;

void Sio0::scheduleAckIrq()
{
    // Always schedule the ACK-clear event — on real hardware the device
    // pulses /ACK for ~2 µs then releases it regardless of IRQ enable.
    const u32 gen = m_ackGeneration;
    if (m_scheduler)
    {
        m_scheduler->schedule(ACK_DELAY_CYCLES, [this, gen]() { fireAckIrqNow(gen); });
    }
    else
    {
        // No scheduler wired — fire immediately (unit-test fallback).
        fireAckIrqNow(gen);
    }
}

void Sio0::fireAckIrqNow(u32 generation)
{
    // Bail out if the protocol was reset after this callback was scheduled.
    if (generation != m_ackGeneration)
    {
        return;
    }
    // The /ACK pulse has ended: clear STAT.7 unconditionally.
    m_stat &= ~STAT_ACK_LEVEL;
    // Only raise the interrupt when CTRL.12 (IRQ enable) is set.
    if (m_ctrl & CTRL_IRQ_ENABLE)
    {
        m_stat |= STAT_IRQ_REQUEST;
        if (m_irqCallback)
        {
            m_irqCallback();
        }
    }
}

void Sio0::processTransferByte(u8 txByte)
{
    // Port 2 selected (CTRL bit 13) → no device connected.
    if (m_ctrl & (1u << 13))
    {
        m_rxData = 0xFF;
        m_stat |= STAT_RX_NOT_EMPTY;
        m_stat &= ~STAT_ACK_LEVEL; // no ACK from absent device
        resetProtocol();
        return;
    }

    switch (m_protoState)
    {
    case ProtoState::Idle:
        if (txByte == 0x01)
        {
            // Controller address — device present on port 1.
            m_activeDevice = DeviceType::Controller;
            m_rxData = 0xFF; // Hi-Z while device recognises address.
            m_protoState = ProtoState::Selected;
            m_stat |= STAT_RX_NOT_EMPTY | STAT_ACK_LEVEL;
            scheduleAckIrq();
        }
        else if (txByte == 0x81)
        {
            // Memory-card address — device not implemented yet.
            // Return Hi-Z with no ACK. Enter Deselected so that subsequent
            // bytes on this transfer are absorbed until /JOY is deasserted.
            m_rxData = 0xFF;
            m_stat |= STAT_RX_NOT_EMPTY;
            m_stat &= ~STAT_ACK_LEVEL;
            m_protoState = ProtoState::Deselected;
        }
        else
        {
            // Unknown address — no device responds.
            m_activeDevice = DeviceType::None;
            m_rxData = 0xFF;
            m_stat |= STAT_RX_NOT_EMPTY;
            m_stat &= ~STAT_ACK_LEVEL;
        }
        break;

    case ProtoState::Selected:
        if (txByte == 0x42)
        {
            // Command 0x42 = "Read Switches" → digital pad ID 0x41.
            m_rxData = 0x41;
            // Latch button state from InputController.
            m_protoButtons = m_input ? m_input->readState() : 0xFFFFu;
            m_protoState = ProtoState::Transferring;
            m_protoByteIndex = 0;
            m_stat |= STAT_RX_NOT_EMPTY | STAT_ACK_LEVEL;
            scheduleAckIrq();
        }
        else
        {
            // Unknown command – abort.
            m_rxData = 0xFF;
            m_stat |= STAT_RX_NOT_EMPTY;
            m_stat &= ~STAT_ACK_LEVEL;
            m_protoState = ProtoState::Idle;
        }
        break;

    case ProtoState::Deselected:
        // Unimplemented device was addressed; ignore bytes until /JOY deassert.
        m_rxData = 0xFF;
        m_stat |= STAT_RX_NOT_EMPTY;
        m_stat &= ~STAT_ACK_LEVEL;
        break;

    case ProtoState::Transferring:
        switch (m_protoByteIndex)
        {
        case 0:
            // "Ready to send data" marker.
            m_rxData = 0x5A;
            m_protoByteIndex = 1;
            m_stat |= STAT_RX_NOT_EMPTY | STAT_ACK_LEVEL;
            scheduleAckIrq();
            break;
        case 1:
            // Buttons low byte.
            m_rxData = static_cast<u8>(m_protoButtons & 0xFFu);
            m_protoByteIndex = 2;
            m_stat |= STAT_RX_NOT_EMPTY | STAT_ACK_LEVEL;
            scheduleAckIrq();
            break;
        case 2:
            // Buttons high byte – last byte, no ACK.
            m_rxData = static_cast<u8>((m_protoButtons >> 8) & 0xFFu);
            m_protoState = ProtoState::Idle;
            m_protoByteIndex = 0;
            m_stat |= STAT_RX_NOT_EMPTY;
            m_stat &= ~STAT_ACK_LEVEL;
            break;
        default:
            // Should not happen.
            m_rxData = 0xFF;
            m_stat |= STAT_RX_NOT_EMPTY;
            m_stat &= ~STAT_ACK_LEVEL;
            m_protoState = ProtoState::Idle;
            break;
        }
        break;
    }
}

// ----------------------------------------------------------------
//  8-bit access
// ----------------------------------------------------------------

u8 Sio0::read8(Address offset)
{
    if (offset == Register::Data)
    {
        // Reading JOY_RX_DATA clears the RX-Not-Empty flag.
        u8 value = m_rxData;
        m_stat &= ~STAT_RX_NOT_EMPTY;
        return value;
    }
    return 0;
}

void Sio0::write8(Address offset, u8 value)
{
    if (offset == Register::Data)
    {
        processTransferByte(value);
    }
}

// ----------------------------------------------------------------
//  16-bit access
// ----------------------------------------------------------------

u16 Sio0::read16(Address offset)
{
    switch (offset)
    {
    case Register::Data:
    {
        // 16-bit read returns RX data; clear RX-Not-Empty like 8/32-bit reads.
        u16 value = static_cast<u16>(m_rxData | (m_rxData << 8));
        m_stat &= ~STAT_RX_NOT_EMPTY;
        return value;
    }
    case Register::Stat:
        // Low 16 bits of JOY_STAT.
        return static_cast<u16>(m_stat & 0xFFFFu);
    case Register::Mode:
        return m_mode;
    case Register::Ctrl:
        return m_ctrl;
    case Register::Baud:
        return m_baud;
    default:
        return 0;
    }
}

void Sio0::write16(Address offset, u16 value)
{
    switch (offset)
    {
    case Register::Data:
        // TX data – process through protocol.
        processTransferByte(static_cast<u8>(value & 0xFFu));
        break;
    case Register::Mode:
        m_mode = value & 0x01FFu; // Bits 0–8 are defined; 9–15 always zero.
        break;
    case Register::Ctrl:
        applyCtrlSideEffects(value);
        break;
    case Register::Baud:
        m_baud = value;
        break;
    default:
        break;
    }
}

// ----------------------------------------------------------------
//  32-bit access
// ----------------------------------------------------------------

u32 Sio0::read32(Address offset)
{
    switch (offset)
    {
    case Register::Data:
    {
        // 32-bit read: all four FIFO preview slots.
        u32 value = static_cast<u32>(m_rxData) * 0x01010101u;
        m_stat &= ~STAT_RX_NOT_EMPTY;
        return value;
    }
    case Register::Stat:
        return m_stat;
    default:
        return 0;
    }
}

void Sio0::write32(Address offset, u32 value)
{
    // No 32-bit writable registers exist on real hardware, but route the
    // low half-word through the 16-bit handler for robustness.
    switch (offset)
    {
    case Register::Data:
        processTransferByte(static_cast<u8>(value & 0xFFu));
        break;
    case Register::Mode:
        write16(Register::Mode, static_cast<u16>(value));
        break;
    case Register::Ctrl:
        write16(Register::Ctrl, static_cast<u16>(value));
        break;
    case Register::Baud:
        write16(Register::Baud, static_cast<u16>(value));
        break;
    default:
        break;
    }
}

// ----------------------------------------------------------------
//  Direct accessors
// ----------------------------------------------------------------

u32 Sio0::stat() const
{
    return m_stat;
}

u16 Sio0::mode() const
{
    return m_mode;
}

u16 Sio0::ctrl() const
{
    return m_ctrl;
}

u16 Sio0::baud() const
{
    return m_baud;
}

u8 Sio0::rxData() const
{
    return m_rxData;
}

// ----------------------------------------------------------------
//  JOY_CTRL side-effects (PSX-SPX)
// ----------------------------------------------------------------

void Sio0::applyCtrlSideEffects(u16 value)
{
    // Bit 4 — Acknowledge: reset JOY_STAT bits 3 (RX Parity Error) and
    //         9 (Interrupt Request).  Write-only, not stored.
    if (value & (1u << 4))
    {
        m_stat &= ~((1u << 3) | (1u << 9));
    }

    // Bit 6 — Reset: reset most JOY registers to zero.  Write-only.
    if (value & (1u << 6))
    {
        // Preserve wiring across reset.
        auto* savedInput = m_input;
        auto* savedScheduler = m_scheduler;
        auto savedIrqCallback = std::move(m_irqCallback);
        reset();
        m_input = savedInput;
        m_scheduler = savedScheduler;
        m_irqCallback = std::move(savedIrqCallback);
        return; // reset() already wiped everything.
    }

    // If /JOY output (bit 1) is being deasserted, reset protocol state
    // so the next assertion starts a fresh transfer sequence.
    u16 prevCtrl = m_ctrl;

    // Store the remaining read/write-able bits.
    // Bits 4 and 6 are write-only triggers – mask them out.
    constexpr u16 storedMask = 0xFFFFu & ~((1u << 4) | (1u << 6));
    m_ctrl = value & storedMask;

    // Detect /JOY deassert (bit 1: 1→0).
    if ((prevCtrl & (1u << 1)) && !(m_ctrl & (1u << 1)))
    {
        resetProtocol();
    }
}

} // namespace runtime
} // namespace psxrecomp
