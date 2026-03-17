/**
 * @file cdrom_bank_trace_test.cpp
 * @brief Integration test for the bank-aware CDROM host-interface tracer.
 *
 * Verifies that DiagCdromBankTracer correctly classifies CDROM MMIO accesses
 * using both the physical offset (0..3) and the bank that was active at the
 * time of access.
 *
 * PSX-SPX bank mapping (relevant rows):
 *   offset 3 write, bank 0  -> REQUEST control (BFRD bit, host chip ctrl)
 *   offset 3 write, bank 1  -> HCLRCTL (IRQ acknowledge)
 *   offset 1 read,  bank 1  -> HINTSTS (interrupt-status flags)
 *   offset 1 read,  bank 3  -> HINTSTS (alternate read)
 *   offset 2 read,  bank 0  -> HINTMSK (interrupt-enable mask)
 *   offset 3 read,  bank 1  -> HINTSTS (offset 3 form)
 *
 * After this test the profile summary must be able to answer in one place
 * whether each category fired, instead of lumping everything under the
 * opaque physical address 0x1F801803.
 */
#include "psxrecomp/runtime/diag_cdrom_bank_tracer.h"
#include "psxrecomp/runtime/diag_explainers.h"
#include "psxrecomp/runtime/psx_system.h"

#include <cassert>
#include <iostream>
#include <stdexcept>
#include <string>

using psxrecomp::u8;
using psxrecomp::u32;
using namespace psxrecomp::runtime;

// ---------------------------------------------------------------------------
// Helper: assert a substring is present, otherwise throw with context.
// ---------------------------------------------------------------------------
static void assertContains(const std::string& haystack, const std::string& needle,
                            const char* context)
{
    if (haystack.find(needle) == std::string::npos)
    {
        throw std::runtime_error(std::string("FAIL [") + context + "]: expected substring \"" +
                                 needle + "\" not found in summary:\n" + haystack);
    }
}

static void assertAbsent(const std::string& haystack, const std::string& needle,
                          const char* context)
{
    if (haystack.find(needle) != std::string::npos)
    {
        throw std::runtime_error(std::string("FAIL [") + context + "]: unexpected substring \"" +
                                 needle + "\" found in summary:\n" + haystack);
    }
}

// ---------------------------------------------------------------------------
// Test 1: unit-level tracer — direct recordRead/recordWrite calls
// ---------------------------------------------------------------------------
static void testTracerDirectAccess()
{
    DiagCdromBankTracer tracer;
    tracer.enable();

    // Scenario synthesised from PSX-SPX flow during stream loop:
    //   1. Write bank-select = 1 (switch to bank 1)
    //   2. Write offset 3, bank 1  -> HCLRCTL (IRQ ack, clears HINTSTS)
    //   3. Read  offset 1, bank 1  -> HINTSTS read
    //   4. Write bank-select = 0 (back to bank 0)
    //   5. Write offset 3, bank 0  -> REQUEST control (BFRD = 0x80)

    // Step 1: bank-select write
    tracer.recordWrite(0, 0 /*bank before write*/, 1 /*new bank*/);

    // Step 2: HCLRCTL write (bank 1, offset 3, value = 0x07, ack INT1/2/3)
    tracer.recordWrite(3, 1, 0x07u);

    // Step 3: HINTSTS read (bank 1, offset 1) — should see INT3 (0x03)
    tracer.recordRead(1, 1, 0x03u);

    // Step 4: bank-select write back to 0
    tracer.recordWrite(0, 1 /*old bank*/, 0 /*new bank*/);

    // Step 5: REQUEST write (bank 0, offset 3, value = 0x80 = BFRD)
    tracer.recordWrite(3, 0, 0x80u);

    const std::string summary = tracer.formatSummary();

    // HCLRCTL write must be reported
    assertContains(summary, "HCLRCTL writes=1", "hclrctl_count");
    assertContains(summary, "last=0x7", "hclrctl_value");

    // REQUEST write must be reported, and BFRD counter must be non-zero
    assertContains(summary, "REQUEST  writes=1", "request_count");
    assertContains(summary, "last=0x80", "request_value");
    assertContains(summary, "BFRD_set count=1", "bfrd_count");

    // HINTSTS read must be reported with INT3 seen
    assertContains(summary, "HINTSTS  reads=1", "hintsts_count");
    assertContains(summary, "INT3_observed=yes", "int3_flag");
    assertAbsent(summary, "INT1_observed=yes", "int1_not_yet");

    std::cout << "[PASS] testTracerDirectAccess\n";
}

// ---------------------------------------------------------------------------
// Test 2: bank 1/3 HINTSTS reads use offset-1 and offset-3 aliases
// ---------------------------------------------------------------------------
static void testTracerHintStsOffsetAliases()
{
    DiagCdromBankTracer tracer;
    tracer.enable();

    // Offset-1 read, bank 3 -> HINTSTS (value = INT1 = 0x01)
    tracer.recordRead(1, 3, 0x01u);

    // Offset-3 read, bank 1 -> HINTSTS (value = INT3 = 0x03)
    tracer.recordRead(3, 1, 0x03u);

    // Offset-2 read, bank 0 -> HINTMSK
    tracer.recordRead(2, 0, 0x1Fu);

    const std::string summary = tracer.formatSummary();

    // Both INT1 and INT3 must be seen
    assertContains(summary, "INT1_observed=yes", "int1_from_offset1_bank3");
    assertContains(summary, "INT3_observed=yes", "int3_from_offset3_bank1");

    // HINTSTS reads (offset-1 path) should count the offset-1 bank-3 read
    assertContains(summary, "HINTSTS  reads=1", "hintsts_offset1_count");

    // HINTSTS reads via offset-3 path are reported separately
    assertContains(summary, "HINTSTS(off3) reads=1", "hintsts_offset3_count");

    // HINTMSK must be counted from offset-2 bank-0
    assertContains(summary, "HINTMSK  reads=1", "hintmsk_count");

    std::cout << "[PASS] testTracerHintStsOffsetAliases\n";
}

// ---------------------------------------------------------------------------
// Test 3: bank 0/2 offset-3 reads map to HINTMSK (not HINTSTS)
// ---------------------------------------------------------------------------
static void testTracerOffset3Bank02ReadsHintmsk()
{
    DiagCdromBankTracer tracer;
    tracer.enable();

    // Offset-3 read, bank 0 -> HINTMSK
    tracer.recordRead(3, 0, 0x1Fu);
    // Offset-3 read, bank 2 -> HINTMSK
    tracer.recordRead(3, 2, 0x1Fu);

    const std::string summary = tracer.formatSummary();

    assertContains(summary, "HINTMSK  reads=2", "hintmsk_from_offset3_bank02");
    // No HINTSTS(off3) should appear
    assertAbsent(summary, "HINTSTS(off3)", "no_hintsts_off3");

    std::cout << "[PASS] testTracerOffset3Bank02ReadsHintmsk\n";
}

// ---------------------------------------------------------------------------
// Test 4: end-to-end through PsxSystem MMIO dispatch
// ---------------------------------------------------------------------------
static void testPsxSystemMmioDispatch()
{
    PsxSystem system;
    if (!system.initialize())
    {
        throw std::runtime_error("failed to initialize PsxSystem for bank trace test");
    }

    // Enable the tracer directly (simulates what loadDiagProfile does when
    // the cdrom_bank_summary explainer is present in the profile).
    system.diagCdromBankTracer().enable();

    // --- Sequence: select bank 1, write HCLRCTL, read HINTSTS, select bank 0,
    //                write REQUEST with BFRD ---

    // Write to 0x1F801800 (bank select): value = 1 → select bank 1
    system.writeMmioExplicit<u8>(0x1F801800u, 1u);

    // Write to 0x1F801803, bank 1 → HCLRCTL (ack)
    system.writeMmioExplicit<u8>(0x1F801803u, 0x07u);

    // Read from 0x1F801801, bank 1 → HINTSTS
    (void)system.readMmioExplicit<u8>(0x1F801801u);

    // Write to 0x1F801800 (bank select): value = 0 → back to bank 0
    system.writeMmioExplicit<u8>(0x1F801800u, 0u);

    // Write to 0x1F801803, bank 0 → REQUEST with BFRD (0x80)
    system.writeMmioExplicit<u8>(0x1F801803u, 0x80u);

    const std::string summary =
        DiagExplainerEngine::explainCdromBankSummary(system.diagCdromBankTracer());

    assertContains(summary, "HCLRCTL writes=1", "psx_hclrctl_count");
    assertContains(summary, "REQUEST  writes=1", "psx_request_count");
    assertContains(summary, "BFRD_set count=1", "psx_bfrd_count");

    std::cout << "[PASS] testPsxSystemMmioDispatch\n";
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main()
{
    try
    {
        testTracerDirectAccess();
        testTracerHintStsOffsetAliases();
        testTracerOffset3Bank02ReadsHintmsk();
        testPsxSystemMmioDispatch();
    }
    catch (const std::exception& ex)
    {
        std::cerr << "cdrom_bank_trace_test FAILED: " << ex.what() << "\n";
        return 1;
    }
    std::cout << "cdrom_bank_trace_test: all tests passed\n";
    return 0;
}
