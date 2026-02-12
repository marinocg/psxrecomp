#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>

typedef struct {
    DISPENV disp[2];
    DRAWENV draw[2];
    int activeBuffer;
} GpuContext;

int main(void) {
    GpuContext ctx;
    int frame = 0;

    ResetGraph(0);
    SetDefDispEnv(&ctx.disp[0], 0, 0, 320, 240);
    SetDefDispEnv(&ctx.disp[1], 0, 240, 320, 240);
    SetDefDrawEnv(&ctx.draw[0], 0, 240, 320, 240);
    SetDefDrawEnv(&ctx.draw[1], 0, 0, 320, 240);

    ctx.draw[0].isbg = 1;
    ctx.draw[1].isbg = 1;
    ctx.draw[0].r0 = 0;
    ctx.draw[0].g0 = 0;
    ctx.draw[0].b0 = 40;
    ctx.draw[1].r0 = 0;
    ctx.draw[1].g0 = 0;
    ctx.draw[1].b0 = 40;

    ctx.activeBuffer = 0;
    PutDispEnv(&ctx.disp[ctx.activeBuffer]);
    PutDrawEnv(&ctx.draw[ctx.activeBuffer]);

    FntLoad(960, 0);
    int fontId = FntOpen(16, 16, 320, 208, 0, 512);

    while (1) {
        int x = frame % 256;
        int y = (frame / 2) % 192;
        uint8_t r = (uint8_t)(frame & 0xFF);
        uint8_t g = (uint8_t)((frame * 2) & 0xFF);
        uint8_t b = (uint8_t)((frame * 3) & 0xFF);

        ctx.draw[ctx.activeBuffer].r0 = r;
        ctx.draw[ctx.activeBuffer].g0 = g;
        ctx.draw[ctx.activeBuffer].b0 = b;
        PutDrawEnv(&ctx.draw[ctx.activeBuffer]);

        FntPrint(fontId, "PSn00bSDK GPU test\n");
        FntPrint(fontId, "Frame: %d\n", frame);
        FntPrint(fontId, "Square: (%d, %d)\n", x, y);
        FntPrint(fontId, "Color: (%d, %d, %d)\n", r, g, b);
        FntFlush(fontId);

        DrawSync(0);
        VSync(0);

        ctx.activeBuffer = !ctx.activeBuffer;
        PutDispEnv(&ctx.disp[ctx.activeBuffer]);
        PutDrawEnv(&ctx.draw[ctx.activeBuffer]);

        ++frame;
    }

    return 0;
}
