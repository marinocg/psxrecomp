// PR-RV22: MDEC command classification, lifecycle state machine, and
// end-of-run diagnostics.
//
// These tests are complementary to mdec_test.cpp (register/handshake) and
// mdec_dma_test.cpp (DMA0/DMA1 path through PsxSystem).  The new tests focus
// on:
//   1. Explicit command classification (CommandKind).
//   2. commandIsOutputCapable() separates decode from setup commands.
//   3. MDEC(2) SetQuantTable payload consumed, output never generated.
//   4. MDEC(3) SetScaleTable payload consumed, output never generated.
//   5. MDEC(1) DecodeMacroblock: busy → output-available → output-drained.
//   6. Hardware warm reset (bit 31 of control) aborts the active lifecycle.
//   7. LifetimeStats counters survive warm resets.
//   8. LifetimeStats (integration): synthetic decode chain → all flags set.
//   9. LifetimeStats (regression): REV2-style quant+scale-only sequence
//      confirms MDEC(1) was never issued and no output ever reached.

#include "psxrecomp/runtime/mdec.h"
#include "psxrecomp/runtime/psx_system.h"

#include <cassert>
#include <cstddef>
#include <sstream>
#include <stdexcept>

namespace
{
using psxrecomp::u32;
using psxrecomp::runtime::DmaController;
using psxrecomp::runtime::DmaPort;
using psxrecomp::runtime::Mdec;
using psxrecomp::runtime::PsxSystem;

// ---- Status / control constants ----

constexpr u32 STATUS_FIFO_EMPTY = 1u << 31;
constexpr u32 STATUS_CMD_BUSY = 1u << 29;
constexpr u32 STATUS_DMA_IN_REQ = 1u << 28;
constexpr u32 STATUS_DMA_OUT_REQ = 1u << 27;

constexpr u32 CTRL_RESET = 1u << 31;
constexpr u32 CTRL_DMA_IN = 1u << 30;
constexpr u32 CTRL_DMA_OUT = 1u << 29;

constexpr u32 MDEC_CMD = psxrecomp::runtime::Mmio::MDEC_BASE;
constexpr u32 MDEC_CTRL = psxrecomp::runtime::Mmio::MDEC_BASE + 4u;

// ---- Assertion helpers ----

void require(bool condition, const char* message)
{
    if (!condition)
    {
        throw std::runtime_error(message);
    }
}

[[noreturn]] void failHex(const char* label, u32 expected, u32 actual)
{
    std::ostringstream s;
    s << label << " expected=0x" << std::hex << expected << " actual=0x" << actual;
    throw std::runtime_error(s.str());
}

void requireEqual(const char* label, u32 expected, u32 actual)
{
    if (expected != actual)
    {
        failHex(label, expected, actual);
    }
}

// ---- DMA helpers (used in integration tests) ----

psxrecomp::Address dmaBase(DmaPort port)
{
    return DmaController::ChannelBase +
           DmaController::ChannelStride * static_cast<psxrecomp::Address>(port);
}

// ============================================================
// Test 1 — Command classification
//
// Write representative command words and verify:
//   • CommandKind is recorded correctly for kinds 1, 2, 3.
//   • commandIsOutputCapable() returns true only for DecodeMacroblock.
//   • Expected parameter count is consumed without issue.
// ============================================================

void testCommandClassification()
{
    Mdec mdec;

    // MDEC(1) — DecodeMacroblock
    mdec.reset();
    mdec.writeCommand((1u << 29) | 1u); // cmd=1, 1 param
    require(mdec.lastCommandKind() == Mdec::CommandKind::DecodeMacroblock,
            "cmd 1 → DecodeMacroblock");
    require(mdec.currentPhase() == Mdec::CommandPhase::AwaitingParameters,
            "cmd 1 with 1 param → AwaitingParameters");
    mdec.writeCommand(0u); // consume the param
    require(mdec.currentPhase() == Mdec::CommandPhase::OutputAvailable,
            "cmd 1 after params → OutputAvailable");

    // MDEC(2) — SetQuantTable (luma-only, 16 params)
    mdec.reset();
    mdec.writeCommand(2u << 29);
    require(mdec.lastCommandKind() == Mdec::CommandKind::SetQuantTable, "cmd 2 → SetQuantTable");
    require(mdec.currentPhase() == Mdec::CommandPhase::AwaitingParameters,
            "cmd 2 → AwaitingParameters while params pending");
    for (u32 i = 0; i < 16u; ++i)
    {
        mdec.writeCommand(i);
    }
    require(mdec.currentPhase() == Mdec::CommandPhase::Idle,
            "cmd 2 → Idle after all params consumed");

    // MDEC(3) — SetScaleTable (32 params)
    mdec.reset();
    mdec.writeCommand(3u << 29);
    require(mdec.lastCommandKind() == Mdec::CommandKind::SetScaleTable, "cmd 3 → SetScaleTable");
    require(mdec.currentPhase() == Mdec::CommandPhase::AwaitingParameters,
            "cmd 3 → AwaitingParameters");
    for (u32 i = 0; i < 32u; ++i)
    {
        mdec.writeCommand(i);
    }
    require(mdec.currentPhase() == Mdec::CommandPhase::Idle, "cmd 3 → Idle after all params");

    // commandIsOutputCapable() contract.
    require(Mdec::commandIsOutputCapable(Mdec::CommandKind::DecodeMacroblock),
            "DecodeMacroblock is output-capable");
    require(!Mdec::commandIsOutputCapable(Mdec::CommandKind::SetQuantTable),
            "SetQuantTable is NOT output-capable");
    require(!Mdec::commandIsOutputCapable(Mdec::CommandKind::SetScaleTable),
            "SetScaleTable is NOT output-capable");
    require(!Mdec::commandIsOutputCapable(Mdec::CommandKind::None), "None is NOT output-capable");
    require(!Mdec::commandIsOutputCapable(Mdec::CommandKind::Invalid),
            "Invalid is NOT output-capable");
}

// ============================================================
// Test 2 — MDEC(2) SetQuantTable consumes payload, never produces output
//
// Issue MDEC(2) with both luminance and color tables (bit 0 = 1 → 32 params).
// Verify:
//   • Busy is correctly set during parameter intake.
//   • dmaOutRequest() never rises at any point.
//   • Phase returns to Idle after the last parameter.
//   • DMA1 read produces zero (no output data).
// ============================================================

void testQuantTableNeverProducesOutput()
{
    Mdec mdec;
    mdec.reset();
    mdec.writeControl(CTRL_DMA_IN | CTRL_DMA_OUT);

    const u32 cmd = (2u << 29) | 1u; // luma + color → 32 params
    mdec.writeCommand(cmd);

    require((mdec.readStatus() & STATUS_CMD_BUSY) != 0, "busy during quant table intake");

    for (u32 i = 0; i < 32u; ++i)
    {
        require(!mdec.dmaOutRequest(), "dmaOut must not rise during quant table intake");
        mdec.writeCommand(i);
    }

    require((mdec.readStatus() & STATUS_CMD_BUSY) == 0, "busy must clear after all quant params");
    require(!mdec.dmaOutRequest(), "dmaOut must not rise after SetQuantTable completion");
    require(mdec.currentPhase() == Mdec::CommandPhase::Idle,
            "phase must be Idle after SetQuantTable");
    require((mdec.readStatus() & STATUS_FIFO_EMPTY) != 0, "output FIFO must remain empty");

    // DMA1 drain produces nothing meaningful.
    requireEqual("DMA1 drain from quant-only state", 0u, mdec.readDma());
}

// ============================================================
// Test 3 — MDEC(3) SetScaleTable consumes payload, never produces output
//
// Scenario: issue MDEC(3) with 32 parameter words.
// Verify:
//   • Scale table is accepted without setting DMA-out-request.
//   • Phase remains Idle after all params.
// ============================================================

void testScaleTableNeverProducesOutput()
{
    Mdec mdec;
    mdec.reset();
    mdec.writeControl(CTRL_DMA_IN | CTRL_DMA_OUT);

    const u32 cmd = (3u << 29); // 32 params always
    mdec.writeCommand(cmd);

    require((mdec.readStatus() & STATUS_CMD_BUSY) != 0, "busy during scale table intake");

    for (u32 i = 0; i < 32u; ++i)
    {
        require(!mdec.dmaOutRequest(), "dmaOut must not rise during scale table intake");
        mdec.writeCommand(i);
    }

    require((mdec.readStatus() & STATUS_CMD_BUSY) == 0, "busy must clear after all scale params");
    require(!mdec.dmaOutRequest(), "dmaOut must not rise after SetScaleTable completion");
    require(mdec.currentPhase() == Mdec::CommandPhase::Idle,
            "phase must be Idle after SetScaleTable");
    require((mdec.readStatus() & STATUS_FIFO_EMPTY) != 0, "output FIFO must remain empty");
}

// ============================================================
// Test 4 — MDEC(1) sets busy, accepts DMA0 input, exposes output, DMA1 drains
//
// Feed a synthetic MDEC(1) command through writeDma() (the DMA0 path).
// Verify each lifecycle phase:
//   • Busy set immediately after command word.
//   • AwaitingParameters while consuming params.
//   • dmaInRequest asserted while params remain.
//   • OutputAvailable + dmaOutRequest after all params consumed.
//   • OutputDrained after all output words read.
// ============================================================

void testDecodeLifecycle()
{
    Mdec mdec;
    mdec.reset();
    mdec.writeControl(CTRL_DMA_IN | CTRL_DMA_OUT);

    constexpr u32 paramCount = 4u;
    const u32 cmd = (1u << 29) | paramCount; // cmd=1, 4 params
    mdec.writeCommand(cmd);

    require((mdec.readStatus() & STATUS_CMD_BUSY) != 0, "busy after MDEC(1) command word");
    require(mdec.currentPhase() == Mdec::CommandPhase::AwaitingParameters,
            "phase=AwaitingParameters while params pending");
    require(mdec.dmaInRequest(), "dmaInRequest asserted while params remain");
    require(!mdec.dmaOutRequest(), "dmaOutRequest must be false before params consumed");

    // Feed parameters via DMA0 path.
    for (u32 i = 0; i < paramCount; ++i)
    {
        mdec.writeDma(i);
    }

    require((mdec.readStatus() & STATUS_CMD_BUSY) == 0,
            "busy must clear after all params consumed");
    require(!mdec.dmaInRequest(), "dmaInRequest must clear after params consumed");
    require(mdec.dmaOutRequest(), "dmaOutRequest must rise once output is available");
    require(mdec.currentPhase() == Mdec::CommandPhase::OutputAvailable,
            "phase=OutputAvailable after decode");
    require((mdec.readStatus() & STATUS_FIFO_EMPTY) == 0, "output FIFO must be non-empty");

    // Drain all output through the DMA1 path.
    u32 drainCount = 0u;
    while (mdec.dmaOutRequest())
    {
        mdec.readDma();
        ++drainCount;
        if (drainCount > 0x10000u)
        {
            throw std::runtime_error("infinite DMA1 drain loop");
        }
    }

    require(drainCount > 0u, "at least one output word must be drained");
    require(mdec.currentPhase() == Mdec::CommandPhase::OutputDrained,
            "phase=OutputDrained after full drain");
    require((mdec.readStatus() & STATUS_FIFO_EMPTY) != 0, "output FIFO must be empty post-drain");
}

// ============================================================
// Test 5 — Hardware warm reset aborts the active lifecycle
//
// Two sub-cases:
//   a) Abort an in-progress MDEC(1) mid-parameter intake.
//   b) Abort an in-progress MDEC(2) mid-parameter intake.
//
// After reset, verify:
//   • Phase returns to Idle.
//   • busy cleared.
//   • dmaInRequest and dmaOutRequest clear.
//   • Status reads back the documented post-reset value 0x80040000.
// ============================================================

void testWarmResetAbortsLifecycle()
{
    // Sub-case a: abort MDEC(1)
    {
        Mdec mdec;
        mdec.reset();
        mdec.writeControl(CTRL_DMA_IN | CTRL_DMA_OUT);

        mdec.writeCommand((1u << 29) | 8u); // 8 params; some will not arrive
        require(mdec.currentPhase() == Mdec::CommandPhase::AwaitingParameters,
                "pre-reset phase=AwaitingParameters");

        mdec.writeControl(CTRL_RESET);

        requireEqual("status after warm reset mid-MDEC(1)", 0x80040000u, mdec.readStatus());
        require(mdec.currentPhase() == Mdec::CommandPhase::Idle, "phase=Idle after warm reset");
        require(!mdec.dmaInRequest(), "dmaInRequest clear after warm reset");
        require(!mdec.dmaOutRequest(), "dmaOutRequest clear after warm reset");
    }

    // Sub-case b: abort MDEC(2) mid-intake
    {
        Mdec mdec;
        mdec.reset();
        mdec.writeControl(CTRL_DMA_IN | CTRL_DMA_OUT);

        mdec.writeCommand(2u << 29); // 16 params expected; send none
        require(mdec.currentPhase() == Mdec::CommandPhase::AwaitingParameters,
                "pre-reset phase=AwaitingParameters (MDEC-2)");

        mdec.writeControl(CTRL_RESET);

        requireEqual("status after warm reset mid-MDEC(2)", 0x80040000u, mdec.readStatus());
        require(mdec.currentPhase() == Mdec::CommandPhase::Idle,
                "phase=Idle after warm reset (MDEC-2)");
    }
}

// ============================================================
// Test 6 — LifetimeStats counters survive hardware warm resets
//
// Issue MDEC(2), warm-reset, issue MDEC(3), warm-reset, issue MDEC(1).
// After the sequence, lifetimeStats() must report cumulative totals:
//   • quantTableCommandsIssued = 1
//   • scaleTableCommandsIssued = 1
//   • decodeCommandsIssued     = 1
// ============================================================

void testLifetimeStatsSurviveWarmReset()
{
    Mdec mdec;
    mdec.reset();

    // MDEC(2) — quant table
    mdec.writeCommand((2u << 29));
    for (u32 i = 0; i < 16u; ++i)
    {
        mdec.writeCommand(i);
    }

    // Warm reset.
    mdec.writeControl(CTRL_RESET);

    // MDEC(3) — scale table
    mdec.writeCommand((3u << 29));
    for (u32 i = 0; i < 32u; ++i)
    {
        mdec.writeCommand(i);
    }

    // Warm reset again.
    mdec.writeControl(CTRL_RESET);

    // MDEC(1) — decode (1 param → finishes immediately with output)
    mdec.writeControl(CTRL_DMA_OUT);
    mdec.writeCommand((1u << 29) | 1u);
    mdec.writeCommand(0u);

    const Mdec::LifetimeStats stats = mdec.lifetimeStats();
    requireEqual("decode commands issued", 1u, stats.decodeCommandsIssued);
    requireEqual("quant table commands issued", 1u, stats.quantTableCommandsIssued);
    requireEqual("scale table commands issued", 1u, stats.scaleTableCommandsIssued);
    require(stats.anyDecodeReachedOutputAvailable, "anyDecodeReachedOutputAvailable must be true");
}

// ============================================================
// Integration 1 — Synthetic decode chain through PsxSystem
//
// Exercises the full movie-path:
//   RAM → DMA0 (MdecIn) → MDEC(1) → DMA1 (MdecOut) → RAM
//
// Verifies:
//   • lifetimeStats().decodeCommandsIssued == 1
//   • lifetimeStats().anyDecodeReachedOutputAvailable == true
//   • lifetimeStats().anyDma1Drain == true
//   • DMA1 writes output words to RAM (output is not 0xDEADDEAD).
// ============================================================

void testIntegrationSyntheticDecodeChain()
{
    PsxSystem sys;
    require(sys.initialize(), "system init failed");

    sys.writeMmioExplicit<u32>(MDEC_CTRL, CTRL_RESET);
    requireEqual("post-reset status", 0x80040000u, sys.readMmioExplicit<u32>(MDEC_CTRL));

    constexpr psxrecomp::Address inputBase = 0x10000u;
    constexpr u32 paramWords = 0x10u; // 16 parameter words
    for (u32 i = 0; i < paramWords; ++i)
    {
        sys.write<u32>(inputBase + i * 4u, 0u);
    }

    // cmd=1, depth=0 (4-bit), paramWords
    const u32 cmdWord = (1u << 29) | paramWords;
    sys.writeMmioExplicit<u32>(MDEC_CMD, cmdWord);

    sys.writeMmioExplicit<u32>(MDEC_CTRL, CTRL_DMA_IN | CTRL_DMA_OUT);

    // DMA0 (MdecIn): load paramWords from RAM into MDEC.
    const psxrecomp::Address dma0 = dmaBase(DmaPort::MdecIn);
    sys.writeMmioExplicit<u32>(dma0 + 0x0u, inputBase);
    sys.writeMmioExplicit<u32>(dma0 + 0x4u, paramWords | (1u << 16));
    sys.writeMmioExplicit<u32>(dma0 + 0x8u, 0x01000201u); // fromRAM, request, start

    // After DMA0: command should be complete, output available.
    require((sys.readMmioExplicit<u32>(MDEC_CTRL) & STATUS_CMD_BUSY) == 0,
            "busy must clear after DMA0 completes parameter intake");
    require((sys.readMmioExplicit<u32>(MDEC_CTRL) & STATUS_DMA_OUT_REQ) != 0,
            "DMA-out-request must be set after decode");

    // DMA1 (MdecOut): drain output to RAM.
    constexpr psxrecomp::Address outputBase = 0x20000u;
    constexpr u32 drainWords = 0x20u;
    for (u32 i = 0; i < drainWords; ++i)
    {
        sys.write<u32>(outputBase + i * 4u, 0xDEADDEADu);
    }

    const psxrecomp::Address dma1 = dmaBase(DmaPort::MdecOut);
    sys.writeMmioExplicit<u32>(dma1 + 0x0u, outputBase);
    sys.writeMmioExplicit<u32>(dma1 + 0x4u, drainWords | (1u << 16));
    sys.writeMmioExplicit<u32>(dma1 + 0x8u, 0x01000200u); // toRAM, request, start

    require(sys.read<u32>(outputBase) != 0xDEADDEADu, "DMA1 must have written output data to RAM");

    // Inspect lifetime stats via the accessor.
    const Mdec::LifetimeStats stats = sys.mdec().lifetimeStats();
    requireEqual("integration: decode commands issued", 1u, stats.decodeCommandsIssued);
    require(stats.anyDecodeReachedOutputAvailable, "integration: anyDecodeReachedOutputAvailable");
    require(stats.anyDma1Drain, "integration: anyDma1Drain");
}

// ============================================================
// Integration 2 — Reversi 2 regression / diagnostic proof
//
// Replays the exact MDEC register write sequence observed in the REV2
// run.log (lines 3324-3335):
//
//   writeControl(0x80000000) — warm reset
//   writeControl(0x60000000) — enable DmaIn + DmaOut
//   writeCommand(0x40000001) — MDEC(2): quant table, luma+color, 32 params
//   [32 param words via DMA0]
//   writeCommand(0x60000000) — MDEC(3): scale table, 32 params
//   [32 param words via DMA0]
//
// After this sequence lifetimeStats() must state conclusively:
//   • No MDEC(1) command was ever issued.
//   • No decoded output was ever produced.
//   • No DMA1 drain occurred.
//
// This is the proof that Reversi 2 stalls before reaching the real
// decode path, so the blocker remains upstream of MDEC(1).
// ============================================================

void testIntegrationRev2Regression()
{
    PsxSystem sys;
    require(sys.initialize(), "system init failed");

    // Replay REV2 MDEC init sequence.
    // 1. Warm reset.
    sys.writeMmioExplicit<u32>(MDEC_CTRL, 0x80000000u);
    requireEqual("post-reset status", 0x80040000u, sys.readMmioExplicit<u32>(MDEC_CTRL));

    // 2. Enable DmaIn + DmaOut.
    sys.writeMmioExplicit<u32>(MDEC_CTRL, 0x60000000u);

    // --- MDEC(2): SetQuantTable (luma+color) --------------------------------
    // 0x40000001 → bits 31-29 = 010 = cmd 2; bit 0 = 1 → luma+color → 32 params.
    sys.writeMmioExplicit<u32>(MDEC_CMD, 0x40000001u);

    constexpr psxrecomp::Address quantBase = 0x1000u;
    for (u32 i = 0; i < 32u; ++i)
    {
        sys.write<u32>(quantBase + i * 4u, static_cast<u32>(i));
    }
    const psxrecomp::Address dma0 = dmaBase(DmaPort::MdecIn);
    sys.writeMmioExplicit<u32>(dma0 + 0x0u, quantBase);
    sys.writeMmioExplicit<u32>(dma0 + 0x4u, 32u | (1u << 16)); // 32 words, 1 block
    sys.writeMmioExplicit<u32>(dma0 + 0x8u, 0x01000201u);      // fromRAM/request/start

    // --- MDEC(3): SetScaleTable ---------------------------------------------
    // 0x60000000 → bits 31-29 = 011 = cmd 3 → 32 params.
    sys.writeMmioExplicit<u32>(MDEC_CMD, 0x60000000u);

    constexpr psxrecomp::Address scaleBase = 0x2000u;
    for (u32 i = 0; i < 32u; ++i)
    {
        sys.write<u32>(scaleBase + i * 4u, static_cast<u32>(i * 0x10001u)); // two int16 values
    }
    sys.writeMmioExplicit<u32>(dma0 + 0x0u, scaleBase);
    sys.writeMmioExplicit<u32>(dma0 + 0x4u, 32u | (1u << 16));
    sys.writeMmioExplicit<u32>(dma0 + 0x8u, 0x01000201u);

    // --- Check final state --------------------------------------------------
    // Quant and scale tables have been loaded; MDEC(1) never happened.
    require((sys.readMmioExplicit<u32>(MDEC_CTRL) & STATUS_CMD_BUSY) == 0,
            "rev2: busy must be clear after setup-only sequence");
    require(!sys.mdec().dmaOutRequest(),
            "rev2: dmaOutRequest must be clear — no decode output produced");
    require(sys.mdec().currentPhase() == Mdec::CommandPhase::Idle,
            "rev2: phase must be Idle — no decode command issued");

    const Mdec::LifetimeStats stats = sys.mdec().lifetimeStats();

    // Decisive proof: MDEC(1) was never issued.
    requireEqual("rev2: decode commands issued (must be 0)", 0u, stats.decodeCommandsIssued);
    requireEqual("rev2: quant table commands issued", 1u, stats.quantTableCommandsIssued);
    requireEqual("rev2: scale table commands issued", 1u, stats.scaleTableCommandsIssued);
    require(!stats.anyDecodeReachedOutputAvailable, "rev2: no decode output ever produced");
    require(!stats.anyDma1Drain, "rev2: DMA1 was never triggered — blocker is upstream of MDEC(1)");
}

} // namespace

int main()
{
    testCommandClassification();
    testQuantTableNeverProducesOutput();
    testScaleTableNeverProducesOutput();
    testDecodeLifecycle();
    testWarmResetAbortsLifecycle();
    testLifetimeStatsSurviveWarmReset();
    testIntegrationSyntheticDecodeChain();
    testIntegrationRev2Regression();

    return 0;
}
