#include "src/psp/gfx/gfx_psp.h"
#include "src/psp/n64ram.h"
#include "src/psp/psp.h"
#include "PR/mbi.h"
#include "PR/gu.h"

#define PSP_GFX_MAX_VTX 64
#define PSP_TEX_SLOTS 16
#define PSP_TEX_POOL_SIZE (1 << 20) /* 2 MB of RGBA4444 texels */
#define PSP_TEX_MIN_HEIGHT 256

static u32 sDrawCount;
static u32 sBudget;
static u32 sCmdCount;
static u32 sTriCount;
static u32 sTexRectCount;
static u32 sOpHist[256];

/* Matrices and vertices */
static float sModelView[4][4];
static float sProjection[4][4];
static Vp sViewport;
static Vtx sVtx[PSP_GFX_MAX_VTX];

/* RDP state used by the 2D path */
static u32 sTexAddr;
static u32 sTexFmt;
static u32 sTexSiz;
static u32 sTexW;
static u32 sTileFmt[8];
static u32 sTileSiz[8];
static u32 sLoadUls;
static u32 sLoadUlt;
static int sLoadValid;
static u32 sPrimColor = 0xFFFFFFFF;
static u32 sFillColor;

/* Decoded texture cache, rebuilt each display list */
typedef struct {
    u32 addr;
    u32 fmt;
    u32 siz;
    u32 srcW;
    u32 potW;
    u32 potH;
    u16* pixels;
} PspTexEntry;

static u16 sTexPool[PSP_TEX_POOL_SIZE] __attribute__((aligned(64)));
static u32 sTexPoolUsed;
static PspTexEntry sTexEntries[PSP_TEX_SLOTS];
static u32 sTexEntryCount;

static int sLoggedTex;

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

/* N64 s15.16 matrix to float, mirrors guMtxL2F without the libultra dependency */
static void psp_gfx_mtx_l2f(float mf[4][4], const Mtx* m) {
    const u32* ai = (const u32*) &m->m[0][0];
    const u32* af = (const u32*) &m->m[2][0];
    int i, j;

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 2; j++) {
            u32 e1 = (*ai & 0xffff0000) | ((*af >> 16) & 0xffff);
            u32 e2 = ((*(ai++) << 16) & 0xffff0000) | (*(af++) & 0xffff);

            mf[i][j * 2] = (float) (s32) e1 / 65536.0f;
            mf[i][j * 2 + 1] = (float) (s32) e2 / 65536.0f;
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
    float mv[4];
    float clip[4];
    float w;
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

    for (i = 0; i < 4; i++) {
        mv[i] = sModelView[i][0] * p[0] + sModelView[i][1] * p[1] + sModelView[i][2] * p[2] + sModelView[i][3] * p[3];
    }
    for (i = 0; i < 4; i++) {
        clip[i] =
            sProjection[i][0] * mv[0] + sProjection[i][1] * mv[1] + sProjection[i][2] * mv[2] + sProjection[i][3] * mv[3];
    }

    w = clip[3];
    if (w < 0.0001f && w > -0.0001f) {
        w = 0.0001f;
    }

    out->x = ((float) sViewport.vp.vscale[0] * (clip[0] / w) + (float) sViewport.vp.vtrans[0]) * 0.25f;
    out->y = ((float) sViewport.vp.vscale[1] * (clip[1] / w) + (float) sViewport.vp.vtrans[1]) * 0.25f;
    out->z = ((float) sViewport.vp.vscale[2] * (clip[2] / w) + (float) sViewport.vp.vtrans[2]) * 0.25f;
    out->color = psp_gfx_vertex_color(v);
}

static void psp_gfx_triangle(u32 i0, u32 i1, u32 i2) {
    PspGfxVertex tri[3];

    psp_gfx_transform(i0, &tri[0]);
    psp_gfx_transform(i1, &tri[1]);
    psp_gfx_transform(i2, &tri[2]);
    PspGfx_DrawTriangle2D(tri);
    sTriCount++;
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

    switch (e->fmt) {
        case G_IM_FMT_RGBA:
            idx = ((u32) y * e->srcW + (u32) x) * 2;
            return psp_gfx_rgba5551_to_4444(((u32) src[idx] << 8) | src[idx + 1]);
        case G_IM_FMT_IA: {
            u32 i;
            u32 a;

            idx = ((u32) y * e->srcW + (u32) x) * 2;
            i = src[idx] >> 4;
            a = src[idx + 1] >> 4;
            return (u16) ((a << 12) | (i << 8) | (i << 4) | i);
        }
        case G_IM_FMT_I: {
            u32 i;

            idx = ((u32) y * e->srcW + (u32) x) >> 1;
            i = (x & 1) ? (src[idx] & 0xF) : (src[idx] >> 4);
            return (u16) (0xF000 | (i << 8) | (i << 4) | i);
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

static PspTexEntry* psp_gfx_tex_get(u32 addr, u32 fmt, u32 siz, u32 srcW, u32 maxRow) {
    PspTexEntry* e;
    u32 potW;
    u32 potH;
    u32 i;
    u32 count;
    u16* pixels;

    if ((addr == 0) || (srcW == 0)) {
        return NULL;
    }

    for (i = 0; i < sTexEntryCount; i++) {
        e = &sTexEntries[i];
        if ((e->addr == addr) && (e->fmt == fmt) && (e->siz == siz) && (e->srcW == srcW)) {
            return e;
        }
    }

    if (sTexEntryCount >= PSP_TEX_SLOTS) {
        return NULL;
    }

    potW = psp_gfx_next_pow2(srcW);
    if (potW < 8) {
        potW = 8;
    }
    if (potW > 512) {
        return NULL;
    }
    potH = psp_gfx_next_pow2(maxRow);
    if (potH < PSP_TEX_MIN_HEIGHT) {
        potH = PSP_TEX_MIN_HEIGHT;
    }
    if (potH > 512) {
        return NULL;
    }

    count = potW * potH;
    if ((sTexPoolUsed + count) > PSP_TEX_POOL_SIZE) {
        PspLog("[gfx] tex pool full (%u texels)\n", (unsigned) sTexPoolUsed);
        return NULL;
    }

    pixels = &sTexPool[sTexPoolUsed];
    sTexPoolUsed += count;

    for (i = 0; i < count; i++) {
        pixels[i] = 0;
    }

    e = &sTexEntries[sTexEntryCount];
    e->addr = addr;
    e->fmt = fmt;
    e->siz = siz;
    e->srcW = srcW;
    e->potW = potW;
    e->potH = potH;
    e->pixels = pixels;
    sTexEntryCount++;

    return e;
}

static void psp_gfx_convert(PspTexEntry* e, const u8* src, s32 sx, s32 sy, s32 w, s32 h) {
    s32 x;
    s32 y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            s32 tx = sx + x;
            s32 ty = sy + y;

            if (tx < 0) {
                tx += (s32) e->srcW;
            }
            if (tx >= (s32) e->srcW) {
                tx -= (s32) e->srcW;
            }
            if ((ty < 0) || (ty >= (s32) e->potH)) {
                continue;
            }
            e->pixels[(u32) ty * e->potW + (u32) tx] = psp_gfx_decode_texel(e, src, tx, ty);
        }
    }
}

static void psp_gfx_texrect(const Gfx* cur) {
    u32 w0 = cur->words.w0;
    u32 w1 = cur->words.w1;
    const Gfx* half1 = cur + 1;
    const Gfx* half2 = cur + 2;
    u32 sBits;
    s32 x0;
    s32 y0;
    s32 x1;
    s32 y1;
    s32 srcX;
    s32 srcY;
    s32 w;
    s32 h;
    const u8* src;
    PspTexEntry* e;
    u32 tile;
    u32 fmt;
    u32 siz;

    if ((half1->words.w0 >> 24) != G_RDPHALF_1 || (half2->words.w0 >> 24) != G_RDPHALF_2) {
        return;
    }

    sBits = half1->words.w1;

    tile = _SHIFTR(w1, 24, 3);
    fmt = (sTileFmt[tile] != 0xFF) ? sTileFmt[tile] : sTexFmt;
    siz = (sTileSiz[tile] != 0xFF) ? sTileSiz[tile] : sTexSiz;

    x1 = (s32) (_SHIFTR(w0, 12, 12) >> 2);
    y1 = (s32) (_SHIFTR(w0, 0, 12) >> 2);
    x0 = (s32) (_SHIFTR(w1, 12, 12) >> 2);
    y0 = (s32) (_SHIFTR(w1, 0, 12) >> 2);

    if ((x1 <= x0) || (y1 <= y0)) {
        return;
    }

    if (sLoadValid) {
        srcX = (s32) sLoadUls;
        srcY = (s32) sLoadUlt;
    } else {
        srcX = ((s32) (s16) (sBits >> 16)) >> 5;
        srcY = ((s32) (s16) (sBits & 0xFFFF)) >> 5;
    }

    w = x1 - x0;
    h = y1 - y0;

    src = psp_gfx_tex_source(sTexAddr);
    if (src != NULL) {
        e = psp_gfx_tex_get(sTexAddr, fmt, siz, sTexW, (u32) (srcY + h));
        if (e != NULL) {
            psp_gfx_convert(e, src, srcX, srcY, w, h);

            if (!sLoggedTex) {
                PspLog("[gfx] tex addr=%08x fmt=%u siz=%u w=%u rect=%d,%d %dx%d src=%d,%d texel=%04x\n",
                       (unsigned) sTexAddr, (unsigned) fmt, (unsigned) siz, (unsigned) sTexW, (int) x0, (int) y0,
                       (int) w, (int) h, (int) srcX, (int) srcY, (unsigned) e->pixels[0]);
                sLoggedTex = 1;
            }

            PspGfx_DrawTexQuad(x0, y0, x1, y1, e->pixels, (s32) e->potW, (s32) e->potH, srcX, srcY, srcX + w,
                               srcY + h, sPrimColor, (fmt == G_IM_FMT_RGBA) ? 0 : 1, 1);
        }
    }

    sTexRectCount++;
    sLoadValid = 0;
}

static void psp_gfx_walk(const Gfx* dl) {
    const Gfx* cur = dl;

    while (sBudget != 0) {
        u32 w0 = cur->words.w0;
        u32 w1 = cur->words.w1;
        u32 op = w0 >> 24;

        sBudget--;
        sCmdCount++;
        sOpHist[op]++;

        switch (op) {
            case G_ENDDL:
                return;
            case G_DL: {
                const Gfx* sub = (const Gfx*)(u32) w1;
                if (sub != NULL && sub != cur) {
                    psp_gfx_walk(sub);
                }
                break;
            }
            case G_VTX: {
                u32 n = _SHIFTR(w0, 12, 8);
                u32 v0 = _SHIFTR(w0, 1, 7) - n;
                const Vtx* src = (const Vtx*)(u32) w1;
                u32 i;

                for (i = 0; i < n && (v0 + i) < PSP_GFX_MAX_VTX; i++) {
                    sVtx[v0 + i] = src[i];
                }
                break;
            }
            case G_MTX: {
                u32 params = (w0 & 0xFF) ^ G_MTX_PUSH;
                float mf[4][4];

                psp_gfx_mtx_l2f(mf, (const Mtx*)(u32) w1);

                if (params & G_MTX_PROJECTION) {
                    if (params & G_MTX_LOAD) {
                        psp_gfx_mat_copy(sProjection, mf);
                    } else {
                        psp_gfx_mat_mul(sProjection, sProjection, mf);
                    }
                } else {
                    if (params & G_MTX_LOAD) {
                        psp_gfx_mat_copy(sModelView, mf);
                    } else {
                        psp_gfx_mat_mul(sModelView, sModelView, mf);
                    }
                }
                break;
            }
            case G_MOVEMEM: {
                u32 index = _SHIFTR(w0, 0, 8);

                if (index == G_MV_VIEWPORT) {
                    sViewport = *(const Vp*)(u32) w1;
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
                sTexFmt = _SHIFTR(w0, 21, 3);
                sTexSiz = _SHIFTR(w0, 19, 2);
                sTexW = _SHIFTR(w0, 0, 12) + 1;
                sTexAddr = w1;
                break;
            case G_SETTILE: {
                u32 fmt = _SHIFTR(w0, 21, 3);
                u32 siz = _SHIFTR(w0, 19, 2);
                u32 tile = _SHIFTR(w1, 24, 3);

                sTileFmt[tile] = fmt;
                sTileSiz[tile] = siz;
                break;
            }
            case G_LOADBLOCK:
                if (sTileSiz[G_TX_RENDERTILE] == G_IM_SIZ_4b) {
                    sLoadUls = _SHIFTR(w0, 12, 12) * 4;
                    sLoadUlt = _SHIFTR(w0, 0, 12) * 4;
                } else {
                    sLoadUls = _SHIFTR(w0, 12, 12);
                    sLoadUlt = _SHIFTR(w0, 0, 12);
                }
                sLoadValid = 1;
                break;
            case G_LOADTILE:
                if (sTileSiz[G_TX_RENDERTILE] == G_IM_SIZ_4b) {
                    sLoadUls = _SHIFTR(w0, 12, 12) >> 1;
                    sLoadUlt = _SHIFTR(w0, 0, 12) >> 1;
                } else {
                    sLoadUls = _SHIFTR(w0, 12, 12) >> 2;
                    sLoadUlt = _SHIFTR(w0, 0, 12) >> 2;
                }
                sLoadValid = 1;
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

                sPrimColor = (u16) (((a >> 4) << 12) | ((b >> 4) << 8) | ((g >> 4) << 4) | (r >> 4));
                break;
            }
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

                PspGfx_DrawSolidRect((s32) ulx, (s32) uly, (s32) lrx, (s32) lry, sFillColor);
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
        sViewport.vp.vscale[0] = 240 * 4;
        sViewport.vp.vscale[1] = 136 * 4;
        sViewport.vp.vscale[2] = G_MAXZ;
        sViewport.vp.vscale[3] = 0;
        sViewport.vp.vtrans[0] = 240 * 4;
        sViewport.vp.vtrans[1] = 136 * 4;
        sViewport.vp.vtrans[2] = 0;
        sViewport.vp.vtrans[3] = 0;
    }

    sTexPoolUsed = 0;
    sTexEntryCount = 0;
    sLoadValid = 0;
    sCmdCount = 0;
    sTriCount = 0;
    sTexRectCount = 0;
    sBudget = 2000000;
    if (dl != NULL) {
        psp_gfx_walk(dl);
    }

    if ((sDrawCount < 4) || ((sDrawCount % 120) == 0)) {
        PspLog("[gfx] dl%u cmds=%u tris=%u texrects=%u\n", (unsigned) sDrawCount, (unsigned) sCmdCount,
               (unsigned) sTriCount, (unsigned) sTexRectCount);
    }
    if (sDrawCount == 1) {
        int i;

        for (i = 0; i < 256; i++) {
            if (sOpHist[i] != 0) {
                PspLog("[gfx] op %02x=%u\n", i, (unsigned) sOpHist[i]);
            }
        }
    }
    sDrawCount++;
}

u32 PspGfx_DrawCount(void) {
    return sDrawCount;
}
