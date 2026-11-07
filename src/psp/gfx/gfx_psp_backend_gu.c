#include <pspdisplay.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspkernel.h>

#include "src/psp/gfx/gfx_psp.h"
#include "src/psp/psp.h"

#define GU_LIST_WORDS 0x40000
#define GU_FB_WIDTH 512
#define GU_FB_HEIGHT 272
#define GU_FB_STRIDE (GU_FB_WIDTH * 4)
#define GU_DRAW_OFFSET 0
#define GU_DISP_OFFSET (GU_FB_STRIDE * GU_FB_HEIGHT)
#define GU_DEPTH_OFFSET (GU_DISP_OFFSET * 2)

#define PSP_SCREEN_W 480.0f
#define PSP_SCREEN_H 272.0f
#define N64_SCREEN_W 320.0f
#define N64_SCREEN_H 240.0f

static u32 sGuList[GU_LIST_WORDS] __attribute__((aligned(64)));
static int sGuReady;
static int sDisplayed;
static u32 sDrawOffset = GU_DRAW_OFFSET;
static const volatile u32* sLastFrame;

typedef struct {
    u16 color;
    s16 x;
    s16 y;
    s16 z;
} PspGuSolidVertex;

typedef struct {
    float u;
    float v;
    u16 color;
    float x;
    float y;
    float z;
} PspGuTexVertex;

static float s2dScaleX;
static float s2dScaleY;
static float s2dOffsetX;
static float s2dOffsetY;

static s16 psp_gfx_map_x(float x) {
    return (s16) ((x * s2dScaleX) + s2dOffsetX + 0.5f);
}

static s16 psp_gfx_map_y(float y) {
    return (s16) ((y * s2dScaleY) + s2dOffsetY + 0.5f);
}

static u16 psp_gfx_rgba8888_to_4444(u32 rgba) {
    u32 r = rgba & 0xFF;
    u32 g = (rgba >> 8) & 0xFF;
    u32 b = (rgba >> 16) & 0xFF;
    u32 a = (rgba >> 24) & 0xFF;

    return (u16) (((a >> 4) << 12) | ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4));
}

int PspGfx_Init(void) {
    if (sGuReady)
        return 1;

    /* From here on the debug console must not draw over the frame */
    PspLog_SetScreen(0);

    /* Preserve 4:3 with pillarbox on the 480x272 panel */
    s2dScaleX = PSP_SCREEN_H / N64_SCREEN_H;
    s2dScaleY = PSP_SCREEN_H / N64_SCREEN_H;
    s2dOffsetX = (PSP_SCREEN_W - (N64_SCREEN_W * s2dScaleX)) * 0.5f;
    s2dOffsetY = 0.0f;

    sceGuInit();
    sceGuStart(GU_DIRECT, sGuList);
    sceGuDrawBuffer(GU_PSM_8888, (void*) GU_DRAW_OFFSET, GU_FB_WIDTH);
    sceGuDispBuffer(480, 272, (void*) GU_DISP_OFFSET, GU_FB_WIDTH);
    sceGuDepthBuffer((void*) GU_DEPTH_OFFSET, GU_FB_WIDTH);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuScissor(0, 0, 480, 272);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuFinish();
    sceGuSync(0, 0);

    sGuReady = 1;
    return 1;
}

void PspGfx_BeginFrame(void) {
    sceGuStart(GU_DIRECT, sGuList);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuScissor(0, 0, 480, 272);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_ALPHA_TEST);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_FOG);
    sceGuDisable(GU_LIGHTING);
    sceGuDepthMask(GU_TRUE);
    sceGuPixelMask(0);
    sceGuClearColor(0xFF000000);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_DEPTH_BUFFER_BIT);
}

void PspGfx_Present(void) {
    if (!sGuReady) {
        return;
    }

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sLastFrame = (const volatile u32*) ((u32) sceGeEdramGetAddr() | 0x40000000u | sDrawOffset);
    sDrawOffset = (u32) sceGuSwapBuffers();

    if (!sDisplayed) {
        sceGuDisplay(GU_TRUE);
        sDisplayed = 1;
    }
}

void PspGfx_DrawSolidRect(s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba) {
    PspGuSolidVertex* v;
    u16 color;

    if (!sGuReady) {
        return;
    }

    v = (PspGuSolidVertex*) sceGuGetMemory(2 * sizeof(PspGuSolidVertex));
    if (v == NULL) {
        return;
    }

    color = psp_gfx_rgba8888_to_4444(rgba);
    sceGuDisable(GU_TEXTURE_2D);
    if (((rgba >> 24) & 0xFF) < 0xFF) {
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    } else {
        sceGuDisable(GU_BLEND);
    }

    v[0].color = color;
    v[0].x = psp_gfx_map_x((float) x0);
    v[0].y = psp_gfx_map_y((float) y0);
    v[0].z = 0;
    v[1].color = color;
    v[1].x = psp_gfx_map_x((float) x1);
    v[1].y = psp_gfx_map_y((float) y1);
    v[1].z = 0;

    sceGuDrawArray(GU_SPRITES, GU_COLOR_4444 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, 2, NULL, v);
}

void PspGfx_DrawTexQuad(s32 x0, s32 y0, s32 x1, s32 y1, const u16* pixels, s32 texW, s32 texH, float u0, float v0,
                        float u1, float v1, u32 tint, int modulate, int blend, int flip) {
    PspGuTexVertex* v;
    int count = flip ? 4 : 2;
    int i;

    if (!sGuReady || pixels == NULL || texW <= 0 || texH <= 0) {
        return;
    }
    v = (PspGuTexVertex*) sceGuGetMemory(count * sizeof(PspGuTexVertex));
    if (v == NULL) {
        return;
    }
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexMode(GU_PSM_4444, 0, 0, 0);
    sceKernelDcacheWritebackRange((void*) pixels, texW * texH * sizeof(u16));
    sceGuTexImage(0, texW, texH, texW, pixels);
    sceGuTexFlush();
    sceGuTexFunc(modulate ? GU_TFX_MODULATE : GU_TFX_REPLACE, GU_TCC_RGBA);
    sceGuTexFilter(GU_NEAREST, GU_NEAREST);
    sceGuTexWrap(GU_REPEAT, GU_REPEAT);

    if (blend) {
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    } else {
        sceGuDisable(GU_BLEND);
    }
    for (i = 0; i < count; i++) {
        int right = flip ? (i & 1) : i;
        int bottom = flip ? (i >> 1) : i;

        v[i].u = (flip ? bottom : right) ? u1 : u0;
        v[i].v = (flip ? right : bottom) ? v1 : v0;
        v[i].color = (u16) tint;
        v[i].x = psp_gfx_map_x((float) (right ? x1 : x0));
        v[i].y = psp_gfx_map_y((float) (bottom ? y1 : y0));
        v[i].z = 0;
    }
    sceGuDrawArray(flip ? GU_TRIANGLE_STRIP : GU_SPRITES,
                   GU_TEXTURE_32BITF | GU_COLOR_4444 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, count, NULL, v);
}

void PspGfx_SetScissor(s32 x0, s32 y0, s32 x1, s32 y1) {
    s32 sx0 = psp_gfx_map_x((float) x0);
    s32 sy0 = psp_gfx_map_y((float) y0);
    s32 sx1 = psp_gfx_map_x((float) x1);
    s32 sy1 = psp_gfx_map_y((float) y1);

    if (sx0 < 0) {
        sx0 = 0;
    }
    if (sy0 < 0) {
        sy0 = 0;
    }
    if (sx1 > 480) {
        sx1 = 480;
    }
    if (sy1 > 272) {
        sy1 = 272;
    }
    if (sx1 <= sx0 || sy1 <= sy0) {
        return;
    }
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuScissor(sx0, sy0, sx1 - sx0, sy1 - sy0);
}

void PspGfx_DrawTriangle2D(const PspGfxVertex v[3]) {
    PspGfxVertex* out;

    if (!sGuReady) {
        return;
    }

    out = (PspGfxVertex*) sceGuGetMemory(3 * sizeof(PspGfxVertex));
    if (out == NULL) {
        return;
    }
    out[0] = v[0];
    out[1] = v[1];
    out[2] = v[2];
    out[0].x = (v[0].x * s2dScaleX) + s2dOffsetX;
    out[0].y = (v[0].y * s2dScaleY) + s2dOffsetY;
    out[1].x = (v[1].x * s2dScaleX) + s2dOffsetX;
    out[1].y = (v[1].y * s2dScaleY) + s2dOffsetY;
    out[2].x = (v[2].x * s2dScaleX) + s2dOffsetX;
    out[2].y = (v[2].y * s2dScaleY) + s2dOffsetY;
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDrawArray(GU_TRIANGLES, GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D, 3, NULL, out);
}

int PspGfx_ReadFramebuffer(u16* pixels, s32 width, s32 height) {
    s32 x, y;
    const volatile u32* frame = sLastFrame;

    if (frame == NULL || pixels == NULL || width != 320 || height != 240)
        return 0;
    for (y = 0; y < height; y++) {
        s32 sy = psp_gfx_map_y((float) y + 0.5f);

        if (sy >= GU_FB_HEIGHT)
            sy = GU_FB_HEIGHT - 1;
        for (x = 0; x < width; x++) {
            s32 sx = psp_gfx_map_x((float) x + 0.5f);
            u32 color = frame[sy * GU_FB_WIDTH + sx];

            pixels[y * width + x] = ((color & 0xF8) << 8) | ((color & 0xF800) >> 5) | ((color & 0xF80000) >> 18) | 1;
        }
    }
    return 1;
}
