#include <assert.h>
#include <stdio.h>
#include <psptypes.h>

#define _ULTRATYPES_H_
#include "src/psp/gfx/gfx_psp_backend_gu.c"

static u32 sTestFrame[512 * 272];
static u16 sReadback[320 * 240];

int main(void) {
    int x, y;

    assert(!PspGfx_ReadFramebuffer(sReadback, 320, 240));
    s2dScaleX = s2dScaleY = 272.0f / 240.0f;
    s2dOffsetX = (480.0f - 320.0f * s2dScaleX) * 0.5f;
    for (y = 0; y < 272; y++) {
        for (x = 0; x < 512; x++) {
            sTestFrame[y * 512 + x] = x < 240 ? 0xFF0000FF : 0xFF00FF00;
        }
    }
    sLastFrame = sTestFrame;
    assert(!PspGfx_ReadFramebuffer(NULL, 320, 240));
    assert(!PspGfx_ReadFramebuffer(sReadback, 640, 480));
    assert(PspGfx_ReadFramebuffer(sReadback, 320, 240));
    assert(sReadback[0] == 0xF801);
    assert(sReadback[319] == 0x07C1);
    assert(sReadback[239 * 320] == 0xF801);
    assert(sReadback[240 * 320 - 1] == 0x07C1);
    for (y = 0; y < 512 * 272; y++)
        sTestFrame[y] = 0xFFFF0000;
    assert(PspGfx_ReadFramebuffer(sReadback, 320, 240));
    assert(sReadback[0] == 0x003F);
    puts("PSP GU framebuffer conversion tests passed");
    return 0;
}
