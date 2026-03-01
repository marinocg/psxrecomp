#include <stdint.h>

#include <psxetc.h>
#include <psxgpu.h>
#include <psxcd.h>

#define SCREEN_XRES 320
#define SCREEN_YRES 240
#define CRC_SECTORS 16
#define CRC_WORDS (CRC_SECTORS * 512)
#define READ_INTERVAL_FRAMES 60
#define READ_TIMEOUT_FRAMES 300
#define EXPECTED_CRC32 0xf105fc36u

typedef struct {
    DISPENV disp[2];
    DRAWENV draw[2];
    int activeBuffer;
} GpuContext;

static uint32_t sBuffer[CRC_WORDS];

static uint32_t crc32Compute(const uint8_t* data, int length) {
    uint32_t crc = 0xffffffffu;

    for (int i = 0; i < length; ++i) {
        crc ^= data[i];

        for (int bit = 0; bit < 8; ++bit) {
            if (crc & 1u) {
                crc = (crc >> 1) ^ 0xedb88320u;
            } else {
                crc >>= 1;
            }
        }
    }

    return crc ^ 0xffffffffu;
}

static void initGpu(GpuContext* ctx) {
    ResetGraph(0);

    SetDefDispEnv(&ctx->disp[0], 0, 0, SCREEN_XRES, SCREEN_YRES);
    SetDefDispEnv(&ctx->disp[1], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&ctx->draw[0], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&ctx->draw[1], 0, 0, SCREEN_XRES, SCREEN_YRES);

    ctx->draw[0].isbg = 1;
    ctx->draw[1].isbg = 1;

    setRGB0(&ctx->draw[0], 10, 18, 10);
    setRGB0(&ctx->draw[1], 10, 18, 10);

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

static int startCrcRead(int startLba) {
    CdlLOC pos;

    CdIntToPos(startLba, &pos);

    if (!CdControl(CdlSetloc, &pos, 0)) {
        return -1;
    }

    if (!CdRead(CRC_SECTORS, sBuffer, CdlModeSpeed)) {
        return -2;
    }

    return 0;
}

int main(void) {
    static GpuContext ctx;

    int fontId;
    int cdReady;
    int haveFile = 0;

    int startLba = 0;
    int frameCounter = 0;
    int iterations = 0;
    int mismatches = 0;
    int readErrors = 0;
    int readInFlight = 0;
    int readWaitFrames = 0;

    uint32_t lastCrc = 0;

    CdlFILE file;

    initGpu(&ctx);

    FntLoad(960, 0);
    fontId = FntOpen(8, 16, SCREEN_XRES - 16, SCREEN_YRES - 24, 0, 768);

    cdReady = CdInit();

    if (cdReady && CdSearchFile(&file, "\\CRCSEED.BIN")) {
        int sectors = (file.size + 2047) / 2048;

        if (sectors >= CRC_SECTORS) {
            haveFile = 1;
            startLba = CdPosToInt(&file.pos);
        }
    }

    while (1) {
        if (cdReady && haveFile) {
            if (readInFlight) {
                int pending = CdReadSync(1, 0);

                if (pending > 0) {
                    readWaitFrames++;

                    if (readWaitFrames > READ_TIMEOUT_FRAMES) {
                        readErrors++;
                        CdReadBreak();
                        CdReadSync(0, 0);
                        readInFlight = 0;
                        readWaitFrames = 0;
                        frameCounter = 0;
                    }
                } else if (pending < 0) {
                    readErrors++;
                    readInFlight = 0;
                    readWaitFrames = 0;
                    frameCounter = 0;
                } else {
                    uint32_t crcValue = crc32Compute((const uint8_t*)sBuffer, CRC_SECTORS * 2048);
                    iterations++;
                    lastCrc = crcValue;

                    if (crcValue != EXPECTED_CRC32) {
                        mismatches++;
                    }

                    readInFlight = 0;
                    readWaitFrames = 0;
                    frameCounter = 0;
                }
            } else {
                frameCounter++;

                if (frameCounter >= READ_INTERVAL_FRAMES) {
                    if (startCrcRead(startLba) != 0) {
                        readErrors++;
                        frameCounter = 0;
                    } else {
                        readInFlight = 1;
                        readWaitFrames = 0;
                    }
                }
            }
        }

        if ((mismatches > 0) || (readErrors > 0)) {
            setRGB0(&ctx.draw[ctx.activeBuffer], 42, 8, 8);
        } else {
            setRGB0(&ctx.draw[ctx.activeBuffer], 10, 24, 10);
        }
        PutDrawEnv(&ctx.draw[ctx.activeBuffer]);

        FntPrint(fontId, "CDROM DIAGNOSTIC: CDCRC\n\n");
        FntPrint(fontId, "CdInit: %s\n", cdReady ? "OK" : "FAILED");
        FntPrint(fontId, "CRCSEED.BIN: %s\n", haveFile ? "FOUND" : "MISSING");
        FntPrint(fontId, "LBA start: %d\n", startLba);
        FntPrint(fontId, "Sectors read: %d\n\n", CRC_SECTORS);
        FntPrint(fontId, "Read state: %s\n", readInFlight ? "READING" : "IDLE");
        FntPrint(fontId, "Read wait frames: %d\n\n", readWaitFrames);

        FntPrint(fontId, "Expected CRC32: 0x%08lx\n", (unsigned long)EXPECTED_CRC32);
        FntPrint(fontId, "Last CRC32:     0x%08lx\n", (unsigned long)lastCrc);
        FntPrint(fontId, "Iterations: %d\n", iterations);
        FntPrint(fontId, "Mismatches: %d\n", mismatches);
        FntPrint(fontId, "Read errors: %d\n\n", readErrors);

        if (!cdReady || !haveFile) {
            FntPrint(fontId, "STATUS: RED\n");
            FntPrint(fontId, "RESULT: FAIL (startup)\n");
        } else if ((mismatches > 0) || (readErrors > 0)) {
            FntPrint(fontId, "STATUS: RED\n");
            FntPrint(fontId, "RESULT: FAIL (CRC mismatch/drift)\n");
        } else if (iterations >= 3) {
            FntPrint(fontId, "STATUS: GREEN\n");
            FntPrint(fontId, "RESULT: PASS (exact CRC, stable over time)\n");
        } else {
            FntPrint(fontId, "STATUS: GREEN\n");
            FntPrint(fontId, "RESULT: WARMING UP...\n");
        }

        FntFlush(fontId);
        flipBuffers(&ctx);
    }

    return 0;
}
