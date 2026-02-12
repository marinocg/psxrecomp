#include <psxapi.h>
#include <psxetc.h>
#include <psxgpu.h>

int main(void) {
    ResetGraph(0);

    FntLoad(960, 0);
    int fontId = FntOpen(32, 24, 320, 32, 0, 256);

    while (1) {
        FntPrint(fontId, "Hello world from PSn00bSDK!\n");
        FntPrint(fontId, "Generated with psxrecomp demo project.\n");
        FntFlush(fontId);

        VSync(0);
    }

    return 0;
}
