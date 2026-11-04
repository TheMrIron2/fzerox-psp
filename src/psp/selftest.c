#include "src/psp/psp.h"
#include "src/psp/platform.h"
#include "PR/os.h"
#include "PR/sptask.h"

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
