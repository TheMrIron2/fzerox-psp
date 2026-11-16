#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <psptypes.h>

#define _ULTRATYPES_H_
#include "src/psp/gfx/gfx_psp_backend_gu.c"

static u16 sTestFrame[512 * 272];
static u16 sReadback[320 * 240];
static u8 sVertexMemory[2048] __attribute__((aligned(16)));
static int sDrawCalls;
static int sSubmittedCount;
static int sClipPlanes;
static float sScaleU;
static float sScaleV;
static int sScaleCalls;
static int sTestShadeModel = -1;
static int sShadeCalls;
static int sTestDepthEnabled;
static int sTestDepthFunction = -1;
static int sTestDepthMask = GU_TRUE;
static int sDepthFunctionCalls;
static int sDepthPixelActive;
static float sPixelDepth;
static u32 sPixelColor;
static int sCaptureReflection;
static int sReflectionDraws;
static u32 sReflectionColors[2];
static int sReflectionDepthMasks[2];
static int sReflectionTextureEnabled;
static int sReflectionTextureStates[2];
static int sReflectionBlendEnabled;
static int sReflectionBlendStates[2];

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
    (void) prim; (void) indices;
    sDrawCalls++;
    sSubmittedCount = count;
    if (sCaptureReflection) {
        assert(sReflectionDraws < 2 && count == 3);
        sReflectionColors[sReflectionDraws] = ((const PspGfxVertex3D*) vertices)[0].color;
        sReflectionDepthMasks[sReflectionDraws] = sTestDepthMask;
        sReflectionTextureStates[sReflectionDraws] = sReflectionTextureEnabled;
        sReflectionBlendStates[sReflectionDraws++] = sReflectionBlendEnabled;
    }
    if (sDepthPixelActive && !(type & GU_TRANSFORM_2D)) {
        const PspGfxVertex3D* triangle = vertices;
        float depth = (1.0f - triangle[0].z) * 0.5f;
        int passes = !sTestDepthEnabled || sTestDepthFunction == GU_ALWAYS ||
                     (sTestDepthFunction == GU_GEQUAL && depth >= sPixelDepth);

        assert(count == 3 && triangle[0].z == triangle[1].z && triangle[1].z == triangle[2].z);
        if (passes) {
            sPixelColor = triangle[0].color;
            if (sTestDepthEnabled && sTestDepthMask == GU_FALSE) sPixelDepth = depth;
        }
    }
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
void sceGuEnable(int state) {
    if (state == GU_CLIP_PLANES) sClipPlanes = 1;
    if (state == GU_DEPTH_TEST) sTestDepthEnabled = 1;
    if (state == GU_TEXTURE_2D) sReflectionTextureEnabled = 1;
    if (state == GU_BLEND) sReflectionBlendEnabled = 1;
}
void sceGuDisable(int state) {
    if (state == GU_CLIP_PLANES) sClipPlanes = 0;
    if (state == GU_DEPTH_TEST) sTestDepthEnabled = 0;
    if (state == GU_TEXTURE_2D) sReflectionTextureEnabled = 0;
    if (state == GU_BLEND) sReflectionBlendEnabled = 0;
}
void sceGuDepthFunc(int function) {
    sTestDepthFunction = function;
    sDepthFunctionCalls++;
}
void sceGuDepthMask(int mask) { sTestDepthMask = mask; }
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
    assert(sClipPlanes);
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
    assert(!sClipPlanes);
    assert(((PspGuTexVertex*) sVertexMemory)[1].u == 16.0f);
    calls = sScaleCalls;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sScaleCalls == calls + 1);
    assert(submitted[0].u * sScaleU == 0.25f);
    assert(submitted[0].v * sScaleV == 2.0f);
}

static void set_clip_attributes(PspGfxVertex3D vertices[3]) {
    int i;

    for (i = 0; i < 3; i++) {
        vertices[i].u = 10.0f + 2.0f * vertices[i].x;
        vertices[i].v = 20.0f + 3.0f * vertices[i].y;
        vertices[i].color = 0xFF000000u | (0xFFu << (i * 8));
    }
}

static void check_clipped_vertices(void) {
    const PspGfxVertex3D* vertices = (const PspGfxVertex3D*) sVertexMemory;
    int i;

    assert(sSubmittedCount >= 3 && sSubmittedCount <= 21 && sSubmittedCount % 3 == 0);
    for (i = 0; i < sSubmittedCount; i++) {
        n64psp_vec4f position = { vertices[i].x, vertices[i].y, vertices[i].z, 1 };
        n64psp_vec4f view;
        n64psp_vec4f clip;
        float margin;

        n64psp_mat4f_transform_vec4(&view, &sClipModelview, &position);
        n64psp_mat4f_transform_vec4(&clip, &sClipProjection, &view);
        margin = 0.0001f * clip.w;
        assert(clip.w > 0);
        assert(fabsf(clip.x) <= clip.w + margin);
        assert(fabsf(clip.y) <= clip.w + margin);
        assert(fabsf(clip.z) <= clip.w + margin);
        assert(fabsf(vertices[i].u - (10.0f + 2.0f * vertices[i].x)) < 0.001f);
        assert(fabsf(vertices[i].v - (20.0f + 3.0f * vertices[i].y)) < 0.001f);
    }
}

static void test_3d_clipping(void) {
    PspGfxDrawState state;
    PspGfxVertex3D vertices[3] = { { 0 } };
    PspGfxVertex3D* submitted = (PspGfxVertex3D*) sVertexMemory;
    n64psp_mat4f projection;
    n64psp_mat4f modelview;
    int calls;
    int i;

    memset(&state, 0, sizeof(state));
    memcpy(&projection, &sIdentityMatrix, sizeof(projection));
    memcpy(&modelview, &sIdentityMatrix, sizeof(modelview));
    state.projection = &projection.m[0][0];
    state.modelview = &modelview.m[0][0];
    state.smoothShade = 1;
    sMatrixValid = 0;
    vertices[0].x = vertices[0].y = -100;
    vertices[1].x = 100;
    vertices[1].y = -100;
    vertices[2].y = 100;
    set_clip_attributes(vertices);
    calls = sDrawCalls;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sClipPlanes && sDrawCalls == calls + 1 && sSubmittedCount == 6);
    check_clipped_vertices();
    for (i = 0; i < sSubmittedCount; i++) {
        u32 color = submitted[i].color;
        u32 sum = (color & 255) + ((color >> 8) & 255) + ((color >> 16) & 255);
        assert((color >> 24) == 255 && sum >= 254 && sum <= 256);
    }
    for (i = 0; i < sSubmittedCount; i += 3) {
        float area = (submitted[i + 1].x - submitted[i].x) * (submitted[i + 2].y - submitted[i].y) -
                     (submitted[i + 1].y - submitted[i].y) * (submitted[i + 2].x - submitted[i].x);
        assert(area > 0);
    }
    state.smoothShade = 0;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    for (i = 0; i < sSubmittedCount; i++) assert(submitted[i].color == vertices[2].color);

    vertices[0].x = -100;
    vertices[0].y = 0;
    vertices[1].x = vertices[2].x = 0.5f;
    vertices[1].y = -0.1f;
    vertices[2].y = 0.1f;
    set_clip_attributes(vertices);
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sSubmittedCount == 6);
    check_clipped_vertices();

    for (i = 0; i < 3; i++) vertices[i].x = 2.0f;
    calls = sDrawCalls;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sDrawCalls == calls);
    vertices[0].x = 4;
    vertices[1].x = vertices[2].x = 0;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sSubmittedCount == 3 && submitted[0].x == 4);

    projection.m[2][2] = -101.0f / 99.0f;
    projection.m[2][3] = -1;
    projection.m[3][2] = -200.0f / 99.0f;
    projection.m[3][3] = 0;
    state.projectionSerial++;
    vertices[0].x = 0;
    vertices[0].y = 1;
    vertices[0].z = 1;
    vertices[1].x = -2;
    vertices[1].y = -1;
    vertices[1].z = -10;
    vertices[2].x = 2;
    vertices[2].y = -1;
    vertices[2].z = -10;
    state.smoothShade = 1;
    set_clip_attributes(vertices);
    calls = sDrawCalls;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sDrawCalls == calls + 1);
    check_clipped_vertices();
    assert(submitted[0].z < -0.99f);

    vertices[0].z = -0.5f;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sClipPlanes && sSubmittedCount == 3 && submitted[0].z == -0.5f);
    vertices[0].z = -200;
    calls = sDrawCalls;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sDrawCalls == calls + 1);
    check_clipped_vertices();

    modelview.m[3][0] = 20;
    state.modelviewSerial++;
    calls = sDrawCalls;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sDrawCalls == calls + 1);
    check_clipped_vertices();
    for (i = 0; i < 3; i++) vertices[i].z = 1;
    calls = sDrawCalls;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sDrawCalls == calls);
    vertices[0].x = NAN;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    assert(sDrawCalls == calls);
}

static void draw_depth_layer(PspGfxDrawState* state, float z, u32 color) {
    PspGfxVertex3D vertices[3] = {
        { 0, 0, color, -0.5f, -0.5f, z },
        { 0, 0, color, 0.5f, -0.5f, z },
        { 0, 0, color, 0, 0.5f, z },
    };

    PspGfx_DrawTriangles3D(vertices, 3, state);
}

static void test_ordered_road_depth(void) {
    PspGfxDrawState state;
    int functionCalls;

    memset(&state, 0, sizeof(state));
    state.projection = (const float*) &sIdentityMatrix;
    sMatrixValid = 0;
    sPixelDepth = 0;
    sPixelColor = 0;
    sDepthPixelActive = 1;

    /* Sorted road chunks write depth while preserving submission order */
    state.depthWrite = 1;
    draw_depth_layer(&state, 0.5f, 0xFF0000FF);
    assert(sTestDepthEnabled && sTestDepthFunction == GU_ALWAYS && sTestDepthMask == GU_FALSE);
    assert(sPixelDepth == 0.25f && sPixelColor == 0xFF0000FF);
    functionCalls = sDepthFunctionCalls;
    draw_depth_layer(&state, 0, 0xFF00FF00);
    assert(sPixelDepth == 0.5f && sPixelColor == 0xFF00FF00);
    assert(sDepthFunctionCalls == functionCalls);

    /* A building behind the road fails its requested depth comparison */
    state.depthTest = 1;
    draw_depth_layer(&state, 0.25f, 0xFFFF0000);
    assert(sTestDepthEnabled && sTestDepthFunction == GU_GEQUAL);
    assert(sDepthFunctionCalls == functionCalls + 1);
    assert(sPixelDepth == 0.5f && sPixelColor == 0xFF00FF00);
    draw_depth_layer(&state, -0.25f, 0xFFFF0000);
    assert(sPixelDepth == 0.625f && sPixelColor == 0xFFFF0000);

    state.depthTest = 0;
    draw_depth_layer(&state, 0.5f, 0xFF0000FF);
    assert(sTestDepthFunction == GU_ALWAYS);
    assert(sPixelDepth == 0.25f && sPixelColor == 0xFF0000FF);

    state.depthTest = 1;
    state.depthWrite = 0;
    draw_depth_layer(&state, -0.5f, 0xFFFFFF00);
    assert(sTestDepthEnabled && sTestDepthFunction == GU_GEQUAL && sTestDepthMask == GU_TRUE);
    assert(sPixelDepth == 0.25f && sPixelColor == 0xFFFFFF00);

    state.depthTest = 0;
    draw_depth_layer(&state, 0.75f, 0xFFFF00FF);
    assert(!sTestDepthEnabled && sTestDepthMask == GU_TRUE);
    assert(sPixelDepth == 0.25f && sPixelColor == 0xFFFF00FF);

    state.depthWrite = 1;
    draw_depth_layer(&state, 0, 0xFF00FF00);
    PspGfx_DrawTexQuad(0, 0, 16, 16, sTestFrame, 16, 16, 0, 0, 16, 16, 0xFFFF, 0, 0, 0);
    assert(!sTestDepthEnabled && sTestDepthMask == GU_TRUE);
    state.depthTest = 1;
    draw_depth_layer(&state, 0.25f, 0xFFFF0000);
    assert(sTestDepthEnabled && sTestDepthFunction == GU_GEQUAL && sTestDepthMask == GU_FALSE);
    assert(sPixelDepth == 0.5f && sPixelColor == 0xFF00FF00);
    sDepthPixelActive = 0;
}

static void test_reflection_pass(void) {
    PspGfxDrawState state;
    PspGfxVertex3D vertices[3] = {
        { 0, 0, 0x4d204060, -0.5f, -0.5f, 0 },
        { 0, 0, 0x80204060, 0.5f, -0.5f, 0 },
        { 0, 0, 0xff204060, 0, 0.5f, 0 },
    };
    memset(&state, 0, sizeof(state));
    state.projection = (const float*) &sIdentityMatrix;
    state.texturePixels = sTestFrame;
    state.texWidth = state.texHeight = 16;
    state.depthTest = state.depthWrite = state.smoothShade = 1;
    state.reflection = 1;
    state.reflectionColor = 0xff785028;
    sReflectionDraws = 0;
    sCaptureReflection = 1;
    PspGfx_DrawTriangles3D(vertices, 3, &state);
    sCaptureReflection = 0;
    assert(sReflectionDraws == 2);
    assert(sReflectionColors[0] == 0x4d204060 && sReflectionColors[1] == 0x4d785028);
    assert(sReflectionTextureStates[0] && !sReflectionTextureStates[1]);
    assert(!sReflectionBlendStates[0] && sReflectionBlendStates[1]);
    assert(sReflectionDepthMasks[0] == GU_FALSE && sReflectionDepthMasks[1] == GU_TRUE);
    assert(sTestDepthMask == GU_FALSE);
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
    test_3d_clipping();
    test_ordered_road_depth();
    test_reflection_pass();
    puts("PSP GU framebuffer texture coordinate clipping and depth tests passed");
    return 0;
}
