#include <stdint.h>

#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>

#define BUFFER_WORDS 1024
#define PATTERN_SEED 0x13579BDFu

typedef struct {
    DISPENV disp[2];
    DRAWENV draw[2];
    int activeBuffer;
} GpuContext;

static uint32_t testBuffer[BUFFER_WORDS];

static uint32_t patternForIndex(int index) {
    return PATTERN_SEED ^ (uint32_t)index * 0x45D9F3Bu;
}

static void initGpu(GpuContext* ctx) {
    ResetGraph(0);

    SetDefDispEnv(&ctx->disp[0], 0, 0, 320, 240);
    SetDefDispEnv(&ctx->disp[1], 0, 240, 320, 240);
    SetDefDrawEnv(&ctx->draw[0], 0, 240, 320, 240);
    SetDefDrawEnv(&ctx->draw[1], 0, 0, 320, 240);

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
    GpuContext ctx;
    int errors = 0;

    initGpu(&ctx);

    FntLoad(960, 0);
    int fontId = FntOpen(16, 16, 320, 208, 0, 512);

    for (int i = 0; i < BUFFER_WORDS; ++i) {
        testBuffer[i] = patternForIndex(i);
    }

    for (int i = 0; i < BUFFER_WORDS; ++i) {
        if (testBuffer[i] != patternForIndex(i)) {
            ++errors;
        }
    }

    while (1) {
        FntPrint(fontId, "PSn00bSDK memory test\n");
        FntPrint(fontId, "Buffer words: %d\n", BUFFER_WORDS);
        FntPrint(fontId, "Errors: %d\n", errors);

        if (errors == 0) {
            FntPrint(fontId, "Result: PASS\n");
        } else {
            FntPrint(fontId, "Result: FAIL\n");
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
