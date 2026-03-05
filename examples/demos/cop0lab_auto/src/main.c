#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>

#define SCREEN_XRES 320
#define SCREEN_YRES 240
#define RESTART_DELAY_FRAMES 180
#define RECENT_RESULTS_TO_SHOW 12
#define VBLANK_SPIN_LIMIT 12000000u

#ifndef COP0LAB_ENABLE_EXCEPTION_SUITE
#define COP0LAB_ENABLE_EXCEPTION_SUITE 0
#endif

typedef struct
{
    DISPENV disp[2];
    DRAWENV draw[2];
    int activeBuffer;
} GpuContext;

typedef enum
{
    TEST_PASS,
    TEST_FAIL,
    TEST_SKIP,
} TestOutcome;

typedef struct
{
    const char* id;
    const char* name;
    int timeoutFrames;
} TestCase;

typedef struct
{
    const char* id;
    const char* name;
    int timeoutFrames;
    int elapsedFrames;
    TestOutcome outcome;
    char detail[96];
} TestResult;

static volatile uint32_t g_exception_count;
static volatile uint32_t g_irq_exception_count;
static volatile uint32_t g_vblank_irq_count;
static volatile uint32_t g_last_exception_cause;
static volatile uint32_t g_last_exception_epc;
static volatile uint32_t g_last_exception_badvaddr;
static volatile uint32_t g_last_exception_status;
static volatile uint32_t g_resume_advance = 4;
static volatile uint32_t g_delay_slot_branch_pc;
static volatile uint32_t g_autoclear_sw_irq;
static volatile uint32_t g_passthrough_irq_exceptions = 1;
static volatile uint32_t g_original_exception_entry;

extern void cop0lab_exception_handler(void);
extern void cop0lab_trigger_syscall(void);
extern void cop0lab_trigger_break(void);
extern void cop0lab_trigger_tlbwi(void);
extern void cop0lab_trigger_lwc0(void);
extern void cop0lab_trigger_swc0(void);
extern void cop0lab_trigger_misaligned_load(uint32_t address);
extern void cop0lab_trigger_misaligned_store(uint32_t address);
extern void cop0lab_trigger_delay_slot_syscall(void);

__asm__(".set push\n"
        ".set noreorder\n"
        ".set noat\n"
        ".globl cop0lab_exception_handler\n"
        "cop0lab_exception_handler:\n"
        "  mfc0  $k0, $13\n"
        "  nop\n"
        "  lui   $k1, %hi(g_last_exception_cause)\n"
        "  sw    $k0, %lo(g_last_exception_cause)($k1)\n"
        "  mfc0  $k0, $14\n"
        "  nop\n"
        "  sw    $k0, %lo(g_last_exception_epc)($k1)\n"
        "  mfc0  $k0, $8\n"
        "  nop\n"
        "  sw    $k0, %lo(g_last_exception_badvaddr)($k1)\n"
        "  mfc0  $k0, $12\n"
        "  nop\n"
        "  sw    $k0, %lo(g_last_exception_status)($k1)\n"
        "  lw    $k0, %lo(g_exception_count)($k1)\n"
        "  addiu $k0, $k0, 1\n"
        "  sw    $k0, %lo(g_exception_count)($k1)\n"
        "  lw    $k0, %lo(g_last_exception_cause)($k1)\n"
        "  srl   $k0, $k0, 2\n"
        "  andi  $k0, $k0, 0x1f\n"
        "  bnez  $k0, 1f\n"
        "  nop\n"
        "  lw    $k0, %lo(g_irq_exception_count)($k1)\n"
        "  addiu $k0, $k0, 1\n"
        "  sw    $k0, %lo(g_irq_exception_count)($k1)\n"
        "  lui   $k0, %hi(g_passthrough_irq_exceptions)\n"
        "  lw    $k0, %lo(g_passthrough_irq_exceptions)($k0)\n"
        "  beqz  $k0, 1f\n"
        "  nop\n"
        "  lui   $k0, %hi(g_original_exception_entry)\n"
        "  lw    $k0, %lo(g_original_exception_entry)($k0)\n"
        "  beqz  $k0, 1f\n"
        "  nop\n"
        "  jr    $k0\n"
        "  nop\n"
        "1:\n"
        "  lui   $k0, %hi(g_autoclear_sw_irq)\n"
        "  lw    $k0, %lo(g_autoclear_sw_irq)($k0)\n"
        "  beqz  $k0, 2f\n"
        "  nop\n"
        "  mtc0  $zero, $13\n"
        "  nop\n"
        "2:\n"
        "  lui   $k1, %hi(g_resume_advance)\n"
        "  lw    $k1, %lo(g_resume_advance)($k1)\n"
        "  lui   $k0, %hi(g_last_exception_cause)\n"
        "  lw    $k0, %lo(g_last_exception_cause)($k0)\n"
        "  srl   $k0, $k0, 2\n"
        "  andi  $k0, $k0, 0x1f\n"
        "  bnez  $k0, 3f\n"
        "  nop\n"
        "  or    $k1, $zero, $zero\n"
        "3:\n"
        "  mfc0  $k0, $14\n"
        "  nop\n"
        "  addu  $k0, $k0, $k1\n"
        "  jr    $k0\n"
        "  rfe\n"
        ".set pop\n");

__asm__(".set push\n"
        ".set noreorder\n"
        ".globl cop0lab_trigger_syscall\n"
        "cop0lab_trigger_syscall:\n"
        "  syscall 0x111\n"
        "  jr    $ra\n"
        "  nop\n"
        ".globl cop0lab_trigger_break\n"
        "cop0lab_trigger_break:\n"
        "  break  0x222\n"
        "  jr    $ra\n"
        "  nop\n"
        ".globl cop0lab_trigger_tlbwi\n"
        "cop0lab_trigger_tlbwi:\n"
        "  .word 0x42000002\n"
        "  jr    $ra\n"
        "  nop\n"
        ".globl cop0lab_trigger_lwc0\n"
        "cop0lab_trigger_lwc0:\n"
        "  .word 0xC0000000\n"
        "  jr    $ra\n"
        "  nop\n"
        ".globl cop0lab_trigger_swc0\n"
        "cop0lab_trigger_swc0:\n"
        "  .word 0xE0000000\n"
        "  jr    $ra\n"
        "  nop\n"
        ".globl cop0lab_trigger_misaligned_load\n"
        "cop0lab_trigger_misaligned_load:\n"
        "  lw    $t0, 0($a0)\n"
        "  jr    $ra\n"
        "  nop\n"
        ".globl cop0lab_trigger_misaligned_store\n"
        "cop0lab_trigger_misaligned_store:\n"
        "  sw    $zero, 0($a0)\n"
        "  jr    $ra\n"
        "  nop\n"
        ".globl cop0lab_trigger_delay_slot_syscall\n"
        "cop0lab_trigger_delay_slot_syscall:\n"
        "  la    $t0, cop0lab_delay_slot_branch\n"
        "  lui   $t1, %hi(g_delay_slot_branch_pc)\n"
        "  sw    $t0, %lo(g_delay_slot_branch_pc)($t1)\n"
        "cop0lab_delay_slot_branch:\n"
        "  b     cop0lab_delay_slot_after\n"
        "  syscall 0x333\n"
        "cop0lab_delay_slot_after:\n"
        "  jr    $ra\n"
        "  nop\n"
        ".set pop\n");

static volatile uint32_t* const kIStat = (volatile uint32_t*)0xBF801070u;
static volatile uint32_t* const kIMask = (volatile uint32_t*)0xBF801074u;
static volatile uint32_t* const kGpuStat = (volatile uint32_t*)0xBF801814u;

static const uint32_t STATUS_IEC_BIT = 1u << 0;
static const uint32_t STATUS_IM_MASK = 0x0000FF00u;
static const uint32_t STATUS_IM0_BIT = 1u << 8;
static const uint32_t STATUS_IM1_BIT = 1u << 9;
static const uint32_t STATUS_IM2_BIT = 1u << 10;
static const uint32_t CAUSE_SW0_BIT = 1u << 8;
static const uint32_t CAUSE_SW1_BIT = 1u << 9;
static const uint32_t CAUSE_IP2_BIT = 1u << 10;
static const uint32_t IRQ_VBLANK_BIT = 1u << 0;
static const uint32_t GPUSTAT_VBLANK_PHASE_BIT = 1u << 22;

static void (*g_previousVBlankCallback)(void);
static bool g_vblankProbeInstalled;

#if COP0LAB_ENABLE_EXCEPTION_SUITE
static uint32_t g_savedExceptionVector[2];
static bool g_exceptionVectorSaved;
static bool g_exceptionVectorInstalled;
#endif
static bool g_exceptionSuiteEnabled;

static int g_suiteIteration;
static int g_currentTest;
static int g_resultsCount;
static int g_restartCountdown;
static bool g_suiteComplete;

static TestResult g_results[16];

static const int kTestCount = 15;

static uint32_t g_alignmentScratch[4];

static void cop0labVBlankProbeCallback(void)
{
    ++g_vblank_irq_count;
    if (g_previousVBlankCallback != NULL)
    {
        g_previousVBlankCallback();
    }
}

static inline uint32_t readIStat(void)
{
    return *kIStat;
}

static inline uint32_t readIMask(void)
{
    return *kIMask;
}

static inline void writeIStat(uint32_t value)
{
    *kIStat = value;
}

static inline void writeIMask(uint32_t value)
{
    *kIMask = value;
}

static inline uint32_t readGpuStat(void)
{
    return *kGpuStat;
}

static inline uint32_t readCop0Status(void)
{
    uint32_t value;
    __asm__ volatile("mfc0 %0, $12" : "=r"(value));
    return value;
}

static inline uint32_t readCop0Cause(void)
{
    uint32_t value;
    __asm__ volatile("mfc0 %0, $13" : "=r"(value));
    return value;
}

static inline uint32_t readCop0Epc(void)
{
    uint32_t value;
    __asm__ volatile("mfc0 %0, $14" : "=r"(value));
    return value;
}

static inline uint32_t readCop0BadVAddr(void)
{
    uint32_t value;
    __asm__ volatile("mfc0 %0, $8" : "=r"(value));
    return value;
}

static inline void writeCop0Status(uint32_t value)
{
    __asm__ volatile("mtc0 %0, $12\n"
                     "nop\n"
                     :
                     : "r"(value)
                     : "memory");
}

static inline void writeCop0Cause(uint32_t value)
{
    __asm__ volatile("mtc0 %0, $13\n"
                     "nop\n"
                     :
                     : "r"(value)
                     : "memory");
}

static void clearVBlankLatch(void)
{
    writeIStat(~IRQ_VBLANK_BIT);
}

static bool waitForNextVBlankEdge(void)
{
    // Keep VBlank source enabled for polling-based pacing.
    const uint32_t mask = readIMask();
    if ((mask & IRQ_VBLANK_BIT) == 0u)
    {
        writeIMask(mask | IRQ_VBLANK_BIT);
    }

    clearVBlankLatch();
    for (uint32_t spin = 0; spin < VBLANK_SPIN_LIMIT; ++spin)
    {
        if ((readIStat() & IRQ_VBLANK_BIT) != 0u)
        {
            return true;
        }
    }
    return false;
}

static bool waitVBlankFrames(int count, int* frameBudget)
{
    for (int i = 0; i < count; ++i)
    {
        if (*frameBudget <= 0)
        {
            return false;
        }
        if (!waitForNextVBlankEdge())
        {
            return false;
        }
        --(*frameBudget);
    }
    return true;
}

static bool waitForNextGpuVBlankRise(void)
{
    bool sawNonVBlank = false;
    for (uint32_t spin = 0; spin < VBLANK_SPIN_LIMIT; ++spin)
    {
        if ((readGpuStat() & GPUSTAT_VBLANK_PHASE_BIT) == 0u)
        {
            sawNonVBlank = true;
            break;
        }
    }
    if (!sawNonVBlank)
    {
        return false;
    }

    for (uint32_t spin = 0; spin < VBLANK_SPIN_LIMIT; ++spin)
    {
        if ((readGpuStat() & GPUSTAT_VBLANK_PHASE_BIT) != 0u)
        {
            return true;
        }
    }
    return false;
}

static bool waitCauseIp2State(bool expectedSet)
{
    for (uint32_t spin = 0; spin < 200000u; ++spin)
    {
        const bool isSet = (readCop0Cause() & CAUSE_IP2_BIT) != 0u;
        if (isSet == expectedSet)
        {
            return true;
        }
    }
    return false;
}

static void formatDetail(char* out, size_t outSize, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(out, outSize, fmt, args);
    va_end(args);
}

static void resetExceptionCapture(void)
{
    g_exception_count = 0;
    g_irq_exception_count = 0;
    g_last_exception_cause = 0;
    g_last_exception_epc = 0;
    g_last_exception_badvaddr = 0;
    g_last_exception_status = 0;
    g_resume_advance = 4;
    g_autoclear_sw_irq = 0;
}

static void installVBlankProbe(void)
{
    if (g_vblankProbeInstalled)
    {
        return;
    }

    g_previousVBlankCallback =
        (void (*)(void))InterruptCallback(IRQ_VBLANK, cop0labVBlankProbeCallback);
    g_vblankProbeInstalled = true;
}

#if COP0LAB_ENABLE_EXCEPTION_SUITE
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
};

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

int main(void)
{
    static GpuContext ctx;
    int heartbeat = 0;

    initGpu(&ctx);
    installVBlankProbe();

    FntLoad(960, 0);
    const int fontId = FntOpen(8, 8, SCREEN_XRES - 16, SCREEN_YRES - 16, 0, 1024);

    beginSuite();

    while (1)
    {
        if (!g_suiteComplete)
        {
            runNextTestStep();
        }
        else
        {
            if (g_restartCountdown > 0)
            {
                --g_restartCountdown;
            }
            else
            {
                beginSuite();
            }
        }

        drawUi(fontId, heartbeat);
        presentFrame(&ctx, fontId);

        ++heartbeat;
    }

    return 0;
}
