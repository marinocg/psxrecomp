#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <psxetc.h>
#include <psxgpu.h>

#define SCREEN_XRES 320
#define SCREEN_YRES 240
#define OT_LENGTH 8
#define BUFFER_LENGTH 4096

typedef struct {
    DISPENV dispEnv;
    DRAWENV drawEnv;
    uint32_t ot[OT_LENGTH];
    uint8_t primBuffer[BUFFER_LENGTH];
} RenderBuffer;

typedef struct {
    RenderBuffer buffers[2];
    uint8_t* nextPacket;
    int activeBuffer;
} RenderContext;

static void setupContext(RenderContext* ctx) {
    SetDefDrawEnv(&ctx->buffers[0].drawEnv, 0, 0, SCREEN_XRES, SCREEN_YRES);
    SetDefDispEnv(&ctx->buffers[0].dispEnv, 0, 0, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&ctx->buffers[1].drawEnv, 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
    SetDefDispEnv(&ctx->buffers[1].dispEnv, 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);

    setRGB0(&ctx->buffers[0].drawEnv, 0, 0, 40);
    setRGB0(&ctx->buffers[1].drawEnv, 0, 0, 40);
    ctx->buffers[0].drawEnv.isbg = 1;
    ctx->buffers[1].drawEnv.isbg = 1;

    ctx->activeBuffer = 0;
    ctx->nextPacket = ctx->buffers[0].primBuffer;
    ClearOTagR(ctx->buffers[0].ot, OT_LENGTH);
    ClearOTagR(ctx->buffers[1].ot, OT_LENGTH);

    PutDispEnv(&ctx->buffers[0].dispEnv);
    PutDrawEnv(&ctx->buffers[0].drawEnv);
    SetDispMask(1);
}

static void drawText(RenderContext* ctx, const char* text) {
    RenderBuffer* buffer = &ctx->buffers[ctx->activeBuffer];

    ctx->nextPacket = (uint8_t*)FntSort(&buffer->ot[0], ctx->nextPacket, 8, 16, text);
    assert(ctx->nextPacket <= &buffer->primBuffer[BUFFER_LENGTH]);
}

static void flipBuffers(RenderContext* ctx) {
    DrawSync(0);
    VSync(0);

    RenderBuffer* buffer = &ctx->buffers[ctx->activeBuffer];

    PutDispEnv(&buffer->dispEnv);
    DrawOTagEnv(&buffer->ot[OT_LENGTH - 1], &buffer->drawEnv);

    ctx->activeBuffer ^= 1;
    RenderBuffer* next = &ctx->buffers[ctx->activeBuffer];
    ctx->nextPacket = next->primBuffer;
    ClearOTagR(next->ot, OT_LENGTH);
}

int main(void) {
    static RenderContext ctx;

    ResetGraph(0);
    FntLoad(960, 0);
    FntOpen(0, 0, SCREEN_XRES, SCREEN_YRES, 0, 512);
    setupContext(&ctx);

    for (;;) {
        drawText(&ctx, "Hello world from PSn00bSDK!");
        flipBuffers(&ctx);
    }

    return 0;
}
