#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <psptypes.h>

#define _ULTRATYPES_H_
#include "src/psp/gfx/gfx_psp_backend_gu.c"

static u16 sTestFrame[512 * 272];
static u16 sReadback[320 * 240];
static u8 sVertexMemory[512] __attribute__((aligned(16)));
static float sScaleU;
static float sScaleV;
static int sScaleCalls;
static int sTestShadeModel = -1;
static int sShadeCalls;

void sceGuShadeModel(int mode) {
    sTestShadeModel = mode;
    sShadeCalls++;
}

void sceGuTexScale(float u, float v) {
    sScaleU = u;
    sScaleV = v;
    sScaleCalls++;
}

void sceGuTexOffset(float u, float v) {
    assert(u == 0.0f && v == 0.0f);
}

void* sceGuGetMemory(int size) {
    assert(size <= (int) sizeof(sVertexMemory));
    return sVertexMemory;
}

void sceGuDrawArray(int prim, int type, int count, const void* indices, const void* vertices) {
    (void) prim; (void) type; (void) count; (void) indices; (void) vertices;
}

void sceGuSetMatrix(int type, const ScePspFMatrix4* matrix) {
    (void) type; (void) matrix;
}

void sceKernelDcacheWritebackRange(const void* data, unsigned int size) {
    (void) data; (void) size;
}

void sceGuTexImage(int level, int width, int height, int stride, const void* pixels) {
    (void) level; (void) width; (void) height; (void) stride; (void) pixels;
}

void sceGuTexMode(int format, int levels, int mc, int swizzle) {
    (void) format; (void) levels; (void) mc; (void) swizzle;
}

void sceGuTexFunc(int function, int components) {
    (void) function; (void) components;
}

void sceGuTexFilter(int min, int mag) { (void) min; (void) mag; }
void sceGuTexWrap(int s, int t) { (void) s; (void) t; }
void sceGuTexFlush(void) {}
void sceGuEnable(int state) { (void) state; }
void sceGuDisable(int state) { (void) state; }
void sceGuDepthFunc(int function) { (void) function; }
void sceGuDepthMask(int mask) { (void) mask; }
void sceGuFrontFace(int order) { (void) order; }
void sceGuViewport(int x, int y, int width, int height) {
    (void) x; (void) y; (void) width; (void) height;
}
void sceGuBlendFunc(int op, int src, int dst, unsigned int srcfix, unsigned int dstfix) {
    (void) op; (void) src; (void) dst; (void) srcfix; (void) dstfix;
}

static void test_3d_texture_units(void) {
    PspGfxDrawState state;
    PspGfxVertex3D vertices[3];
    PspGfxVertex3D* submitted = (PspGfxVertex3D*) sVertexMemory;
    int calls;
    int shadeCalls;

    memset(&state, 0, sizeof(state));
    memset(vertices, 0, sizeof(vertices));
    state.projection = (const float*) &sIdentityMatrix;
    state.texturePixels = sTestFrame;
    state.texWidth = 16;
    state.texHeight = 32;
    state.smoothShade = 1;
    vertices[0].u = 16.0f;
    vertices[0].v = 32.0f;
    sGuReady = 1;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sTestShadeModel == GU_SMOOTH);
    shadeCalls = sShadeCalls;
    /* One image width and height must span one repeat in the GE */
    assert(submitted[0].u * sScaleU == 1.0f);
    assert(submitted[0].v * sScaleV == 1.0f);
    calls = sScaleCalls;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sScaleCalls == calls);
    assert(sShadeCalls == shadeCalls);

    state.smoothShade = 0;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sTestShadeModel == GU_FLAT);
    assert(sShadeCalls == shadeCalls + 1);
    state.smoothShade = 1;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sTestShadeModel == GU_SMOOTH);
    assert(sShadeCalls == shadeCalls + 2);

    state.texWidth = 64;
    state.texHeight = 16;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(submitted[0].u * sScaleU == 0.25f);
    assert(submitted[0].v * sScaleV == 2.0f);

    PspGfx_DrawTexQuad(0, 0, 16, 32, sTestFrame, 16, 32, 0, 0, 16, 32, 0xFFFF, 0, 0, 0);
    assert(((PspGuTexVertex*) sVertexMemory)[1].u == 16.0f);
    calls = sScaleCalls;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sScaleCalls == calls + 1);
    assert(submitted[0].u * sScaleU == 0.25f);
    assert(submitted[0].v * sScaleV == 2.0f);
}

int main(void) {
    int x, y;

    assert(!PspGfx_ReadFramebuffer(sReadback, 320, 240));
    s2dScaleX = s2dScaleY = 272.0f / 240.0f;
    s2dOffsetX = (480.0f - 320.0f * s2dScaleX) * 0.5f;
    for (y = 0; y < 272; y++) {
        for (x = 0; x < 512; x++) {
            sTestFrame[y * 512 + x] = x < 240 ? 0x001F : 0x07E0;
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
        sTestFrame[y] = 0xF800;
    assert(PspGfx_ReadFramebuffer(sReadback, 320, 240));
    assert(sReadback[0] == 0x003F);
    test_3d_texture_units();
    puts("PSP GU framebuffer and texture coordinate tests passed");
    return 0;
}
