#include <pspdisplay.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspkernel.h>
#include <math.h>
#include <string.h>
#include <n64psp/clip.h>

#include "src/psp/gfx/gfx_psp.h"
#include "src/psp/psp.h"

#define GU_LIST_WORDS 0x40000
#define GU_FB_WIDTH 512
#define GU_FB_HEIGHT 272
#define GU_FB_STRIDE (GU_FB_WIDTH * 2)
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
static const volatile u16* sLastFrame;

/* Cached 3D draw state */
static int sViewportWide = -1;
static int sMatrixValid;
static const float* sProjection;
static const float* sModelview;
static u32 sProjectionSerial;
static u32 sModelviewSerial;
static n64psp_mat4f sClipProjection;
static n64psp_mat4f sClipModelview;
static int sDepthTestEnabled;
static int sDepthFunction = -1;
static int sDepthWriteEnabled;
static int sCullEnabled;
static int sCullFront;
static int sShadeModel = -1;

/* Cached 3D texture binding */
static int sTexBoundValid;
static const u16* sTexBoundPixels;
static s32 sTexBoundWidth;
static s32 sTexBoundHeight;
static int sTexBoundTexFunc;
static u32 sTexBoundWrap;
static int sTexBoundFilter;

static const ScePspFMatrix4 sIdentityMatrix __attribute__((aligned(16))) = {
    { 1.0f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.0f, 0.0f, 0.0f, 1.0f },
};

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

/* Full-width viewport for 2D, pillarboxed 320x240 viewport for 3D */
static void psp_gfx_gu_viewport(int wide) {
    int width;

    if (sViewportWide == wide) {
        return;
    }
    width = wide ? (int) PSP_SCREEN_W : (int) (N64_SCREEN_W * s2dScaleX + 0.5f);
    sceGuViewport(2048, 2048, width, (int) PSP_SCREEN_H);
    if (wide) {
        sceGuDisable(GU_CLIP_PLANES);
    } else {
        sceGuEnable(GU_CLIP_PLANES);
    }
    sViewportWide = wide;
}

/* 2D overlays must not inherit the 3D depth state */
static void psp_gfx_gu_2d_state(void) {
    psp_gfx_gu_viewport(1);
    if (sDepthTestEnabled) {
        sceGuDisable(GU_DEPTH_TEST);
        sDepthTestEnabled = 0;
    }
    if (sDepthWriteEnabled) {
        sceGuDepthMask(GU_TRUE);
        sDepthWriteEnabled = 0;
    }
    /* 2D texture draws bypass the 3D bind cache */
    sTexBoundValid = 0;
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
    sceGuDrawBuffer(GU_PSM_5650, (void*) GU_DRAW_OFFSET, GU_FB_WIDTH);
    sceGuDispBuffer(480, 272, (void*) GU_DISP_OFFSET, GU_FB_WIDTH);
    sceGuDepthBuffer((void*) GU_DEPTH_OFFSET, GU_FB_WIDTH);
    sceGuOffset(2048 - 240, 2048 - 136);
    sceGuViewport(2048, 2048, 480, 272);
    sceGuScissor(0, 0, 480, 272);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuDisable(GU_DEPTH_TEST);
    sceGuDepthFunc(GU_GEQUAL);
    sceGuDepthRange(65535, 0);
    sceGuClearDepth(0);
    sceGuDepthMask(GU_TRUE);
    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_TEXTURE_2D);
    sceGuDisable(GU_BLEND);
    sceGuDisable(GU_CLIP_PLANES);
    sceGuFinish();
    sceGuSync(0, 0);

    sViewportWide = 1;
    sMatrixValid = 0;
    sDepthTestEnabled = 0;
    sDepthFunction = GU_GEQUAL;
    /* BeginFrame issues GU_TRUE; track writes as disabled */
    sDepthWriteEnabled = 0;
    sCullEnabled = 0;
    sCullFront = 0;

    sGuReady = 1;
    return 1;
}

void PspGfx_BeginFrame(void) {
    sceGuStart(GU_DIRECT, sGuList);
    psp_gfx_gu_viewport(1);
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
    sMatrixValid = 0;
    sDepthTestEnabled = 0;
    sDepthFunction = -1;
    /* sceGuDepthMask(GU_TRUE) disabled writes above */
    sDepthWriteEnabled = 0;
    sCullEnabled = 0;
    sCullFront = 0;
    sShadeModel = -1;
}

void PspGfx_Present(void) {
    if (!sGuReady) {
        return;
    }

    sceGuFinish();
    sceGuSync(0, 0);
    /* The GU is done with the decoded tiles queued this frame */
    PspGfx_EndFrame();
    sceDisplayWaitVblankStart();
    sLastFrame = (const volatile u16*) ((u32) sceGeEdramGetAddr() | 0x40000000u | sDrawOffset);
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

    psp_gfx_gu_2d_state();
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
    psp_gfx_gu_2d_state();
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

    psp_gfx_gu_2d_state();
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

static void psp_gfx_gu_submit(const PspGfxVertex3D* vertices, u32 count) {
    PspGfxVertex3D* out;

    out = (PspGfxVertex3D*) sceGuGetMemory(count * sizeof(PspGfxVertex3D));
    if (out == NULL) {
        return;
    }
    memcpy(out, vertices, count * sizeof(*out));
    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, count,
                   NULL, out);
}

static PspGfxVertex3D psp_gfx_gu_interpolate(const PspGfxVertex3D vertices[3], const float weight[3], int smooth) {
    PspGfxVertex3D out;
    u32 channel;

    out.x = vertices[0].x * weight[0] + vertices[1].x * weight[1] + vertices[2].x * weight[2];
    out.y = vertices[0].y * weight[0] + vertices[1].y * weight[1] + vertices[2].y * weight[2];
    out.z = vertices[0].z * weight[0] + vertices[1].z * weight[1] + vertices[2].z * weight[2];
    out.u = vertices[0].u * weight[0] + vertices[1].u * weight[1] + vertices[2].u * weight[2];
    out.v = vertices[0].v * weight[0] + vertices[1].v * weight[1] + vertices[2].v * weight[2];
    out.color = vertices[2].color;
    if (smooth) {
        out.color = 0;
        for (channel = 0; channel < 32; channel += 8) {
            float value = ((vertices[0].color >> channel) & 255) * weight[0] +
                          ((vertices[1].color >> channel) & 255) * weight[1] +
                          ((vertices[2].color >> channel) & 255) * weight[2];

            value = value < 0 ? 0 : value > 255 ? 255 : value;
            out.color |= (u32) (value + 0.5f) << channel;
        }
    }
    return out;
}

static void psp_gfx_gu_clip_triangle(const PspGfxVertex3D vertices[3], int smooth) {
    n64psp_vec4f positions[3];
    n64psp_vec4f_pair transformed[3];
    n64psp_vec4f clip[3];
    u32 shared = 63;
    int safe = 1;
    u32 i;

    for (i = 0; i < 3; i++) {
        positions[i].x = vertices[i].x;
        positions[i].y = vertices[i].y;
        positions[i].z = vertices[i].z;
        positions[i].w = 1.0f;
    }
    n64psp_mat4f_transform_vec4_chain2_batch(transformed, &sClipModelview, &sClipProjection, positions, 3);
    for (i = 0; i < 3; i++) {
        u32 code;

        clip[i] = transformed[i].second;
        if (!isfinite(clip[i].x) || !isfinite(clip[i].y) || !isfinite(clip[i].z) || !isfinite(clip[i].w)) {
            return;
        }
        code = (clip[i].x < -clip[i].w) | ((clip[i].x > clip[i].w) << 1) |
               ((clip[i].y < -clip[i].w) << 2) | ((clip[i].y > clip[i].w) << 3) |
               ((clip[i].z < -clip[i].w) << 4) | ((clip[i].z > clip[i].w) << 5);
        shared &= code;
        /* Eight viewport half widths leave room inside the GE coordinate limit */
        if (clip[i].w <= 0 || fabsf(clip[i].x) > 8.0f * clip[i].w ||
            fabsf(clip[i].y) > 8.0f * clip[i].w || clip[i].z > clip[i].w) {
            safe = 0;
        }
    }
    if (shared != 0) {
        return;
    }
    if (safe) {
        /* The GE clips the near plane and keeps perspective interpolation */
        psp_gfx_gu_submit(vertices, 3);
    } else {
        n64psp_clip_vertex clipped[N64PSP_CLIP_MAX_VERTICES];
        PspGfxVertex3D polygon[N64PSP_CLIP_MAX_VERTICES];
        PspGfxVertex3D triangles[(N64PSP_CLIP_MAX_VERTICES - 2) * 3];
        size_t count = n64psp_clip_triangle(clipped, clip);
        u32 emitted = 0;

        for (i = 0; i < count; i++) {
            if (!(clipped[i].clip.w > 0)) {
                return;
            }
            polygon[i] = psp_gfx_gu_interpolate(vertices, clipped[i].weight, smooth);
        }
        for (i = 1; i + 1 < count; i++) {
            triangles[emitted++] = polygon[0];
            triangles[emitted++] = polygon[i];
            triangles[emitted++] = polygon[i + 1];
        }
        if (emitted != 0) {
            psp_gfx_gu_submit(triangles, emitted);
        }
    }
}

void PspGfx_DrawTriangles3D(const PspGfxVertex3D* vertices, u32 count, const PspGfxDrawState* state) {
    int shadeModel;
    int depthEnabled;
    int depthFunction;
    u32 i;

    if (!sGuReady || vertices == NULL || count == 0 || (count % 3) != 0 || state == NULL ||
        state->projection == NULL || (state->cullFront && state->cullBack)) {
        return;
    }

    psp_gfx_gu_viewport(0);
    shadeModel = state->smoothShade ? GU_SMOOTH : GU_FLAT;
    if (sShadeModel != shadeModel) {
        sceGuShadeModel(shadeModel);
        sShadeModel = shadeModel;
    }

    if (!sMatrixValid || sProjection != state->projection || sProjectionSerial != state->projectionSerial) {
        sceGuSetMatrix(GU_PROJECTION, (const ScePspFMatrix4*) state->projection);
        sProjection = state->projection;
        sProjectionSerial = state->projectionSerial;
        memcpy(&sClipProjection, state->projection, sizeof(sClipProjection));
    }
    if (!sMatrixValid) {
        sceGuSetMatrix(GU_VIEW, &sIdentityMatrix);
    }
    if (!sMatrixValid || sModelview != state->modelview || sModelviewSerial != state->modelviewSerial) {
        sceGuSetMatrix(GU_MODEL, state->modelview != NULL ? (const ScePspFMatrix4*) state->modelview
                                                          : &sIdentityMatrix);
        sModelview = state->modelview;
        sModelviewSerial = state->modelviewSerial;
        memcpy(&sClipModelview, state->modelview != NULL ? state->modelview : (const float*) &sIdentityMatrix,
               sizeof(sClipModelview));
    }
    sMatrixValid = 1;

    /* GE depth writes require an enabled test even when comparison is off */
    depthEnabled = state->depthTest || state->depthWrite;
    depthFunction = state->depthTest ? GU_GEQUAL : GU_ALWAYS;
    if (sDepthTestEnabled != depthEnabled) {
        if (depthEnabled) {
            sceGuEnable(GU_DEPTH_TEST);
        } else {
            sceGuDisable(GU_DEPTH_TEST);
        }
        sDepthTestEnabled = depthEnabled;
    }
    if (depthEnabled && sDepthFunction != depthFunction) {
        sceGuDepthFunc(depthFunction);
        sDepthFunction = depthFunction;
    }
    if (sDepthWriteEnabled != (state->depthWrite != 0)) {
        sceGuDepthMask(state->depthWrite ? GU_FALSE : GU_TRUE);
        sDepthWriteEnabled = state->depthWrite != 0;
    }

    if (state->cullFront || state->cullBack) {
        if (!sCullEnabled) {
            sceGuEnable(GU_CULL_FACE);
            sCullEnabled = 1;
        }
        if (sCullFront != (state->cullFront != 0)) {
            /* Map N64 cull mode onto GU front-face winding */
            sceGuFrontFace(state->cullBack ? GU_CCW : GU_CW);
            sCullFront = state->cullFront != 0;
        }
    } else if (sCullEnabled) {
        sceGuDisable(GU_CULL_FACE);
        sCullEnabled = 0;
    }

    sceGuDisable(GU_ALPHA_TEST);

    if (state->blend) {
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    } else {
        sceGuDisable(GU_BLEND);
    }

    if (state->texturePixels != NULL && state->texWidth > 0 && state->texHeight > 0) {
        u32 wrap = (u32) ((state->wrapS != 0) | ((state->wrapT != 0) << 1));
        int filter = state->filter != 0;
        int texFunc = state->texFunc;

        if (!sTexBoundValid || sTexBoundPixels != state->texturePixels ||
            sTexBoundWidth != state->texWidth || sTexBoundHeight != state->texHeight ||
            sTexBoundTexFunc != texFunc || sTexBoundWrap != wrap || sTexBoundFilter != filter) {
            sceGuEnable(GU_TEXTURE_2D);
            sceGuTexMode(GU_PSM_4444, 0, 0, 0);
            sceKernelDcacheWritebackRange((void*) state->texturePixels,
                                          (unsigned) (state->texWidth * state->texHeight * (s32) sizeof(u16)));
            sceGuTexImage(0, state->texWidth, state->texHeight, state->texWidth, state->texturePixels);
            /* The interpreter supplies texels and the GE expects normalized coordinates */
            sceGuTexScale(1.0f / state->texWidth, 1.0f / state->texHeight);
            sceGuTexOffset(0.0f, 0.0f);
            sceGuTexFlush();
            sceGuTexFunc(texFunc ? GU_TFX_REPLACE : GU_TFX_MODULATE, GU_TCC_RGBA);
            sceGuTexFilter(filter ? GU_LINEAR : GU_NEAREST, filter ? GU_LINEAR : GU_NEAREST);
            sceGuTexWrap(state->wrapS ? GU_CLAMP : GU_REPEAT, state->wrapT ? GU_CLAMP : GU_REPEAT);
            sTexBoundPixels = state->texturePixels;
            sTexBoundWidth = state->texWidth;
            sTexBoundHeight = state->texHeight;
            sTexBoundTexFunc = texFunc;
            sTexBoundWrap = wrap;
            sTexBoundFilter = filter;
            sTexBoundValid = 1;
        }
    } else {
        sceGuDisable(GU_TEXTURE_2D);
        sTexBoundValid = 0;
    }

    for (i = 0; i < count; i += 3) {
        psp_gfx_gu_clip_triangle(vertices + i, state->smoothShade);
    }
}

int PspGfx_ReadFramebuffer(u16* pixels, s32 width, s32 height) {
    s32 x, y;
    const volatile u16* frame = sLastFrame;

    if (frame == NULL || pixels == NULL || width != 320 || height != 240)
        return 0;
    for (y = 0; y < height; y++) {
        s32 sy = psp_gfx_map_y((float) y + 0.5f);

        if (sy >= GU_FB_HEIGHT)
            sy = GU_FB_HEIGHT - 1;
        for (x = 0; x < width; x++) {
            s32 sx = psp_gfx_map_x((float) x + 0.5f);
            u32 color = frame[sy * GU_FB_WIDTH + sx];
            /* PSP 5650 keeps R in the low bits, B in the high bits */
            u32 r = color & 0x1F;
            u32 g = (color >> 5) & 0x3F;
            u32 b = (color >> 11) & 0x1F;

            pixels[y * width + x] = (u16) ((r << 11) | ((g >> 1) << 6) | (b << 1) | 1);
        }
    }
    return 1;
}
