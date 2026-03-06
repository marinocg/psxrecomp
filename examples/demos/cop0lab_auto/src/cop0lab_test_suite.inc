static bool installExceptionVector(void)
{
    volatile uint32_t* const vector = (volatile uint32_t*)0xA0000080u;
    const uint32_t handlerAddress = (uint32_t)(uintptr_t)&cop0lab_exception_handler;
    const uint32_t jumpInsn = 0x08000000u | ((handlerAddress >> 2) & 0x03FFFFFFu);

    if (!g_exceptionVectorSaved)
    {
        g_savedExceptionVector[0] = vector[0];
        g_savedExceptionVector[1] = vector[1];

        // Keep hardware IRQs on BIOS' original path to avoid IRQ livelocks
        // in environments where we patch the generic exception vector.
        g_original_exception_entry = 0u;
        if ((g_savedExceptionVector[0] & 0xFC000000u) == 0x08000000u)
        {
            g_original_exception_entry =
                0x80000000u | ((g_savedExceptionVector[0] & 0x03FFFFFFu) << 2);
        }
        g_exceptionVectorSaved = true;
    }

    if (g_original_exception_entry == 0u)
    {
        g_exceptionVectorInstalled = false;
        return false;
    }

    vector[0] = jumpInsn;
    vector[1] = 0x00000000u;
    FlushCache();

    g_exceptionVectorInstalled = (vector[0] == jumpInsn) && (vector[1] == 0x00000000u);
    if (!g_exceptionVectorInstalled)
    {
        vector[0] = g_savedExceptionVector[0];
        vector[1] = g_savedExceptionVector[1];
        FlushCache();
    }
    return g_exceptionVectorInstalled;
}
#endif

static const char* outcomeName(TestOutcome outcome)
{
    switch (outcome)
    {
    case TEST_PASS:
        return "PASS";
    case TEST_FAIL:
        return "FAIL";
    case TEST_SKIP:
        return "SKIP";
    default:
        return "????";
    }
}

static uint32_t exceptionCode(void)
{
    return (g_last_exception_cause >> 2) & 0x1Fu;
}

static TestOutcome runT00(int* frameBudget, char* detail, size_t detailSize)
{
    (void)frameBudget;
    const uint32_t iStat = readIStat();
    const uint32_t iMask = readIMask();
    writeIMask(iMask);
    const uint32_t iMaskAfter = readIMask();
    formatDetail(detail, detailSize, "I_STAT=0x%08lx I_MASK=0x%08lx", (unsigned long)iStat,
                 (unsigned long)iMask);
    if (iMaskAfter != iMask)
    {
        return TEST_FAIL;
    }
    return TEST_PASS;
}

static TestOutcome runT01(int* frameBudget, char* detail, size_t detailSize)
{
    (void)frameBudget;
#if COP0LAB_ENABLE_EXCEPTION_SUITE
    if (!installExceptionVector())
    {
        g_exceptionSuiteEnabled = false;
        formatDetail(detail, detailSize, "vector patch/readback failed");
        return TEST_FAIL;
    }

    resetExceptionCapture();
    cop0lab_trigger_syscall();
    if (g_exception_count == 0 || exceptionCode() != 8u)
    {
        g_exceptionSuiteEnabled = false;
        formatDetail(detail, detailSize, "probe failed cnt=%lu code=%lu",
                     (unsigned long)g_exception_count, (unsigned long)exceptionCode());
        return TEST_FAIL;
    }

    g_exceptionSuiteEnabled = true;
    formatDetail(detail, detailSize, "handler=0x%08lx irq_passthru=0x%08lx",
                 (unsigned long)(uintptr_t)&cop0lab_exception_handler,
                 (unsigned long)g_original_exception_entry);
    return TEST_PASS;
#else
    g_exceptionSuiteEnabled = false;
    formatDetail(detail, detailSize, "disabled at build (phase2 tests will SKIP)");
    return TEST_PASS;
#endif
}

static TestOutcome runT10(int* frameBudget, char* detail, size_t detailSize)
{
    (void)frameBudget;
    const uint32_t mask = STATUS_IEC_BIT | STATUS_IM2_BIT;
    const uint32_t original = readCop0Status();
    const uint32_t toggled = original ^ mask;

    writeCop0Status(toggled);
    const uint32_t after = readCop0Status();
    writeCop0Status(original);

    const bool ok = (after & mask) == (toggled & mask);
    formatDetail(detail, detailSize, "orig=0x%08lx after=0x%08lx", (unsigned long)original,
                 (unsigned long)after);
    return ok ? TEST_PASS : TEST_FAIL;
}

static TestOutcome runT11(int* frameBudget, char* detail, size_t detailSize)
{
    (void)frameBudget;
    const uint32_t swMask = CAUSE_SW0_BIT | CAUSE_SW1_BIT;
    const uint32_t unstableMask = swMask | CAUSE_IP2_BIT;
    const uint32_t original = readCop0Cause();
    const uint32_t originalSw = original & swMask;
    const uint32_t toggledSw = originalSw ^ swMask;

    writeCop0Cause(toggledSw);
    const uint32_t after = readCop0Cause();
    writeCop0Cause(originalSw);

    const bool swOk = (after & swMask) == toggledSw;
    const bool stableOk = (after & ~unstableMask) == (original & ~unstableMask);
    formatDetail(detail, detailSize, "orig=0x%08lx after=0x%08lx", (unsigned long)original,
                 (unsigned long)after);
    return (swOk && stableOk) ? TEST_PASS : TEST_FAIL;
}

static TestOutcome runT12(int* frameBudget, char* detail, size_t detailSize)
{
    const uint32_t originalStatus = readCop0Status();
    const uint32_t originalMask = readIMask();

    writeCop0Status(originalStatus & ~STATUS_IEC_BIT);
    writeIMask(IRQ_VBLANK_BIT);
    clearVBlankLatch();

    const bool ip2LowBefore = waitCauseIp2State(false);

    if (!waitVBlankFrames(1, frameBudget))
    {
        writeCop0Status(originalStatus);
        writeIMask(originalMask);
        formatDetail(detail, detailSize, "timeout waiting first VBlank edge");
        return TEST_FAIL;
    }

    const uint32_t iStatHigh = readIStat();
    const uint32_t iMaskHigh = readIMask();
    const bool pending = ((iStatHigh & iMaskHigh) & IRQ_VBLANK_BIT) != 0u;
    const bool ip2High = waitCauseIp2State(true);

    clearVBlankLatch();
    const bool ip2Cleared = waitCauseIp2State(false);

    writeCop0Status(originalStatus);
    writeIMask(originalMask);

    formatDetail(detail, detailSize, "ip2_low_before=%d pending=%d ip2_high=%d ip2_clear=%d",
                 ip2LowBefore ? 1 : 0, pending ? 1 : 0, ip2High ? 1 : 0, ip2Cleared ? 1 : 0);
    return (ip2LowBefore && pending && ip2High && ip2Cleared) ? TEST_PASS : TEST_FAIL;
}

static TestOutcome runT13(int* frameBudget, char* detail, size_t detailSize)
{
    const uint32_t originalStatus = readCop0Status();
    const uint32_t originalMask = readIMask();

    writeCop0Status(originalStatus & ~STATUS_IEC_BIT);
    writeIMask(IRQ_VBLANK_BIT);

    if (!waitVBlankFrames(1, frameBudget))
    {
        writeCop0Status(originalStatus);
        writeIMask(originalMask);
        formatDetail(detail, detailSize, "timeout arranging VBlank pending");
        return TEST_FAIL;
    }

    const bool ip2Before = waitCauseIp2State(true);
    const uint32_t causeBefore = readCop0Cause();
    const uint32_t swBefore = causeBefore & (CAUSE_SW0_BIT | CAUSE_SW1_BIT);
    const uint32_t swAfter = swBefore ^ (CAUSE_SW0_BIT | CAUSE_SW1_BIT);

    writeCop0Cause(swAfter);
    const uint32_t causeAfter = readCop0Cause();
    const bool ip2After = waitCauseIp2State(true);

    clearVBlankLatch();
    writeCop0Cause(swBefore);
    writeCop0Status(originalStatus);
    writeIMask(originalMask);

    const bool swChanged = (causeAfter & (CAUSE_SW0_BIT | CAUSE_SW1_BIT)) == swAfter;

    formatDetail(detail, detailSize, "ip2_before=%d ip2_after=%d sw_changed=%d", ip2Before ? 1 : 0,
                 ip2After ? 1 : 0, swChanged ? 1 : 0);
    return (ip2Before && ip2After && swChanged) ? TEST_PASS : TEST_FAIL;
}

static int runIrqGateWindow(bool iecEnabled, bool im2Enabled, int frames, int* frameBudget)
{
    uint32_t status = readCop0Status();
    status &= ~(STATUS_IEC_BIT | STATUS_IM_MASK);
    if (iecEnabled)
    {
        status |= STATUS_IEC_BIT;
    }
    if (im2Enabled)
    {
        status |= STATUS_IM2_BIT;
    }

    writeCop0Status(status);
    clearVBlankLatch();
    const uint32_t before = g_vblank_irq_count;

    for (int i = 0; i < frames; ++i)
    {
        if (*frameBudget <= 0)
        {
            return -1;
        }
        if (!waitForNextGpuVBlankRise())
        {
            return -1;
        }
        --(*frameBudget);
    }

    const uint32_t after = g_vblank_irq_count;
    return (int)(after - before);
}

static TestOutcome runT14(int* frameBudget, char* detail, size_t detailSize)
{
    const uint32_t originalStatus = readCop0Status();
    const uint32_t originalMask = readIMask();

    writeIMask(IRQ_VBLANK_BIT);

    const int caseA = runIrqGateWindow(false, true, 6, frameBudget);
    const int caseB = runIrqGateWindow(true, false, 6, frameBudget);
    const int caseC = runIrqGateWindow(true, true, 6, frameBudget);

    writeCop0Status(originalStatus);
    writeIMask(originalMask);

    if (caseA < 0 || caseB < 0 || caseC < 0)
    {
        formatDetail(detail, detailSize, "timeout in IRQ gate windows");
        return TEST_FAIL;
    }

    formatDetail(detail, detailSize, "IE0/IM2=%d IE1/IM2=0:%d IE1/IM2=1:%d", caseA, caseB, caseC);
    return (caseA == 0 && caseB == 0 && caseC > 0) ? TEST_PASS : TEST_FAIL;
}

static TestOutcome runT15(int* frameBudget, char* detail, size_t detailSize)
{
    if (!g_exceptionSuiteEnabled)
    {
        formatDetail(detail, detailSize, "requires exception wrapper telemetry");
        return TEST_SKIP;
    }

    const uint32_t originalStatus = readCop0Status();
    const uint32_t originalCause = readCop0Cause();
    const uint32_t originalSw = originalCause & (CAUSE_SW0_BIT | CAUSE_SW1_BIT);

    uint32_t status = originalStatus;
    status &= ~(STATUS_IM0_BIT | STATUS_IM1_BIT | STATUS_IM2_BIT | STATUS_IEC_BIT);
    status |= STATUS_IM0_BIT | STATUS_IEC_BIT;

    writeCop0Status(status);
    resetExceptionCapture();
    g_autoclear_sw_irq = 1;
    writeCop0Cause(CAUSE_SW0_BIT);

    for (int i = 0; i < 12000; ++i)
    {
        if (g_irq_exception_count > 0)
        {
            break;
        }
        __asm__ volatile("nop");
    }

    const uint32_t beforeClear = g_irq_exception_count;
    writeCop0Cause(0u);

    if (!waitVBlankFrames(1, frameBudget))
    {
        writeCop0Cause(originalSw);
        writeCop0Status(originalStatus);
        formatDetail(detail, detailSize, "timeout after SW0 clear");
        return TEST_FAIL;
    }

    const uint32_t afterClear = g_irq_exception_count;

    g_autoclear_sw_irq = 0;
    writeCop0Cause(originalSw);
    writeCop0Status(originalStatus);

    const bool triggered = beforeClear > 0u;
    const bool stopped = afterClear <= (beforeClear + 1u);

    formatDetail(detail, detailSize, "before_clear=%lu after_clear=%lu", (unsigned long)beforeClear,
                 (unsigned long)afterClear);
    return (triggered && stopped) ? TEST_PASS : TEST_FAIL;
}

static TestOutcome runExceptionExpectation(void (*trigger)(void), uint32_t expectedCode,
                                           uint32_t expectedBdBit, uint32_t expectedBadvaddr,
                                           bool checkBadvaddr, int* frameBudget, char* detail,
                                           size_t detailSize)
{
    (void)frameBudget;
    if (!g_exceptionSuiteEnabled)
    {
        formatDetail(detail, detailSize, "exception wrapper unavailable");
        return TEST_SKIP;
    }

    resetExceptionCapture();
    trigger();

    if (g_exception_count == 0)
    {
        formatDetail(detail, detailSize, "no exception observed");
        return TEST_FAIL;
    }

    const uint32_t cause = g_last_exception_cause;
    const uint32_t code = exceptionCode();
    const uint32_t bd = (cause >> 31) & 1u;

    if (code != expectedCode)
    {
        formatDetail(detail, detailSize, "code=%lu expected=%lu", (unsigned long)code,
                     (unsigned long)expectedCode);
        return TEST_FAIL;
    }

    if (bd != expectedBdBit)
    {
        formatDetail(detail, detailSize, "bd=%lu expected=%lu", (unsigned long)bd,
                     (unsigned long)expectedBdBit);
        return TEST_FAIL;
    }

    if (checkBadvaddr && g_last_exception_badvaddr != expectedBadvaddr)
    {
        formatDetail(detail, detailSize, "badv=0x%08lx expected=0x%08lx",
                     (unsigned long)g_last_exception_badvaddr, (unsigned long)expectedBadvaddr);
        return TEST_FAIL;
    }

    formatDetail(detail, detailSize, "cause=0x%08lx epc=0x%08lx", (unsigned long)cause,
                 (unsigned long)g_last_exception_epc);
    return TEST_PASS;
}

static void triggerLwc0Only(void)
{
    cop0lab_trigger_lwc0();
}

static void triggerDelaySlot(void)
{
    g_resume_advance = 8;
    cop0lab_trigger_delay_slot_syscall();
    g_resume_advance = 4;
}

static void triggerMisalignedLoad(void)
{
    cop0lab_trigger_misaligned_load((uint32_t)(uintptr_t)((uint8_t*)g_alignmentScratch + 2));
}

static void triggerMisalignedStore(void)
{
    cop0lab_trigger_misaligned_store((uint32_t)(uintptr_t)((uint8_t*)g_alignmentScratch + 2));
}

static TestOutcome runT20(int* frameBudget, char* detail, size_t detailSize)
{
    return runExceptionExpectation(cop0lab_trigger_syscall, 8u, 0u, 0u, false, frameBudget, detail,
                                   detailSize);
}

static TestOutcome runT21(int* frameBudget, char* detail, size_t detailSize)
{
    return runExceptionExpectation(cop0lab_trigger_break, 9u, 0u, 0u, false, frameBudget, detail,
                                   detailSize);
}

static TestOutcome runT22(int* frameBudget, char* detail, size_t detailSize)
{
    return runExceptionExpectation(cop0lab_trigger_tlbwi, 10u, 0u, 0u, false, frameBudget, detail,
                                   detailSize);
}

static TestOutcome runT23(int* frameBudget, char* detail, size_t detailSize)
{
    return runExceptionExpectation(triggerLwc0Only, 11u, 0u, 0u, false, frameBudget, detail,
                                   detailSize);
}

static TestOutcome runT24(int* frameBudget, char* detail, size_t detailSize)
{
    const uint32_t badvaddr = (uint32_t)(uintptr_t)((uint8_t*)g_alignmentScratch + 2);
    return runExceptionExpectation(triggerMisalignedLoad, 4u, 0u, badvaddr, true, frameBudget,
                                   detail, detailSize);
}

static TestOutcome runT25(int* frameBudget, char* detail, size_t detailSize)
{
    const uint32_t badvaddr = (uint32_t)(uintptr_t)((uint8_t*)g_alignmentScratch + 2);
    return runExceptionExpectation(triggerMisalignedStore, 5u, 0u, badvaddr, true, frameBudget,
                                   detail, detailSize);
}

static TestOutcome runT26(int* frameBudget, char* detail, size_t detailSize)
{
    (void)frameBudget;
    if (!g_exceptionSuiteEnabled)
    {
        formatDetail(detail, detailSize, "exception wrapper unavailable");
        return TEST_SKIP;
    }

    g_delay_slot_branch_pc = 0;
    resetExceptionCapture();
    triggerDelaySlot();

    if (g_exception_count == 0)
    {
        formatDetail(detail, detailSize, "no exception observed");
        return TEST_FAIL;
    }

    const uint32_t code = exceptionCode();
    const bool bd = (g_last_exception_cause & 0x80000000u) != 0u;
    const bool epcMatches =
        g_delay_slot_branch_pc != 0u && g_last_exception_epc == g_delay_slot_branch_pc;

    formatDetail(detail, detailSize, "code=%lu bd=%d epc=0x%08lx branch=0x%08lx",
                 (unsigned long)code, bd ? 1 : 0, (unsigned long)g_last_exception_epc,
                 (unsigned long)g_delay_slot_branch_pc);

    return (code == 8u && bd && epcMatches) ? TEST_PASS : TEST_FAIL;
}

typedef TestOutcome (*TestFn)(int* frameBudget, char* detail, size_t detailSize);

static const struct
{
    TestCase meta;
    TestFn fn;
} kTestTable[] = {
    {{"T00", "MMIO sanity", 30}, runT00},
    {{"T01", "Exception wrapper install", 60}, runT01},
    {{"T10", "Status roundtrip (IEc/IM10)", 30}, runT10},
    {{"T11", "Cause SW bits 8-9 latch", 30}, runT11},
    {{"T12", "IP10 mirrors (I_STAT & I_MASK)", 60}, runT12},
    {{"T13", "MTC0(Cause) preserves IP10", 60}, runT13},
    {{"T14", "IRQ gating (IEc+IM10)", 120}, runT14},
    {{"T15", "SW interrupt delivery", 90}, runT15},
    {{"T20", "SYSCALL exception code", 30}, runT20},
    {{"T21", "BREAK exception code", 30}, runT21},
    {{"T22", "RI via TLBWI", 30}, runT22},
    {{"T23", "CpU via LWC0", 30}, runT23},
    {{"T24", "AdEL + BadVAddr", 30}, runT24},
    {{"T25", "AdES + BadVAddr", 30}, runT25},
    {{"T26", "Delay-slot BD/EPC", 30}, runT26},
