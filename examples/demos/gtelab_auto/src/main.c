#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <inline_c.h>
#include <psxetc.h>
#include <psxgpu.h>
#include <psxgte.h>

#define SCREEN_XRES 320
#define SCREEN_YRES 240
#define CENTER_X (SCREEN_XRES / 2)
#define CENTER_Y (SCREEN_YRES / 2)

#define OT_LENGTH 128
#define PACKET_BUFFER_LENGTH 8192

typedef struct
{
    DISPENV disp;
    DRAWENV draw;
    uint32_t ot[OT_LENGTH];
    uint8_t packetBuffer[PACKET_BUFFER_LENGTH];
} RenderBuffer;

typedef struct
{
    RenderBuffer buffers[2];
    int activeBuffer;
    uint8_t* nextPacket;
} RenderContext;

typedef struct
{
    uint8_t v0;
    uint8_t v1;
    uint8_t v2;
} Face;

static MATRIX g_colorMatrix = {
    {
        {ONE * 3 / 4, 0, 0},
        {ONE * 3 / 4, 0, 0},
        {ONE * 3 / 4, 0, 0},
    },
    {0, 0, 0},
};

static MATRIX g_baseLightMatrix = {
    {
        {-2310, -2310, -2310},
        {0, 0, 0},
        {0, 0, 0},
    },
    {0, 0, 0},
};

static const VECTOR kTranslation = {0, 0, 520};

static const SVECTOR kProbeVector = {ONE / 4, ONE / 8, ONE / 2, 0};
static CVECTOR g_probeColor = {180, 140, 110, 0x20};

static const SVECTOR kVertices[] = {
    {0, -88, 0, 0},
    {-68, 56, -72, 0},
    {68, 56, -72, 0},
    {0, 56, 72, 0},
};

static const SVECTOR kFaceNormals[] = {
    {0, -2896, -2896, 0},
    {-2896, -2896, 0, 0},
    {2896, -2896, 0, 0},
    {0, -1024, 4096, 0},
};

static const Face kFaces[] = {
    {0, 1, 2},
    {0, 3, 1},
    {0, 2, 3},
    {1, 3, 2},
};

static uint32_t g_frameCounter;
static uint32_t g_mtc2Mfc2RoundTrip;
static uint32_t g_ctc2Cfc2RoundTrip;
static uint32_t g_lwc2Swc2RoundTrip;
static int32_t g_mvmvaIr1;
static int32_t g_mvmvaIr2;
static int32_t g_mvmvaIr3;
static int32_t g_lastNclip;
static uint32_t g_lastAvsz4;

static inline void writeCop2DataReg9(uint32_t value)
{
    __asm__ volatile("mtc2 %0, $9\n"
                     "nop\n"
                     "nop\n"
                     :
                     : "r"(value)
                     : "memory");
}

static inline uint32_t readCop2DataReg9(void)
{
    uint32_t value;
    __asm__ volatile("mfc2 %0, $9\n"
                     "nop\n"
                     "nop\n"
                     : "=r"(value)
                     :
                     : "memory");
    return value;
}

static inline void writeCop2ControlReg26(uint32_t value)
{
    __asm__ volatile("ctc2 %0, $26\n"
                     "nop\n"
                     "nop\n"
                     :
                     : "r"(value)
                     : "memory");
}

static inline uint32_t readCop2ControlReg26(void)
{
    uint32_t value;
    __asm__ volatile("cfc2 %0, $26\n"
                     "nop\n"
                     "nop\n"
                     : "=r"(value)
                     :
                     : "memory");
    return value;
}

static inline void loadCop2DataReg0(const uint32_t* source)
{
    __asm__ volatile("lwc2 $0, 0(%0)" : : "r"(source) : "memory");
}

static inline void storeCop2DataReg0(uint32_t* destination)
{
    __asm__ volatile("swc2 $0, 0(%0)" : : "r"(destination) : "memory");
}

static inline void writeCop2ControlReg29(uint32_t value)
{
    __asm__ volatile("ctc2 %0, $29" : : "r"(value) : "memory");
}

static inline void writeCop2ControlReg30(uint32_t value)
{
    __asm__ volatile("ctc2 %0, $30" : : "r"(value) : "memory");
}

static void* allocatePrimitive(RenderContext* ctx, int depth, size_t primitiveSize)
{
    RenderBuffer* buffer = &ctx->buffers[ctx->activeBuffer];
    uint8_t* end = &buffer->packetBuffer[PACKET_BUFFER_LENGTH];

    if (depth < 0)
    {
        depth = 0;
    }
    else if (depth >= OT_LENGTH)
    {
        depth = OT_LENGTH - 1;
    }

    if ((size_t)(end - ctx->nextPacket) < primitiveSize)
    {
        return NULL;
    }

    void* primitive = ctx->nextPacket;
    addPrim(&buffer->ot[depth], primitive);
    ctx->nextPacket += primitiveSize;

    return primitive;
}

static void initRenderer(RenderContext* ctx)
{
    ResetGraph(0);

    SetDefDispEnv(&ctx->buffers[0].disp, 0, 0, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&ctx->buffers[0].draw, 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);

    SetDefDispEnv(&ctx->buffers[1].disp, 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&ctx->buffers[1].draw, 0, 0, SCREEN_XRES, SCREEN_YRES);

    setRGB0(&ctx->buffers[0].draw, 8, 8, 26);
    setRGB0(&ctx->buffers[1].draw, 8, 8, 26);
    ctx->buffers[0].draw.isbg = 1;
    ctx->buffers[1].draw.isbg = 1;

    ClearOTagR(ctx->buffers[0].ot, OT_LENGTH);
    ClearOTagR(ctx->buffers[1].ot, OT_LENGTH);

    ctx->activeBuffer = 0;
    ctx->nextPacket = ctx->buffers[0].packetBuffer;

    PutDispEnv(&ctx->buffers[0].disp);
    PutDrawEnv(&ctx->buffers[0].draw);
    SetDispMask(1);

    FntLoad(960, 0);
}

static void flipBuffers(RenderContext* ctx)
{
    RenderBuffer* current = &ctx->buffers[ctx->activeBuffer];

    DrawSync(0);
    VSync(0);

    PutDispEnv(&current->disp);
    DrawOTagEnv(&current->ot[OT_LENGTH - 1], &current->draw);

    ctx->activeBuffer ^= 1;
    RenderBuffer* next = &ctx->buffers[ctx->activeBuffer];

    ctx->nextPacket = next->packetBuffer;
    ClearOTagR(next->ot, OT_LENGTH);
}

static void runCop2TransferProbes(void)
{
    const uint32_t sourceWord = 0x0030FFD0u;
    uint32_t destinationWord = 0u;

    loadCop2DataReg0(&sourceWord);
    __asm__ volatile("nop\n"
                     "nop\n" ::
                         : "memory");
    storeCop2DataReg0(&destinationWord);

    g_lwc2Swc2RoundTrip = destinationWord;

    writeCop2DataReg9(0x00000123u + (g_frameCounter & 0x1Fu));
    g_mtc2Mfc2RoundTrip = readCop2DataReg9();

    writeCop2ControlReg26(CENTER_X);
    g_ctc2Cfc2RoundTrip = readCop2ControlReg26();
}

static void runMvmvaProbe(void)
{
    SVECTOR transformed = {0, 0, 0, 0};

    gte_ldv0(&kProbeVector);
    gte_mvmva(1, 0, 0, 3, 0);
    gte_stsv(&transformed);

    g_mvmvaIr1 = transformed.vx;
    g_mvmvaIr2 = transformed.vy;
    g_mvmvaIr3 = transformed.vz;
}

static void runAvsz4Probe(void)
{
    const uint32_t z0 = 0x0180u + ((g_frameCounter & 0x0Fu) << 1);
    const uint32_t z1 = 0x01C0u + ((g_frameCounter & 0x0Fu) << 1);
    const uint32_t z2 = 0x0200u + ((g_frameCounter & 0x0Fu) << 1);
    const uint32_t z3 = 0x0240u + ((g_frameCounter & 0x0Fu) << 1);

    gte_ldsz4(z0, z1, z2, z3);
    gte_avsz4();
    gte_stotz(&g_lastAvsz4);
}

static void drawHud(RenderContext* ctx)
{
    RenderBuffer* buffer = &ctx->buffers[ctx->activeBuffer];
    uint8_t* end = &buffer->packetBuffer[PACKET_BUFFER_LENGTH];
    char text[640];

    const int written = snprintf(
        text, sizeof(text),
        "GTELAB_AUTO (no controller)\n"
        "Frame: %lu\n"
        "MTC2/MFC2 IR1: 0x%08lx\n"
        "CTC2/CFC2 H:   0x%08lx\n"
        "LWC2/SWC2 VXY0: 0x%08lx\n"
        "MVMVA IR: (%ld, %ld, %ld)\n"
        "NCLIP: %ld  AVSZ4 OTZ: %lu\n"
        "Ops: RTPS RTPT NCLIP AVSZ3 AVSZ4 MVMVA NCDS\n",
        (unsigned long)g_frameCounter, (unsigned long)g_mtc2Mfc2RoundTrip,
        (unsigned long)g_ctc2Cfc2RoundTrip, (unsigned long)g_lwc2Swc2RoundTrip, (long)g_mvmvaIr1,
        (long)g_mvmvaIr2, (long)g_mvmvaIr3, (long)g_lastNclip, (unsigned long)g_lastAvsz4);

    if (written <= 0)
    {
        return;
    }

    uint8_t* nextPacket = (uint8_t*)FntSort(&buffer->ot[0], ctx->nextPacket, 8, 8, text);
    if ((nextPacket != NULL) && (nextPacket <= end))
    {
        ctx->nextPacket = nextPacket;
    }
}

int main(void)
{
    static RenderContext ctx;

    SVECTOR rotation = {0, 0, 0, 0};
    InitGeom();
    initRenderer(&ctx);
    FntOpen(8, 8, SCREEN_XRES - 16, SCREEN_YRES - 16, 0, 512);

    gte_SetGeomOffset(CENTER_X, CENTER_Y);
    gte_SetGeomScreen(CENTER_X);

    gte_SetBackColor(24, 24, 24);
    gte_SetColorMatrix(&g_colorMatrix);
    writeCop2ControlReg29(ONE / 3);
    writeCop2ControlReg30(ONE / 4);

    for (;;)
    {
        MATRIX modelMatrix;
        MATRIX frameLightMatrix;
        uint32_t projectedPoint;

        RotMatrix(&rotation, &modelMatrix);
        TransMatrix(&modelMatrix, (VECTOR*)&kTranslation);

        MulMatrix0(&g_baseLightMatrix, &modelMatrix, &frameLightMatrix);

        gte_SetRotMatrix(&modelMatrix);
        gte_SetTransMatrix(&modelMatrix);
        gte_SetLightMatrix(&frameLightMatrix);

        runCop2TransferProbes();
        runMvmvaProbe();
        runAvsz4Probe();

        for (size_t i = 0; i < (sizeof(kFaces) / sizeof(kFaces[0])); ++i)
        {
            const Face* face = &kFaces[i];
            int32_t nclip = 0;
            int32_t otz = 0;
            CVECTOR litColor = {0, 0, 0, 0};

            gte_ldv3(&kVertices[face->v0], &kVertices[face->v1], &kVertices[face->v2]);
            gte_rtpt();

            gte_nclip();
            gte_stopz(&nclip);
            g_lastNclip = nclip;
            if (nclip <= 0)
            {
                continue;
            }

            gte_avsz3();
            gte_stotz(&otz);

            int depth = otz >> 2;
            if (depth < 1)
            {
                depth = 1;
            }
            if (depth >= OT_LENGTH)
            {
                depth = OT_LENGTH - 1;
            }

            POLY_F3* tri = (POLY_F3*)allocatePrimitive(&ctx, depth, sizeof(POLY_F3));
            if (tri == NULL)
            {
                break;
            }

            setPolyF3(tri);
            gte_stsxy0(&tri->x0);
            gte_stsxy1(&tri->x1);
            gte_stsxy2(&tri->x2);

            gte_ldrgb(&g_probeColor);
            gte_ldv0(&kFaceNormals[i]);
            gte_ncds();
            gte_strgb(&litColor);

            setRGB0(tri, litColor.r, litColor.g, litColor.b);
        }

        gte_ldv0(&kVertices[0]);
        gte_rtps();
        gte_stsxy(&projectedPoint);

        int16_t markerX = (int16_t)(projectedPoint & 0xFFFFu);
        int16_t markerY = (int16_t)((projectedPoint >> 16) & 0xFFFFu);

        TILE* marker = (TILE*)allocatePrimitive(&ctx, 0, sizeof(TILE));
        if (marker != NULL)
        {
            setTile(marker);
            setXY0(marker, markerX - 2, markerY - 2);
            setWH(marker, 4, 4);
            setRGB0(marker, 255, 220, 64);
        }

        drawHud(&ctx);

        flipBuffers(&ctx);

        rotation.vx += 18;
        rotation.vy += 26;
        rotation.vz += 14;
        ++g_frameCounter;
    }

    return 0;
}
