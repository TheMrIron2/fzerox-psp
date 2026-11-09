#include "src/psp/platform.h"
#include "src/psp/gfx/gfx_psp.h"

#include <pspctrl.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspkernel.h>

typedef struct PspEvent {
    OSMesgQueue* mq;
    OSMesg msg;
} PspEvent;

static PspEvent sEvents[OS_NUM_EVENTS];
static OSMesgQueue* sViMq;
static OSMesg sViMsg;
static u32 sViRetraceCount = 1;
static u32 sViRetraceCounter;
static volatile u32 sViCount;
/* Sampled on the input thread; the renderer only reads this flag */
static volatile int sDiagKey;
static SceUID sViThreadId = -1;
static volatile u32 sGfxTaskCount;
static volatile u32 sAudioTaskCount;

static int psp_vi_thread(SceSize args, void* argp) {
    (void) args;
    (void) argp;

    for (;;) {
        sceDisplayWaitVblankStart();
        sViCount++;
        if (sViMq != NULL) {
            if (++sViRetraceCounter >= sViRetraceCount) {
                sViRetraceCounter = 0;
                osSendMesg(sViMq, sViMsg, OS_MESG_NOBLOCK);
            }
        }
    }

    return 0;
}

void PspPlatform_Init(void) {
    if (sViThreadId >= 0) {
        return;
    }

    sViThreadId = sceKernelCreateThread("vi_thread", psp_vi_thread, 0x20, 0x2000, PSP_THREAD_ATTR_USER, NULL);
    if (sViThreadId >= 0) {
        sceKernelStartThread(sViThreadId, 0, NULL);
    }
}

void PspPlatform_SetEventMesg(OSEvent event, OSMesgQueue* mq, OSMesg msg) {
    if (event < OS_NUM_EVENTS) {
        sEvents[event].mq = mq;
        sEvents[event].msg = msg;
    }
}

void PspPlatform_SetViEvent(OSMesgQueue* mq, OSMesg msg, u32 retraceCount) {
    sViMq = mq;
    sViMsg = msg;
    sViRetraceCount = (retraceCount == 0) ? 1 : retraceCount;
    sViRetraceCounter = 0;
}

static s8 psp_stick_axis(s32 delta) {
    s32 scaled = (delta * 80) / 127;

    if (scaled > 80) {
        scaled = 80;
    } else if (scaled < -80) {
        scaled = -80;
    }
    return (s8) scaled;
}

void PspPlatform_PollInput(OSContPad* pads) {
    SceCtrlData data;
    u16 button;
    s32 i;

    if (pads == NULL) {
        return;
    }

    sceCtrlReadBufferPositive(&data, 1);

    for (i = 0; i < MAXCONTROLLERS; i++) {
        pads[i].button = 0;
        pads[i].stick_x = 0;
        pads[i].stick_y = 0;
        pads[i].errno = (i == 0) ? 0 : CONT_NO_RESPONSE_ERROR;
    }

    /* Provisional mapping: C buttons still need a proper remap path */
    button = 0;
    if (data.Buttons & PSP_CTRL_CROSS) {
        button |= A_BUTTON;
    }
    if (data.Buttons & PSP_CTRL_CIRCLE) {
        button |= B_BUTTON;
    }
    if (data.Buttons & PSP_CTRL_SQUARE) {
        button |= Z_TRIG;
    }
    if (data.Buttons & PSP_CTRL_TRIANGLE) {
        button |= U_CBUTTONS;
    }
    if (data.Buttons & PSP_CTRL_LTRIGGER) {
        button |= L_TRIG;
    }
    if (data.Buttons & PSP_CTRL_RTRIGGER) {
        button |= R_TRIG;
    }
    if (data.Buttons & PSP_CTRL_START) {
        button |= START_BUTTON;
    }
    if (data.Buttons & PSP_CTRL_UP) {
        button |= U_JPAD;
    }
    if (data.Buttons & PSP_CTRL_DOWN) {
        button |= D_JPAD;
    }
    if (data.Buttons & PSP_CTRL_LEFT) {
        button |= L_JPAD;
    }
    if (data.Buttons & PSP_CTRL_RIGHT) {
        button |= R_JPAD;
    }

    pads[0].button = button;
    pads[0].stick_x = psp_stick_axis((s32) data.Lx - 128);
    pads[0].stick_y = psp_stick_axis(128 - (s32) data.Ly);
    sDiagKey = (data.Buttons & PSP_CTRL_SELECT) != 0;
}

int PspPlatform_DiagKey(void) {
    return sDiagKey;
}

void PspPlatform_RunGfxTask(OSTask* task) {
    static int sGfxInit;

    sGfxTaskCount++;

    if (task != NULL) {
        if (!sGfxInit) {
            PspGfx_Init();
            sGfxInit = 1;
        }
        PspGfx_BeginFrame();
        PspGfx_RunDisplayList((const Gfx*) task->t.data_ptr, task->t.data_size);
        PspGfx_Present();
    }

    /* No RSP/RDP runs; report SP and DP completion so scheduling advances */
    if (sEvents[OS_EVENT_SP].mq != NULL) {
        osSendMesg(sEvents[OS_EVENT_SP].mq, sEvents[OS_EVENT_SP].msg, OS_MESG_NOBLOCK);
    }
    if (sEvents[OS_EVENT_DP].mq != NULL) {
        osSendMesg(sEvents[OS_EVENT_DP].mq, sEvents[OS_EVENT_DP].msg, OS_MESG_NOBLOCK);
    }
}

void PspPlatform_RunAudioTask(OSTask* task) {
    (void) task;
    sAudioTaskCount++;

    if (sEvents[OS_EVENT_SP].mq != NULL) {
        osSendMesg(sEvents[OS_EVENT_SP].mq, sEvents[OS_EVENT_SP].msg, OS_MESG_NOBLOCK);
    }
    if (sEvents[OS_EVENT_AI].mq != NULL) {
        osSendMesg(sEvents[OS_EVENT_AI].mq, sEvents[OS_EVENT_AI].msg, OS_MESG_NOBLOCK);
    }
}

void PspPlatform_Log(const char* message) {
    if (message != NULL) {
        pspDebugScreenPrintf("%s\n", message);
    }
}

u32 PspPlatform_GetViCount(void) {
    return sViCount;
}

u32 PspPlatform_GetGfxTaskCount(void) {
    return sGfxTaskCount;
}

u32 PspPlatform_GetAudioTaskCount(void) {
    return sAudioTaskCount;
}
