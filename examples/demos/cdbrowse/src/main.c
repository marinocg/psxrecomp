#include <stddef.h>
#include <stdint.h>

#include <psxetc.h>
#include <psxgpu.h>
#include <psxcd.h>

#define SCREEN_XRES 320
#define SCREEN_YRES 240
#define REFRESH_INTERVAL_FRAMES 60
#define PASS_MIN_REFRESHES 8

#define LOGICAL_SECTOR_SIZE 2048
#define MAX_DIR_SECTORS 8
#define MAX_ENTRIES 32
#define LIST_LINES 12
#define READ_TIMEOUT_FRAMES 240

typedef struct {
    DISPENV disp[2];
    DRAWENV draw[2];
    int activeBuffer;
} GpuContext;

typedef struct {
    char name[17];
    int size;
    int isDirectory;
} DirEntry;

static uint32_t sPvdWords[LOGICAL_SECTOR_SIZE / 4];
static uint32_t sDirWords[(MAX_DIR_SECTORS * LOGICAL_SECTOR_SIZE) / 4];
static DirEntry sEntries[MAX_ENTRIES];

static uint32_t readLe32(const uint8_t* ptr) {
    return (uint32_t)ptr[0] |
           ((uint32_t)ptr[1] << 8) |
           ((uint32_t)ptr[2] << 16) |
           ((uint32_t)ptr[3] << 24);
}

static uint32_t fnv1aUpdate(uint32_t hash, const uint8_t* data, size_t length) {
    const uint32_t prime = 16777619u;

    for (size_t i = 0; i < length; ++i) {
        hash ^= data[i];
        hash *= prime;
    }

    return hash;
}

static int readSectorsLba(int lba, int sectors, uint32_t* destination) {
    CdlLOC pos;

    CdIntToPos(lba, &pos);

    if (!CdControl(CdlSetloc, &pos, 0)) {
        return -1;
    }

    if (!CdRead(sectors, destination, CdlModeSpeed)) {
        return -2;
    }

    for (int frame = 0; frame < READ_TIMEOUT_FRAMES; ++frame) {
        int pending = CdReadSync(1, 0);

        if (pending == 0) {
            return 0;
        }

        if (pending < 0) {
            return -3;
        }

        VSync(0);
    }

    CdReadBreak();
    CdReadSync(0, 0);
    return -4;
}

static int parseDirectoryEntries(const uint8_t* data, int byteCount, DirEntry* entries, int maxEntries, uint32_t* outHash) {
    int offset = 0;
    int count = 0;
    uint32_t hash = 2166136261u;

    while (offset < byteCount) {
        uint8_t length = data[offset];

        if (length == 0) {
            int sectorBoundary = ((offset / LOGICAL_SECTOR_SIZE) + 1) * LOGICAL_SECTOR_SIZE;
            offset = sectorBoundary;
            continue;
        }

        if ((offset + length) > byteCount) {
            break;
        }

        const uint8_t* record = &data[offset];
        uint8_t nameLength = record[32];

        if ((33 + nameLength) > length) {
            offset += length;
            continue;
        }

        if ((nameLength == 1) && ((record[33] == 0) || (record[33] == 1))) {
            offset += length;
            continue;
        }

        if (count < maxEntries) {
            int copyLength = (nameLength < 16) ? nameLength : 16;

            for (int i = 0; i < copyLength; ++i) {
                entries[count].name[i] = (char)record[33 + i];
            }
            entries[count].name[copyLength] = '\0';

            entries[count].size = (int)readLe32(&record[10]);
            entries[count].isDirectory = (record[25] & 0x02) ? 1 : 0;

            hash = fnv1aUpdate(hash, (const uint8_t*)entries[count].name, sizeof(entries[count].name));
            hash = fnv1aUpdate(hash, (const uint8_t*)&entries[count].size, sizeof(entries[count].size));
            hash = fnv1aUpdate(hash, (const uint8_t*)&entries[count].isDirectory, sizeof(entries[count].isDirectory));

            count++;
        }

        offset += length;
    }

    *outHash = hash;
    return count;
}

static void initGpu(GpuContext* ctx) {
    ResetGraph(0);

    SetDefDispEnv(&ctx->disp[0], 0, 0, SCREEN_XRES, SCREEN_YRES);
    SetDefDispEnv(&ctx->disp[1], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&ctx->draw[0], 0, SCREEN_YRES, SCREEN_XRES, SCREEN_YRES);
    SetDefDrawEnv(&ctx->draw[1], 0, 0, SCREEN_XRES, SCREEN_YRES);

    ctx->draw[0].isbg = 1;
    ctx->draw[1].isbg = 1;

    setRGB0(&ctx->draw[0], 0, 20, 40);
    setRGB0(&ctx->draw[1], 0, 20, 40);

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

int main(void) {
    static GpuContext ctx;

    int fontId;
    int refreshFrame = 0;
    int refreshCount = 0;
    int instabilityCount = 0;
    int queryFailures = 0;
    int entriesCount = 0;
    int cdReady = 0;
    int discReady = 0;

    uint8_t hashValid = 0;
    uint32_t stableHash = 0;

    char volumeLabel[33];

    initGpu(&ctx);

    FntLoad(960, 0);
    fontId = FntOpen(8, 16, SCREEN_XRES - 16, SCREEN_YRES - 24, 0, 768);

    for (int i = 0; i < 32; ++i) {
        volumeLabel[i] = '\0';
    }
    volumeLabel[32] = '\0';

    cdReady = CdInit();

    while (1) {
        if (cdReady && (refreshFrame >= REFRESH_INTERVAL_FRAMES)) {
            const uint8_t* pvd = (const uint8_t*)sPvdWords;
            int ok = 1;
            uint32_t rootExtent = 0;
            uint32_t rootSize = 0;
            int rootSectors = 0;
            uint32_t currentHash = 0;

            refreshFrame = 0;
            refreshCount++;

            if (readSectorsLba(16, 1, sPvdWords) != 0) {
                ok = 0;
            }

            if (ok) {
                if (!((pvd[0] == 1) && (pvd[1] == 'C') && (pvd[2] == 'D') && (pvd[3] == '0') && (pvd[4] == '0') && (pvd[5] == '1'))) {
                    ok = 0;
                }
            }

            if (ok) {
                for (int i = 0; i < 32; ++i) {
                    char c = (char)pvd[40 + i];
                    volumeLabel[i] = (c == ' ') ? '\0' : c;
                }

                rootExtent = readLe32(&pvd[156 + 2]);
                rootSize = readLe32(&pvd[156 + 10]);
                rootSectors = (int)((rootSize + (LOGICAL_SECTOR_SIZE - 1)) / LOGICAL_SECTOR_SIZE);

                if ((rootSectors <= 0) || (rootSectors > MAX_DIR_SECTORS)) {
                    ok = 0;
                }
            }

            if (ok) {
                if (readSectorsLba((int)rootExtent, rootSectors, sDirWords) != 0) {
                    ok = 0;
                }
            }

            if (ok) {
                int dirBytes = rootSectors * LOGICAL_SECTOR_SIZE;
                entriesCount = parseDirectoryEntries((const uint8_t*)sDirWords, dirBytes, sEntries, MAX_ENTRIES, &currentHash);
                discReady = 1;

                if (hashValid && (stableHash != currentHash)) {
                    instabilityCount++;
                }

                stableHash = currentHash;
                hashValid = 1u;
            } else {
                discReady = 0;
                entriesCount = 0;
                queryFailures++;
            }
        }

        refreshFrame++;

        FntPrint(fontId, "CDROM SMOKE: CDBROWSE\n\n");
        FntPrint(fontId, "CD init: %s\n", cdReady ? "OK" : "FAILED");
        FntPrint(fontId, "Disc ready: %s\n", discReady ? "YES" : "NO");
        FntPrint(fontId, "Volume: %s\n", discReady ? volumeLabel : "(unavailable)");
        FntPrint(fontId, "Refreshes: %d\n", refreshCount);
        FntPrint(fontId, "Query failures: %d\n", queryFailures);
        FntPrint(fontId, "Instability count: %d\n", instabilityCount);
        FntPrint(fontId, "Entries: %d\n\n", entriesCount);

        for (int i = 0; (i < entriesCount) && (i < LIST_LINES); ++i) {
            FntPrint(
                fontId,
                "%-16s %s %6d\n",
                sEntries[i].name,
                sEntries[i].isDirectory ? "DIR " : "FILE",
                sEntries[i].size
            );
        }

        if (!cdReady) {
            FntPrint(fontId, "\nRESULT: FAIL (CdInit failed)\n");
        } else if (queryFailures > 0) {
            FntPrint(fontId, "\nRESULT: FAIL (read/parse failed)\n");
        } else if (instabilityCount > 0) {
            FntPrint(fontId, "\nRESULT: FAIL (listing drift)\n");
        } else if (refreshCount >= PASS_MIN_REFRESHES) {
            FntPrint(fontId, "\nRESULT: PASS (stable repeated listing)\n");
        } else {
            FntPrint(fontId, "\nRESULT: WARMING UP...\n");
        }

        FntFlush(fontId);
        flipBuffers(&ctx);
    }

    return 0;
}
