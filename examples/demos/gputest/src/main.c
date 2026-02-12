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
    RECT screenRect = {0, 0, 320, 240};
    RECT boxRect = {0, 0, 48, 48};
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
    FntOpen(16, 16, 320, 208, 0, 512);

    while (1) {
        int x = frame % 256;
        int y = (frame / 2) % 192;
        u_char r = (u_char)(frame & 0xFF);
        u_char g = (u_char)((frame * 2) & 0xFF);
        u_char b = (u_char)((frame * 3) & 0xFF);

        boxRect.x = (short)x;
        boxRect.y = (short)y;

        ClearImage(&screenRect, 0, 0, 40);
        ClearImage(&boxRect, r, g, b);

        FntPrint("PSn00bSDK GPU test\n");
        FntPrint("Frame: %d\n", frame);
        FntPrint("Square: (%d, %d)\n", x, y);
        FntPrint("Color: (%d, %d, %d)\n", r, g, b);
        FntFlush(-1);

        DrawSync(0);
        VSync(0);

        ctx.activeBuffer = !ctx.activeBuffer;
        PutDispEnv(&ctx.disp[ctx.activeBuffer]);
        PutDrawEnv(&ctx.draw[ctx.activeBuffer]);

        ++frame;
    }

    return 0;
}
