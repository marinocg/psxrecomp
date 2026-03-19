# CD-ROM Emulation Roadmap

This roadmap tracks runtime CD-ROM emulation work needed for accurate PSX execution.

## Current Status
- [x] Index-selected MMIO register bank (`1F801800h` index + `+1..+3` logical ports) wired for command/response/data/IRQ paths.
- [x] IRQ queue model: ACK-driven promotion of queued IRQ types with interrupt flag/enable masking, plus PSX-SPX-aligned HCLRCTL semantics (drain unread result bytes and defer buffered read `INT1` promotion until after a short post-ACK gap).
- [x] BIOS CD-ROM IRQ ordering now follows the PSX-SPX priority-chain model: the BIOS-owned CD path is serviced as part of the priority-0 `SysEnqIntRP` chain before lower-priority user handlers and before `HookEntryInt`.
- [x] BIOS/kernel CD event routing split: raw IRQ subtype delivery now matches PSX-SPX (`INT3->0x0010`, `INT2->0x0020`, `INT1->0x0040`, `INT4->0x0080`, `INT5->0x8000`), while BIOS-owned async helpers still translate their documented completion events onto `0x0020`.
- [x] Basic command responses and interrupt flag plumbing.
- [x] Boot-critical command coverage (`Getstat`, `Setloc`, `SeekL`, `ReadN/ReadS`, `Pause/Stop`,
      `Setmode`, `GetlocL/GetlocP`, `GetTN/GetTD`, `GetID`) with command-specific response shapes.
- [x] Setloc/read pipeline with MSF->LBA conversion (pregap), timed sector cadence, and data-fifo payload selection (0x800 vs 0x924 mode).
- [x] Data-sector FIFO path with DMA-readable payload words.
- [x] Bounded ReadN/ReadS sector-ready signaling: INT1 now reflects the buffered drive window rather than an unbounded software queue, so overrun can skip older sectors per PSX-SPX.
- [x] Finite-read completion path: when `ReadN/ReadS` can no longer produce another valid sector, the controller now leaves `readActive`, drains any already-buffered INT1 sectors, then emits a single `INT4/DataEnd` completion instead of remaining in a silent active-read limbo.
- [x] Explicit buffered/published/draining sector ownership: the next buffered sector, the current INT1-published sector, and the sector currently backing `RDDATA`/DMA are now tracked separately, so late INT1 publication no longer rewrites the identity of bytes that are still draining.
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
