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
/* Marks a presented frame so decoded tiles from older frames can be recycled */
void PspGfx_EndFrame(void);
/* Number of texture tiles decoded since boot (0 on a repeated frame = cache hit) */
u32 PspGfx_TexDecodeCount(void);
/* Texture pixels were transformed in place; drop any decoded copies */
void PspGfx_InvalidateTexture(const void* data, u32 size);

/* 3D triangle already transformed to screen space by the interpreter */
void PspGfx_DrawTriangle2D(const PspGfxVertex v[3]);

/* 2D helpers, N64 screen pixels */
void PspGfx_DrawSolidRect(s32 x0, s32 y0, s32 x1, s32 y1, u32 rgba);
/* RGBA4444 texels already converted to PSP byte order, power-of-two size */
void PspGfx_DrawTexQuad(s32 x0, s32 y0, s32 x1, s32 y1, const u16* pixels, s32 texW, s32 texH, float u0, float v0,
                        float u1, float v1, u32 tint, int modulate, int blend, int flip);
/* Native colors use the same byte order as the machine */
void PspGfx_SetTextureByteOrder(const void* data, u32 size, int native);
int PspGfx_TextureByteOrder(const void* data);
int PspGfx_ReadFramebuffer(u16* pixels, s32 width, s32 height);
void PspGfx_PresentFramebuffer(const u16* pixels);
void PspGfx_SetScissor(s32 x0, s32 y0, s32 x1, s32 y1);

#endif
