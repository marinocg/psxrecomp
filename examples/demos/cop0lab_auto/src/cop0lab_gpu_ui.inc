static void initGpu(GpuContext* ctx)
{
    ResetGraph(0);

    SetDefDispEnv(&ctx->disp[0], 0, 0, SCREEN_XRES, SCREEN_YRES);
    SetDefDispEnv(&ctx->disp[1], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&ctx->draw[0], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&ctx->draw[1], 0, 0, SCREEN_XRES, SCREEN_YRES);

    ctx->draw[0].isbg = 1;
    ctx->draw[1].isbg = 1;
    ctx->draw[0].r0 = 6;
    ctx->draw[0].g0 = 10;
    ctx->draw[0].b0 = 22;
    ctx->draw[1].r0 = 6;
    ctx->draw[1].g0 = 10;
    ctx->draw[1].b0 = 22;

    ctx->activeBuffer = 0;
    PutDispEnv(&ctx->disp[ctx->activeBuffer]);
    PutDrawEnv(&ctx->draw[ctx->activeBuffer]);
    SetDispMask(1);
}

static void beginSuite(void)
{
    g_exceptionSuiteEnabled = false;
    g_currentTest = 0;
    g_resultsCount = 0;
    g_suiteComplete = false;
    g_restartCountdown = RESTART_DELAY_FRAMES;
    ++g_suiteIteration;
}

static void runNextTestStep(void)
{
    if (g_currentTest >= kTestCount)
    {
        g_suiteComplete = true;
        return;
    }

    const TestCase* meta = &kTestTable[g_currentTest].meta;
    TestResult* out = &g_results[g_resultsCount];

    out->id = meta->id;
    out->name = meta->name;
    out->timeoutFrames = meta->timeoutFrames;
    out->elapsedFrames = 0;
    out->outcome = TEST_FAIL;
    out->detail[0] = '\0';

    int frameBudget = meta->timeoutFrames;
    out->outcome = kTestTable[g_currentTest].fn(&frameBudget, out->detail, sizeof(out->detail));
    out->elapsedFrames = meta->timeoutFrames - frameBudget;

    if (out->outcome == TEST_FAIL && out->detail[0] == '\0' && frameBudget <= 0)
    {
        formatDetail(out->detail, sizeof(out->detail), "timeout");
    }

    if (out->detail[0] == '\0')
    {
        formatDetail(out->detail, sizeof(out->detail), "-");
    }

    ++g_currentTest;
    ++g_resultsCount;

    if (g_currentTest >= kTestCount)
    {
        g_suiteComplete = true;
    }
}

static void drawUi(int fontId, int heartbeat)
{
    const char spinner[4] = {'|', '/', '-', '\\'};
    int passCount = 0;
    int failCount = 0;
    int skipCount = 0;

    for (int i = 0; i < g_resultsCount; ++i)
    {
        if (g_results[i].outcome == TEST_PASS)
        {
            ++passCount;
        }
        else if (g_results[i].outcome == TEST_FAIL)
        {
            ++failCount;
        }
        else
        {
            ++skipCount;
        }
    }

    const uint32_t status = readCop0Status();
    const uint32_t cause = readCop0Cause();
    const uint32_t epc = readCop0Epc();
    const uint32_t bad = readCop0BadVAddr();
    const uint32_t iStat = readIStat();
    const uint32_t iMask = readIMask();

    FntPrint(fontId, "COP0LAB AUTO %c\n", spinner[heartbeat & 3]);
    FntPrint(fontId, "Build: %s %s\n", __DATE__, __TIME__);
    FntPrint(fontId, "Suite run: %d\n", g_suiteIteration);

    if (!g_suiteComplete)
    {
        FntPrint(fontId, "State: running tests... (%d/%d)\n", g_currentTest + 1, kTestCount);
        FntPrint(fontId, "Now: %s %s\n", kTestTable[g_currentTest].meta.id,
                 kTestTable[g_currentTest].meta.name);
    }
    else
    {
        FntPrint(fontId, "State: complete, restart in %d frames\n", g_restartCountdown);
        FntPrint(fontId, "Now: summary\n");
    }

    FntPrint(fontId, "Summary: PASS=%d FAIL=%d SKIP=%d\n\n", passCount, failCount, skipCount);

    FntPrint(fontId, "COP0 Status=0x%08lx Cause=0x%08lx EPC=0x%08lx\n", (unsigned long)status,
             (unsigned long)cause, (unsigned long)epc);
    FntPrint(fontId, "COP0 BadVAddr=0x%08lx I_STAT=0x%08lx I_MASK=0x%08lx\n\n", (unsigned long)bad,
             (unsigned long)iStat, (unsigned long)iMask);

    const int start =
        (g_resultsCount > RECENT_RESULTS_TO_SHOW) ? (g_resultsCount - RECENT_RESULTS_TO_SHOW) : 0;
    FntPrint(fontId, "Last results:\n");
    for (int i = start; i < g_resultsCount; ++i)
    {
        const TestResult* result = &g_results[i];
        FntPrint(fontId, "%s %-4s %2df %s\n", result->id, outcomeName(result->outcome),
                 result->elapsedFrames, result->detail);
    }

    if (!g_exceptionSuiteEnabled)
    {
        FntPrint(fontId, "\nException suite unavailable: T15 + phase 2 tests SKIP.\n");
    }
}

static void presentFrame(GpuContext* ctx, int fontId)
{
    FntFlush(fontId);
    DrawSync(0);

    // Match the usual VSync swap ordering: wait first, then swap.
    if (!waitForNextVBlankEdge())
    {
        // Keep running; the UI will show stale frame when vblank polling is unavailable.
    }

    PutDispEnv(&ctx->disp[ctx->activeBuffer]);
    ctx->activeBuffer ^= 1;
    PutDrawEnv(&ctx->draw[ctx->activeBuffer]);
}
