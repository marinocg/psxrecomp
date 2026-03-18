# CD-ROM Emulation Roadmap

This roadmap tracks runtime CD-ROM emulation work needed for accurate PSX execution.

## Current Status
- [x] Index-selected MMIO register bank (`1F801800h` index + `+1..+3` logical ports) wired for command/response/data/IRQ paths.
- [x] IRQ queue model: ACK-driven promotion of queued IRQ types with interrupt flag/enable masking.
- [x] Basic command responses and interrupt flag plumbing.
- [x] Boot-critical command coverage (`Getstat`, `Setloc`, `SeekL`, `ReadN/ReadS`, `Pause/Stop`,
      `Setmode`, `GetlocL/GetlocP`, `GetTN/GetTD`, `GetID`) with command-specific response shapes.
- [x] Setloc/read pipeline with MSF->LBA conversion (pregap), timed sector cadence, and data-fifo payload selection (0x800 vs 0x924 mode).
- [x] Data-sector FIFO path with DMA-readable payload words.
- [x] XA streaming baseline (Setmode + ReadN/ReadS sector pumping).
- [x] XA subheader-aware payload path (Mode2/Form2 validation + Setfilter file/channel matching).
- [x] XA ADPCM sector decode path feeding SPU CD-audio mixer input.
- [x] XA sector classification and delivery tracing (PR-RV23): per-sector `xa_audio_deliver` /
      `cpu_data_deliver` / `filter_reject` / `format_reject` / `submode_reject` phase trace events;
      end-of-run counters; INT1 suppression fix for XA-ADPCM sectors; `formatXaClassificationSummary()`
      API; `cdrom_xa_classification` diagnostic explainer for `rev2.movie.diag.json`.
- [x] Disc-change door-open transition state and INT5 no-disc/read-fail error responses.
- [x] Save-state serialization/deserialization for CD-ROM runtime state (FIFOs, current LBA, queued IRQs, mode flags, partially-consumed sector buffers).

## Phase 1: Baseline Command Transport
- [x] Model command and parameter FIFOs.
- [x] Return deterministic response bytes for common commands.
- [x] Surface interrupt flags/enables for command completion.

## Phase 2: Data Path Integration
- [x] Provide sector payload buffering for runtime data reads.
- [x] Connect CD-ROM data path to DMA (CD-ROM -> RAM transfers).
- [x] Support queued sector sources from pipeline/runtime fixtures.

## Phase 3: M3 XA Readiness
- [x] Implement Setmode XA enable bit handling.
- [x] Implement ReadN/ReadS scheduling hooks with sector cadence.
- [x] Feed XA payload bytes into the data FIFO using Mode2/Form2 subheader validation.
- [x] Implement Setfilter (file/channel) matching for XA ADPCM-shaped sectors.
- [x] XA-ADPCM INT1 suppression: Mode2/Form2/audio+realtime sectors with Setmode bit6 set no longer
      generate INT1 (per PSX-SPX); they are decoded to SPU and the cadence slot is consumed silently.
- [x] XA sector classification tracing: `SectorPhaseReason` extended with 5 new enum values; per-sector
      phase ring events and end-of-run counters (`m_xaDeliveryCount`, `m_filterRejectCount`,
      `m_formatRejectCount`, `m_submodeRejectCount`, `m_cpuDeliveryCount`) exposed via
      `formatXaClassificationSummary()` and the `CdromXaClassification` diag explainer kind.

## Phase 4: Accuracy Expansion (Post-M3)
- [ ] Command-specific timing/latency model (seek/read transitions, busy windows).
- [ ] Full sector/subheader validation and error code behavior.
- [ ] Hardware trace parity tests for command/result sequencing.
