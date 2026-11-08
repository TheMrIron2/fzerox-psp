#include "src/psp/gfx/gfx_psp.h"
#include "src/psp/n64ram.h"
#include "src/psp/psp.h"
#include "PR/mbi.h"
#include "PR/gu.h"

#include <math.h>
#include <n64psp/lighting.h>

#define PSP_GFX_MAX_VTX 64
#define PSP_TEX_SLOTS 512
#define PSP_TEX_POOL_SIZE (1 << 21) /* 4 MB of RGBA4444 texels */
#define PSP_TEX_MIN_HEIGHT 8

extern u32 gSegments[16];

extern u8 D_1000000[];
extern u8 D_2000000[];

#define PSP_GFX_STUB_SIZE 0x40000u

/* Resolve N64 0x0Sxxxxxx segmented addresses; PSP host pointers pass through */
static u32 psp_gfx_ram_addr(u32 addr) {
    u32 seg;

    /* The PRX loader re-bases the stub arrays, so generated code stores host
     * addresses in those ranges; map them onto the live segment bases. */
    if (gSegments[1] != 0 && addr >= (u32) &D_1000000 && (addr - (u32) &D_1000000) < PSP_GFX_STUB_SIZE) {
        u32 resolved = (u32) gSegments[1] + (addr - (u32) &D_1000000);
        static u32 sLoggedStub1;

        if (sLoggedStub1 < 4) {
            sLoggedStub1++;
            PspLog("[gfx] stub1 addr=%08x base=%08x out=%08x\n", (unsigned) addr, (unsigned) gSegments[1],
                   (unsigned) resolved);
        }
        return resolved;
    }
    if (gSegments[2] != 0 && addr >= (u32) &D_2000000 && (addr - (u32) &D_2000000) < PSP_GFX_STUB_SIZE) {
        return (u32) gSegments[2] + (addr - (u32) &D_2000000);
    }

    if ((addr >= 0x01000000u) && (addr < 0x08000000u)) {
        seg = (addr >> 24) & 0xF;
        if (gSegments[seg] != 0) {
            u32 resolved = (u32) (gSegments[seg] + (addr & 0x00FFFFFFu));

            if (seg == 1) {
                static u32 sLoggedSeg1;

                if (sLoggedSeg1 < 12) {
                    sLoggedSeg1++;
                    PspLog("[gfx] ram addr=%08x seg=1 base=%08x out=%08x\n", (unsigned) addr,
                           (unsigned) gSegments[1], (unsigned) resolved);
                }
            }
            return resolved;
        }
        if (seg == 1) {
            static u32 sLoggedMiss;

            if (sLoggedMiss < 4) {
                sLoggedMiss++;
                PspLog("[gfx] ram MISS addr=%08x seg=1 base=0\n", (unsigned) addr);
            }
        }
    }
    return addr;
}

static u32 sDrawCount;
static u32 sBudget;

#define PSP_GFX_MTX_STACK 16

/* Matrices and vertices */
static float sModelView[4][4] __attribute__((aligned(16)));
static float sProjection[4][4] __attribute__((aligned(16)));
static float sMP[4][4];
static float sModelViewStack[PSP_GFX_MTX_STACK][4][4];
static float sProjectionStack[PSP_GFX_MTX_STACK][4][4];
static int sModelViewDepth;
static int sProjectionDepth;
static int sProjectionPerspective;
static Vp sViewport;
static Vtx sVtx[PSP_GFX_MAX_VTX];

/* RDP state used by the 2D path */
static u32 sTexAddr;
static u32 sTexSiz;
static u32 sTexW;
static u32 sTileFmt[8];
static u32 sTileSiz[8];
static u32 sTileTmem[8];
static u32 sTilePalette[8];
static u16 sPalette[256];
/* Content hash of the loaded TLUT so identical reloads still hit the cache */
static u32 sPaletteHash;
static u32 sTileWidth[8];
static u32 sTileHeight[8];
static u32 sTileUls[8];
static u32 sTileUlt[8];
static u32 sLoadAddr;
static u32 sLoadW;
static u32 sLoadSiz;
static u32 sOtherModeH;
static u32 sLoadUls;
static u32 sLoadUlt;
static int sLoadBlock;
static u32 sPrimColor = 0xFFFFFFFF;
static u32 sPrimColor8888 = 0xFFFFFFFF;
static u32 sCombineW0;
static u32 sCombineW1;
static u32 sFillColor;
static u32 sEnvColor = 0xFFFFFFFF;
static u32 sPrimLodFrac;
/* Current RDP colour image; 0 means the game never bound one (draw to screen) */
static u32 sColorTarget;

/* RSP 3D state */
static u32 sGeometryMode;
static u32 sOtherModeL;
static int sProjectionSet;
static u32 sProjectionSerial;
static u32 sModelViewSerial;

/* Projection with the N64 viewport folded in, fed to the GE */
static float sViewportProjection[4][4] __attribute__((aligned(16)));
static u32 sViewportProjectionSerial;

typedef struct {
    float x;
    float y;
    float z;
    u8 r;
    u8 g;
    u8 b;
} PspGfxLightSlot;

static PspGfxLightSlot sLightSlots[8];
static u8 sAmbientR;
static u8 sAmbientG;
static u8 sAmbientB;
static int sNumLights;

/* Lighting scratch */
static n64psp_snorm8x4 sLitNormals[PSP_GFX_MAX_VTX] __attribute__((aligned(16)));
static n64psp_vec4f sLitOutput[PSP_GFX_MAX_VTX] __attribute__((aligned(16)));
static n64psp_directional_lightf sLitLights[8] __attribute__((aligned(16)));
static n64psp_vec4f sLitAmbient __attribute__((aligned(16)));
static n64psp_mat4f sLitNormalMatrix __attribute__((aligned(16)));

/* The game's visible framebuffers are mapped pointers; offscreen clears such as
 * D_3000170 target raw physical addresses (0x400) that we cannot render to */
static int psp_gfx_targets_display(void) {
    u32 addr = sColorTarget;

    return addr == 0 || addr >= 0x08000000u;
}

typedef struct {
    u32 addr;
    u32 size;
    int native;
} PspTextureOrder;

#define PSP_TEXTURE_ORDERS 512

static PspTextureOrder sTextureOrders[PSP_TEXTURE_ORDERS];
static u32 sTextureOrderCount;

static void psp_gfx_tex_invalidate(u32 base, u32 size);

/* Oldest first; a new range purges whatever it overlaps */
void PspGfx_SetTextureByteOrder(const void* data, u32 size, int native) {
    u32 base = (u32) data;
    u32 end;
    u32 i;

    if (data == NULL || size == 0) {
        return;
    }
    end = base + size;
    for (i = 0; i < sTextureOrderCount;) {
        const PspTextureOrder* order = &sTextureOrders[i];

        if (order->addr < end && base < order->addr + order->size) {
            u32 j;

            for (j = i; j + 1 < sTextureOrderCount; j++) {
                sTextureOrders[j] = sTextureOrders[j + 1];
            }
            sTextureOrderCount--;
        } else {
            i++;
        }
    }
    if (sTextureOrderCount >= PSP_TEXTURE_ORDERS) {
        u32 j;

        for (j = 0; j + 1 < sTextureOrderCount; j++) {
            sTextureOrders[j] = sTextureOrders[j + 1];
        }
        sTextureOrderCount--;
    }
    sTextureOrders[sTextureOrderCount].addr = base;
    sTextureOrders[sTextureOrderCount].size = size;
    sTextureOrders[sTextureOrderCount].native = native;
    sTextureOrderCount++;
    /* The buffer was (re)loaded or its byte order changed, so drop decoded copies */
    psp_gfx_tex_invalidate(base, size);
}

int PspGfx_TextureByteOrder(const void* data) {
    u32 addr = (u32) data;
    u32 count = sTextureOrderCount;
    u32 i;

    for (i = 0; i < count; i++) {
        const PspTextureOrder* order = &sTextureOrders[sTextureOrderCount - i - 1];

        if (addr >= order->addr && addr - order->addr < order->size) {
            return order->native;
        }
    }
    return addr < (u32) gPspN64Ram || addr >= (u32) gPspN64Ram + 0x800000;
}

static u16 psp_gfx_read16(const u8* data, u32 index, int native) {
    return native ? ((const u16*) data)[index] : ((u16) data[index * 2] << 8) | data[index * 2 + 1];
}

/* Decoded texture cache, rebuilt each display list */
typedef struct {
    u32 addr;
    u32 fmt;
    u32 siz;
    u32 srcW;
    u32 width;
    u32 height;
    u32 srcX;
    u32 srcY;
    u32 paletteKey;
    u32 variant;
    u32 combine0;
    u32 combine1;
    u32 mode;
    u32 lod;
    u32 prim;
    u32 env;
    int native;
    u32 potW;
    u32 potH;
    u32 stamp;
    u16* pixels;
} PspTexEntry;

static u16 sTexPool[PSP_TEX_POOL_SIZE] __attribute__((aligned(64)));
static u32 sTexPoolUsed;
static PspTexEntry sTexEntries[PSP_TEX_SLOTS];
static u32 sTexEntryCount;
static u32 sTexReplaceNext;
/* Decoded tiles survive across frames until the pool needs the space again */
static u32 sFrameStamp;
static u32 sTexDecodeCount;

/* Drop decoded tiles whose source was rewritten or reinterpreted */
static void psp_gfx_tex_invalidate(u32 base, u32 size) {
    u32 end = base + size;
    u32 i = 0;

    while (i < sTexEntryCount) {
        u32 addr = sTexEntries[i].addr;

        if (addr >= base && addr < end) {
            u32 j;

            for (j = i; j + 1 < sTexEntryCount; j++) {
                sTexEntries[j] = sTexEntries[j + 1];
            }
            sTexEntryCount--;
        } else {
            i++;
        }
    }
}

/* Reclaim older tiles and pack the kept pixels down. Only safe between frames,
 * after the GU has finished, so queued draws never lose their texture */
static void psp_gfx_tex_compact(u32 keepFrom) {
    u32 kept = 0;
    u32 used = 0;
    u32 i;

    for (i = 0; i < sTexEntryCount; i++) {
        PspTexEntry entry = sTexEntries[i];
        u32 count;
        u32 j;

        if (entry.stamp < keepFrom) {
            continue;
        }
        count = entry.potW * entry.potH;
        if (used != (u32) (entry.pixels - sTexPool)) {
            u16* dst = &sTexPool[used];
            const u16* src = entry.pixels;

            for (j = 0; j < count; j++) {
                dst[j] = src[j];
            }
            entry.pixels = dst;
        }
        sTexEntries[kept++] = entry;
        used += count;
    }
    sTexEntryCount = kept;
    sTexPoolUsed = used;
    sTexReplaceNext = 0;
}

static void psp_gfx_mat_identity(float m[4][4]) {
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            m[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
}

static void psp_gfx_mat_mul(float out[4][4], const float a[4][4], const float b[4][4]) {
    float t[4][4];
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            t[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + a[i][3] * b[3][j];
        }
    }
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            out[i][j] = t[i][j];
        }
    }
}

static void psp_gfx_mat_copy(float out[4][4], const float in[4][4]) {
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            out[i][j] = in[i][j];
        }
    }
}

/* Fold the N64 quarter-pixel viewport into the projection for the GE */
static void psp_gfx_update_viewport_projection(void) {
    float scaleX = (float) sViewport.vp.vscale[0];
    float scaleY = (float) sViewport.vp.vscale[1];
    float transX = (float) sViewport.vp.vtrans[0];
    float transY = (float) sViewport.vp.vtrans[1];
    float sx = scaleX / (320.0f * 2.0f);
    float sy = scaleY / (240.0f * 2.0f);
    float tx = transX / (320.0f * 2.0f) - 1.0f;
    float ty = 1.0f - transY / (240.0f * 2.0f);
    int i;

    for (i = 0; i < 4; i++) {
        sViewportProjection[i][0] = sProjection[i][0] * sx + sProjection[i][3] * tx;
        sViewportProjection[i][1] = sProjection[i][1] * sy + sProjection[i][3] * ty;
        sViewportProjection[i][2] = sProjection[i][2];
        sViewportProjection[i][3] = sProjection[i][3];
    }
    if (++sViewportProjectionSerial == 0) {
        sViewportProjectionSerial = 1;
    }
}

/* N64 s15.16 matrix to float. The game stores integer and fraction parts
 * element-wise in u.i/u.f (see Matrix_FromMtx), so decode the same way. */
static void psp_gfx_mtx_l2f(float mf[4][4], const Mtx* m) {
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            s32 fixed = ((s32) (s16) m->u.i[i][j] << 16) | (u32) m->u.f[i][j];

            mf[i][j] = (float) fixed / 65536.0f;
        }
    }
}

static u32 psp_gfx_vertex_color(const Vtx* v) {
    u32 r = v->v.cn[0];
    u32 g = v->v.cn[1];
    u32 b = v->v.cn[2];
    u32 a = v->v.cn[3];

    if ((r | g | b) == 0) {
        return 0xFFFFFFFFu;
    }
    if (a == 0) {
        a = 0xFF;
    }
    return r | (g << 8) | (b << 16) | (a << 24);
}

static void psp_gfx_transform(int index, PspGfxVertex* out) {
    const Vtx* v;
    float p[4];
    float clip[4];
    float w;
    float ndcY;
    int i;

    if (index < 0 || index >= PSP_GFX_MAX_VTX) {
        out->x = 0.0f;
        out->y = 0.0f;
        out->z = 0.0f;
        out->color = 0xFFFFFFFFu;
        return;
    }

    v = &sVtx[index];
    p[0] = (float) v->v.ob[0];
    p[1] = (float) v->v.ob[1];
    p[2] = (float) v->v.ob[2];
    p[3] = 1.0f;

    /* N64/Fast3D row-vector convention: p' = p * (MV * P) */
    for (i = 0; i < 4; i++) {
        clip[i] = p[0] * sMP[0][i] + p[1] * sMP[1][i] + p[2] * sMP[2][i] + p[3] * sMP[3][i];
    }

    w = clip[3];
    if (w < 0.0001f && w > -0.0001f) {
        w = 0.0001f;
    }

    out->x = ((float) sViewport.vp.vscale[0] * (clip[0] / w) + (float) sViewport.vp.vtrans[0]) * 0.25f;
    /* The N64 viewport flips y, ortho matrices already carry their own flip */
    ndcY = clip[1] / w;
    if (sProjectionPerspective) {
        ndcY = -ndcY;
    }
    out->y = ((float) sViewport.vp.vscale[1] * ndcY + (float) sViewport.vp.vtrans[1]) * 0.25f;
    out->z = ((float) sViewport.vp.vscale[2] * (clip[2] / w) + (float) sViewport.vp.vtrans[2]) * 0.25f;
    out->color = psp_gfx_vertex_color(v);
}

static PspTexEntry* psp_gfx_tex_get(u32 fmt, u32 siz, u32 width, u32 height, u32 tile, u32 variant);

/* 3D texture cache: refetch when the load/tile state changes */
static PspTexEntry* s3dTexEntry;
static int s3dTexDirty = 1;

static u8 psp_gfx_remap_light(float value) {
    if (value <= 0.0f) {
        return 0;
    }
    if (value >= 255.0f) {
        return 255;
    }
    return (u8) value;
}

static void psp_gfx_build_light_defs(void) {
    int i;
    int j;

    for (i = 0; i < sNumLights && i < 8; i++) {
        sLitLights[i].direction.x = sLightSlots[i].x;
        sLitLights[i].direction.y = sLightSlots[i].y;
        sLitLights[i].direction.z = sLightSlots[i].z;
        sLitLights[i].direction.w = 0.0f;
        sLitLights[i].color.x = (float) sLightSlots[i].r;
        sLitLights[i].color.y = (float) sLightSlots[i].g;
        sLitLights[i].color.z = (float) sLightSlots[i].b;
        sLitLights[i].color.w = 0.0f;
    }
    sLitAmbient.x = (float) sAmbientR;
    sLitAmbient.y = (float) sAmbientG;
    sLitAmbient.z = (float) sAmbientB;
    sLitAmbient.w = 0.0f;
    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            sLitNormalMatrix.m[i][j] = sModelView[i][j];
        }
    }
}

static int psp_gfx_3d_final_cycle(void) {
    return (sOtherModeH & (3u << G_MDSFT_CYCLETYPE)) == G_CYC_2CYCLE ? 1 : 0;
}

static int psp_gfx_3d_combiner_uses_texture(void) {
    u32 a;
    u32 b;
    u32 d;
    u32 aa;
    u32 ab;
    u32 ad;

    if (psp_gfx_3d_final_cycle()) {
        a = (sCombineW0 >> 5) & 15;
        b = (sCombineW1 >> 24) & 15;
        d = (sCombineW1 >> 6) & 7;
        aa = (sCombineW1 >> 21) & 7;
        ab = (sCombineW1 >> 3) & 7;
        ad = sCombineW1 & 7;
    } else {
        a = (sCombineW0 >> 20) & 15;
        b = (sCombineW1 >> 28) & 15;
        d = (sCombineW1 >> 15) & 7;
        aa = (sCombineW0 >> 12) & 7;
        ab = (sCombineW1 >> 12) & 7;
        ad = (sCombineW0 >> 9) & 7;
    }
    return a == G_CCMUX_TEXEL0 || b == G_CCMUX_TEXEL0 || d == G_CCMUX_TEXEL0 ||
           aa == G_ACMUX_TEXEL0 || ab == G_ACMUX_TEXEL0 || ad == G_ACMUX_TEXEL0;
}

static int psp_gfx_3d_combiner_modulate(void) {
    u32 a;
    u32 b;
    u32 d;

    if (psp_gfx_3d_final_cycle()) {
        a = (sCombineW0 >> 5) & 15;
        b = (sCombineW1 >> 24) & 15;
        d = (sCombineW1 >> 6) & 7;
    } else {
        a = (sCombineW0 >> 20) & 15;
        b = (sCombineW1 >> 28) & 15;
        d = (sCombineW1 >> 15) & 7;
    }
    /* Pure TEXEL0 output is a replace; anything else modulates the shade */
    return !(a == b && d == G_CCMUX_TEXEL0);
}

static void psp_gfx_triangle3d(u32 i0, u32 i1, u32 i2) {
    PspGfxVertex3D tri[3];
    PspGfxDrawState state;
    u32 idx[3];
    PspTexEntry* tex = NULL;
    int textured = 0;
    int modulate = 1;
    int lit = (sGeometryMode & G_LIGHTING) && sNumLights > 0;
    int k;

    if (i0 >= PSP_GFX_MAX_VTX || i1 >= PSP_GFX_MAX_VTX || i2 >= PSP_GFX_MAX_VTX) {
        return;
    }
    idx[0] = i0;
    idx[1] = i1;
    idx[2] = i2;

    /* Textures use explicit UVs; texgen machines stay untextured for now */
    if (!(sGeometryMode & G_TEXTURE_GEN) && sTileFmt[0] != 0xFF && psp_gfx_3d_combiner_uses_texture()) {
        if (s3dTexDirty) {
            s3dTexEntry = psp_gfx_tex_get(sTileFmt[0], sTileSiz[0], sTileWidth[0], sTileHeight[0], 0, 0);
            s3dTexDirty = 0;
        }
        tex = s3dTexEntry;
        if (tex != NULL) {
            textured = 1;
            modulate = psp_gfx_3d_combiner_modulate();
        }
    }

    {
        static int sLogged3D;

        if (!sLogged3D) {
            sLogged3D = 1;
            PspLog("[gfx] 3D active tex=%d p00=%d p11=%d p23=%d seg1=%08x vp=%d,%d,%d,%d zbuf=1\n", textured,
                   (int) (sViewportProjection[0][0] * 1024.0f), (int) (sViewportProjection[1][1] * 1024.0f),
                   (int) (sViewportProjection[2][3] * 1024.0f), (unsigned) gSegments[1], sViewport.vp.vscale[0],
                   sViewport.vp.vscale[1], sViewport.vp.vtrans[0], sViewport.vp.vtrans[1]);
        }
    }

    if (lit) {
        for (k = 0; k < 3; k++) {
            const Vtx* v = &sVtx[idx[k]];

            sLitNormals[k].x = (s8) v->v.cn[0];
            sLitNormals[k].y = (s8) v->v.cn[1];
            sLitNormals[k].z = (s8) v->v.cn[2];
            sLitNormals[k].w = 0;
        }
        psp_gfx_build_light_defs();
        n64psp_directional_light_snorm8_batch(sLitOutput, &sLitNormalMatrix, sLitNormals, sLitLights,
                                             &sLitAmbient, (size_t) sNumLights, 3);
    }

    for (k = 0; k < 3; k++) {
        const Vtx* v = &sVtx[idx[k]];
        u32 r;
        u32 g;
        u32 b;
        u32 a;

        if (lit) {
            r = psp_gfx_remap_light(sLitOutput[k].x);
            g = psp_gfx_remap_light(sLitOutput[k].y);
            b = psp_gfx_remap_light(sLitOutput[k].z);
            a = v->v.cn[3];
            if (a == 0) {
                a = 255;
            }
        } else {
            u32 c = psp_gfx_vertex_color(v);

            r = c & 0xFF;
            g = (c >> 8) & 0xFF;
            b = (c >> 16) & 0xFF;
            a = (c >> 24) & 0xFF;
        }
        tri[k].x = (float) v->v.ob[0];
        tri[k].y = (float) v->v.ob[1];
        tri[k].z = (float) v->v.ob[2];
        if (textured) {
            tri[k].u = (float) (s16) v->v.tc[0] / 32.0f - (float) sTileUls[0] / 4.0f;
            tri[k].v = (float) (s16) v->v.tc[1] / 32.0f - (float) sTileUlt[0] / 4.0f;
        } else {
            tri[k].u = 0.0f;
            tri[k].v = 0.0f;
        }
        tri[k].color = r | (g << 8) | (b << 16) | (a << 24);
    }

    state.projection = &sViewportProjection[0][0];
    state.modelview = &sModelView[0][0];
    state.projectionSerial = sViewportProjectionSerial;
    state.modelviewSerial = sModelViewSerial;
    state.depthTest = (sOtherModeL & Z_CMP) != 0;
    state.depthWrite = (sOtherModeL & Z_UPD) != 0;
    state.cullFront = (sGeometryMode & G_CULL_FRONT) != 0;
    state.cullBack = (sGeometryMode & G_CULL_BACK) != 0;
    state.texturePixels = textured ? tex->pixels : NULL;
    state.texWidth = textured ? (s32) tex->potW : 0;
    state.texHeight = textured ? (s32) tex->potH : 0;
    state.modulate = modulate;
    PspGfx_DrawTriangles3D(tri, 3, &state);
}

static void psp_gfx_triangle(u32 i0, u32 i1, u32 i2) {
    PspGfxVertex tri[3];
    static int sLoggedPath;

    if (sColorTarget != 0 && !psp_gfx_targets_display()) {
        return;
    }
    if (!sLoggedPath) {
        sLoggedPath = 1;
        PspLog("[gfx] tri geom=%08x proj=%d otherL=%08x zbuf=%d\n", (unsigned) sGeometryMode, sProjectionSet,
               (unsigned) sOtherModeL, (sGeometryMode & G_ZBUFFER) != 0);
    }
    if ((sGeometryMode & G_ZBUFFER) && sProjectionSet) {
        psp_gfx_triangle3d(i0, i1, i2);
        return;
    }
    psp_gfx_transform(i0, &tri[0]);
    psp_gfx_transform(i1, &tri[1]);
    psp_gfx_transform(i2, &tri[2]);
    PspGfx_DrawTriangle2D(tri);
}

static const u8* psp_gfx_tex_source(u32 addr) {
    if (addr == 0) {
        return NULL;
    }
    /* Low values are N64 physical addresses (e.g. the framebuffer) */
    if (addr < 0x100000) {
        return gPspN64Ram + addr;
    }
    return (const u8*) addr;
}

static u16 psp_gfx_rgba5551_to_4444(u32 texel) {
    u32 r = (texel >> 11) & 0x1F;
    u32 g = (texel >> 6) & 0x1F;
    u32 b = (texel >> 1) & 0x1F;
    u32 a = texel & 1;

    return (u16) ((a ? 0xF000 : 0) | ((b >> 1) << 8) | ((g >> 1) << 4) | (r >> 1));
}

static u16 psp_gfx_decode_texel(const PspTexEntry* e, const u8* src, s32 x, s32 y) {
    u32 idx;

    x += e->srcX;
    y += e->srcY;
    idx = (u32) y * e->srcW + (u32) x;
    switch (e->fmt) {
        case G_IM_FMT_RGBA: {
            u32 texel;

            if (e->siz != G_IM_SIZ_16b) {
                return 0;
            }
            if (e->native) {
                texel = ((const u16*) src)[idx];
            } else {
                texel = ((u32) src[idx * 2] << 8) | src[idx * 2 + 1];
            }
            return psp_gfx_rgba5551_to_4444(texel);
        }
        case G_IM_FMT_CI: {
            u32 index;
            u16 texel;

            if (e->siz == G_IM_SIZ_4b) {
                index = ((idx & 1) ? src[idx >> 1] & 15 : src[idx >> 1] >> 4) + (e->paletteKey & 15) * 16;
            } else if (e->siz == G_IM_SIZ_8b) {
                index = src[idx];
            } else {
                return 0;
            }
            texel = sPalette[index];
            if ((sOtherModeH & (3u << G_MDSFT_TEXTLUT)) == G_TT_IA16) {
                u32 i = texel >> 12;

                return ((texel & 0xF0) << 8) | (i << 8) | (i << 4) | i;
            }
            return psp_gfx_rgba5551_to_4444(texel);
        }
        case G_IM_FMT_IA: {
            u32 i;
            u32 a;

            if (e->siz == G_IM_SIZ_16b) {
                if (e->native) {
                    u16 texel = ((const u16*) src)[idx];

                    i = texel >> 12;
                    a = (texel >> 4) & 0xF;
                } else {
                    i = src[idx * 2] >> 4;
                    a = src[idx * 2 + 1] >> 4;
                }
            } else if (e->siz == G_IM_SIZ_8b) {
                i = src[idx] >> 4;
                a = src[idx] & 0xF;
            } else if (e->siz == G_IM_SIZ_4b) {
                u32 texel = (idx & 1) ? (src[idx >> 1] & 0xF) : (src[idx >> 1] >> 4);

                i = texel >> 1;
                i = (i << 1) | (i >> 2);
                a = (texel & 1) ? 15 : 0;
            } else {
                return 0;
            }
            return (u16) ((a << 12) | (i << 8) | (i << 4) | i);
        }
        case G_IM_FMT_I: {
            u32 i;

            if (e->siz == G_IM_SIZ_8b) {
                i = src[idx] >> 4;
            } else if (e->siz == G_IM_SIZ_4b) {
                i = (idx & 1) ? (src[idx >> 1] & 0xF) : (src[idx >> 1] >> 4);
            } else {
                return 0;
            }
            return (u16) ((i << 12) | (i << 8) | (i << 4) | i);
        }
        default:
            return 0;
    }
}

static u32 psp_gfx_next_pow2(u32 v) {
    u32 p = 1;

    while (p < v) {
        p <<= 1;
    }
    return p;
}

static void psp_gfx_pool_full(u32 requested) {
    static int sLogged;

    if (!sLogged) {
        sLogged = 1;
        PspLog("[gfx] tex pool full bytes=%u requested=%u limit=%u\n", (unsigned) (sTexPoolUsed * sizeof(u16)),
               (unsigned) (requested * sizeof(u16)), (unsigned) sizeof(sTexPool));
    }
}

typedef struct {
    u32 a, b, c, d;
    u32 aa, ab, ac, ad;
} PspCombineCycle;

static PspCombineCycle psp_gfx_combine_cycle(int second) {
    PspCombineCycle c;

    if (second) {
        c.a = (sCombineW0 >> 5) & 15;
        c.b = (sCombineW1 >> 24) & 15;
        c.c = sCombineW0 & 31;
        c.d = (sCombineW1 >> 6) & 7;
        c.aa = (sCombineW1 >> 21) & 7;
        c.ab = (sCombineW1 >> 3) & 7;
        c.ac = (sCombineW1 >> 18) & 7;
        c.ad = sCombineW1 & 7;
    } else {
        c.a = (sCombineW0 >> 20) & 15;
        c.b = (sCombineW1 >> 28) & 15;
        c.c = (sCombineW0 >> 15) & 31;
        c.d = (sCombineW1 >> 15) & 7;
        c.aa = (sCombineW0 >> 12) & 7;
        c.ab = (sCombineW1 >> 12) & 7;
        c.ac = (sCombineW0 >> 9) & 7;
        c.ad = (sCombineW1 >> 9) & 7;
    }
    return c;
}

static int psp_gfx_combine_value(u32 mux, int component, int multiplier, const int tex[4], const int combined[4]) {
    if (multiplier && mux >= 7) {
        switch (mux) {
            case 7:
                return combined[3];
            case 8:
                return tex[3];
            case 10:
                return (sPrimColor8888 >> 24) & 255;
            case 11:
                return 255;
            case 12:
                return (sEnvColor >> 24) & 255;
            case 14:
                return sPrimLodFrac;
            default:
                return 0;
        }
    }
    switch (mux) {
        case 0:
            return combined[component];
        case 1:
            return tex[component];
        case 3:
            return (sPrimColor8888 >> (component * 8)) & 255;
        case 4:
            return 255;
        case 5:
            return (sEnvColor >> (component * 8)) & 255;
        case 6:
            return multiplier ? 0 : 255;
        default:
            return 0;
    }
}

static u16 psp_gfx_combine_texel(u16 pixel) {
    int tex[4];
    int combined[4] = { 0, 0, 0, 0 };
    int result[4];
    int cycles = (sOtherModeH & (3u << G_MDSFT_CYCLETYPE)) == G_CYC_2CYCLE ? 2 : 1;
    int i, cycle;
    u16 out = 0;

    for (i = 0; i < 4; i++) {
        tex[i] = ((pixel >> (i * 4)) & 15) * 17;
    }
    for (cycle = 0; cycle < cycles; cycle++) {
        PspCombineCycle c = psp_gfx_combine_cycle(cycles == 1 || cycle == 1);

        for (i = 0; i < 4; i++) {
            int a, b, factor, d;

            if (i == 3) {
                a = psp_gfx_combine_value(c.aa, 3, 0, tex, combined);
                b = psp_gfx_combine_value(c.ab, 3, 0, tex, combined);
                factor = c.ac == 0   ? 0
                         : c.ac == 6 ? (int) sPrimLodFrac
                                     : psp_gfx_combine_value(c.ac, 3, 0, tex, combined);
                d = psp_gfx_combine_value(c.ad, 3, 0, tex, combined);
            } else {
                a = psp_gfx_combine_value(c.a, i, 0, tex, combined);
                b = psp_gfx_combine_value(c.b, i, 0, tex, combined);
                factor = psp_gfx_combine_value(c.c, i, 1, tex, combined);
                d = psp_gfx_combine_value(c.d, i, 0, tex, combined);
            }
            result[i] = ((a - b) * factor) / 255 + d;
            if (result[i] < 0)
                result[i] = 0;
            if (result[i] > 255)
                result[i] = 255;
        }
        for (i = 0; i < 4; i++)
            combined[i] = result[i];
    }
    for (i = 0; i < 4; i++)
        out |= (combined[i] >> 4) << (i * 4);
    return out;
}

static u32 psp_gfx_texture_style(u32* tint, int* modulate) {
    PspCombineCycle c = psp_gfx_combine_cycle(1);
    int rgbTex = (c.a == c.b || c.c == 31) && c.d == G_CCMUX_TEXEL0;
    int rgbPrim = c.a == G_CCMUX_TEXEL0 && c.b == 15 && c.c == G_CCMUX_PRIMITIVE && c.d == 7;
    int alphaTex = (c.aa == c.ab || c.ac == 7) && c.ad == G_ACMUX_TEXEL0;
    int alphaPrim = c.aa == G_ACMUX_TEXEL0 && c.ab == 7 && c.ac == G_ACMUX_PRIMITIVE && c.ad == 7;

    *tint = 0xFFFF;
    *modulate = 0;
    if ((sOtherModeH & (3u << G_MDSFT_CYCLETYPE)) == G_CYC_COPY)
        return 0;
    if ((sOtherModeH & (3u << G_MDSFT_CYCLETYPE)) != G_CYC_2CYCLE && (rgbTex || rgbPrim) && (alphaTex || alphaPrim)) {
        *tint = (rgbPrim ? sPrimColor & 0xFFF : 0xFFF) | (alphaPrim ? sPrimColor & 0xF000 : 0xF000);
        *modulate = rgbPrim || alphaPrim;
        return 0;
    }
    return 1;
}

static PspTexEntry* psp_gfx_tex_get(u32 fmt, u32 siz, u32 width, u32 height, u32 tile, u32 variant) {
    PspTexEntry* e;
    const u8* src = psp_gfx_tex_source(sLoadAddr);
    u32 srcW = sLoadBlock ? width : (sLoadW << sLoadSiz) >> siz;
    u32 srcX = (sLoadUls << sLoadSiz) >> siz;
    /* Tiled loads encode T at <<1, so use the render tile's 10.2 ult instead */
    u32 srcY = sLoadBlock ? sLoadUlt : (sTileUlt[tile] >> 2);
    u32 paletteKey = fmt == G_IM_FMT_CI
                         ? (sPaletteHash << 6) | (((sOtherModeH >> G_MDSFT_TEXTLUT) & 3) << 4) | sTilePalette[tile]
                         : 0;

    u32 potW;
    u32 potH;
    u32 count;
    u32 i;
    u32 x;
    u32 y;

    if (src == NULL || width == 0 || height == 0 || width > 512 || height > 512 || srcW == 0) {
        return NULL;
    }
    for (i = 0; i < sTexEntryCount; i++) {
        e = &sTexEntries[i];
        if (e->addr == sLoadAddr && e->fmt == fmt && e->siz == siz && e->srcW == srcW && e->width == width &&
            e->height == height && e->srcX == srcX && e->srcY == srcY && e->paletteKey == paletteKey &&
            e->variant == variant &&
            (!variant || (e->prim == sPrimColor8888 && e->env == sEnvColor && e->combine0 == sCombineW0 &&
                          e->combine1 == sCombineW1 && e->mode == sOtherModeH && e->lod == sPrimLodFrac))) {
            e->stamp = sFrameStamp;
            return e;
        }
    }
    potW = psp_gfx_next_pow2(width);
    potH = psp_gfx_next_pow2(height);
    if (potW < 8) {
        potW = 8;
    }
    if (potH < PSP_TEX_MIN_HEIGHT) {
        potH = PSP_TEX_MIN_HEIGHT;
    }
    count = potW * potH;
    if (sTexPoolUsed + count > PSP_TEX_POOL_SIZE) {
        psp_gfx_pool_full(count);
        return NULL;
    }

    if (sTexEntryCount < PSP_TEX_SLOTS) {
        e = &sTexEntries[sTexEntryCount++];
    } else {
        u32 victim = sTexReplaceNext;
        u32 i;

        /* Prefer a tile that is not referenced by the frame being built */
        for (i = 0; i < PSP_TEX_SLOTS; i++) {
            u32 index = (sTexReplaceNext + i) % PSP_TEX_SLOTS;

            if (sTexEntries[index].stamp != sFrameStamp) {
                victim = index;
                break;
            }
        }
        e = &sTexEntries[victim];
        sTexReplaceNext = (victim + 1) % PSP_TEX_SLOTS;
    }
    e->addr = sLoadAddr;
    e->fmt = fmt;
    e->siz = siz;
    e->srcW = srcW;
    e->width = width;
    e->height = height;
    e->srcX = srcX;
    e->srcY = srcY;
    e->native = PspGfx_TextureByteOrder(src);
    e->paletteKey = paletteKey;
    e->variant = variant;
    e->combine0 = sCombineW0;
    e->combine1 = sCombineW1;
    e->mode = sOtherModeH;
    e->lod = sPrimLodFrac;
    e->prim = sPrimColor8888;
    e->env = sEnvColor;
    e->potW = potW;
    e->potH = potH;
    e->stamp = sFrameStamp;
    sTexDecodeCount++;
    /* Each queued texture starts on a cache line */
    e->pixels = &sTexPool[sTexPoolUsed];
    sTexPoolUsed += count;
    for (y = 0; y < potH; y++) {
        for (x = 0; x < potW; x++) {
            e->pixels[y * potW + x] =
                psp_gfx_decode_texel(e, src, x < width ? x : width - 1, y < height ? y : height - 1);
            if (variant)
                e->pixels[y * potW + x] = psp_gfx_combine_texel(e->pixels[y * potW + x]);
        }
    }
    return e;
}

static void psp_gfx_texrect(const Gfx* cur) {
    u32 w0 = cur->words.w0;
    u32 w1 = cur->words.w1;
    const Gfx* half1 = cur + 1;
    const Gfx* half2 = cur + 2;
    u32 tile = _SHIFTR(w1, 24, 3);
    u32 cycle = sOtherModeH & (3u << G_MDSFT_CYCLETYPE);
    s32 x0 = _SHIFTR(w1, 12, 12) >> 2;
    s32 y0 = _SHIFTR(w1, 0, 12) >> 2;
    s32 x1 = _SHIFTR(w0, 12, 12) >> 2;
    s32 y1 = _SHIFTR(w0, 0, 12) >> 2;
    float u0;
    float v0;
    float du;
    float dv;
    PspTexEntry* e;
    u32 tint;
    u32 variant;
    int modulate;

    if ((half1->words.w0 >> 24) != G_RDPHALF_1 || (half2->words.w0 >> 24) != G_RDPHALF_2) {
        return;
    }
    if (!psp_gfx_targets_display()) {
        return;
    }
    if (cycle == G_CYC_COPY) {
        x1++;
        y1++;
    }
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    if (cycle != G_CYC_COPY && sCombineW0 == 0xFCFFFFFFu && sCombineW1 == 0xFFFDF6FBu) {
        PspGfx_DrawSolidRect(x0, y0, x1, y1, sPrimColor8888);
        return;
    }

    u0 = (float) (s16) (half1->words.w1 >> 16) / 32.0f - (float) sTileUls[tile] / 4.0f;
    v0 = (float) (s16) half1->words.w1 / 32.0f - (float) sTileUlt[tile] / 4.0f;
    du = (float) (s16) (half2->words.w1 >> 16) / 1024.0f;
    dv = (float) (s16) half2->words.w1 / 1024.0f;
    if (cycle == G_CYC_COPY) {
        du *= 0.25f;
    }
    variant = psp_gfx_texture_style(&tint, &modulate);
    e = psp_gfx_tex_get(sTileFmt[tile], sTileSiz[tile], sTileWidth[tile], sTileHeight[tile], tile, variant);
    if (e != NULL) {
        int flip = (w0 >> 24) == G_TEXRECTFLIP;
        float u1 = u0 + du * (flip ? y1 - y0 : x1 - x0);
        float v1 = v0 + dv * (flip ? x1 - x0 : y1 - y0);

        PspGfx_DrawTexQuad(x0, y0, x1, y1, e->pixels, e->potW, e->potH, u0, v0, u1, v1, tint, modulate, 1, flip);
    }
}

static void psp_gfx_walk(const Gfx* dl) {
    const Gfx* cur = dl;

    while (sBudget != 0) {
        u32 w0 = cur->words.w0;
        u32 w1 = cur->words.w1;
        u32 op = w0 >> 24;

        sBudget--;

        switch (op) {
            case G_ENDDL:
                return;
            case G_DL: {
                const Gfx* sub = (const Gfx*) (u32) psp_gfx_ram_addr(w1);
                if (sub != NULL && sub != cur) {
                    psp_gfx_walk(sub);
                }
                break;
            }
            case G_VTX: {
                u32 n = _SHIFTR(w0, 12, 8);
                u32 v0 = _SHIFTR(w0, 1, 7) - n;
                const Vtx* src = (const Vtx*) (u32) psp_gfx_ram_addr(w1);
                u32 i;

                for (i = 0; i < n && (v0 + i) < PSP_GFX_MAX_VTX; i++) {
                    sVtx[v0 + i] = src[i];
                }
                break;
            }
            case G_MTX: {
                u32 params = (w0 & 0xFF) ^ G_MTX_PUSH;
                float mf[4][4];

                psp_gfx_mtx_l2f(mf, (const Mtx*) (u32) psp_gfx_ram_addr(w1));

                if (params & G_MTX_PROJECTION) {
                    static int sLoggedProj;

                    if (sLoggedProj < 6) {
                        sLoggedProj++;
                        PspLog("[gfx] proj w1=%08x seg1=%08x m00=%d\n", (unsigned) w1, (unsigned) gSegments[1],
                               (int) (mf[0][0] * 1024.0f));
                    }
                    if ((params & G_MTX_PUSH) && (sProjectionDepth < PSP_GFX_MTX_STACK)) {
                        psp_gfx_mat_copy(sProjectionStack[sProjectionDepth], sProjection);
                        sProjectionDepth++;
                    }
                    if (params & G_MTX_LOAD) {
                        psp_gfx_mat_copy(sProjection, mf);
                    } else {
                        psp_gfx_mat_mul(sProjection, mf, sProjection);
                    }
                    sProjectionSet = 1;
                    /* Frustum and combined view matrices keep -1 in the w column */
                    sProjectionPerspective = sProjection[2][3] != 0.0f;
                    if (++sProjectionSerial == 0) {
                        sProjectionSerial = 1;
                    }
                    psp_gfx_update_viewport_projection();
                } else {
                    if ((params & G_MTX_PUSH) && (sModelViewDepth < PSP_GFX_MTX_STACK)) {
                        psp_gfx_mat_copy(sModelViewStack[sModelViewDepth], sModelView);
                        sModelViewDepth++;
                    }
                    if (params & G_MTX_LOAD) {
                        psp_gfx_mat_copy(sModelView, mf);
                    } else {
                        psp_gfx_mat_mul(sModelView, mf, sModelView);
                    }
                    if (++sModelViewSerial == 0) {
                        sModelViewSerial = 1;
                    }
                }
                psp_gfx_mat_mul(sMP, sModelView, sProjection);
                break;
            }
            case G_POPMTX: {
                u32 n = w1;

                while ((n-- != 0) && (sModelViewDepth > 0)) {
                    sModelViewDepth--;
                    psp_gfx_mat_copy(sModelView, sModelViewStack[sModelViewDepth]);
                }
                psp_gfx_mat_mul(sMP, sModelView, sProjection);
                if (++sModelViewSerial == 0) {
                    sModelViewSerial = 1;
                }
                break;
            }
            case G_MOVEMEM: {
                u32 index = _SHIFTR(w0, 0, 8);

                if (index == G_MV_VIEWPORT) {
                    const Vp* vp = (const Vp*) (u32) psp_gfx_ram_addr(w1);

                    if (vp != NULL) {
                        sViewport = *vp;
                        psp_gfx_update_viewport_projection();
                    }
                } else if (index == G_MV_LIGHT) {
                    u32 ofs = _SHIFTR(w0, 8, 8) * 8;
                    const Light* light = (const Light*) (u32) psp_gfx_ram_addr(w1);

                    /* L0 starts at offset 48; lookat entries are below it */
                    if (light != NULL && ofs >= 48) {
                        u32 n = ofs / 24 - 1;

                        if (n >= 1 && n <= 8) {
                            PspGfxLightSlot* slot = &sLightSlots[n - 1];
                            float x = (float) (s8) light->l.dir[0];
                            float y = (float) (s8) light->l.dir[1];
                            float z = (float) (s8) light->l.dir[2];
                            float len2 = (x * x) + (y * y) + (z * z);

                            if (len2 > 0.000001f) {
                                float inverseLength = 1.0f / sqrtf(len2);

                                x *= inverseLength;
                                y *= inverseLength;
                                z *= inverseLength;
                            } else {
                                x = 0.0f;
                                y = 0.0f;
                                z = 0.0f;
                            }
                            slot->x = x;
                            slot->y = y;
                            slot->z = z;
                            slot->r = light->l.col[0];
                            slot->g = light->l.col[1];
                            slot->b = light->l.col[2];
                        }
                    }
                }
                break;
            }
            case G_TRI1:
                psp_gfx_triangle(_SHIFTR(w0, 17, 7), _SHIFTR(w0, 9, 7), _SHIFTR(w0, 1, 7));
                break;
            case G_TRI2:
                psp_gfx_triangle(_SHIFTR(w0, 17, 7), _SHIFTR(w0, 9, 7), _SHIFTR(w0, 1, 7));
                psp_gfx_triangle(_SHIFTR(w1, 17, 7), _SHIFTR(w1, 9, 7), _SHIFTR(w1, 1, 7));
                break;
            case G_SETTIMG:
                sTexSiz = _SHIFTR(w0, 19, 2);
                sTexW = _SHIFTR(w0, 0, 12) + 1;
                sTexAddr = psp_gfx_ram_addr(w1);
                s3dTexDirty = 1;
                break;
            case G_SETTILE: {
                u32 fmt = _SHIFTR(w0, 21, 3);
                u32 siz = _SHIFTR(w0, 19, 2);
                u32 tile = _SHIFTR(w1, 24, 3);

                sTileFmt[tile] = fmt;
                sTileSiz[tile] = siz;
                sTileTmem[tile] = w0 & 511;
                sTilePalette[tile] = (w1 >> 20) & 15;
                s3dTexDirty = 1;
                break;
            }
            case G_SETTILESIZE: {
                u32 tile = _SHIFTR(w1, 24, 3);

                sTileUls[tile] = _SHIFTR(w0, 12, 12);
                sTileUlt[tile] = _SHIFTR(w0, 0, 12);
                sTileWidth[tile] = ((_SHIFTR(w1, 12, 12) - sTileUls[tile]) >> 2) + 1;
                sTileHeight[tile] = ((_SHIFTR(w1, 0, 12) - sTileUlt[tile]) >> 2) + 1;
                s3dTexDirty = 1;
                break;
            }
            case G_LOADTLUT: {
                u32 tile = (w1 >> 24) & 7;
                u32 first = sTileTmem[tile] >= 256 ? sTileTmem[tile] - 256 : 0;
                u32 count = ((w1 >> 14) & 1023) + 1;
                const u8* src = psp_gfx_tex_source(sTexAddr);
                u32 i;

                if (src != NULL) {
                    int native = PspGfx_TextureByteOrder(src);
                    u32 hash = 2166136261u;

                    for (i = 0; i < count && first + i < 256; i++) {
                        sPalette[first + i] = psp_gfx_read16(src, i, native);
                    }
                    for (i = 0; i < 256; i++) {
                        hash = (hash ^ sPalette[i]) * 16777619u;
                    }
                    sPaletteHash = hash & 0xFFFFu;
                }
                s3dTexDirty = 1;
                break;
            }
            case G_SETCIMG:
                sColorTarget = w1;
                break;
            case G_SETENVCOLOR:
                sEnvColor = ((w1 >> 24) & 255) | ((w1 >> 8) & 0xFF00) | ((w1 << 8) & 0xFF0000) | (w1 << 24);
                break;
            case G_LOADBLOCK:
            case G_LOADTILE:
                sLoadAddr = sTexAddr;
                sLoadW = sTexW;
                sLoadSiz = sTexSiz;
                sLoadBlock = op == G_LOADBLOCK;
                sLoadUls = _SHIFTR(w0, 12, 12) >> (sLoadBlock ? 0 : 2);
                sLoadUlt = _SHIFTR(w0, 0, 12) >> (sLoadBlock ? 0 : 2);
                s3dTexDirty = 1;
                break;
            case G_SETOTHERMODE_H: {
                u32 length = (w0 & 0xFF) + 1;
                u32 shift = 32 - _SHIFTR(w0, 8, 8) - length;
                u32 mask = (0xFFFFFFFFu >> (32 - length)) << shift;

                sOtherModeH = (sOtherModeH & ~mask) | (w1 & mask);
                break;
            }
            case G_SETOTHERMODE_L: {
                u32 length = (w0 & 0xFF) + 1;
                u32 shift = 32 - _SHIFTR(w0, 8, 8) - length;
                u32 mask = (0xFFFFFFFFu >> (32 - length)) << shift;

                sOtherModeL = (sOtherModeL & ~mask) | (w1 & mask);
                break;
            }
            case G_GEOMETRYMODE:
                sGeometryMode = (sGeometryMode & (w0 & 0x00FFFFFFu)) | w1;
                break;
            case G_MOVEWORD: {
                u32 index = _SHIFTR(w0, 16, 8);
                u32 offset = w0 & 0xFFFF;

                if (index == G_MW_NUMLIGHT) {
                    sNumLights = (s32) (w1 / 24);
                    if (sNumLights > 7) {
                        sNumLights = 7;
                    }
                    /* The highest slot holds the ambient light */
                    if (sNumLights >= 0) {
                        sAmbientR = sLightSlots[sNumLights].r;
                        sAmbientG = sLightSlots[sNumLights].g;
                        sAmbientB = sLightSlots[sNumLights].b;
                    }
                } else if (index == G_MW_LIGHTCOL) {
                    u32 slot = offset / 0x18;

                    if (slot < 8) {
                        sLightSlots[slot].r = (w1 >> 24) & 0xFF;
                        sLightSlots[slot].g = (w1 >> 16) & 0xFF;
                        sLightSlots[slot].b = (w1 >> 8) & 0xFF;
                    }
                } else if (index == G_MW_SEGMENT) {
                    u32 segment = offset / 4;

                    if (segment < 16) {
                        gSegments[segment] = w1;
                    }
                }
                break;
            }
            case G_MODIFYVTX: {
                u32 vtx = (w0 & 0xFFFF) >> 1;
                u32 where = _SHIFTR(w0, 16, 8);

                if (vtx < PSP_GFX_MAX_VTX && where == G_MWO_POINT_ST) {
                    sVtx[vtx].v.tc[0] = (w1 >> 16) & 0xFFFF;
                    sVtx[vtx].v.tc[1] = w1 & 0xFFFF;
                }
                break;
            }
            case G_RDPSETOTHERMODE:
                sOtherModeH = w0 & 0x00FFFFFFu;
                break;
            case G_TEXRECT:
            case G_TEXRECTFLIP:
                psp_gfx_texrect(cur);
                cur += 2;
                break;
            case G_SETPRIMCOLOR: {
                u32 r = (w1 >> 24) & 0xFF;
                u32 g = (w1 >> 16) & 0xFF;
                u32 b = (w1 >> 8) & 0xFF;
                u32 a = w1 & 0xFF;

                sPrimLodFrac = w0 & 255;
                sPrimColor = (u16) (((a >> 4) << 12) | ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4));
                sPrimColor8888 = r | (g << 8) | (b << 16) | (a << 24);
                break;
            }
            case G_SETCOMBINE:
                sCombineW0 = w0;
                sCombineW1 = w1;
                break;
            case G_SETSCISSOR:
                PspGfx_SetScissor((s32) (_SHIFTR(w0, 12, 12) >> 2), (s32) (_SHIFTR(w0, 0, 12) >> 2),
                                  (s32) (_SHIFTR(w1, 12, 12) >> 2), (s32) (_SHIFTR(w1, 0, 12) >> 2));
                break;
            case G_SETFILLCOLOR: {
                u32 texel = (w1 >> 16) & 0xFFFF;
                u32 r = (texel >> 11) & 0x1F;
                u32 g = (texel >> 6) & 0x1F;
                u32 b = (texel >> 1) & 0x1F;

                sFillColor = ((r << 3) | (r >> 2)) | (((g << 3) | (g >> 2)) << 8) | (((b << 3) | (b >> 2)) << 16) |
                             (0xFFu << 24);
                break;
            }
            case G_FILLRECT: {
                u32 ulx = _SHIFTR(w1, 14, 10);
                u32 uly = _SHIFTR(w1, 2, 10);
                u32 lrx = _SHIFTR(w0, 14, 10);
                u32 lry = _SHIFTR(w0, 2, 10);
                u32 cycle = sOtherModeH & (3u << G_MDSFT_CYCLETYPE);
                int inclusive = cycle == G_CYC_FILL || cycle == G_CYC_COPY;

                if (!psp_gfx_targets_display()) {
                    break;
                }

                PspGfx_DrawSolidRect(ulx, uly, lrx + inclusive, lry + inclusive,
                                     cycle == G_CYC_FILL ? sFillColor : sPrimColor8888);
                break;
            }
            default:
                break;
        }
        cur++;
    }
}

void PspGfx_RunDisplayList(const Gfx* dl, u32 size) {
    int first = (sDrawCount == 0);

    (void) size;

    if (first) {
        int i;

        for (i = 0; i < 8; i++) {
            sTileFmt[i] = 0xFF;
            sTileSiz[i] = 0xFF;
        }
        psp_gfx_mat_identity(sModelView);
        psp_gfx_mat_identity(sProjection);
        psp_gfx_mat_identity(sMP);
        sProjectionPerspective = 0;
        sModelViewDepth = 0;
        sProjectionDepth = 0;
        sViewport.vp.vscale[0] = 240 * 4;
        sViewport.vp.vscale[1] = 136 * 4;
        sViewport.vp.vscale[2] = G_MAXZ;
        sViewport.vp.vscale[3] = 0;
        sViewport.vp.vtrans[0] = 240 * 4;
        sViewport.vp.vtrans[1] = 136 * 4;
        sViewport.vp.vtrans[2] = 0;
        sViewport.vp.vtrans[3] = 0;
        psp_gfx_update_viewport_projection();
    }

    sColorTarget = 0;
    sBudget = 2000000;
    if (dl != NULL) {
        psp_gfx_walk(dl);
    }

    sDrawCount++;
}

/* Called once per presented frame, after the GU has finished */
void PspGfx_EndFrame(void) {
    sFrameStamp++;
    /* Pool compaction may move tile pixels between frames */
    s3dTexDirty = 1;
    /* Reclaim once the pool is half full, keeping this frame's tiles for reuse */
    if (sTexPoolUsed > PSP_TEX_POOL_SIZE / 2) {
        psp_gfx_tex_compact(sFrameStamp - 1);
    }
}

u32 PspGfx_DrawCount(void) {
    return sDrawCount;
}

u32 PspGfx_TexDecodeCount(void) {
    return sTexDecodeCount;
}

/* Pixel data was rewritten in place (colour transform), so drop decoded copies */
void PspGfx_InvalidateTexture(const void* data, u32 size) {
    if (data != NULL && size != 0) {
        psp_gfx_tex_invalidate((u32) data, size);
    }
}

/* Scratch for presenting a CPU framebuffer; kept out of the tile cache */
static u16 sPresentBuffer[512 * 256] __attribute__((aligned(64)));

void PspGfx_PresentFramebuffer(const u16* pixels) {
    u32 x, y;

    if (pixels == NULL || !PspGfx_Init())
        return;
    for (y = 0; y < 256; y++) {
        for (x = 0; x < 512; x++) {
            sPresentBuffer[y * 512 + x] = x < 320 && y < 240 ? psp_gfx_rgba5551_to_4444(pixels[y * 320 + x]) : 0;
        }
    }
    PspGfx_BeginFrame();
    PspGfx_DrawTexQuad(0, 0, 320, 240, sPresentBuffer, 512, 256, 0, 0, 320, 240, 0xFFFF, 0, 0, 0);
    PspGfx_Present();
}
