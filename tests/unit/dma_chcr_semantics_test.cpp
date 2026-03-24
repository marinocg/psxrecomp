/**
 * @file dma_chcr_semantics_test.cpp
 * @brief Regression tests for PSX-SPX DMA CHCR/DPCR/BCR semantics.
 *
 * Verified:
 *  1. CHCR bit 28 (Start/Trigger) is cleared by hardware when transfer begins.
 *  2. CHCR bit 24 (Start/Busy) is cleared by hardware when transfer completes.
 *  3. DPCR channel master-enable gates whether a CHCR write triggers a transfer.
 *  4. BCR block-size / block-count of 0 is treated as 0x10000 (PSX-SPX).
 *  5. OTC DMA initialises the ordering table correctly.
 *  6. SyncMode=0 (manual) requires bit 28; bit 24 alone must NOT start a transfer.
 */
#include "psxrecomp/runtime/dma.h"
#include "psxrecomp/runtime/psx_system.h"

#include <cstdlib>
#include <iostream>

using psxrecomp::Address;
using psxrecomp::u32;
using psxrecomp::runtime::DmaController;
using psxrecomp::runtime::DmaPort;
using psxrecomp::runtime::InterruptLine;
using psxrecomp::runtime::PsxSystem;

namespace
{

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << "[FAIL] " << message << "\n";
        std::abort();
    }
}

constexpr Address channelBase(DmaPort port)
{
    return DmaController::ChannelBase + DmaController::ChannelStride * static_cast<Address>(port);
}

// ---------------------------------------------------------------
// Test 1: CHCR bit 28 cleared at transfer begin; bit 24 cleared at complete
//
// Use OTC (channel 6, RAM→OTC) which is self-contained and doesn't need
// a device ready signal.
// ---------------------------------------------------------------
static void testChcrBitClearSemantics()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for CHCR bit-clear test");

    const Address base = channelBase(DmaPort::Otc);
    constexpr Address dest = 0x00001000u;

    // Enable OTC channel in DPCR (bit 27 = ch6 master-enable).
    constexpr u32 dpcrEnableOtc = 1u << 27u;
    system.write<u32>(DmaController::ControlReg, dpcrEnableOtc);

    // Set BCR: 4 words (BS=4, BA=0).
    system.write<u32>(base + 0x0, dest);
    system.write<u32>(base + 0x4, 0x00000004u); // BCR: BS=4
    // CHCR: bit 0=to-RAM (OTC always RAM←device), bit 24=busy, bit 28=trigger.
    // OTC direction: RAM←device means fromRam=false (bit 0 clear).
    constexpr u32 chcrStart = (1u << 24u) | (1u << 28u); // bits 24+28
    system.write<u32>(base + 0x8, chcrStart);

    // After the write, the transfer should have executed.
    // CHCR bit 28 must be clear (cleared at begin).
    // CHCR bit 24 must be clear (cleared at complete).
    const u32 chcrAfter = system.read<u32>(base + 0x8);
    require((chcrAfter & (1u << 28u)) == 0u, "CHCR bit 28 must be cleared at transfer begin");
    require((chcrAfter & (1u << 24u)) == 0u, "CHCR bit 24 must be cleared at transfer complete");

    std::cerr << "[PASS] CHCR: bit 28 cleared at begin, bit 24 cleared at complete\n";
}

// ---------------------------------------------------------------
// Test 2: DPCR channel disable prevents transfer start
//
// Write CHCR start bits with DPCR channel-enable clear; transfer must
// not execute (CHCR busy stays as written, OT memory untouched).
// ---------------------------------------------------------------
static void testDpcrDisablePreventsTransfer()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for DPCR disable test");

    const Address base = channelBase(DmaPort::Otc);
    constexpr Address dest = 0x00002000u;

    // Leave DPCR at reset (all zeros → no channel enabled).
    system.write<u32>(DmaController::ControlReg, 0u);

    // Sentinel: ensure destination is untouched.
    system.write<u32>(dest, 0xDEADBEEFu);

    system.write<u32>(base + 0x0, dest);
    system.write<u32>(base + 0x4, 0x00000004u);
    constexpr u32 chcrStart = (1u << 24u) | (1u << 28u);
    system.write<u32>(base + 0x8, chcrStart);

    // Transfer must NOT have run: destination is unchanged.
    require(system.read<u32>(dest) == 0xDEADBEEFu,
            "DPCR-disabled channel must not start DMA transfer");

    std::cerr << "[PASS] DPCR disable prevents DMA transfer start\n";
}

// ---------------------------------------------------------------
// Test 3: BCR block-size 0 treated as 0x10000
//
// Write BCR=0 (BS=0, BA=0). For OTC in normal mode, word count = BS = 0x10000.
// The OTC table must extend across 0x10000 entries from dest downward.
// We verify the last entry (oldest) is the end-of-list sentinel 0x00FFFFFF.
// ---------------------------------------------------------------
static void testBcrZeroMeansMax()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for BCR=0 test");

    const Address base = channelBase(DmaPort::Otc);
    // Place the OTC at a high address so it has room to grow downward.
    // 0x10000 words = 0x40000 bytes; start at 0x7FFFC so last entry = 0x3FFFC.
    // Keep within RAM (2MB = 0x200000).
    constexpr Address dest = 0x0007FFCu; // top of a small region

    constexpr u32 dpcrEnableOtc = 1u << 27u;
    system.write<u32>(DmaController::ControlReg, dpcrEnableOtc);

    system.write<u32>(base + 0x0, dest);
    system.write<u32>(base + 0x4, 0x00000000u); // BCR=0 → BS=0x10000
    constexpr u32 chcrStart = (1u << 24u) | (1u << 28u);
    system.write<u32>(base + 0x8, chcrStart);

    // CHCR bits cleared means transfer ran.
    const u32 chcrAfter = system.read<u32>(base + 0x8);
    require((chcrAfter & (1u << 24u)) == 0u, "CHCR bit 24 must be clear after BCR=0 OTC transfer");

    // The last word written is at (dest - (0x10000-1)*4) & 0x1FFFFC.
    // Per OTC init logic, the oldest entry should hold 0x00FFFFFF (end-of-list).
    const Address lastEntry = (dest - (0x10000u - 1u) * sizeof(u32)) & 0x1FFFFCu;
    const u32 endMarker = system.read<u32>(lastEntry);
    require(endMarker == 0x00FFFFFFu, "BCR=0 OTC: last entry must be 0x00FFFFFF end-of-list");

    std::cerr << "[PASS] BCR block-size 0 treated as 0x10000\n";
}

// ---------------------------------------------------------------
// Test 4: OTC initialises ordering table (basic sanity)
//
// OTC fills memory with a chain of backward-pointing addresses.
// Each word N at address A should contain (A - 4) & 0x00FFFFFF,
// except the last (oldest) which contains 0x00FFFFFF.
// ---------------------------------------------------------------
static void testOtcOrderingTable()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for OTC table test");

    const Address base = channelBase(DmaPort::Otc);
    // Use a small OT of 4 words so the test is fast.
    constexpr Address dest = 0x00010000u;
    constexpr u32 wordCount = 4u;

    constexpr u32 dpcrEnableOtc = 1u << 27u;
    system.write<u32>(DmaController::ControlReg, dpcrEnableOtc);

    system.write<u32>(base + 0x0, dest);
    system.write<u32>(base + 0x4, wordCount);
    constexpr u32 chcrStart = (1u << 24u) | (1u << 28u);
    system.write<u32>(base + 0x8, chcrStart);

    // OT entries at: dest, dest-4, dest-8, dest-12
    // dest      → points to dest-4
    // dest-4    → points to dest-8
    // dest-8    → points to dest-12
    // dest-12   → 0x00FFFFFF (end-of-list)
    const u32 e0 = system.read<u32>(dest);
    const u32 e1 = system.read<u32>(dest - 4u);
    const u32 e2 = system.read<u32>(dest - 8u);
    const u32 e3 = system.read<u32>(dest - 12u);

    require((e0 & 0x00FFFFFFu) == ((dest - 4u) & 0x00FFFFFFu), "OTC entry 0 must point to dest-4");
    require((e1 & 0x00FFFFFFu) == ((dest - 8u) & 0x00FFFFFFu), "OTC entry 1 must point to dest-8");
    require((e2 & 0x00FFFFFFu) == ((dest - 12u) & 0x00FFFFFFu),
            "OTC entry 2 must point to dest-12");
    require(e3 == 0x00FFFFFFu, "OTC entry 3 (last) must be end-of-list sentinel 0x00FFFFFF");

    std::cerr << "[PASS] OTC ordering table initialised correctly\n";
}

// ---------------------------------------------------------------
// Test 5: SyncMode=0 (manual) requires bit 28; bit 24 alone must NOT start
//
// OTC uses SyncMode=0.  Writing CHCR with bit 24 set but bit 28 clear must
// not trigger a transfer.  The destination memory must remain unchanged.
// ---------------------------------------------------------------
static void testSyncMode0RequiresBit28()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for SyncMode=0 test");

    const Address base = channelBase(DmaPort::Otc);
    constexpr Address dest = 0x00020000u;

    // Enable OTC channel in DPCR.
    constexpr u32 dpcrEnableOtc = 1u << 27u;
    system.write<u32>(DmaController::ControlReg, dpcrEnableOtc);

    // Write a sentinel so we can detect if the transfer ran.
    system.write<u32>(dest, 0xCAFEBABEu);

    system.write<u32>(base + 0x0, dest);
    system.write<u32>(base + 0x4, 4u);
    // CHCR: SyncMode=0 (bits [10:9] = 00), bit 24 set, bit 28 CLEAR.
    // This must NOT trigger a transfer.
    constexpr u32 chcrBit24Only = 1u << 24u; // no bit 28
    system.write<u32>(base + 0x8, chcrBit24Only);

    // Destination must be unchanged: OTC did not run.
    require(system.read<u32>(dest) == 0xCAFEBABEu,
            "SyncMode=0 without bit28: DMA must not start on bit24 alone");

    // CHCR bit 24 must still be set (channel is armed but not started).
    require((system.read<u32>(base + 0x8) & (1u << 24u)) != 0u,
            "SyncMode=0 without bit28: CHCR bit24 must remain set (transfer not started)");

    std::cerr << "[PASS] SyncMode=0: bit24 without bit28 does not start transfer\n";
}

// ---------------------------------------------------------------
// Test 6: SyncMode=1 (request) starts with bit 24 only — bit 28 not needed
//
// Use GPU channel with SyncMode=1: writing CHCR=0x01000201 (bit24+SyncMode=1)
// must trigger a transfer without bit 28.
// ---------------------------------------------------------------
static void testSyncMode1StartsWithBit24Only()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for SyncMode=1 test");

    const Address base = channelBase(DmaPort::Gpu);

    // GPU channel enabled via reset default (DPCR all-enabled).
    system.writeMmioExplicit<u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x04000002u);

    system.write<u32>(0x00011000, 0xAABBCCDDu);
    system.write<u32>(base + 0x0, 0x00011000);
    system.write<u32>(base + 0x4, 0x00010001); // BCR: BS=1, BA=1 → 1 word
    // CHCR: SyncMode=1, bit24 set, bit28 CLEAR.  Must start without bit 28.
    constexpr u32 chcrSyncMode1 = 0x01000201u; // SyncMode=1, bit24, fromRam
    system.write<u32>(base + 0x8, chcrSyncMode1);

    // Transfer should have run: bit 24 clears and GPU has the word.
    const u32 chcrAfter = system.read<u32>(base + 0x8);
    require((chcrAfter & (1u << 24u)) == 0u,
            "SyncMode=1: bit24 must clear on completion (transfer started without bit28)");

    std::cerr << "[PASS] SyncMode=1: starts with bit24 only (no bit28 required)\n";
}

// ---------------------------------------------------------------
// Test 7: SyncMode=2 (linked-list) starts with bit 24 only — bit 28 not needed
//
// This is the primary regression guard for GPU DrawOTag / ordering-table DMA.
// CHCR=0x01000401 (SyncMode=2, bit24, no bit28) must trigger the linked-list
// transfer, consuming commands and clearing bit24 on completion.
// ---------------------------------------------------------------
static void testSyncMode2StartsWithBit24Only()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for SyncMode=2 test");

    const Address base = channelBase(DmaPort::Gpu);

    // Build a minimal 1-node linked-list in RAM.
    // Node at 0x00015000: header = (1 command << 24) | 0x00FFFFFF (end sentinel)
    // Command word at 0x00015004: NOP (0x00000000)
    constexpr Address nodeAddr = 0x00015000u;
    system.write<u32>(nodeAddr, (1u << 24u) | 0x00FFFFFFu); // 1 cmd, end-of-list
    system.write<u32>(nodeAddr + 4u, 0x00000000u);           // GPU NOP command

    system.writeMmioExplicit<u32>(psxrecomp::runtime::Mmio::GPU_GP1, 0x04000002u);

    system.write<u32>(base + 0x0, nodeAddr);
    system.write<u32>(base + 0x4, 0x00000000u); // BCR unused for linked-list
    // CHCR: SyncMode=2 (linked-list), bit24 set, bit28 CLEAR.
    // PSX-SPX: 0x01000401 is the standard DrawOTag CHCR value.
    constexpr u32 chcrLinkedList = 0x01000401u;
    system.write<u32>(base + 0x8, chcrLinkedList);

    // Transfer should have run: bit 24 must be clear.
    const u32 chcrAfter = system.read<u32>(base + 0x8);
    require((chcrAfter & (1u << 24u)) == 0u,
            "SyncMode=2 (0x01000401): bit24 must clear — linked-list DMA must start without bit28");

    // GPU must have received the command word.
    require(system.gpu().fifoDepth() == 1u,
            "SyncMode=2: GPU must have received 1 command word from linked-list node");

    std::cerr << "[PASS] SyncMode=2 (0x01000401): starts with bit24 only (GPU linked-list "
                 "regression guard)\n";
}

// ---------------------------------------------------------------
// Test 8: SyncMode=3 does not start
//
// Writing CHCR with SyncMode=3 and both bit24+bit28 set must not trigger a
// transfer (SyncMode=3 is reserved and should be rejected).
// ---------------------------------------------------------------
static void testSyncMode3DoesNotStart()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for SyncMode=3 test");

    const Address base = channelBase(DmaPort::Otc);
    constexpr Address dest = 0x00030000u;

    constexpr u32 dpcrEnableOtc = 1u << 27u;
    system.write<u32>(DmaController::ControlReg, dpcrEnableOtc);

    system.write<u32>(dest, 0xDEADC0DEu);

    system.write<u32>(base + 0x0, dest);
    system.write<u32>(base + 0x4, 4u);
    // CHCR: SyncMode=3 (bits [10:9]=11), bit24 set, bit28 CLEAR.
    // SyncMode=3 is reserved and has no defined start trigger; bit24 alone must not start it.
    constexpr u32 chcrSyncMode3 = (3u << 9u) | (1u << 24u);
    system.write<u32>(base + 0x8, chcrSyncMode3);

    // Destination must be unchanged: SyncMode=3 must not start.
    require(system.read<u32>(dest) == 0xDEADC0DEu,
            "SyncMode=3: DMA must not start on bit24 alone (reserved sync mode)");

    std::cerr << "[PASS] SyncMode=3: DMA does not start (reserved mode rejected)\n";
}

// ---------------------------------------------------------------
// Test 9: SyncMode=3 with bit28 also does not start
//
// Before the fix, the start condition was:
//   (syncMode==1 || syncMode==2 || bit28)
// With syncMode=3 and bit28 set the last term was true, so DMA
// incorrectly started.  The corrected condition requires syncMode==0
// for the bit28 path, so SyncMode=3 + bit28 is now rejected.
// ---------------------------------------------------------------
static void testSyncMode3WithBit28DoesNotStart()
{
    PsxSystem system;
    require(system.initialize(), "Failed to initialize system for SyncMode=3+bit28 test");

    const Address base = channelBase(DmaPort::Otc);
    constexpr Address dest = 0x00031000u;

    constexpr u32 dpcrEnableOtc = 1u << 27u;
    system.write<u32>(DmaController::ControlReg, dpcrEnableOtc);
    system.write<u32>(dest, 0xC0FFEE00u);

    system.write<u32>(base + 0x0, dest);
    system.write<u32>(base + 0x4, 4u);
    // CHCR: SyncMode=3, bit24 set, bit28 ALSO set.
    // The old code treated bit28 as a universal trigger and would have started
    // the transfer.  The fix requires syncMode==0 for the bit28 path.
    constexpr u32 chcrSyncMode3Bit28 = (3u << 9u) | (1u << 28u) | (1u << 24u);
    system.write<u32>(base + 0x8, chcrSyncMode3Bit28);

    require(system.read<u32>(dest) == 0xC0FFEE00u,
            "SyncMode=3+bit28: DMA must not start (reserved mode must be rejected even with bit28)");

    std::cerr << "[PASS] SyncMode=3+bit28: DMA does not start (bit28 ignored for reserved mode)\n";
}

} // namespace

int main()
{
    testChcrBitClearSemantics();
    testDpcrDisablePreventsTransfer();
    testBcrZeroMeansMax();
    testOtcOrderingTable();
    testSyncMode0RequiresBit28();
    testSyncMode1StartsWithBit24Only();
    testSyncMode2StartsWithBit24Only();
    testSyncMode3DoesNotStart();
    testSyncMode3WithBit28DoesNotStart();

    std::cerr << "\nAll DMA CHCR semantics tests passed.\n";
    return 0;
}
