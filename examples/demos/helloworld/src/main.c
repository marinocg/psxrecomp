#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>

typedef struct {
    DISPENV disp[2];
    DRAWENV draw[2];
    int activeBuffer;
} GpuContext;

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

    initGpu(&ctx);

    FntLoad(960, 0);
    int fontId = FntOpen(32, 24, 320, 32, 0, 256);

    while (1) {
        FntPrint(fontId, "Hello world from PSn00bSDK!\n");
        FntPrint(fontId, "Generated with psxrecomp demo project.\n");
        FntFlush(fontId);

        DrawSync(0);
        VSync(0);

        ctx.activeBuffer = !ctx.activeBuffer;
        PutDispEnv(&ctx.disp[ctx.activeBuffer]);
        PutDrawEnv(&ctx.draw[ctx.activeBuffer]);
    }

    return 0;
}
