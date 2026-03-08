#include "psxrecomp/runtime/stall_classifier.h"

#include <cassert>
#include <iostream>
#include <string>

using psxrecomp::runtime::MmioAccessEntry;
using psxrecomp::runtime::RingBuffer;
using psxrecomp::runtime::StallClassifier;
using psxrecomp::runtime::StallReason;
using psxrecomp::runtime::stallReasonLabel;

// ---------------------------------------------------------------------------
// RingBuffer tests
// ---------------------------------------------------------------------------

static void testRingBufferBasic()
{
    RingBuffer<int, 4> ring;
    assert(ring.count() == 0);

    ring.push(10);
    assert(ring.count() == 1);
    assert(ring.recent(0) == 10);

    ring.push(20);
    ring.push(30);
    ring.push(40);
    assert(ring.count() == 4);
    assert(ring.recent(0) == 40);
    assert(ring.recent(1) == 30);
    assert(ring.recent(2) == 20);
    assert(ring.recent(3) == 10);
}

static void testRingBufferWrapAround()
{
    RingBuffer<int, 4> ring;
    ring.push(1);
    ring.push(2);
    ring.push(3);
    ring.push(4);
    ring.push(5); // wraps, evicts 1
    assert(ring.count() == 4);
    assert(ring.recent(0) == 5);
    assert(ring.recent(3) == 2);
}

static void testRingBufferClear()
{
    RingBuffer<int, 4> ring;
    ring.push(1);
    ring.push(2);
    ring.clear();
    assert(ring.count() == 0);
}

// ---------------------------------------------------------------------------
// StallReason label tests
// ---------------------------------------------------------------------------

static void testStallReasonLabels()
{
    assert(std::string(stallReasonLabel(StallReason::Unknown)) == "unknown");
    assert(std::string(stallReasonLabel(StallReason::CdromIrqWait)) == "CD-ROM IRQ wait");
    assert(std::string(stallReasonLabel(StallReason::ControllerPolling)) ==
           "polling JOY_STAT/JOY_CTRL forever");
    assert(std::string(stallReasonLabel(StallReason::MdecPolling)) ==
           "touching unimplemented MDEC registers");
    assert(std::string(stallReasonLabel(StallReason::BiosEventWait)) ==
           "stalled in WaitEvent / TestEvent");
    assert(std::string(stallReasonLabel(StallReason::SpinLoop)) == "CPU spin loop (repeated PC)");
}

// ---------------------------------------------------------------------------
// Classification: spin loop detection
// ---------------------------------------------------------------------------

static void testDetectSpinLoop()
{
    StallClassifier classifier;
    // Feed a repeated PC pattern: ABABABAB...
    for (int i = 0; i < 32; ++i)
    {
        classifier.recordPc(0x80010000u);
        classifier.recordPc(0x80010004u);
    }
    const auto summary = classifier.classify();
    assert(summary.find("CPU spin loop") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Classification: CD-ROM polling
// ---------------------------------------------------------------------------

static void testDetectCdromPolling()
{
    StallClassifier classifier;
    // Simulate repeated reads from CD-ROM status register.
    for (int i = 0; i < 20; ++i)
    {
        classifier.recordMmioAccess(0x1F801800u, 0x38u, false);
    }
    const auto summary = classifier.classify();
    assert(summary.find("CD-ROM register polling") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Classification: controller polling
// ---------------------------------------------------------------------------

static void testDetectControllerPolling()
{
    StallClassifier classifier;
    for (int i = 0; i < 20; ++i)
    {
        classifier.recordMmioAccess(0x1F801044u, 0x05u, false); // JOY_STAT
    }
    const auto summary = classifier.classify();
    assert(summary.find("polling JOY_STAT/JOY_CTRL forever") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Classification: MDEC polling
// ---------------------------------------------------------------------------

static void testDetectMdecPolling()
{
    StallClassifier classifier;
    for (int i = 0; i < 20; ++i)
    {
        classifier.recordMmioAccess(0x1F801824u, 0x00u, false); // MDEC status
    }
    const auto summary = classifier.classify();
    assert(summary.find("MDEC") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Classification: BIOS WaitEvent loop
// ---------------------------------------------------------------------------

static void testDetectBiosWaitEvent()
{
    StallClassifier classifier;
    for (int i = 0; i < 10; ++i)
    {
        classifier.recordBiosCall(0xB0, 0x0A, 0x0001u); // B0:0A = WaitEvent
    }
    const auto summary = classifier.classify();
    assert(summary.find("WaitEvent") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Classification: BIOS file I/O
// ---------------------------------------------------------------------------

static void testDetectBiosFileIo()
{
    StallClassifier classifier;
    for (int i = 0; i < 10; ++i)
    {
        classifier.recordBiosCall(0xB0, 0x34, 0x0000u); // B0:34 = read
    }
    const auto summary = classifier.classify();
    assert(summary.find("file/device I/O") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Classification: GPU busy polling
// ---------------------------------------------------------------------------

static void testDetectGpuBusy()
{
    StallClassifier classifier;
    for (int i = 0; i < 20; ++i)
    {
        classifier.recordMmioAccess(0x1F801814u, 0x14802000u, false); // GPUSTAT
    }
    const auto summary = classifier.classify();
    assert(summary.find("GPU busy") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Summary includes PC, MMIO, BIOS, DMA, CDROM sections
// ---------------------------------------------------------------------------

static void testSummaryIncludesAllSections()
{
    StallClassifier classifier;
    classifier.recordPc(0x80010000u);
    classifier.recordMmioAccess(0x1F801800u, 0x10, false);
    classifier.recordBiosCall(0xA0, 0x3C, 0x1234u);
    classifier.recordDmaTrigger(2, 0x00100000u, 0x00010001u);
    classifier.recordCdromIrqState(0x03, 0x0004, 0x0004, true);

    const auto summary = classifier.classify();
    assert(summary.find("Last PCs") != std::string::npos);
    assert(summary.find("Last MMIO") != std::string::npos);
    assert(summary.find("Last BIOS") != std::string::npos);
    assert(summary.find("Last DMA") != std::string::npos);
    assert(summary.find("CD-ROM/IRQ") != std::string::npos);
}

// ---------------------------------------------------------------------------
// Reset clears all buffers
// ---------------------------------------------------------------------------

static void testResetClearsAll()
{
    StallClassifier classifier;
    classifier.recordPc(0x80010000u);
    classifier.recordMmioAccess(0x1F801800u, 0x10, false);
    classifier.recordBiosCall(0xA0, 0x3C, 0x0u);
    classifier.recordDmaTrigger(2, 0x00100000u, 0x00010001u);
    classifier.recordCdromIrqState(0x03, 0x0004, 0x0004, true);
    classifier.reset();
    assert(classifier.pcRing().count() == 0);
    assert(classifier.mmioRing().count() == 0);
    assert(classifier.biosRing().count() == 0);
    assert(classifier.dmaRing().count() == 0);
    assert(classifier.cdromRing().count() == 0);
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main()
{
    testRingBufferBasic();
    testRingBufferWrapAround();
    testRingBufferClear();
    testStallReasonLabels();
    testDetectSpinLoop();
    testDetectCdromPolling();
    testDetectControllerPolling();
    testDetectMdecPolling();
    testDetectBiosWaitEvent();
    testDetectBiosFileIo();
    testDetectGpuBusy();
    testSummaryIncludesAllSections();
    testResetClearsAll();

    std::cout << "All stall classifier tests passed.\n";
    return 0;
}
