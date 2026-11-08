#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psppower.h>

#include "n64psp/platform.h"
#include "n64psp/renderer.h"
#include "n64psp/runtime.h"

#include "src/psp/psp.h"

#ifdef PSP_FULL
void bootproc(void);
#endif

PSP_MODULE_INFO("F-Zero X", PSP_MODULE_USER, 1, 0);
PSP_MAIN_THREAD_ATTR(PSP_THREAD_ATTR_USER | PSP_THREAD_ATTR_VFPU);
PSP_HEAP_SIZE_KB(-1024);

static int exit_callback(int arg1, int arg2, void* common) {
    (void) arg1;
    (void) arg2;
    (void) common;

    sceKernelExitGame();
    return 0;
}

static int callback_thread(SceSize args, void* argp) {
    int callbackId;

    (void) args;
    (void) argp;

    callbackId = sceKernelCreateCallback("Exit Callback", exit_callback, NULL);
    sceKernelRegisterExitCallback(callbackId);
    sceKernelSleepThreadCB();

    return 0;
}

static void setup_callbacks(void) {
    int threadId;

    threadId = sceKernelCreateThread("update_thread", callback_thread, 0x11, 0xFA0, 0, 0);
    if (threadId >= 0) {
        sceKernelStartThread(threadId, 0, 0);
    }
}

static int init_n64psp(void) {
    n64psp_platform_callbacks platform;
    n64psp_renderer_callbacks renderer;

    if (n64psp_platform_psp_get_callbacks(&platform) != N64PSP_OK) {
        pspDebugScreenPrintf("[psp] platform callbacks failed\n");
        return 0;
    }
    if (n64psp_trace_backend_get_callbacks(&renderer) != N64PSP_OK) {
        pspDebugScreenPrintf("[psp] renderer callbacks failed\n");
        return 0;
    }
    if (n64psp_runtime_register_platform(&platform) != N64PSP_OK) {
        pspDebugScreenPrintf("[psp] register platform failed\n");
        return 0;
    }
    if (n64psp_runtime_register_renderer(&renderer) != N64PSP_OK) {
        pspDebugScreenPrintf("[psp] register renderer failed\n");
        return 0;
    }
    if (n64psp_runtime_init() != N64PSP_OK) {
        pspDebugScreenPrintf("[psp] runtime init failed\n");
        return 0;
    }

    pspDebugScreenPrintf("[psp] n64psp runtime ready\n");
    return 1;
}

int main(int argc, char* argv[]) {
    (void) argc;
    (void) argv;

    setup_callbacks();
    pspDebugScreenInit();
    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);
    scePowerSetClockFrequency(333, 333, 166);

    pspDebugScreenPrintf("F-Zero X\n");
#ifdef PSP_FULL
    pspDebugScreenPrintf("PSP game build\n\n");
#else
    pspDebugScreenPrintf("PSP bootstrap build\n\n");
#endif

    if (init_n64psp()) {
        PspPlatform_Init();
#ifdef PSP_FULL
        pspDebugScreenPrintf("[psp] bootproc enter\n");
        bootproc();
        pspDebugScreenPrintf("[psp] bootproc returned\n");
#else
        PspSelfTest_Run();
        PspSelfTest_Gfx3D();
#endif
    }

    pspDebugScreenPrintf("\nPress HOME to exit\n");

    while (1) {
        sceDisplayWaitVblankStart();
    }

    return 0;
}
