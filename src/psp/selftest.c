#include "src/psp/psp.h"
#include "src/psp/platform.h"
#include "src/psp/gfx/gfx_psp.h"
#include "PR/os.h"
#include "PR/sptask.h"

#include <math.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>
#include <stdint.h>

static volatile int sThreadRan;
static OSMesgQueue sThreadDoneQueue;
static OSMesg sThreadDoneBuf[1];
static OSMesgQueue sTimerQueue;
static OSMesg sTimerBuf[1];

static void psp_selftest_thread(void* arg) {
    (void) arg;
    sThreadRan = 1;
    osSendMesg(&sThreadDoneQueue, (OSMesg) 1, OS_MESG_NOBLOCK);
}

void PspSelfTest_Run(void) {
    OSMesgQueue queue;
    OSMesg queueBuf[2];
    OSMesg msg;
    OSThread thread;
    OSTimer timer;
    OSContPad pads[MAXCONTROLLERS];
    OSContStatus status[MAXCONTROLLERS];
    u8 bitpattern;
    u32 viBefore;
    u32 viAfter;
    int pass = 1;

    pspDebugScreenPrintf("\n[selftest] begin\n");

    osInitialize();
    pspDebugScreenPrintf("[selftest] osInitialize ok\n");

    osCreateMesgQueue(&queue, queueBuf, 2);
    if (osSendMesg(&queue, (OSMesg) (uintptr_t) 0x1234, OS_MESG_NOBLOCK) == 0 &&
        osRecvMesg(&queue, &msg, OS_MESG_NOBLOCK) == 0 && msg == (OSMesg) (uintptr_t) 0x1234) {
        pspDebugScreenPrintf("[selftest] mesg queue ok\n");
    } else {
        pspDebugScreenPrintf("[selftest] mesg queue FAIL\n");
        pass = 0;
    }

    sThreadRan = 0;
    osCreateMesgQueue(&sThreadDoneQueue, sThreadDoneBuf, 1);
    osCreateThread(&thread, 1, psp_selftest_thread, NULL, NULL, 10);
    osStartThread(&thread);
    if (osRecvMesg(&sThreadDoneQueue, &msg, OS_MESG_BLOCK) == 0 && sThreadRan) {
        pspDebugScreenPrintf("[selftest] thread ok\n");
    } else {
        pspDebugScreenPrintf("[selftest] thread FAIL\n");
        pass = 0;
    }

    /* Timers use their own queue; n64psp rejects re-creating a live queue */
    osCreateMesgQueue(&sTimerQueue, sTimerBuf, 1);
    if (osSetTimer(&timer, 200000, 0, &sTimerQueue, (OSMesg) (uintptr_t) 0x55) == 0 &&
        osRecvMesg(&sTimerQueue, &msg, OS_MESG_BLOCK) == 0) {
        pspDebugScreenPrintf("[selftest] timer ok\n");
    } else {
        pspDebugScreenPrintf("[selftest] timer FAIL\n");
        pass = 0;
    }

    {
        OSTime start = osGetTime();
        osSetTimer(&timer, 2000000, 0, &sTimerQueue, (OSMesg) (uintptr_t) 0x56);
        osRecvMesg(&sTimerQueue, &msg, OS_MESG_BLOCK);
        pspDebugScreenPrintf("[selftest] time delta %u ticks\n", (unsigned int) (osGetTime() - start));
    }

    osContInit(NULL, &bitpattern, status);
    osContGetReadData(pads);
    pspDebugScreenPrintf("[selftest] input buttons=0x%04x stick=%d,%d\n",
                         (unsigned int) pads[0].button, (int) pads[0].stick_x, (int) pads[0].stick_y);

    viBefore = PspPlatform_GetViCount();
    sceKernelDelayThread(50000);
    viAfter = PspPlatform_GetViCount();
    if (viAfter != viBefore) {
        pspDebugScreenPrintf("[selftest] vi %u vblanks ok\n", (unsigned int) (viAfter - viBefore));
    } else {
        pspDebugScreenPrintf("[selftest] vi FAIL (no vblanks)\n");
        pass = 0;
    }

    pspDebugScreenPrintf("[selftest] %s\n", pass ? "PASS" : "FAIL");
}

/* Debug-only 3D smoke: perspective triangle, vertex colours, depth, motion */
void PspSelfTest_Gfx3D(void) {
    static float sProjection[4][4];
    static float sModelview[4][4];
    PspGfxDrawState state = { 0 };
    PspGfxVertex3D triangle[3];
    int frame;
    int i;
    int j;
    float cot;
    float fovy = 60.0f * 3.1415926f / 180.0f;
    const float nearZ = 1.0f;
    const float farZ = 100.0f;

    if (!PspGfx_Init()) {
        return;
    }

    for (i = 0; i < 4; i++) {
        for (j = 0; j < 4; j++) {
            sProjection[i][j] = (i == j) ? 1.0f : 0.0f;
        }
    }
    cot = cosf(fovy * 0.5f) / sinf(fovy * 0.5f);
    sProjection[0][0] = cot / (4.0f / 3.0f);
    sProjection[1][1] = cot;
    sProjection[2][2] = (nearZ + farZ) / (nearZ - farZ);
    sProjection[2][3] = -1.0f;
    sProjection[3][2] = (2.0f * nearZ * farZ) / (nearZ - farZ);
    sProjection[3][3] = 0.0f;

    triangle[0].x = 0.0f;
    triangle[0].y = 1.2f;
    triangle[0].z = 0.0f;
    triangle[0].color = 0xFF0000FF; /* red */
    triangle[1].x = -1.2f;
    triangle[1].y = -1.2f;
    triangle[1].z = 0.0f;
    triangle[1].color = 0xFF00FF00; /* green */
    triangle[2].x = 1.2f;
    triangle[2].y = -1.2f;
    triangle[2].z = 0.0f;
    triangle[2].color = 0xFFFF0000; /* blue */

    state.projection = &sProjection[0][0];
    state.modelview = &sModelview[0][0];
    state.projectionSerial = 1;
    state.modelviewSerial = 1;
    state.depthTest = 1;
    state.depthWrite = 1;
    state.cullFront = 0;
    state.cullBack = 0;
    state.smoothShade = 1;

    for (frame = 0; frame < 180; frame++) {
        float angle = (float) frame * (6.2831853f / 180.0f);
        float c = cosf(angle);
        float s = sinf(angle);

        for (i = 0; i < 4; i++) {
            for (j = 0; j < 4; j++) {
                sModelview[i][j] = (i == j) ? 1.0f : 0.0f;
            }
        }
        /* Rotate about X, then translate back to z = -5 */
        sModelview[1][1] = c;
        sModelview[1][2] = -s;
        sModelview[2][1] = s;
        sModelview[2][2] = c;
        sModelview[3][2] = -5.0f;

        PspGfx_BeginFrame();
        PspGfx_DrawTriangles3D(triangle, 3, &state);
        PspGfx_Present();
    }
}
