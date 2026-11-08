#include <pspdebug.h>
#include <pspiofilemgr.h>
#include <pspiofilemgr_fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Per-line file I/O is very slow; keep it off while tracing */
#ifndef PSP_LOG_TO_FILE
#define PSP_LOG_TO_FILE 0
#endif

static int sScreenEnabled = 1;

void PspLog_SetScreen(int enable) {
    sScreenEnabled = enable;
}

/* Print to the debug screen and append to host0:/fzerox.log (or ms0) */
void PspLog(const char* fmt, ...) {
    char buffer[256];
    va_list ap;
#if PSP_LOG_TO_FILE
    static int first = 1;
    SceUID fd;
    int flags = PSP_O_WRONLY | PSP_O_CREAT | (first ? PSP_O_TRUNC : PSP_O_APPEND);

    first = 0;
#endif

    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    if (sScreenEnabled) {
        pspDebugScreenPrintf("%s", buffer);
    }
    printf("%s", buffer);
    fflush(stdout);

#if PSP_LOG_TO_FILE
    fd = sceIoOpen("host0:/fzerox.log", flags, 0777);
    if (fd < 0) {
        fd = sceIoOpen("ms0:/fzerox.log", flags, 0777);
    }
    if (fd >= 0) {
        sceIoWrite(fd, buffer, strlen(buffer));
        sceIoClose(fd);
    }
#endif
}
