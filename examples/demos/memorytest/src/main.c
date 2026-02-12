#include <stdint.h>

#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>

#define BUFFER_WORDS 1024
#define PATTERN_SEED 0x13579BDFu

static uint32_t testBuffer[BUFFER_WORDS];

static uint32_t patternForIndex(int index) {
    return PATTERN_SEED ^ (uint32_t)index * 0x45D9F3Bu;
}

int main(void) {
    int errors = 0;

    ResetGraph(0);
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
        VSync(0);
    }

    return 0;
}
