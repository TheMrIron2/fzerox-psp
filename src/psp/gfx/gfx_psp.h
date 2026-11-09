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

/* 3D vertex in native GE order: u, v, color, object-space x, y, z */
typedef struct {
    float u;
    float v;
    u32 color;
    float x;
    float y;
    float z;
} PspGfxVertex3D;

/* Matrices use the n64psp mat4f layout, which already matches GU column-major */
typedef struct {
    const float* projection;
    const float* modelview;
    u32 projectionSerial;
    u32 modelviewSerial;
    int depthTest;
    int depthWrite;
    int cullFront;
    int cullBack;
    /* Decoded RGBA4444 tile, power-of-two size; NULL means untextured */
    const u16* texturePixels;
    s32 texWidth;
    s32 texHeight;
    int modulate;
    /* 0 = modulate, 1 = replace, 2 = blend by texel alpha */
    int texFunc;
    /* 1 = alpha blend source over destination, 0 = opaque */
    int blend;
    /* 1 = clamp, 0 = repeat */
    int wrapS;
    int wrapT;
    /* 1 = bilinear, 0 = nearest */
    int filter;
} PspGfxDrawState;

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

/* 3D triangle already in object space; the GE applies modelview and projection */
void PspGfx_DrawTriangles3D(const PspGfxVertex3D* vertices, u32 count, const PspGfxDrawState* state);

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
