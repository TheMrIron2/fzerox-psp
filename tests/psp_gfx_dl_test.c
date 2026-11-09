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
int PspPlatform_DiagKey(void) {
    return 0;
}
u8 gPspN64Ram[0x40000];
u8 D_1000000[0x40000];
u8 D_2000000[0x40000];
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
static PspGfxVertex3D sTri3[3];
static PspGfxDrawState sTri3State;
static u32 sTri3Count;
static PspGfxDrawState sTri3States[2048];
static u32 sTri3Calls;

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
void PspGfx_DrawTriangles3D(const PspGfxVertex3D* vertices, u32 count, const PspGfxDrawState* state) {
    assert(sTri3Calls < 2048);
    sTri3States[sTri3Calls++] = *state;
    sTri3Count = count;
    if (count >= 3) {
        sTri3[0] = vertices[0];
        sTri3[1] = vertices[1];
        sTri3[2] = vertices[2];
    }
    sTri3State = *state;
}
void n64psp_directional_light_snorm8_batch(n64psp_vec4f* output, const n64psp_mat4f* matrix,
                                           const n64psp_snorm8x4* normals, const n64psp_directional_lightf* lights,
                                           const n64psp_vec4f* ambient, size_t lightCount, size_t count) {
    (void) output;
    (void) matrix;
    (void) normals;
    (void) lights;
    (void) ambient;
    (void) lightCount;
    (void) count;
}
static int sTexgenCalls;
void n64psp_texgen_snorm8_batch(n64psp_texcoord_s10_5* output, const n64psp_mat4f* modelview,
                                const void* packed_vertices, n64psp_texgen_mode mode, size_t count) {
    size_t i;

    (void) modelview;
    (void) packed_vertices;
    (void) mode;
    sTexgenCalls++;
    for (i = 0; i < count; i++) {
        output[i].s = 4096;
        output[i].t = 2048;
    }
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
    sTri3Calls = 0;
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

static void test_3d_triangle(void) {
    Gfx* p = sCommands;
    static Vtx vtx[3];
    static const Vp vp = { { { 640, 480, 511, 0 }, { 640, 480, 511, 0 } } };

    psp_gfx_mat_identity(sProjection);
    sProjectionSet = 1;
    sViewport = vp;
    psp_gfx_update_viewport_projection();

    vtx[0].v.ob[0] = -10;
    vtx[0].v.ob[1] = -10;
    vtx[0].v.ob[2] = 0;
    vtx[0].v.cn[0] = 255;
    vtx[0].v.cn[1] = 0;
    vtx[0].v.cn[2] = 0;
    vtx[0].v.cn[3] = 255;
    vtx[1].v.ob[0] = 10;
    vtx[1].v.ob[1] = -10;
    vtx[1].v.ob[2] = 0;
    vtx[1].v.cn[0] = 0;
    vtx[1].v.cn[1] = 255;
    vtx[1].v.cn[2] = 0;
    vtx[1].v.cn[3] = 255;
    vtx[2].v.ob[0] = 0;
    vtx[2].v.ob[1] = 10;
    vtx[2].v.ob[2] = -5;
    vtx[2].v.cn[0] = 0;
    vtx[2].v.cn[1] = 0;
    vtx[2].v.cn[2] = 255;
    vtx[2].v.cn[3] = 255;

    sTri3Count = 0;
    gSPSetGeometryMode(p++, G_ZBUFFER | G_SHADE | G_CULL_BACK | G_SHADING_SMOOTH);
    gDPSetRenderMode(p++, Z_CMP | Z_UPD, Z_CMP | Z_UPD);
    gSPVertex(p++, vtx, 3, 0);
    gSPModifyVertex(p++, 0, G_MWO_POINT_ST, 0x12345678);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);

    assert(sTri3Count == 3);
    assert(sTri3[0].x == -10.0f && sTri3[0].y == -10.0f && sTri3[0].z == 0.0f);
    assert(sTri3[2].z == -5.0f);
    assert(sTri3[0].color == 0xFF0000FFu);
    assert(sTri3[1].color == 0xFF00FF00u);
    assert(sTri3[2].color == 0xFFFF0000u);
    assert(sTri3State.depthTest == 1);
    assert(sTri3State.depthWrite == 1);
    assert(sTri3State.cullBack == 1 && sTri3State.cullFront == 0);
    assert(sTri3State.smoothShade == 1);
    assert(sTri3State.projection == &sViewportProjection[0][0]);
    assert(sVtx[0].v.tc[0] == 0x1234 && sVtx[0].v.tc[1] == 0x5678);

    p = sCommands;
    gSPClearGeometryMode(p++, G_SHADING_SMOOTH);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);
    assert(sTri3State.smoothShade == 0);

    /* Clear 3D state so later 2D tests stay on the software path */
    p = sCommands;
    gSPClearGeometryMode(p++, G_ZBUFFER | G_CULL_BACK);
    sProjectionSet = 0;
    run(p);
}

static void test_perspective_y(void) {
    static const Vp vp = { { { 320, 240, 511, 0 }, { 640, 600, 511, 0 } } };
    PspGfxVertex out;

    sViewport = vp;
    psp_gfx_mat_identity(sProjection);
    sProjection[2][3] = -1.0f;
    sProjectionPerspective = 1;
    psp_gfx_mat_identity(sModelView);
    psp_gfx_mat_mul(sMP, sModelView, sProjection);

    /* ndc y = +0.1 must land above the centre row */
    sVtx[0].v.ob[0] = 0;
    sVtx[0].v.ob[1] = 100;
    sVtx[0].v.ob[2] = -1000;
    psp_gfx_transform(0, &out);
    assert(fabsf(out.y - 144.0f) < 0.01f);

    /* The GE path folds the same viewport, so both must agree */
    psp_gfx_update_viewport_projection();
    assert(fabsf(sViewportProjection[1][1] - 0.5f) < 0.0001f);
    assert(fabsf(sViewportProjection[3][1] + 0.25f) < 0.0001f);

    /* Ortho matrices already carry the flip, w stays 1 */
    sProjection[2][3] = 0.0f;
    sProjectionPerspective = 0;
    psp_gfx_mat_mul(sMP, sModelView, sProjection);
    sVtx[0].v.ob[1] = 1;
    sVtx[0].v.ob[2] = 0;
    psp_gfx_transform(0, &out);
    assert(fabsf(out.y - 210.0f) < 0.01f);

    sProjectionSet = 0;
}

static void test_3d_no_zbuffer(void) {
    static Vtx vtx[3];
    static const Vp vp = { { { 320, 240, 511, 0 }, { 640, 600, 511, 0 } } };
    Gfx* p;
    int i;

    sViewport = vp;
    psp_gfx_mat_identity(sProjection);
    sProjection[2][3] = -1.0f;
    sProjectionPerspective = 1;
    sProjectionSet = 1;
    psp_gfx_mat_identity(sModelView);
    psp_gfx_mat_mul(sMP, sModelView, sProjection);
    psp_gfx_update_viewport_projection();

    for (i = 0; i < 3; i++) {
        vtx[i].v.ob[0] = (s16) (i * 10);
        vtx[i].v.ob[1] = 0;
        vtx[i].v.ob[2] = -100;
        vtx[i].v.cn[0] = 0;
        vtx[i].v.cn[1] = 0;
        vtx[i].v.cn[2] = 0;
        vtx[i].v.cn[3] = 255;
    }

    /* Perspective without G_ZBUFFER still belongs on the GE path */
    p = sCommands;
    gSPSetGeometryMode(p++, G_SHADE);
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPSetCombineMode(p++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(p++, 0, 0, 10, 20, 30, 255);
    gDPSetRenderMode(p++, G_RM_XLU_SURF, G_RM_XLU_SURF2);
    gSPVertex(p++, vtx, 3, 0);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);

    assert(sTri3Count == 3);
    assert(sTri3State.depthTest == 0 && sTri3State.depthWrite == 0);
    assert(sTri3State.blend == 1);
    /* G_CC_PRIMITIVE shades from the primitive colour, not the vertices */
    assert(sTri3[0].color == (10u | (20u << 8) | (30u << 16) | 0xFF000000u));

    /* Opaque render mode disables blending */
    p = sCommands;
    gDPSetRenderMode(p++, G_RM_OPA_SURF, G_RM_OPA_SURF2);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);
    assert(sTri3Count == 3);
    assert(sTri3State.blend == 0);

    p = sCommands;
    sProjectionPerspective = 0;
    sProjectionSet = 0;
    gSPClearGeometryMode(p++, G_SHADE);
    run(p);
}

static void test_3d_texgen(void) {
    static Vtx vtx[3];
    static const Vp vp = { { { 320, 240, 511, 0 }, { 640, 600, 511, 0 } } };
    Gfx* p;
    int i;

    sViewport = vp;
    psp_gfx_mat_identity(sProjection);
    sProjection[2][3] = -1.0f;
    sProjectionPerspective = 1;
    sProjectionSet = 1;
    psp_gfx_mat_identity(sModelView);
    psp_gfx_mat_mul(sMP, sModelView, sProjection);
    psp_gfx_update_viewport_projection();

    for (i = 0; i < 3; i++) {
        vtx[i].v.ob[0] = (s16) (i * 10);
        vtx[i].v.ob[1] = 0;
        vtx[i].v.ob[2] = -100;
        /* Normals drive the generated coordinates */
        vtx[i].v.cn[0] = 127;
        vtx[i].v.cn[1] = 0;
        vtx[i].v.cn[2] = 0;
        vtx[i].v.cn[3] = 255;
        /* Explicit coordinates must be ignored while generating */
        vtx[i].v.tc[0] = 999;
        vtx[i].v.tc[1] = 999;
    }

    sTexgenCalls = 0;
    p = sCommands;
    gSPSetGeometryMode(p++, G_ZBUFFER | G_SHADE | G_LIGHTING | G_TEXTURE_GEN);
    gSPNumLights(p++, NUMLIGHTS_1);
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPSetTextureFilter(p++, G_TF_BILERP);
    gDPSetCombineMode(p++, G_CC_MODULATEIA, G_CC_MODULATEIA);
    gDPSetRenderMode(p++, G_RM_ZB_OPA_SURF, G_RM_ZB_OPA_SURF2);
    gDPLoadTextureBlock(p++, sNative, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0, 0, 0, 0, 0, 0, 0);
    gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gSPVertex(p++, vtx, 3, 0);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);

    assert(sTri3Count == 3);
    assert(sTexgenCalls == 1);
    /* One texture width spans the sphere at full gSPTexture scale */
    assert(fabsf(sTri3[0].u - 2.0f) < 0.01f);
    assert(fabsf(sTri3[0].v - 1.0f) < 0.01f);
    assert(fabsf(sTri3[2].u - 2.0f) < 0.01f);
    assert(sTri3State.filter == 1);

    p = sCommands;
    sProjectionPerspective = 0;
    sProjectionSet = 0;
    gSPClearGeometryMode(p++, G_ZBUFFER | G_SHADE | G_LIGHTING | G_TEXTURE_GEN);
    gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_OFF);
    run(p);
}

static void test_3d_two_cycle_texture(void) {
    static Vtx vtx[3];
    static const Vp vp = { { { 320, 240, 511, 0 }, { 640, 600, 511, 0 } } };
    Gfx* p;
    int i;

    sViewport = vp;
    psp_gfx_mat_identity(sProjection);
    sProjection[2][3] = -1.0f;
    sProjectionPerspective = 1;
    sProjectionSet = 1;
    psp_gfx_mat_identity(sModelView);
    psp_gfx_mat_mul(sMP, sModelView, sProjection);
    psp_gfx_update_viewport_projection();

    for (i = 0; i < 3; i++) {
        vtx[i].v.ob[0] = (s16) (i * 10);
        vtx[i].v.ob[1] = 0;
        vtx[i].v.ob[2] = -100;
        vtx[i].v.tc[0] = (s16) (i * 32);
        vtx[i].v.tc[1] = 0;
        vtx[i].v.cn[0] = 0;
        vtx[i].v.cn[1] = 0;
        vtx[i].v.cn[2] = 0;
        vtx[i].v.cn[3] = 255;
    }

    /* Cycle 1 samples, cycle 2 passes: the machine body pattern */
    p = sCommands;
    gSPSetGeometryMode(p++, G_ZBUFFER | G_SHADE | G_LIGHTING);
    gSPNumLights(p++, NUMLIGHTS_1);
    gDPSetCycleType(p++, G_CYC_2CYCLE);
    gDPSetCombineLERP(p++, TEXEL0, 0, SHADE, 0, COMBINED, 0, SHADE, 0, TEXEL0, 0, SHADE, 0, COMBINED, 0, SHADE,
                      0);
    gDPSetRenderMode(p++, G_RM_PASS, G_RM_ZB_OPA_SURF2);
    gDPLoadTextureBlock(p++, sNative, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0, 0, 0, 0, 0, 0, 0);
    gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gSPVertex(p++, vtx, 3, 0);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);

    assert(sTri3Count == 3);
    assert(sTri3State.texturePixels != NULL);
    assert(sTri3State.texFunc == 0);

    p = sCommands;
    sProjectionPerspective = 0;
    sProjectionSet = 0;
    gSPClearGeometryMode(p++, G_ZBUFFER | G_SHADE | G_LIGHTING);
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    run(p);
}

static void test_3d_mirror_wrap(void) {
    static Vtx vtx[3];
    static const Vp vp = { { { 320, 240, 511, 0 }, { 640, 600, 511, 0 } } };
    Gfx* p;
    int i;

    sViewport = vp;
    psp_gfx_mat_identity(sProjection);
    sProjection[2][3] = -1.0f;
    sProjectionPerspective = 1;
    sProjectionSet = 1;
    psp_gfx_mat_identity(sModelView);
    psp_gfx_mat_mul(sMP, sModelView, sProjection);
    psp_gfx_update_viewport_projection();

    for (i = 0; i < 3; i++) {
        vtx[i].v.ob[0] = (s16) (i * 10);
        vtx[i].v.ob[1] = 0;
        vtx[i].v.ob[2] = -100;
        vtx[i].v.tc[0] = 32; /* one texel */
        vtx[i].v.tc[1] = 32;
        vtx[i].v.cn[0] = 0;
        vtx[i].v.cn[1] = 0;
        vtx[i].v.cn[2] = 0;
        vtx[i].v.cn[3] = 255;
    }

    /* S mirrors, T clamps */
    for (i = 0; i < 16 * 16; i++) {
        sNative[i] = ((i & 15) << 11) | 1;
    }
    touch(sNative, sizeof(sNative));
    p = sCommands;
    gSPSetGeometryMode(p++, G_ZBUFFER | G_SHADE);
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPSetCombineMode(p++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetRenderMode(p++, G_RM_ZB_OPA_SURF, G_RM_ZB_OPA_SURF2);
    gDPLoadTextureBlock(p++, sNative, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0, G_TX_MIRROR, G_TX_CLAMP, 0, 0, 0,
                        0);
    gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gSPVertex(p++, vtx, 3, 0);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);

    assert(sTri3Count == 3);
    assert(sTri3State.texturePixels != NULL);
    assert(sTri3State.wrapS == 0 && sTri3State.wrapT == 1);
    /* Mirroring extends the repeat period without changing texel coordinates */
    assert(sTri3State.texWidth == 32);
    assert(fabsf(sTri3[0].u - 1.0f) < 0.01f);
    assert(fabsf(sTri3[0].v - 1.0f) < 0.01f);
    assert(sTri3State.texturePixels[0] == 0xF000);
    assert(sTri3State.texturePixels[15] == 0xF007);
    assert(sTri3State.texturePixels[16] == 0xF007);
    assert(sTri3State.texturePixels[31] == 0xF000);

    /* MIRROR|CLAMP is a bitfield and clamps: no bake, no doubling */
    sTri3Count = 0;
    p = sCommands;
    gDPLoadTextureBlock(p++, sNative, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0, G_TX_MIRROR | G_TX_CLAMP,
                        G_TX_MIRROR | G_TX_CLAMP, 0, 0, 0, 0);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);

    assert(sTri3Count == 3);
    assert(sTri3State.wrapS == 1 && sTri3State.wrapT == 1);
    assert(sTri3State.texWidth == 16);
    assert(fabsf(sTri3[0].u - 1.0f) < 0.01f);
    assert(fabsf(sTri3[0].v - 1.0f) < 0.01f);

    p = sCommands;
    sProjectionPerspective = 0;
    sProjectionSet = 0;
    gSPClearGeometryMode(p++, G_ZBUFFER | G_SHADE);
    run(p);
}

static void test_3d_uv_scale(void) {
    static Vtx vtx[3];
    static const Vp vp = { { { 320, 240, 511, 0 }, { 640, 600, 511, 0 } } };
    Gfx* p;
    int i;

    sViewport = vp;
    psp_gfx_mat_identity(sProjection);
    sProjection[2][3] = -1.0f;
    sProjectionPerspective = 1;
    sProjectionSet = 1;
    psp_gfx_mat_identity(sModelView);
    psp_gfx_mat_mul(sMP, sModelView, sProjection);
    psp_gfx_update_viewport_projection();

    for (i = 0; i < 3; i++) {
        vtx[i].v.ob[0] = (s16) (i * 10);
        vtx[i].v.ob[1] = 0;
        vtx[i].v.ob[2] = -100;
        vtx[i].v.tc[0] = 64; /* two texels */
        vtx[i].v.tc[1] = 32; /* one texel */
        vtx[i].v.cn[0] = 0;
        vtx[i].v.cn[1] = 0;
        vtx[i].v.cn[2] = 0;
        vtx[i].v.cn[3] = 255;
    }

    /* Explicit coordinates ignore the gSPTexture scale, which only drives
     * generated ones; the tuning screen sets it to 2600 and would collapse them */
    p = sCommands;
    gSPSetGeometryMode(p++, G_ZBUFFER | G_SHADE);
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPSetCombineMode(p++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPSetRenderMode(p++, G_RM_ZB_OPA_SURF, G_RM_ZB_OPA_SURF2);
    gDPLoadTextureBlock(p++, sNative, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0, 0, 0, 0, 0, 0, 0);
    gSPTexture(p++, 0x8000, 0x8000, 0, G_TX_RENDERTILE, G_ON);
    gSPVertex(p++, vtx, 3, 0);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);

    assert(sTri3Count == 3);
    assert(fabsf(sTri3[0].u - 2.0f) < 0.01f);
    assert(fabsf(sTri3[0].v - 1.0f) < 0.01f);

    p = sCommands;
    sProjectionPerspective = 0;
    sProjectionSet = 0;
    gSPClearGeometryMode(p++, G_ZBUFFER | G_SHADE);
    gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    run(p);
}

static void test_3d_env_lerp_bake(void) {
    static Vtx vtx[3];
    static const Vp vp = { { { 320, 240, 511, 0 }, { 640, 600, 511, 0 } } };
    static const u8 i4[8] = { 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88 };
    Gfx* p;
    int i;

    sViewport = vp;
    psp_gfx_mat_identity(sProjection);
    sProjection[2][3] = -1.0f;
    sProjectionPerspective = 1;
    sProjectionSet = 1;
    psp_gfx_mat_identity(sModelView);
    psp_gfx_mat_mul(sMP, sModelView, sProjection);
    psp_gfx_update_viewport_projection();

    for (i = 0; i < 3; i++) {
        vtx[i].v.ob[0] = (s16) (i * 10);
        vtx[i].v.ob[1] = 0;
        vtx[i].v.ob[2] = -100;
        vtx[i].v.tc[0] = 0;
        vtx[i].v.tc[1] = 0;
        vtx[i].v.cn[0] = 255;
        vtx[i].v.cn[1] = 255;
        vtx[i].v.cn[2] = 255;
        vtx[i].v.cn[3] = 255;
    }

    /* Machine body: lerp from the paint colour toward the texel by its intensity */
    p = sCommands;
    gSPSetGeometryMode(p++, G_ZBUFFER | G_SHADE);
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    /* Zero alpha must survive opaque vertex colours */
    gDPSetCombineLERP(p++, TEXEL0, ENVIRONMENT, TEXEL0_ALPHA, ENVIRONMENT, 0, 0, 0, 0, TEXEL0, ENVIRONMENT,
                      TEXEL0_ALPHA, ENVIRONMENT, 0, 0, 0, 0);
    gDPSetRenderMode(p++, G_RM_ZB_OPA_SURF, G_RM_ZB_OPA_SURF2);
    gDPSetEnvColor(p++, 0, 0, 255, 255);
    gDPLoadTextureBlock_4b(p++, i4, G_IM_FMT_I, 4, 4, 0, 0, 0, 4, 4, 0, 0);
    gSPTexture(p++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gSPVertex(p++, vtx, 3, 0);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);

    assert(sTri3Count == 3);
    assert(sTri3State.texturePixels != NULL);
    assert(sTri3State.texFunc == 1);
    /* Half intensity keeps blue paint with a grey highlight and zero alpha */
    assert(sTri3State.texturePixels[0] == 0x0C44);

    p = sCommands;
    gDPSetCycleType(p++, G_CYC_2CYCLE);
    gDPSetCombineLERP(p++, TEXEL0, ENVIRONMENT, TEXEL0_ALPHA, ENVIRONMENT, 0, 0, 0, ENVIRONMENT,
                      COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);
    assert(sTri3State.texFunc == 0);
    assert(sTri3State.texturePixels[0] == 0xFC44);

    p = sCommands;
    gDPSetPrimColor(p++, 0, 0, 255, 255, 0, 255);
    gDPSetCombineLERP(p++, PRIMITIVE, ENVIRONMENT, TEXEL0, ENVIRONMENT, 0, 0, 0, ENVIRONMENT,
                      COMBINED, 0, SHADE, 0, 0, 0, 0, COMBINED);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);
    /* Stripe interpolates yellow and blue rather than texel and blue */
    assert(sTri3State.texturePixels[0] == 0xF788);

    {
        const u16* pixels = sTri3State.texturePixels;
        u32 decoded = sTexDecodeCount;

        p = sCommands;
        gSP1Triangle(p++, 0, 1, 2, 0);
        run(p);
        assert(sTexDecodeCount == decoded);
        assert(sTri3State.texturePixels == pixels);
    }

    p = sCommands;
    sProjectionPerspective = 0;
    sProjectionSet = 0;
    gSPClearGeometryMode(p++, G_ZBUFFER | G_SHADE | G_LIGHTING);
    run(p);
}

static void test_3d_material_tiles(void) {
    static u8 maps[8][512];
    static u16 rgba[16 * 16];
    static Vtx vtx[3];
    static const u32 bases[8] = { 0, 0x10, 0x50, 0x60, 0x70, 0xB0, 0xD0, 0x110 };
    static const u32 widths[8] = { 16, 16, 16, 16, 64, 16, 32, 32 };
    static const u32 heights[8] = { 16, 16, 16, 16, 16, 32, 32, 32 };
    static const u16 expected[8] = { 0x1111, 0xF00F, 0xF222, 0x4444, 0x5555, 0x6666, 0x7777, 0x8888 };
    Gfx* p = sCommands;
    u32 i, j;

    sProjectionSet = sProjectionPerspective = 1;
    psp_gfx_mat_identity(sProjection);
    psp_gfx_mat_identity(sModelView);
    for (i = 0; i < 8; i++) {
        for (j = 0; j < sizeof(maps[i]); j++) {
            maps[i][j] = (i + 1) * 0x11;
        }
        touch(maps[i], sizeof(maps[i]));
    }
    for (i = 0; i < 16 * 16; i++) {
        rgba[i] = 0xF801;
    }
    PspGfx_SetTextureByteOrder(rgba, sizeof(rgba), 1);
    gSPClearGeometryMode(p++, G_LIGHTING | G_TEXTURE_GEN);
    gSPSetGeometryMode(p++, G_ZBUFFER | G_SHADE);
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPSetCombineMode(p++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gSPVertex(p++, vtx, 3, 0);
    /* Reuse load tile seven for every material as the machine loader does */
    for (i = 0; i < 8; i++) {
        if (i == 1) {
            gDPLoadMultiBlock(p++, rgba, bases[i], i, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16,
                             0, G_TX_CLAMP, G_TX_CLAMP, 4, 4, 0, 0);
        } else {
            gDPLoadMultiBlock_4b(p++, maps[i], bases[i], i, i == 2 ? G_IM_FMT_IA : G_IM_FMT_I,
                                widths[i], heights[i], 0, G_TX_CLAMP, G_TX_CLAMP, 0, 0, 0, 0);
        }
    }
    for (i = 0; i < 16; i++) {
        gSPTexture(p++, 0xFFFF, 0xFFFF, 0, i & 7, G_ON);
        gSP1Triangle(p++, 0, 1, 2, 0);
    }
    run(p);
    assert(sTri3Calls == 16);
    for (i = 0; i < 16; i++) {
        const PspGfxDrawState* state = &sTri3States[i];

        assert(state->texturePixels != NULL);
        assert(state->texWidth == widths[i & 7]);
        assert(state->texHeight == heights[i & 7]);
        assert(state->texturePixels[0] == expected[i & 7]);
        assert(state->texturePixels[state->texWidth * state->texHeight - 1] == expected[i & 7]);
    }

    p = sCommands;
    /* Reload one TMEM region without changing the render tile descriptor */
    gDPSetTextureImage(p++, G_IM_FMT_I, G_IM_SIZ_16b, 1, maps[7]);
    gDPSetTile(p++, G_IM_FMT_I, G_IM_SIZ_16b, 0, bases[2], 7, 0, 0, 0, 0, 0, 0, 0);
    gDPLoadBlock(p++, 7, 0, 0, 63, CALC_DXT_4b(16));
    gSPTexture(p++, 0xFFFF, 0xFFFF, 0, 2, G_ON);
    gSP1Triangle(p++, 0, 1, 2, 0);
    gSPTexture(p++, 0xFFFF, 0xFFFF, 0, 1, G_ON);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);
    assert(sTri3States[0].texturePixels[0] == 0x0999);
    assert(sTri3States[1].texturePixels[0] == 0xF00F);
    sProjectionSet = sProjectionPerspective = 0;
}

static void test_3d_blend_color_outline(void) {
    static Vtx vertices[3];
    Gfx* p = sCommands;
    u32 decoded = sTexDecodeCount;
    int i;

    sProjectionSet = sProjectionPerspective = 1;
    psp_gfx_mat_identity(sProjection);
    psp_gfx_mat_identity(sModelView);
    for (i = 0; i < 3; i++) {
        vertices[i].v.cn[2] = 255;
        vertices[i].v.cn[3] = 255;
    }
    gSPClearGeometryMode(p++, G_LIGHTING | G_TEXTURE_GEN);
    gSPSetGeometryMode(p++, G_ZBUFFER | G_SHADE);
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPSetCombineMode(p++, G_CC_DECALRGBA, G_CC_DECALRGBA);
    gDPLoadTextureBlock(p++, sNative, G_IM_FMT_RGBA, G_IM_SIZ_16b, 16, 16, 0, 0, 0, 0, 0, 0, 0);
    gSPTexture(p++, 0xFFFF, 0xFFFF, 0, 0, G_ON);
    gDPSetBlendColor(p++, 0, 0, 0, 255);
    /* The enlarged tuning silhouette selects the constant blend colour */
    gDPSetRenderMode(p++, Z_CMP | CVG_DST_FULL | ZMODE_OPA |
                             GBL_c1(G_BL_CLR_BL, G_BL_0, G_BL_CLR_BL, G_BL_1),
                    Z_CMP | CVG_DST_FULL | ZMODE_OPA |
                             GBL_c2(G_BL_CLR_BL, G_BL_0, G_BL_CLR_BL, G_BL_1));
    gSPVertex(p++, vertices, 3, 0);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);
    assert(sTri3Calls == 1);
    assert(sTexDecodeCount == decoded);
    assert(sTri3State.texturePixels == NULL);
    assert(sTri3State.depthTest == 1 && sTri3State.depthWrite == 0);
    assert(sTri3State.blend == 0);
    for (i = 0; i < 3; i++) {
        assert(sTri3[i].color == 0xFF000000u);
    }

    p = sCommands;
    gDPSetCycleType(p++, G_CYC_2CYCLE);
    gDPSetBlendColor(p++, 24, 48, 96, 255);
    gDPSetRenderMode(p++, G_RM_PASS, Z_CMP | CVG_DST_FULL | ZMODE_OPA |
                         GBL_c2(G_BL_CLR_IN, G_BL_0, G_BL_CLR_BL, G_BL_1));
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);
    assert(sTri3[0].color == 0xFF603018u);
    assert(sTri3State.texturePixels == NULL);
    assert(sTri3State.blend == 0);

    p = sCommands;
    gDPSetCycleType(p++, G_CYC_1CYCLE);
    gDPSetRenderMode(p++, G_RM_ZB_OPA_SURF, G_RM_ZB_OPA_SURF2);
    gSP1Triangle(p++, 0, 1, 2, 0);
    run(p);
    /* The following body draw must recover its own material and shade */
    assert(sTri3State.texturePixels != NULL);
    assert(sTri3[0].color == 0xFFFF0000u);
    assert(sTri3State.depthWrite == 1);
    sProjectionSet = sProjectionPerspective = 0;
}

static void test_mtx_decode(void) {
    Mtx m;
    float mf[4][4];
    int i;
    int j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            m.u.i[i][j] = 0;
            m.u.f[i][j] = 0;
        }
    }
    /* Element-wise s15.16: integer part in u.i, fraction in u.f */
    m.u.i[0][0] = 2;
    m.u.f[0][0] = 0x8000; /* 2.5 */
    m.u.i[1][2] = 0xFFFE; /* -2 */
    m.u.f[1][2] = 0xC000; /* -1.25 */
    m.u.i[3][3] = 1;

    psp_gfx_mtx_l2f(mf, &m);
    assert(mf[0][0] == 2.5f);
    assert(mf[1][2] == -1.25f);
    assert(mf[3][3] == 1.0f);
    assert(mf[0][1] == 0.0f);
    assert(mf[1][0] == 0.0f);
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
    test_3d_triangle();
    test_perspective_y();
    test_3d_no_zbuffer();
    test_3d_texgen();
    test_3d_two_cycle_texture();
    test_3d_mirror_wrap();
    test_3d_uv_scale();
    test_3d_env_lerp_bake();
    test_3d_material_tiles();
    test_3d_blend_color_outline();
    test_mtx_decode();
    test_boot_framebuffer();
    puts("PSP HUD display list tests passed");
    return 0;
}
