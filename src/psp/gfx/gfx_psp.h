#ifndef PSP_GFX_H
#define PSP_GFX_H

#include "PR/ultratypes.h"
#include "PR/gbi.h"

/* Backend-neutral renderer interface.
 * The display-list interpreter decodes N64 semantics and talks only to this.
 * Screen coordinates are N64 pixels; the backend maps them to the PSP display. */

typedef struct {
    u32 color;
    float x;
    float y;
    float z;
} PspGfxVertex;

int PspGfx_Init(void);
void PspGfx_BeginFrame(void);
void PspGfx_RunDisplayList(const Gfx* dl, u32 size);
void PspGfx_Present(void);
u32 PspGfx_DrawCount(void);

/* 3D triangle already transformed to screen space by the interpreter */
void PspGfx_DrawTriangle2D(const PspGfxVertex v[3]);

/* 2D helpers, N64 screen pixels */
void PspGfx_DrawSolidRect(s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba);
/* RGBA4444 texels already converted to PSP byte order, power-of-two size */
void PspGfx_DrawTexQuad(s32 x0, s32 y0, s32 x1, s32 y1, const u16* pixels, s32 texW, s32 texH, s32 u0, s32 v0,
                        s32 u1, s32 v1, u32 tint, int modulate, int blend);

#endif
