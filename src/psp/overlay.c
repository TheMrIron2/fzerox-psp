#include <pspgu.h>
#include <pspctrl.h>
#include <pspkernel.h>
#include <stdio.h>
#include "src/psp/overlay.h"
#include "src/psp/profiler.h"

static const u8 sFont[][5] = {
    {0,0,0,0,0}, {0,0,95,0,0}, {0,7,0,7,0}, {20,127,20,127,20},
    {36,42,127,42,18}, {35,19,8,100,98}, {54,73,85,34,80}, {0,5,3,0,0},
    {0,28,34,65,0}, {0,65,34,28,0}, {20,8,62,8,20}, {8,8,62,8,8},
    {0,80,48,0,0}, {8,8,8,8,8}, {0,96,96,0,0}, {32,16,8,4,2},
    {62,81,73,69,62}, {0,66,127,64,0}, {66,97,81,73,70}, {33,65,69,75,49},
    {24,20,18,127,16}, {39,69,69,69,57}, {60,74,73,73,48}, {1,113,9,5,3},
    {54,73,73,73,54}, {6,73,73,41,30}, {0,54,54,0,0}, {0,86,54,0,0},
    {8,20,34,65,0}, {20,20,20,20,20}, {0,65,34,20,8}, {2,1,81,9,6},
    {50,73,121,65,62}, {126,17,17,17,126}, {127,73,73,73,54}, {62,65,65,65,34},
    {127,65,65,34,28}, {127,73,73,73,65}, {127,9,9,9,1}, {62,65,73,73,122},
    {127,8,8,8,127}, {0,65,127,65,0}, {32,64,65,63,1}, {127,8,20,34,65},
    {127,64,64,64,64}, {127,2,12,2,127}, {127,4,8,16,127}, {62,65,65,65,62},
    {127,9,9,9,6}, {62,65,81,33,94}, {127,9,25,41,70}, {70,73,73,73,49},
    {1,1,127,1,1}, {63,64,64,64,63}, {31,32,64,32,31}, {63,64,56,64,63},
    {99,20,8,20,99}, {7,8,112,8,7}, {97,81,73,69,67}, {0,127,65,65,0},
    {2,4,8,16,32}, {0,65,65,127,0}, {4,2,1,2,4}, {64,64,64,64,64}
};

static volatile int sOpen;
static unsigned int sPrevious;
static int sRelease;

typedef struct { unsigned int color; short x, y, z; } OverlayVertex;

int PspOverlay_Input(unsigned int buttons) {
    unsigned int pressed = buttons & ~sPrevious;
    int consume;
    sPrevious = buttons;
    consume = sOpen;
    if (pressed & PSP_CTRL_SELECT) {
        sOpen = !sOpen;
        if (!sOpen) { sRelease = 1; PspProfile_Control(1); }
    }
    if (sOpen && (pressed & PSP_CTRL_CROSS)) PspProfile_Control(0);
    if (sRelease && !(buttons & (PSP_CTRL_SELECT | PSP_CTRL_UP | PSP_CTRL_DOWN | PSP_CTRL_LEFT | PSP_CTRL_RIGHT | PSP_CTRL_CROSS))) sRelease = 0;
    return consume || sOpen || sRelease;
}

static void overlay_text(OverlayVertex* v, int* count, int x, int y, const char* text) {
    while (*text) {
        int c = (unsigned char) *text++;
        int col, row;
        if (c >= 32 && c < 96) {
            for (col = 0; col < 5; col++) for (row = 0; row < 7; row++) {
                if (sFont[c - 32][col] & (1 << row)) {
                    v[*count].color = 0xffffffff; v[*count].x = x + col; v[*count].y = y + row; v[*count].z = 0;
                    (*count)++;
                    v[*count].color = 0xffffffff; v[*count].x = x + col + 1; v[*count].y = y + row + 1; v[*count].z = 0;
                    (*count)++;
                }
            }
        }
        x += 6;
    }
}

static unsigned int sFpsTenths, sFrameTenths, sWorkTenths, sSyncTenths, sWaitTenths;

void PspOverlay_RecordFrame(unsigned int workUs, unsigned int syncUs, unsigned int waitUs,
                            unsigned long long presentedUs) {
    static unsigned long long start, work, sync, wait;
    static unsigned int frames;
    unsigned long long elapsed;
    if (!start) { start = presentedUs; return; }
    frames++;
    work += workUs;
    sync += syncUs;
    wait += waitUs;
    elapsed = presentedUs - start;
    if (elapsed < 1000000) return;
    sFpsTenths = (unsigned int) ((frames * 10000000ULL + elapsed / 2) / elapsed);
    sFrameTenths = (unsigned int) ((elapsed + frames * 50ULL) / (frames * 100ULL));
    sWorkTenths = (unsigned int) ((work + frames * 50ULL) / (frames * 100ULL));
    sSyncTenths = (unsigned int) ((sync + frames * 50ULL) / (frames * 100ULL));
    sWaitTenths = (unsigned int) ((wait + frames * 50ULL) / (frames * 100ULL));
    start = presentedUs;
    frames = 0;
    work = sync = wait = 0;
}

void PspOverlay_Draw(void) {
    int open = sOpen;
    OverlayVertex* v;
    char text[48];
    int n = 0;
    v = sceGuGetMemory(sizeof(*v) * (open ? 8192 : 1024));
    if (!v) return;
    sceGuDisable(GU_TEXTURE_2D); sceGuDisable(GU_DEPTH_TEST); sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_ALPHA_TEST); sceGuDisable(GU_CLIP_PLANES); sceGuDisable(GU_BLEND);
    sceGuDepthMask(GU_TRUE); sceGuScissor(0, 0, 480, 272);
    v[n].color = 0xff000000; v[n].x = 0; v[n].y = 0; v[n].z = 0; n++;
    v[n].color = 0xff000000; v[n].x = open ? 310 : 160; v[n].y = open ? 100 : 12; v[n].z = 0; n++;
    snprintf(text, sizeof(text), "FPS %u.%u  %u.%u MS", sFpsTenths / 10, sFpsTenths % 10,
             sFrameTenths / 10, sFrameTenths % 10);
    overlay_text(v, &n, 4, 4, text);
    if (open) {
        overlay_text(v, &n, 4, 20, "F ZERO X OPTIONS");
        overlay_text(v, &n, 4, 60, "SELECT CLOSE");
#ifdef PSP_PROFILE
        snprintf(text, sizeof(text), "CROSS PROFILE %s", PspProfile_Status());
        overlay_text(v, &n, 4, 40, text);
#endif
        snprintf(text, sizeof(text), "CPU %u.%u GE WAIT %u.%u MS", sWorkTenths / 10, sWorkTenths % 10,
                 sSyncTenths / 10, sSyncTenths % 10);
        overlay_text(v, &n, 4, 76, text);
        snprintf(text, sizeof(text), "VBLANK WAIT %u.%u MS", sWaitTenths / 10, sWaitTenths % 10);
        overlay_text(v, &n, 4, 86, text);
    }
    sceGuDrawArray(GU_SPRITES, GU_COLOR_8888 | GU_VERTEX_16BIT | GU_TRANSFORM_2D, n, NULL, v);
}
