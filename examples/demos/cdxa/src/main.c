#include <stdint.h>

#include <psxetc.h>
#include <psxgpu.h>
#include <psxcd.h>

#define SCREEN_XRES 320
#define SCREEN_YRES 240
#define CHUNK_SECTORS 16
#define CHUNK_WORDS (CHUNK_SECTORS * 512)
#define READ_TIMEOUT_FRAMES 180
#define PASS_MIN_LOOPS 3

typedef struct {
    DISPENV disp[2];
    DRAWENV draw[2];
    int activeBuffer;
} GpuContext;

typedef struct {
    int startLba;
    int totalSectors;
    int currentSector;

    int inFlight;
    int sectorsInFlight;
    int waitFrames;
    int maxWaitFrames;

    int completedReads;
    int loops;
    int readErrors;
    int timeoutErrors;

    uint32_t bytesRead;
    uint32_t rollingCrc;
} StressState;

static uint32_t sReadBuffer[CHUNK_WORDS];

static uint32_t crc32Update(uint32_t crc, const uint8_t* data, int length) {
    uint32_t value = crc ^ 0xffffffffu;

    for (int i = 0; i < length; ++i) {
        value ^= data[i];

        for (int bit = 0; bit < 8; ++bit) {
            if (value & 1u) {
                value = (value >> 1) ^ 0xedb88320u;
            } else {
                value >>= 1;
            }
        }
    }

    return value ^ 0xffffffffu;
}

static void initGpu(GpuContext* ctx) {
    ResetGraph(0);

    SetDefDispEnv(&ctx->disp[0], 0, 0, SCREEN_XRES, SCREEN_YRES);
    SetDefDispEnv(&ctx->disp[1], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&ctx->draw[0], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&ctx->draw[1], 0, 0, SCREEN_XRES, SCREEN_YRES);

    ctx->draw[0].isbg = 1;
    ctx->draw[1].isbg = 1;
    setRGB0(&ctx->draw[0], 14, 18, 36);
    setRGB0(&ctx->draw[1], 14, 18, 36);

    ctx->activeBuffer = 0;
    PutDispEnv(&ctx->disp[ctx->activeBuffer]);
    PutDrawEnv(&ctx->draw[ctx->activeBuffer]);
    SetDispMask(1);
}

static void flipBuffers(GpuContext* ctx) {
    DrawSync(0);
    VSync(0);

    PutDispEnv(&ctx->disp[ctx->activeBuffer]);
    PutDrawEnv(&ctx->draw[ctx->activeBuffer]);
    SetDispMask(1);
    ctx->activeBuffer = !ctx->activeBuffer;
}

static int startRead(StressState* state) {
    CdlLOC pos;
    int remaining;
    int sectors;

    if (state->totalSectors <= 0) {
        state->readErrors++;
        return 0;
    }

    remaining = state->totalSectors - state->currentSector;
    if (remaining <= 0) {
        state->currentSector = 0;
        remaining = state->totalSectors;
    }

    sectors = (remaining < CHUNK_SECTORS) ? remaining : CHUNK_SECTORS;

    CdIntToPos(state->startLba + state->currentSector, &pos);

    if (!CdControl(CdlSetloc, &pos, 0)) {
        state->readErrors++;
        return 0;
    }

    if (!CdRead(sectors, sReadBuffer, CdlModeSpeed)) {
        state->readErrors++;
        return 0;
    }

    state->inFlight = 1;
    state->sectorsInFlight = sectors;
    state->waitFrames = 0;
    return 1;
}

int main(void) {
    static GpuContext ctx;
    static StressState state;

    int fontId;
    int cdReady;
    int haveFile = 0;

    CdlFILE file;

    initGpu(&ctx);

    FntLoad(960, 0);
    fontId = FntOpen(8, 16, SCREEN_XRES - 16, SCREEN_YRES - 24, 0, 768);

    cdReady = CdInit();

    if (cdReady && CdSearchFile(&file, "\\STRESS.XA")) {
        haveFile = 1;
        state.startLba = CdPosToInt(&file.pos);
        state.totalSectors = (file.size + 2047) / 2048;
    }

    while (1) {
        if (cdReady && haveFile) {
            if (!state.inFlight) {
                startRead(&state);
            } else {
                uint8_t status = 0;
                int pending = CdReadSync(1, &status);

                if (pending > 0) {
                    state.waitFrames++;

                    if (state.waitFrames > state.maxWaitFrames) {
                        state.maxWaitFrames = state.waitFrames;
                    }

                    if (state.waitFrames > READ_TIMEOUT_FRAMES) {
                        state.timeoutErrors++;
                        CdReadBreak();
                        CdReadSync(0, 0);

                        state.inFlight = 0;
                        state.currentSector = 0;
                    }
                } else if (pending == 0) {
                    const int bytesThisRead = state.sectorsInFlight * 2048;

                    state.completedReads++;
                    state.bytesRead += (uint32_t)bytesThisRead;
                    state.rollingCrc = crc32Update(state.rollingCrc, (const uint8_t*)sReadBuffer, bytesThisRead);

                    state.currentSector += state.sectorsInFlight;
                    if (state.currentSector >= state.totalSectors) {
                        state.currentSector = 0;
                        state.loops++;
                    }

                    state.inFlight = 0;
                } else {
                    state.readErrors++;
                    state.inFlight = 0;
                    state.currentSector = 0;
                }
            }
        }

        FntPrint(fontId, "CDROM STRESS: CDXA\n\n");
        FntPrint(fontId, "CdInit: %s\n", cdReady ? "OK" : "FAILED");
        FntPrint(fontId, "STRESS.XA: %s\n", haveFile ? "FOUND" : "MISSING");
        FntPrint(fontId, "Total sectors: %d\n", state.totalSectors);
        FntPrint(fontId, "Cursor sector: %d\n", state.currentSector);
        FntPrint(fontId, "Read state: %s\n", state.inFlight ? "READING" : "IDLE");
        FntPrint(fontId, "Wait frames (max): %d (%d)\n", state.waitFrames, state.maxWaitFrames);
        FntPrint(fontId, "Completed reads: %d\n", state.completedReads);
        FntPrint(fontId, "EOF loops: %d\n", state.loops);
        FntPrint(fontId, "Bytes read: %lu\n", (unsigned long)state.bytesRead);
        FntPrint(fontId, "Rolling CRC32: 0x%08lx\n", (unsigned long)state.rollingCrc);
        FntPrint(fontId, "Read errors: %d\n", state.readErrors);
        FntPrint(fontId, "Timeout errors: %d\n\n", state.timeoutErrors);

        if (!cdReady || !haveFile) {
            FntPrint(fontId, "RESULT: FAIL (startup)\n");
        } else if ((state.readErrors > 0) || (state.timeoutErrors > 0)) {
            FntPrint(fontId, "RESULT: FAIL (deadlock/read error)\n");
        } else if (state.loops >= PASS_MIN_LOOPS) {
            FntPrint(fontId, "RESULT: PASS (sustained loop stable)\n");
        } else {
            FntPrint(fontId, "RESULT: WARMING UP...\n");
        }

        FntFlush(fontId);
        flipBuffers(&ctx);
    }

    return 0;
}
