# CD-ROM Emulation Roadmap

This roadmap tracks runtime CD-ROM emulation work needed for accurate PSX execution.

## Current Status
- [x] Command/status register skeleton exposed through MMIO.
- [x] Basic command responses and interrupt flag plumbing.
- [x] Data-sector FIFO path with DMA-readable payload words.
- [x] XA streaming baseline (Setmode + ReadN/ReadS sector pumping).

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
- [x] Feed XA payload bytes into the data FIFO with header skip behavior.

## Phase 4: Accuracy Expansion (Post-M3)
- [ ] Command-specific timing/latency model (seek/read transitions, busy windows).
- [ ] Full sector/subheader validation and error code behavior.
- [ ] XA-ADPCM decode handoff into SPU mixing path.
- [ ] Hardware trace parity tests for command/result sequencing.
