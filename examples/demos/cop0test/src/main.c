#include <stdint.h>

#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>

#define SCREEN_XRES 320
#define SCREEN_YRES 240

typedef struct {
    DISPENV disp[2];
    DRAWENV draw[2];
    int activeBuffer;
} GpuContext;

volatile uint32_t g_exception_cause;
volatile uint32_t g_exception_epc;
volatile uint32_t g_exception_count;

extern void cop0_exception_handler(void);

__asm__(
    ".set push\n"
    ".set noreorder\n"
    ".set noat\n"
    ".globl cop0_exception_handler\n"
    "cop0_exception_handler:\n"
    "  mfc0  $k0, $13\n"
    "  nop\n"
    "  lui   $k1, %hi(g_exception_cause)\n"
    "  sw    $k0, %lo(g_exception_cause)($k1)\n"
    "  mfc0  $k0, $14\n"
    "  nop\n"
    "  sw    $k0, %lo(g_exception_epc)($k1)\n"
    "  lw    $k0, %lo(g_exception_count)($k1)\n"
    "  addiu $k0, $k0, 1\n"
    "  sw    $k0, %lo(g_exception_count)($k1)\n"
    "  mfc0  $k0, $14\n"
    "  nop\n"
    "  addiu $k0, $k0, 4\n"
    "  jr    $k0\n"
    "  rfe\n"
    ".set pop\n"
);

static uint32_t s_savedVector[2];

static inline uint32_t readCop0Status(void) {
    uint32_t value;
    __asm__ volatile("mfc0 %0, $12" : "=r"(value));
    return value;
}

static inline uint32_t readCop0Cause(void) {
    uint32_t value;
    __asm__ volatile("mfc0 %0, $13" : "=r"(value));
    return value;
}

static inline uint32_t readCop0Epc(void) {
    uint32_t value;
    __asm__ volatile("mfc0 %0, $14" : "=r"(value));
    return value;
}

static inline void writeCop0Status(uint32_t value) {
    __asm__ volatile(
        "mtc0 %0, $12\n"
        "nop\n"
        :
        : "r"(value)
        : "memory"
    );
}

static inline void triggerSyscallException(void) {
    __asm__ volatile(
        ".set push\n"
        ".set noreorder\n"
        "syscall 0x1234\n"
        "nop\n"
        ".set pop\n"
        :
        :
        : "memory"
    );
}

static void installExceptionVector(void) {
    volatile uint32_t* const vector = (volatile uint32_t*)0xa0000080u;
    const uint32_t handlerAddress = (uint32_t)&cop0_exception_handler;

    s_savedVector[0] = vector[0];
    s_savedVector[1] = vector[1];

    vector[0] = 0x08000000u | ((handlerAddress >> 2) & 0x03ffffffu);
    vector[1] = 0x00000000u;

    FlushCache();
}

static void restoreExceptionVector(void) {
    volatile uint32_t* const vector = (volatile uint32_t*)0xa0000080u;

    vector[0] = s_savedVector[0];
    vector[1] = s_savedVector[1];

    FlushCache();
}

static void initGpu(GpuContext* ctx) {
    ResetGraph(0);

    SetDefDispEnv(&ctx->disp[0], 0, 0, SCREEN_XRES, SCREEN_YRES);
    SetDefDispEnv(&ctx->disp[1], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&ctx->draw[0], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&ctx->draw[1], 0, 0, SCREEN_XRES, SCREEN_YRES);

    ctx->draw[0].isbg = 1;
    ctx->draw[1].isbg = 1;
    ctx->draw[0].r0 = 0;
    ctx->draw[0].g0 = 0;
    ctx->draw[0].b0 = 0;
    ctx->draw[1].r0 = 0;
    ctx->draw[1].g0 = 0;
    ctx->draw[1].b0 = 0;

    ctx->activeBuffer = 0;
    PutDispEnv(&ctx->disp[ctx->activeBuffer]);
    PutDrawEnv(&ctx->draw[ctx->activeBuffer]);
    SetDispMask(1);
}

int main(void) {
    static GpuContext ctx;

    uint32_t statusInitial;
    uint32_t causeInitial;
    uint32_t epcInitial;
    uint32_t statusAfterEnable;
    uint32_t statusAfterException;
    uint32_t causeAfterException;
    uint32_t epcAfterException;
    uint32_t exceptionCode;

    initGpu(&ctx);

    FntLoad(960, 0);
    int fontId = FntOpen(16, 16, SCREEN_XRES, SCREEN_YRES - 32, 0, 512);

    statusInitial = readCop0Status();
    causeInitial = readCop0Cause();
    epcInitial = readCop0Epc();

    writeCop0Status(statusInitial | 0x00000001u);
    statusAfterEnable = readCop0Status();

    g_exception_cause = 0;
    g_exception_epc = 0;
    g_exception_count = 0;

    installExceptionVector();
    triggerSyscallException();
    restoreExceptionVector();

    statusAfterException = readCop0Status();
    causeAfterException = readCop0Cause();
    epcAfterException = readCop0Epc();
    exceptionCode = (g_exception_cause >> 2) & 0x1fu;

    while (1) {
        FntPrint(fontId, "COP0 TEST (MFC0/MTC0/RFE)\n\n");

        FntPrint(fontId, "Initial Status: 0x%08lx\n", (unsigned long)statusInitial);
        FntPrint(fontId, "Initial Cause : 0x%08lx\n", (unsigned long)causeInitial);
        FntPrint(fontId, "Initial EPC   : 0x%08lx\n\n", (unsigned long)epcInitial);

        FntPrint(fontId, "Status after IEc set : 0x%08lx\n\n", (unsigned long)statusAfterEnable);

        FntPrint(fontId, "Handler count  : %lu\n", (unsigned long)g_exception_count);
        FntPrint(fontId, "Handler Cause  : 0x%08lx\n", (unsigned long)g_exception_cause);
        FntPrint(fontId, "Handler EPC    : 0x%08lx\n", (unsigned long)g_exception_epc);
        FntPrint(fontId, "Handler ExcCode: %lu\n\n", (unsigned long)exceptionCode);

        FntPrint(fontId, "Post Status    : 0x%08lx\n", (unsigned long)statusAfterException);
        FntPrint(fontId, "Post Cause     : 0x%08lx\n", (unsigned long)causeAfterException);
        FntPrint(fontId, "Post EPC       : 0x%08lx\n\n", (unsigned long)epcAfterException);

        if ((g_exception_count > 0u) && (exceptionCode == 8u)) {
            FntPrint(fontId, "OK after exception\n");
        } else {
            FntPrint(fontId, "FAILED: exception path not recovered\n");
        }

        FntFlush(fontId);

        DrawSync(0);
        VSync(0);

        PutDispEnv(&ctx.disp[ctx.activeBuffer]);
        ctx.activeBuffer = !ctx.activeBuffer;
        PutDrawEnv(&ctx.draw[ctx.activeBuffer]);
    }

    return 0;
}
