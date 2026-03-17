/// MDEC unit tests -- register handshake, command state machine, reset.
#include "psxrecomp/runtime/mdec.h"

#include <cassert>
#include <cstdio>
#include <sstream>
#include <stdexcept>

namespace
{
using psxrecomp::u32;
using psxrecomp::runtime::Mdec;

constexpr u32 STATUS_FIFO_EMPTY = 1u << 31;
constexpr u32 STATUS_DATA_IN_FULL = 1u << 30;
constexpr u32 STATUS_CMD_BUSY = 1u << 29;
constexpr u32 STATUS_DMA_IN_REQ = 1u << 28;
constexpr u32 STATUS_DMA_OUT_REQ = 1u << 27;
constexpr u32 STATUS_BLOCK_MASK = 0x7u << 16;
constexpr u32 STATUS_BLOCK_CR = 4u << 16;
constexpr u32 CTRL_RESET = 1u << 31;
constexpr u32 CTRL_DMA_IN = 1u << 30;
constexpr u32 CTRL_DMA_OUT = 1u << 29;

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

// ---- Reset ----

void testResetProducesDocumentedStatus()
{
    Mdec mdec;
    mdec.reset();
    mdec.writeControl(CTRL_RESET);
    requireEqual("status after control reset", 0x80040000u, mdec.readStatus());
}

void testResetAbortsBusyCommand()
{
    Mdec mdec;
    mdec.reset();

    // Start a decode command with 16 parameter words expected.
    const u32 cmd = (1u << 29) | 16u; // command=1, depth=0, 16 params
    mdec.writeCommand(cmd);
    require((mdec.readStatus() & STATUS_CMD_BUSY) != 0, "should be busy after command");

    // Reset should abort and return documented idle.
    mdec.writeControl(CTRL_RESET);
    requireEqual("status after reset mid-command", 0x80040000u, mdec.readStatus());
    require(!mdec.dmaInRequest(), "dmaInRequest should be false after reset");
    require(!mdec.dmaOutRequest(), "dmaOutRequest should be false after reset");
}

void testResetClearsQueuedOutput()
{
    Mdec mdec;
    mdec.reset();

    // Issue a decode command with 1 parameter word.
    const u32 cmd = (1u << 29) | 1u;
    mdec.writeCommand(cmd);
    mdec.writeCommand(0x12345678u); // parameter -> finishes command

    // Output should now be available.
    mdec.writeControl(CTRL_DMA_OUT);
    require(mdec.dmaOutRequest(), "output should be available before reset");

    // Reset clears it.
    mdec.writeControl(CTRL_RESET);
    require(!mdec.dmaOutRequest(), "output should be gone after reset");
    requireEqual("status after reset with output", 0x80040000u, mdec.readStatus());
}

// ---- Command acceptance ----

void testDecodeCommandSetsBusy()
{
    Mdec mdec;
    mdec.reset();

    const u32 cmd = (1u << 29) | 4u; // decode, 4 parameter words
    mdec.writeCommand(cmd);

    const u32 status = mdec.readStatus();
    require((status & STATUS_CMD_BUSY) != 0, "busy must be set");
    // Low 16 bits = remaining-1 = 3.
    requireEqual("remaining-1", 3u, status & 0xFFFFu);
}

void testDecodeCommandWithZeroParams()
{
    Mdec mdec;
    mdec.reset();

    // Decode with 0 parameter words should finish immediately.
    const u32 cmd = (1u << 29) | 0u;
    mdec.writeCommand(cmd);

    require((mdec.readStatus() & STATUS_CMD_BUSY) == 0,
            "busy should clear immediately with 0 params");
}

void testQuantTableCommand()
{
    Mdec mdec;
    mdec.reset();

    // Luminance only (bit 0 = 0): 16 parameter words.
    const u32 cmd = (2u << 29);
    mdec.writeCommand(cmd);
    require((mdec.readStatus() & STATUS_CMD_BUSY) != 0, "should be busy");

    for (u32 i = 0; i < 16; ++i)
    {
        mdec.writeCommand(i);
    }
    require((mdec.readStatus() & STATUS_CMD_BUSY) == 0, "should not be busy after all params");
}

void testQuantTableCommandWithColor()
{
    Mdec mdec;
    mdec.reset();

    // Luminance + color (bit 0 = 1): 32 parameter words.
    const u32 cmd = (2u << 29) | 1u;
    mdec.writeCommand(cmd);

    for (u32 i = 0; i < 32; ++i)
    {
        mdec.writeCommand(i);
    }
    require((mdec.readStatus() & STATUS_CMD_BUSY) == 0, "should not be busy after 32 quant params");
}

void testScaleTableCommand()
{
    Mdec mdec;
    mdec.reset();

    const u32 cmd = (3u << 29);
    mdec.writeCommand(cmd);
    require((mdec.readStatus() & STATUS_CMD_BUSY) != 0, "should be busy");

    for (u32 i = 0; i < 32; ++i)
    {
        mdec.writeCommand(i);
    }
    require((mdec.readStatus() & STATUS_CMD_BUSY) == 0, "should not be busy after 32 scale params");
}

void testNoFunctionCommandReflectsLow16()
{
    Mdec mdec;
    mdec.reset();

    // Command 0 (no-function): bits 0-15 should reflect in status.
    const u32 cmd = (0u << 29) | 0x1234u;
    mdec.writeCommand(cmd);

    require((mdec.readStatus() & STATUS_CMD_BUSY) == 0, "no-function should not set busy");
    requireEqual("reflected low16", 0x1234u, mdec.readStatus() & 0xFFFFu);
}

// ---- DMA request signals ----

void testDmaInRequestRequiresEnable()
{
    Mdec mdec;
    mdec.reset();

    const u32 cmd = (1u << 29) | 4u;
    mdec.writeCommand(cmd);

    // Without enable, request should be false.
    require(!mdec.dmaInRequest(), "dmaIn should be false without enable");

    // Enable DMA in.
    mdec.writeControl(CTRL_DMA_IN);
    require(mdec.dmaInRequest(), "dmaIn should be true when enabled + busy + params left");
}

void testDmaOutRequestRequiresEnable()
{
    Mdec mdec;
    mdec.reset();

    // Issue and complete a decode command.
    const u32 cmd = (1u << 29) | 1u;
    mdec.writeCommand(cmd);
    mdec.writeCommand(0u); // parameter -> finishes

    // Without enable, request should be false.
    require(!mdec.dmaOutRequest(), "dmaOut should be false without enable");

    mdec.writeControl(CTRL_DMA_OUT);
    require(mdec.dmaOutRequest(), "dmaOut should be true when enabled + output available");
}

void testDmaInRequestClearsWhenParamsConsumed()
{
    Mdec mdec;
    mdec.reset();
    mdec.writeControl(CTRL_DMA_IN);

    const u32 cmd = (1u << 29) | 2u;
    mdec.writeCommand(cmd);
    require(mdec.dmaInRequest(), "should request before params consumed");

    mdec.writeDma(0u);
    mdec.writeDma(0u);

    // All params consumed -> command finished -> busy cleared.
    require(!mdec.dmaInRequest(), "should not request after params consumed");
}

// ---- Output availability ----

void testOutputAvailableAfterDecode()
{
    Mdec mdec;
    mdec.reset();
    mdec.writeControl(CTRL_DMA_OUT);

    // 15-bit depth (bits 28-27 = 3, command = 1 at bits 31-29).
    // depth=3 → bits 28-27=11 → command word = 001_11_xxx...
    const u32 cmd = (1u << 29) | (3u << 27) | 2u; // decode, depth=3, 2 params
    mdec.writeCommand(cmd);
    mdec.writeCommand(0u);
    mdec.writeCommand(0u);

    const u32 status = mdec.readStatus();
    require((status & STATUS_CMD_BUSY) == 0, "should not be busy");
    require((status & STATUS_FIFO_EMPTY) == 0, "FIFO should not be empty");
    require((status & STATUS_DMA_OUT_REQ) != 0, "DMA out request should be set");
}

void testOutputDrainableViaReadDma()
{
    Mdec mdec;
    mdec.reset();
    mdec.writeControl(CTRL_DMA_OUT);

    const u32 cmd = (1u << 29) | 1u;
    mdec.writeCommand(cmd);
    mdec.writeCommand(0u);

    require(mdec.dmaOutRequest(), "output should be available");

    // Drain all output.
    u32 reads = 0;
    while (mdec.dmaOutRequest())
    {
        mdec.readDma();
        ++reads;
        if (reads > 0x10000u)
        {
            throw std::runtime_error("infinite drain loop");
        }
    }

    require(reads > 0, "should have drained at least one word");
    require((mdec.readStatus() & STATUS_FIFO_EMPTY) != 0, "FIFO should be empty after drain");
}

// ---- Status bit mapping ----

void testStatusDepthMapping()
{
    Mdec mdec;
    mdec.reset();

    // depth=2 (24-bit) → status bits 26-25 = 2.
    const u32 cmd = (1u << 29) | (2u << 27) | 1u;
    mdec.writeCommand(cmd);

    const u32 depth = (mdec.readStatus() >> 25) & 0x3u;
    requireEqual("depth field", 2u, depth);
}

void testStatusCurrentBlockAfterReset()
{
    Mdec mdec;
    mdec.reset();
    mdec.writeControl(CTRL_RESET);

    requireEqual("current block after reset", STATUS_BLOCK_CR,
                 mdec.readStatus() & STATUS_BLOCK_MASK);
}

} // namespace

int main()
{
    testResetProducesDocumentedStatus();
    testResetAbortsBusyCommand();
    testResetClearsQueuedOutput();
    testDecodeCommandSetsBusy();
    testDecodeCommandWithZeroParams();
    testQuantTableCommand();
    testQuantTableCommandWithColor();
    testScaleTableCommand();
    testNoFunctionCommandReflectsLow16();
    testDmaInRequestRequiresEnable();
    testDmaOutRequestRequiresEnable();
    testDmaInRequestClearsWhenParamsConsumed();
    testOutputAvailableAfterDecode();
    testOutputDrainableViaReadDma();
    testStatusDepthMapping();
    testStatusCurrentBlockAfterReset();

    return 0;
}
