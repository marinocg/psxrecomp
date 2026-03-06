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

#include "cop0lab_asm.inc"

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
#include "cop0lab_gpu_ui.inc"
#include "cop0lab_test_suite.inc"
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
