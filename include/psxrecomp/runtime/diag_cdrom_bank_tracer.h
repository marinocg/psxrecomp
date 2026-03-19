#pragma once

#include "psxrecomp/types.h"

#include <string>

namespace psxrecomp
{
namespace runtime
{

/// Lightweight bank-aware tracer for CDROM host-interface registers.
///
/// PSX-SPX §CDROM registers are "banked": address offset 0..3 maps to
/// different hardware registers depending on the current bank (bits 0-1 of
/// offset-0, i.e. status register).
///
/// This tracer is fed each byte-level CDROM access together with the bank
/// that was *current at that moment* (read from m_index via readStatus() & 3).
/// It classifies the access into one of the named logical registers and
/// increments per-register counters, records last-seen values, and tracks
/// whether INT1/INT3 were both observed.
///
/// The tracer is zero-overhead when no profile is loaded because
/// PsxSystem calls it only when it is enabled.
class DiagCdromBankTracer
{
  public:
    DiagCdromBankTracer() = default;

    /// Enable the tracer. Called during profile load.
    void enable();

    /// Return true if the tracer is active.
    bool isEnabled() const;

    /// Record a byte write to the CDROM register file.
    ///
    /// @param offset  Physical offset from 0x1F801800 (0..3).
    /// @param bank    Current bank index (0..3), from readStatus() & 0x3.
    /// @param value   Byte value written.
    void recordWrite(u8 offset, u8 bank, u8 value);

    /// Record a byte read from the CDROM register file.
    ///
    /// @param offset  Physical offset from 0x1F801800 (0..3).
    /// @param bank    Current bank index (0..3), from readStatus() & 0x3.
    /// @param value   Byte value read.
    void recordRead(u8 offset, u8 bank, u8 value);

    /// Return a multi-line human-readable summary of all captured activity.
    /// Suitable for embedding in the stall report.
    std::string formatSummary() const;

    /// Reset all counters and state (but keep enabled status).
    void reset();

  private:
    // --- Per-register counters and last values ---

    // Offset 0 (any bank – bank select / status)
    u32 m_bankSelectWriteCount = 0;
    u8 m_lastBankSelectWrite = 0;
    u32 m_statusReadCount = 0;
    u8 m_lastStatusRead = 0;

    // Offset 1 writes: bank 0 → COMMAND, banks 1/2/3 → SOUND MAP DATA (no-op)
    u32 m_commandWriteCount = 0; // bank 0
    u8 m_lastCommandWrite = 0;
    u32 m_soundMapWriteCount = 0; // banks 1/2/3

    // Offset 1 reads: bank 1/3 → HINTSTS (INT flags), bank 0/2 → RESPONSE FIFO
    u32 m_hintStsReadCount = 0; // banks 1/3 reads
    u8 m_lastHintStsRead = 0;
    u32 m_responseFifoReadCount = 0; // banks 0/2 reads
    u8 m_lastResponseFifoRead = 0;

    // Offset 2 writes: bank 0 → PARAM FIFO, bank 1 → HINTMSK, banks 2/3 → audio vol
    u32 m_paramPushCount = 0;    // bank 0
    u32 m_hintMskWriteCount = 0; // bank 1
    u8 m_lastHintMskWrite = 0;
    u32 m_audioVolWriteCount = 0; // banks 2/3

    // Offset 2 reads: bank 0/2 → HINTMSK, bank 1/3 → HINTSTS
    u32 m_hintMskReadCount = 0; // banks 0/2 reads
    u8 m_lastHintMskRead = 0;

    // Offset 3 writes: bank 0 → REQUEST (BFRD/HCLRCTL), bank 1 → HCLRCTL (IRQ ack),
    //                  banks 2/3 → audio vol
    u32 m_requestWriteCount = 0; // bank 0
    u8 m_lastRequestWrite = 0;
    u32 m_hclrctlWriteCount = 0; // bank 1
    u8 m_lastHclrctlWrite = 0;
    u32 m_audioVol3WriteCount = 0; // banks 2/3

    // Offset 3 reads: bank 0/2 → HINTMSK, bank 1/3 → HINTSTS
    u32 m_hintSts3ReadCount = 0; // banks 1/3 reads (offset 3)
    u8 m_lastHintSts3Read = 0;

    // DMA-class counters (bumped by recordRead/Write at offset 0 or host chip)
    u32 m_dma3TriggerCount = 0; // REQUEST writes with bit 7 set (BFRD)
    u32 m_dma0WriteCount = 0;   // MDEC-In DMA (not CDROM, tracked elsewhere)
    u32 m_dma1WriteCount = 0;   // MDEC-Out DMA

    // INT observation flags
    bool m_sawInt1 = false; // HINTSTS read where bits 0-2 == 1
    bool m_sawInt3 = false; // HINTSTS read where bits 0-2 == 3

    // Last bank that was selected
    u8 m_lastBankSelected = 0;

    bool m_enabled = false;
};

} // namespace runtime
} // namespace psxrecomp
