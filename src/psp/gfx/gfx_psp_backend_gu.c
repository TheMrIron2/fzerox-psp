#include <pspdisplay.h>
#include <pspgu.h>
#include <pspge.h>
#include <pspkernel.h>
#include <math.h>
#include <string.h>
#include <n64psp/clip.h>

#include "src/psp/gfx/gfx_psp.h"
#include "src/psp/psp.h"
#include "src/psp/overlay.h"
#include "src/psp/profiler.h"

#define GU_LIST_WORDS 0x40000
#define GU_FB_WIDTH 512
#define GU_FB_HEIGHT 272
#define GU_FB_STRIDE (GU_FB_WIDTH * 2)
#define GU_DRAW_OFFSET 0
#define GU_DISP_OFFSET (GU_FB_STRIDE * GU_FB_HEIGHT)
#define GU_THIRD_OFFSET (GU_DISP_OFFSET * 2)
#define GU_DEPTH_OFFSET (GU_DISP_OFFSET * 3)
#define GU_BATCH_VERTICES 192

#define PSP_SCREEN_W 480.0f
#define PSP_SCREEN_H 272.0f
#define N64_SCREEN_W 320.0f
#define N64_SCREEN_H 240.0f

static u32 sGuList[GU_LIST_WORDS] __attribute__((aligned(64)));
static int sGuReady;
static u32 sDrawOffset = GU_DRAW_OFFSET;
static u32 sPendingOffset = GU_DISP_OFFSET;
static const volatile u16* sLastFrame;
static u64 sFrameStartUs;
static PspGfxVertex3D sBatchVertices[GU_BATCH_VERTICES];
static u32 sBatchCount;
#ifdef PSP_PROFILE
u32 gPspProfileSyncUs;
u32 gPspProfileVblankUs;
#endif

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
static int sBlendEnabled;

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

static void psp_gfx_gu_flush(void);

static u32 psp_gfx_free_buffer(u32 displayed, u32 pending) {
    u32 offset;
    for (offset = 0; offset < GU_DEPTH_OFFSET; offset += GU_DISP_OFFSET) {
        if (offset != displayed && offset != pending) return offset;
    }
    return GU_DEPTH_OFFSET;
}

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
    psp_gfx_gu_flush();
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
    sBlendEnabled = -1;
}

static void psp_gfx_gu_blend(int enabled) {
    if (sBlendEnabled == enabled) return;
    psp_gfx_gu_flush();
    if (enabled) {
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    } else {
        sceGuDisable(GU_BLEND);
    }
    sBlendEnabled = enabled;
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
    sceGuDisplay(GU_TRUE);

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
    sBatchCount = 0;
    sFrameStartUs = sceKernelGetSystemTimeWide();
    sceGuStart(GU_DIRECT, sGuList);
    sceGuDrawBufferList(GU_PSM_5650, (void*) sDrawOffset, GU_FB_WIDTH);
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
    sBlendEnabled = 0;
}

void PspGfx_Present(void) {
    u64 workEnd, syncEnd, waitStart, waitEnd;
    if (!sGuReady) {
        return;
    }

    psp_gfx_gu_flush();
    PspOverlay_Draw();
    sceGuFinish();
    workEnd = sceKernelGetSystemTimeWide();
    sceGuSync(0, 0);
    syncEnd = sceKernelGetSystemTimeWide();
#ifdef PSP_PROFILE
    gPspProfileSyncUs = (u32) (syncEnd - workEnd);
#endif
    /* The GU is done with the decoded tiles queued this frame */
    PspGfx_EndFrame();
    waitStart = sceKernelGetSystemTimeWide();
    {
        void* current;
        int stride, format;
        u32 currentOffset;
        u32 pendingAddress = (u32) sceGeEdramGetAddr() + sPendingOffset;

        for (;;) {
            if (sceDisplayGetFrameBuf(&current, &stride, &format, PSP_DISPLAY_SETBUF_IMMEDIATE) == 0 &&
                (((u32) current & 0x1fffffffu) == (pendingAddress & 0x1fffffffu))) break;
            sceDisplayWaitVblankStart();
        }
        currentOffset = sPendingOffset;
        while (sceDisplaySetFrameBuf((void*) ((u32) sceGeEdramGetAddr() + sDrawOffset), GU_FB_WIDTH,
                                     PSP_DISPLAY_PIXEL_FORMAT_565, PSP_DISPLAY_SETBUF_NEXTFRAME) < 0) {
            sceDisplayWaitVblankStart();
        }
        sPendingOffset = sDrawOffset;
        sLastFrame = (const volatile u16*) ((u32) sceGeEdramGetAddr() | 0x40000000u | sDrawOffset);
        sDrawOffset = psp_gfx_free_buffer(currentOffset, sPendingOffset);
    }
    waitEnd = sceKernelGetSystemTimeWide();
#ifdef PSP_PROFILE
    gPspProfileVblankUs = (u32) (waitEnd - waitStart);
#endif
    PspOverlay_RecordFrame((u32) (workEnd - sFrameStartUs), (u32) (syncEnd - workEnd),
                           (u32) (waitEnd - waitStart), sceKernelGetSystemTimeWide());

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
    PROFILE_COUNT(texture_binds, 1);
    PROFILE_COUNT(texture_bytes, texW * texH * sizeof(u16));
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
    psp_gfx_gu_flush();
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

static int psp_gfx_gu_submit_inner(const PspGfxVertex3D* vertices, u32 count, int batch) {
    PspGfxVertex3D* out;
    u32 i;
#ifdef PSP_PROFILE
    u64 copyStart;
#endif

    out = (PspGfxVertex3D*) sceGuGetMemory(count * sizeof(PspGfxVertex3D));
    if (out == NULL) {
        return 0;
    }
    PROFILE_COUNT(draws, 1);
    PROFILE_COUNT(submitted_vertices, count);
#ifdef PSP_PROFILE
    copyStart = batch && gPspProfileFrame ? PROFILE_NOW() : 0;
#endif
    /* GU vertex memory is word aligned and uncached */
    for (i = 0; i < count; i++) {
        out[i] = vertices[i];
    }
#ifdef PSP_PROFILE
    if (batch && gPspProfileFrame) {
        gPspProfileFrame->batch_copy_us += (u32) (PROFILE_NOW() - copyStart);
    }
#endif
    sceGuDrawArray(GU_TRIANGLES, GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_3D, count,
                   NULL, out);
    return 1;
}

static void psp_gfx_gu_flush(void) {
#ifdef PSP_PROFILE
    u64 start;
#endif
    if (sBatchCount == 0) {
        return;
    }
#ifdef PSP_PROFILE
    start = gPspProfileFrame ? PROFILE_NOW() : 0;
#endif
    if (psp_gfx_gu_submit_inner(sBatchVertices, sBatchCount, 1)) {
        PROFILE_COUNT(batch_draws, 1);
        PROFILE_COUNT(batch_vertices, sBatchCount);
    }
#ifdef PSP_PROFILE
    if (gPspProfileFrame) {
        gPspProfileFrame->batch_submit_us += (u32) (PROFILE_NOW() - start);
    }
#endif
    sBatchCount = 0;
}

static void psp_gfx_gu_queue_triangle(const PspGfxVertex3D vertices[3]) {
    if (sBatchCount == GU_BATCH_VERTICES) {
        psp_gfx_gu_flush();
    }
    sBatchVertices[sBatchCount] = vertices[0];
    sBatchVertices[sBatchCount + 1] = vertices[1];
    sBatchVertices[sBatchCount + 2] = vertices[2];
    sBatchCount += 3;
}

static void psp_gfx_gu_submit(const PspGfxVertex3D* vertices, u32 count) {
#ifdef PSP_PROFILE
    u64 start;
#endif
    psp_gfx_gu_flush();
#ifdef PSP_PROFILE
    start = gPspProfileSample ? PROFILE_NOW() : 0;
#endif
    psp_gfx_gu_submit_inner(vertices, count, 0);
#ifdef PSP_PROFILE
    if (gPspProfileSample) gPspProfileFrame->submit_us += (u32)(PROFILE_NOW() - start);
#endif
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

static void psp_gfx_gu_clip_triangle_inner(const PspGfxVertex3D vertices[3], int smooth, int batch) {
    n64psp_vec4f positions[3];
    n64psp_vec4f_pair transformed[3];
    n64psp_vec4f clip[3];
    u32 shared = 63;
    int safe = 1;
    u32 i;

    PROFILE_COUNT(clip_calls, 1);
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
        PROFILE_COUNT(clip_rejects, 1);
        return;
    }
    if (safe) {
        /* The GE clips the near plane and keeps perspective interpolation */
        if (batch) {
            psp_gfx_gu_queue_triangle(vertices);
        } else {
            psp_gfx_gu_submit(vertices, 3);
        }
    } else {
        n64psp_clip_vertex clipped[N64PSP_CLIP_MAX_VERTICES];
        PspGfxVertex3D polygon[N64PSP_CLIP_MAX_VERTICES];
        PspGfxVertex3D triangles[(N64PSP_CLIP_MAX_VERTICES - 2) * 3];
        size_t count = n64psp_clip_triangle(clipped, clip);
        PROFILE_COUNT(clipped, 1);
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

static void psp_gfx_gu_clip_triangle(const PspGfxVertex3D vertices[3], int smooth, int batch) {
#ifdef PSP_PROFILE
    u64 start = gPspProfileSample ? PROFILE_NOW() : 0;
#endif
    psp_gfx_gu_clip_triangle_inner(vertices, smooth, batch);
#ifdef PSP_PROFILE
    if (gPspProfileSample) gPspProfileFrame->clip_us += (u32)(PROFILE_NOW() - start);
#endif
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
        psp_gfx_gu_flush();
        sceGuShadeModel(shadeModel);
        sShadeModel = shadeModel;
    }

    if (!sMatrixValid || sProjection != state->projection || sProjectionSerial != state->projectionSerial) {
        psp_gfx_gu_flush();
        sceGuSetMatrix(GU_PROJECTION, (const ScePspFMatrix4*) state->projection);
        sProjection = state->projection;
        sProjectionSerial = state->projectionSerial;
        memcpy(&sClipProjection, state->projection, sizeof(sClipProjection));
    }
    if (!sMatrixValid) {
        sceGuSetMatrix(GU_VIEW, &sIdentityMatrix);
    }
    if (!sMatrixValid || sModelview != state->modelview || sModelviewSerial != state->modelviewSerial) {
        psp_gfx_gu_flush();
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
        psp_gfx_gu_flush();
        if (depthEnabled) {
            sceGuEnable(GU_DEPTH_TEST);
        } else {
            sceGuDisable(GU_DEPTH_TEST);
        }
        sDepthTestEnabled = depthEnabled;
    }
    if (depthEnabled && sDepthFunction != depthFunction) {
        psp_gfx_gu_flush();
        sceGuDepthFunc(depthFunction);
        sDepthFunction = depthFunction;
    }
    if (sDepthWriteEnabled != (state->depthWrite != 0)) {
        psp_gfx_gu_flush();
        sceGuDepthMask(state->depthWrite ? GU_FALSE : GU_TRUE);
        sDepthWriteEnabled = state->depthWrite != 0;
    }

    if (state->cullFront || state->cullBack) {
        if (!sCullEnabled) {
            psp_gfx_gu_flush();
            sceGuEnable(GU_CULL_FACE);
            sCullEnabled = 1;
        }
        if (sCullFront != (state->cullFront != 0)) {
            psp_gfx_gu_flush();
            /* Map N64 cull mode onto GU front-face winding */
            sceGuFrontFace(state->cullBack ? GU_CCW : GU_CW);
            sCullFront = state->cullFront != 0;
        }
    } else if (sCullEnabled) {
        psp_gfx_gu_flush();
        sceGuDisable(GU_CULL_FACE);
        sCullEnabled = 0;
    }

    psp_gfx_gu_blend(state->blend != 0);

    if (state->texturePixels != NULL && state->texWidth > 0 && state->texHeight > 0) {
        u32 wrap = (u32) ((state->wrapS != 0) | ((state->wrapT != 0) << 1));
        int filter = state->filter != 0;
        int texFunc = state->texFunc;

        if (!sTexBoundValid || sTexBoundPixels != state->texturePixels ||
            sTexBoundWidth != state->texWidth || sTexBoundHeight != state->texHeight ||
            sTexBoundTexFunc != texFunc || sTexBoundWrap != wrap || sTexBoundFilter != filter) {
            psp_gfx_gu_flush();
            sceGuEnable(GU_TEXTURE_2D);
            sceGuTexMode(GU_PSM_4444, 0, 0, 0);
            PROFILE_COUNT(texture_binds, 1);
            PROFILE_COUNT(texture_bytes, state->texWidth * state->texHeight * sizeof(u16));
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
        if (sTexBoundValid) {
            psp_gfx_gu_flush();
        }
        sceGuDisable(GU_TEXTURE_2D);
        sTexBoundValid = 0;
    }

    /* Keep each base triangle before its overlay */
    if (state->reflection) {
        psp_gfx_gu_flush();
    }
    for (i = 0; i < count; i += 3) {
        psp_gfx_gu_clip_triangle(vertices + i, state->smoothShade, !state->reflection);
        if (state->reflection) {
            PspGfxVertex3D overlay[3];
            u32 j;

            for (j = 0; j < 3; j++) {
                overlay[j] = vertices[i + j];
                overlay[j].color = (state->reflectionColor & 0x00ffffffu) |
                                   (vertices[i + j].color & 0xff000000u);
            }
            sceGuDisable(GU_TEXTURE_2D);
            sTexBoundValid = 0;
            psp_gfx_gu_blend(1);
            sceGuDepthMask(GU_TRUE);
            sDepthWriteEnabled = 0;
            PROFILE_COUNT(reflections, 1);
            psp_gfx_gu_clip_triangle(overlay, state->smoothShade, 0);
            if (state->depthWrite) {
                sceGuDepthMask(GU_FALSE);
                sDepthWriteEnabled = 1;
            }
            psp_gfx_gu_blend(0);
            if (state->texturePixels != NULL) {
                sceGuEnable(GU_TEXTURE_2D);
            }
        }
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
