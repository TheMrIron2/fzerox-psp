#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

/* Match PSP integer widths when exercising command decoding on the host */
#define _ULTRATYPES_H_
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;
typedef float f32;
typedef double f64;
#include "src/psp/gfx/gfx_psp_dl.c"

u32 gSegments[16];
u8 gPspN64Ram[0x40000];
static u16 sNative[32 * 32] = { 0xF801 };
static Gfx sCommands[2048];
static const u16* sQueuedPixels[2048];
static int sTextures;
static int sSolids;
static s32 sRect[4];
static float sUv[4];
static u16 sPixels[512 * 512];
static s32 sWidth;
static s32 sHeight;
static int sFlip;
static u32 sTint;
static int sModulate;
static u16 sNativePalette[256] = { 0xF801 };
static u16 sCpuFrame[320 * 240];

int PspGfx_Init(void) {
    return 1;
}
void PspGfx_BeginFrame(void) {
}
void PspGfx_Present(void) {
}
void PspLog(const char* fmt, ...) {
    (void) fmt;
}
void PspGfx_DrawTriangle2D(const PspGfxVertex v[3]) {
    (void) v;
}
void PspGfx_SetScissor(s32 x0, s32 y0, s32 x1, s32 y1) {
    (void) x0;
    (void) y0;
    (void) x1;
    (void) y1;
}
void PspGfx_DrawSolidRect(s32 x0, s32 y0, s32 x1, s32 y1, u32 color) {
    sSolids++;
    sRect[0] = x0;
    sRect[1] = y0;
    sRect[2] = x1;
    sRect[3] = y1;
    sPixels[0] = color & 0xFFFF;
}
void PspGfx_DrawTexQuad(s32 x0, s32 y0, s32 x1, s32 y1, const u16* pixels, s32 width, s32 height, float u0, float v0,
                        float u1, float v1, u32 tint, int modulate, int blend, int flip) {
    int i;

    sTint = tint;
    sModulate = modulate;
    (void) blend;
    sQueuedPixels[sTextures++] = pixels;
    sRect[0] = x0;
    sRect[1] = y0;
    sRect[2] = x1;
    sRect[3] = y1;
    sUv[0] = u0;
    sUv[1] = v0;
    sUv[2] = u1;
    sUv[3] = v1;
    sWidth = width;
    sHeight = height;
    sFlip = flip;
    for (i = 0; i < width * height; i++) {
        sPixels[i] = pixels[i];
    }
}

static void run(Gfx* end) {
    gSPEndDisplayList(end++);
    sTextures = sSolids = 0;
    PspGfx_RunDisplayList(sCommands, (end - sCommands) * sizeof(Gfx));
    PspGfx_EndFrame();
}

/* Content written in place must be registered, exactly like the asset loader */
static void touch(const void* data, u32 size) {
    PspGfx_SetTextureByteOrder(data, size, PspGfx_TextureByteOrder(data));
}

static void test_portrait(void) {
    Gfx* p = sCommands;
    int i;

    for (i = 0; i < 32 * 32; i++) {
        sNative[i] = (i & 1) ? 0x07C1 : 0xF801;
    }
    touch(sNative, sizeof(sNative));
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPLoadTextureBlock(p++, sNative, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0, 0, 0, 0, 0, 0, 0);
    gSPTextureRectangle(p++, 24 << 2, 16 << 2, 48 << 2, 40 << 2, 0, 0, 0, 1365, 1365);
    run(p);
    assert(sTextures == 1 && sWidth == 32 && sHeight == 32);
    assert(sPixels[0] == 0xF00F && sPixels[1] == 0xF0F0 && sPixels[32] == 0xF00F);
    assert(fabsf(sUv[2] - 31.9921875f) < 0.00001f);
}

static void test_atlas_and_copy(void) {
    Gfx* p = sCommands;

    gDPSetCycleType(p++, G_CYC_COPY);
    gDPLoadTextureBlock(p++, sNative, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 72, 0, 0, 0, 0, 0, 0, 0);
    gSPTextureRectangle(p++, 10 << 2, 20 << 2, 17 << 2, 31 << 2, 0, 0, 36 << 5, 4096, 1024);
    run(p);
    assert(sTextures == 1 && sWidth == 8 && sHeight == 128);
    assert(sRect[2] == 18 && sRect[3] == 32);
    assert(sUv[0] == 0 && sUv[1] == 36 && sUv[2] == 8 && sUv[3] == 48);
}

static void test_energy_and_primitive(void) {
    Gfx* p = sCommands;

    gDPSetCycleType(p++, G_CYC_FILL);
    gDPSetFillColor(p++, 0xF801F801);
    gDPFillRectangle(p++, 20, 30, 50, 30);
    run(p);
    assert(sSolids == 1 && sRect[2] == 51 && sRect[3] == 31);
    assert(sPixels[0] == 0x00FF);

    p = sCommands;
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPSetPrimColor(p++, 0, 0, 0, 255, 0, 128);
    gDPSetCombineMode(p++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPFillRectangle(p++, 20, 30, 50, 40);
    run(p);
    assert(sSolids == 1 && sRect[2] == 50 && sRect[3] == 40);
    assert(sPixels[0] == 0xFF00);
}

static void test_tile_and_byte_formats(void) {
    Gfx* p = sCommands;
    int i;

    gDPSetCombineMode(p++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    for (i = 0; i < 64; i++) {
        gPspN64Ram[i] = (u8) i;
    }
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPLoadTextureTile(p++, gPspN64Ram, G_IM_FMT_IA, G_IM_SIZ_8b, 16, 4, 4, 1, 7, 2, 0, 0, 0, 0, 0, 0, 0);
    gSPTextureRectangle(p++, 0, 0, 4 << 2, 2 << 2, 0, 4 << 5, 1 << 5, 1024, 1024);
    run(p);
    assert(sTextures == 1 && sUv[0] == 0 && sUv[1] == 0);
    assert(sPixels[0] == 0x4111 && sPixels[8] == 0x4222);

    p = sCommands;
    gDPLoadTextureBlock_4b(p++, gPspN64Ram, G_IM_FMT_I, 8, 8, 0, 0, 0, 0, 0, 0, 0);
    gSPTextureRectangle(p++, 0, 0, 8 << 2, 8 << 2, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sTextures == 1 && sPixels[2] == 0 && sPixels[3] == 0x1111);

    p = sCommands;
    gDPLoadTextureBlock(p++, gPspN64Ram, G_IM_FMT_I, G_IM_SIZ_8b, 8, 8, 0, 0, 0, 0, 0, 0, 0);
    gSPTextureRectangle(p++, 0, 0, 8 << 2, 8 << 2, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sTextures == 1 && sPixels[16] == 0x1111);
}

static void test_remaining_ia_formats(void) {
    Gfx* p = sCommands;

    sNative[0] = 0xA050;
    gDPLoadTextureBlock(p++, sNative, G_IM_FMT_IA, G_IM_SIZ_16b, 8, 8, 0, 0, 0, 0, 0, 0, 0);
    gSPTextureRectangle(p++, 0, 0, 8 << 2, 8 << 2, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sTextures == 1 && sPixels[0] == 0x5AAA);

    p = sCommands;
    gPspN64Ram[0] = 0xA0;
    gPspN64Ram[1] = 0x50;
    touch(gPspN64Ram, 128);
    gDPLoadTextureBlock(p++, gPspN64Ram, G_IM_FMT_IA, G_IM_SIZ_16b, 8, 8, 0, 0, 0, 0, 0, 0, 0);
    gSPTextureRectangle(p++, 0, 0, 8 << 2, 8 << 2, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sTextures == 1 && sPixels[0] == 0x5AAA);

    p = sCommands;
    gPspN64Ram[0] = 0xF0;
    gPspN64Ram[1] = 0x91;
    touch(gPspN64Ram, 128);
    gDPLoadTextureBlock_4b(p++, gPspN64Ram, G_IM_FMT_IA, 8, 8, 0, 0, 0, 0, 0, 0, 0);
    gSPTextureRectangle(p++, 0, 0, 8 << 2, 8 << 2, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sTextures == 1 && sPixels[0] == 0xFFFF && sPixels[1] == 0 && sPixels[2] == 0xF999);

    p = sCommands;
    gDPLoadTextureTile_4b(p++, gPspN64Ram, G_IM_FMT_I, 16, 4, 4, 1, 7, 2, 0, 0, 0, 0, 0, 0, 0);
    gPspN64Ram[10] = 0xAB;
    gPspN64Ram[18] = 0xCD;
    touch(gPspN64Ram, 128);
    gSPTextureRectangle(p++, 0, 0, 4 << 2, 2 << 2, 0, 4 << 5, 1 << 5, 1024, 1024);
    run(p);
    assert(sTextures == 1 && sUv[0] == 0 && sUv[1] == 0);
    assert(sPixels[0] == 0xAAAA && sPixels[1] == 0xBBBB && sPixels[8] == 0xCCCC);
}

static void test_loaded_source_and_flip(void) {
    Gfx* p = sCommands;

    gPspN64Ram[0] = 0xF8;
    gPspN64Ram[1] = 1;
    touch(gPspN64Ram, 128);
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPLoadTextureBlock(p++, gPspN64Ram, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 8, 0, 0, 0, 0, 0, 0, 0);
    gDPSetTextureImage(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 1, sNative);
    gSPTextureRectangleFlip(p++, 0, 0, 8 << 2, 4 << 2, 0, 16, 8, 1024, 512);
    run(p);
    assert(sTextures == 1 && sPixels[0] == 0xF00F && sFlip);
    assert(sUv[0] == 0.5f && sUv[1] == 0.25f && sUv[2] == 4.5f && sUv[3] == 4.25f);
}

static void test_wipe_then_logo(void) {
    Gfx* p = sCommands;
    int row;
    int x;

    for (row = 0; row < 227; row++) {
        for (x = 0; x < 296; x++) {
            int offset = (row * 296 + x) * 2;

            gPspN64Ram[offset] = row < 3 ? 0xF8 : 0x07;
            gPspN64Ram[offset + 1] = row < 3 ? 0x01 : 0xC1;
        }
    }
    for (x = 0; x < 32 * 32; x++) {
        sNative[x] = 0x003F;
    }
    touch(gPspN64Ram, 296 * 227 * 2);
    touch(sNative, sizeof(sNative));
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPSetCombineMode(p++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetTextureImage(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 296, gPspN64Ram);
    gDPSetTile(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
    gDPSetTile(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 74, 0, G_TX_RENDERTILE, 0, 0, 0, 0, 0, 0, 0);
    gDPSetTileSize(p++, G_TX_RENDERTILE, 0, 0, 295 << 2, 2 << 2);
    for (row = 0; row < 224; row += 3) {
        int bottom = row + 3 < 224 ? row + 3 : 224;

        gDPLoadBlock(p++, G_TX_LOADTILE, 0, row, 296 * 3 - 1, CALC_DXT(296, 2));
        gSPTextureRectangle(p++, 12 << 2, (row + 8) << 2, 308 << 2, (bottom + 8) << 2, G_TX_RENDERTILE, 0, 0, 1024,
                            1024);
    }
    gDPLoadTextureBlock(p++, sNative, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0, 0, 0, 0, 0, 0, 0);
    gSPTextureRectangle(p++, 0, 0, 32 << 2, 32 << 2, 0, 0, 0, 1024, 1024);
    gSPTextureRectangle(p++, 32 << 2, 0, 64 << 2, 32 << 2, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sTextures == 77);
    assert(sWidth == 32 && sHeight == 32 && sPixels[0] == 0xFF00);
    assert(sQueuedPixels[0][0] == 0xF00F && sQueuedPixels[74][0] == 0xF0F0);
    assert(sQueuedPixels[75] == sQueuedPixels[76]);

    /* Replaying the same frame must come entirely from the decoded tile cache */
    {
        u32 before = PspGfx_TexDecodeCount();

        p = sCommands;
        gDPSetCycleType(p++, G_CYC_1CYCLE);
        gDPSetCombineMode(p++, G_CC_DECALRGBA, G_CC_DECALRGBA);
        gDPSetTextureImage(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 296, gPspN64Ram);
        gDPSetTile(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 0, 0, G_TX_LOADTILE, 0, 0, 0, 0, 0, 0, 0);
        gDPSetTile(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 74, 0, G_TX_RENDERTILE, 0, 0, 0, 0, 0, 0, 0);
        gDPSetTileSize(p++, G_TX_RENDERTILE, 0, 0, 295 << 2, 2 << 2);
        for (row = 0; row < 224; row += 3) {
            int bottom = row + 3 < 224 ? row + 3 : 224;

            gDPLoadBlock(p++, G_TX_LOADTILE, 0, row, 296 * 3 - 1, CALC_DXT(296, 2));
            gSPTextureRectangle(p++, 12 << 2, (row + 8) << 2, 308 << 2, (bottom + 8) << 2, G_TX_RENDERTILE, 0, 0,
                                1024, 1024);
        }
        gDPLoadTextureBlock(p++, sNative, G_IM_FMT_RGBA, G_IM_SIZ_16b, 32, 32, 0, 0, 0, 0, 0, 0, 0);
        gSPTextureRectangle(p++, 0, 0, 32 << 2, 32 << 2, 0, 0, 0, 1024, 1024);
        gSPTextureRectangle(p++, 32 << 2, 0, 64 << 2, 32 << 2, 0, 0, 0, 1024, 1024);
        run(p);
        assert(sTextures == 77);
        assert(sPixels[0] == 0xFF00);
        assert(PspGfx_TexDecodeCount() == before);
    }
}

static void test_palette_and_tint(void) {
    Gfx* p = sCommands;
    int i;

    for (i = 0; i < 256; i++)
        sNativePalette[i] = (i & 1) ? 0x07C1 : 0xF801;
    for (i = 0; i < 64; i++)
        gPspN64Ram[i] = i & 1;
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPSetTextureLUT(p++, G_TT_RGBA16);
    gDPLoadTLUT_pal256(p++, sNativePalette);
    gDPSetCombineMode(p++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(p++, 0, 0, 128, 255, 0, 255);
    gDPLoadTextureBlock(p++, gPspN64Ram, G_IM_FMT_CI, G_IM_SIZ_8b, 8, 8, 0, 0, 0, 0, 0, 0, 0);
    gSPTextureRectangle(p++, 0, 0, 32, 32, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sTextures == 1 && sPixels[0] == 0xF00F && sPixels[1] == 0xF0F0);
    assert(sModulate && sTint == 0xF0F8);

    p = sCommands;
    gDPLoadTLUT_pal16(p++, 3, sNativePalette);
    gDPLoadTextureBlock_4b(p++, gPspN64Ram, G_IM_FMT_CI, 8, 8, 3, 0, 0, 0, 0, 0, 0);
    gSPTextureRectangle(p++, 0, 0, 32, 32, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sTextures == 1 && sPixels[0] == 0xF00F && sPixels[2] == 0xF00F && sPixels[3] == 0xF0F0);
}

static void test_rgba_blink_and_background(void) {
    Gfx* p = sCommands;

    sNative[0] = 0xFFFF;
    gDPSetTextureLUT(p++, G_TT_NONE);
    gDPSetCombineMode(p++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPLoadTextureBlock(p++, sNative, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 8, 0, 0, 0, 0, 0, 0, 0);
    gDPSetPrimColor(p++, 0, 0, 255, 0, 0, 255);
    gSPTextureRectangle(p++, 0, 0, 32, 32, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sModulate && sTint == 0xF00F);
    p = sCommands;
    gDPSetPrimColor(p++, 0, 0, 0, 255, 0, 255);
    gSPTextureRectangle(p++, 0, 0, 32, 32, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sModulate && sTint == 0xF0F0);
    p = sCommands;
    gDPSetPrimColor(p++, 0, 0, 75, 75, 75, 180);
    gSPTextureRectangle(p++, 0, 0, 32, 32, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sModulate && sTint == 0xB444);
}

static void test_combine_alpha_and_environment(void) {
    Gfx* p = sCommands;

    sNative[0] = 0xFFFF;
    gDPLoadTextureBlock(p++, sNative, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 8, 0, 0, 0, 0, 0, 0, 0);
    gDPSetCombineLERP(p++, 0, 0, 0, TEXEL0, 0, 0, 0, PRIMITIVE, 0, 0, 0, TEXEL0, 0, 0, 0, PRIMITIVE);
    gDPSetPrimColor(p++, 0, 0, 0, 0, 0, 128);
    gSPTextureRectangle(p++, 0, 0, 32, 32, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sPixels[0] == 0x8FFF && !sModulate);
    p = sCommands;
    gDPSetPrimColor(p++, 0, 0, 0, 0, 0, 255);
    gDPSetEnvColor(p++, 0, 0, 255, 255);
    gDPSetCombineLERP(p++, TEXEL0, 0, ENVIRONMENT, 0, 0, 0, 0, TEXEL0, TEXEL0, 0, ENVIRONMENT, 0, 0, 0, 0, TEXEL0);
    gSPTextureRectangle(p++, 0, 0, 32, 32, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sPixels[0] == 0xFF00);
    p = sCommands;
    gDPSetCycleType(p++, G_CYC_2CYCLE);
    gDPSetPrimColor(p++, 0, 0, 255, 0, 255, 255);
    gDPSetEnvColor(p++, 128, 255, 255, 255);
    gDPSetCombineLERP(p++, TEXEL0, 0, PRIMITIVE, 0, TEXEL0, 0, PRIMITIVE, 0, COMBINED, 0, ENVIRONMENT, 0, COMBINED, 0,
                      ENVIRONMENT, 0);
    gSPTextureRectangle(p++, 0, 0, 32, 32, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sPixels[0] == 0xFF08);
    p = sCommands;
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPSetCombineMode(p++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    run(p);
}

static void test_texture_byte_order(void) {
    Gfx* p = sCommands;

    ((u16*) gPspN64Ram)[0] = 0xF801;
    PspGfx_SetTextureByteOrder(gPspN64Ram, 128, 1);
    gDPLoadTextureBlock(p++, gPspN64Ram, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 8, 0, 0, 0, 0, 0, 0, 0);
    gSPTextureRectangle(p++, 0, 0, 32, 32, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sPixels[0] == 0xF00F);
    gPspN64Ram[0] = 0x07;
    gPspN64Ram[1] = 0xC1;
    PspGfx_SetTextureByteOrder(gPspN64Ram, 128, 0);
    {
        u32 count = sTextureOrderCount;
        int i;

        for (i = 0; i < 1024; i++)
            PspGfx_SetTextureByteOrder(gPspN64Ram, 128, 0);
        assert(sTextureOrderCount == count);
    }
    run(p);
    assert(sPixels[0] == 0xF0F0);
    /* A reused buffer must drop the stale range it overlaps */
    PspGfx_SetTextureByteOrder(gPspN64Ram, 128, 1);
    PspGfx_SetTextureByteOrder(gPspN64Ram + 16, 16, 0);
    assert(PspGfx_TextureByteOrder(gPspN64Ram + 16) == 0);
    {
        u32 base = (u32) (uintptr_t) gPspN64Ram;
        u32 overlapping = 0;
        u32 k;

        for (k = 0; k < sTextureOrderCount; k++) {
            if (sTextureOrders[k].addr < base + 128 && base < sTextureOrders[k].addr + sTextureOrders[k].size) {
                overlapping++;
            }
        }
        assert(overlapping == 1);
    }
}

static void test_boot_framebuffer(void) {
    sCpuFrame[70 * 320 + 96] = 0xF801;
    sTextures = 0;
    PspGfx_PresentFramebuffer(sCpuFrame);
    assert(sTextures == 1 && sRect[2] == 320 && sRect[3] == 240);
    assert(sWidth == 512 && sHeight == 256 && sPixels[70 * 512 + 96] == 0xF00F);
}

static void test_offscreen_fill(void) {
    Gfx* p = sCommands;

    for (int i = 0; i < 32 * 32; i++) {
        sNative[i] = 0xF801;
    }
    /* Offscreen render target: the white clear must not reach the screen */
    gDPSetColorImage(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 0x00000400);
    gDPSetCycleType(p++, G_CYC_FILL);
    gDPSetFillColor(p++, 0xFFFCFFFC);
    gDPFillRectangle(p++, 12, 8, 307, 231);
    /* Bind a visible framebuffer (mapped pointer) and draw a quad */
    gDPSetColorImage(p++, G_IM_FMT_RGBA, G_IM_SIZ_16b, 320, 0x09000000);
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPSetCombineMode(p++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPLoadTextureBlock(p++, sNative, G_IM_FMT_RGBA, G_IM_SIZ_16b, 8, 8, 0, 0, 0, 0, 0, 0, 0);
    gSPTextureRectangle(p++, 0, 0, 8 << 2, 8 << 2, 0, 0, 0, 1024, 1024);
    run(p);
    assert(sSolids == 0);
    assert(sTextures == 1);
}

int main(void) {
    Gfx* p = sCommands;

    gDPSetCombineMode(p++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    run(p);
    test_portrait();
    test_atlas_and_copy();
    test_energy_and_primitive();
    test_tile_and_byte_formats();
    test_remaining_ia_formats();
    test_loaded_source_and_flip();
    test_wipe_then_logo();
    test_palette_and_tint();
    test_rgba_blink_and_background();
    test_combine_alpha_and_environment();
    test_texture_byte_order();
    test_offscreen_fill();
    test_boot_framebuffer();
    puts("PSP HUD display list tests passed");
    return 0;
}
