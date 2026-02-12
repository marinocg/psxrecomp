#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#include <psxgpu.h>

#define OT_LENGTH 16
#define BUFFER_LENGTH 8192
#define SCREEN_XRES 320
#define SCREEN_YRES 240

typedef struct {
    DISPENV disp_env;
    DRAWENV draw_env;

    uint32_t ot[OT_LENGTH];
    uint8_t buffer[BUFFER_LENGTH];
} RenderBuffer;

typedef struct {
    RenderBuffer buffers[2];
    uint8_t* next_packet;
    int active_buffer;
} RenderContext;

static void setup_context(RenderContext* ctx, int w, int h, int r, int g, int b) {
    SetDefDrawEnv(&(ctx->buffers[0].draw_env), 0, 0, w, h);
    SetDefDispEnv(&(ctx->buffers[0].disp_env), 0, 0, w, h);
    SetDefDrawEnv(&(ctx->buffers[1].draw_env), 0, h, w, h);
    SetDefDispEnv(&(ctx->buffers[1].disp_env), 0, h, w, h);

    setRGB0(&(ctx->buffers[0].draw_env), r, g, b);
    setRGB0(&(ctx->buffers[1].draw_env), r, g, b);
    ctx->buffers[0].draw_env.isbg = 1;
    ctx->buffers[1].draw_env.isbg = 1;

    ctx->active_buffer = 0;
    ctx->next_packet = ctx->buffers[0].buffer;
    ClearOTagR(ctx->buffers[0].ot, OT_LENGTH);

    SetDispMask(1);
}

static void flip_buffers(RenderContext* ctx) {
    DrawSync(0);
    VSync(0);

    RenderBuffer* draw_buffer = &(ctx->buffers[ctx->active_buffer]);
    RenderBuffer* disp_buffer = &(ctx->buffers[ctx->active_buffer ^ 1]);

    PutDispEnv(&(disp_buffer->disp_env));
    DrawOTagEnv(&(draw_buffer->ot[OT_LENGTH - 1]), &(draw_buffer->draw_env));

    ctx->active_buffer ^= 1;
    ctx->next_packet = disp_buffer->buffer;
    ClearOTagR(disp_buffer->ot, OT_LENGTH);
}

static void* new_primitive(RenderContext* ctx, int z, size_t size) {
    RenderBuffer* buffer = &(ctx->buffers[ctx->active_buffer]);
    uint8_t* prim = ctx->next_packet;

    addPrim(&(buffer->ot[z]), prim);
    ctx->next_packet += size;

    assert(ctx->next_packet <= &(buffer->buffer[BUFFER_LENGTH]));

    return (void*)prim;
}

static void draw_text(RenderContext* ctx, int x, int y, int z, const char* text) {
    RenderBuffer* buffer = &(ctx->buffers[ctx->active_buffer]);

    ctx->next_packet = (uint8_t*)FntSort(&(buffer->ot[z]), ctx->next_packet, x, y, text);

    assert(ctx->next_packet <= &(buffer->buffer[BUFFER_LENGTH]));
}

int main(void) {
    ResetGraph(0);
    FntLoad(960, 0);

    RenderContext ctx;
    setup_context(&ctx, SCREEN_XRES, SCREEN_YRES, 63, 0, 127);

    int x = 0;
    int y = 0;
    int dx = 1;
    int dy = 1;

    for (;;) {
        if (x < 0 || x > (SCREEN_XRES - 64)) {
            dx = -dx;
        }
        if (y < 0 || y > (SCREEN_YRES - 64)) {
            dy = -dy;
        }

        x += dx;
        y += dy;

        TILE* tile = (TILE*)new_primitive(&ctx, 1, sizeof(TILE));
        setTile(tile);
        setXY0(tile, x, y);
        setWH(tile, 64, 64);
        setRGB0(tile, 255, 255, 0);

        draw_text(&ctx, 8, 16, 0, "Hello world!");
        flip_buffers(&ctx);
    }

    return 0;
}
