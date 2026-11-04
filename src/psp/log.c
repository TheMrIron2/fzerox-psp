#include <pspdebug.h>
#include <pspiofilemgr.h>
#include <pspiofilemgr_fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Print to the debug screen and append to host0:/fzerox.log (or ms0) */
void PspLog(const char* fmt, ...) {
    static int first = 1;
    char buffer[256];
    va_list ap;
    SceUID fd;
    int flags = PSP_O_WRONLY | PSP_O_CREAT | (first ? PSP_O_TRUNC : PSP_O_APPEND);

    first = 0;

    va_start(ap, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, ap);
    va_end(ap);

    pspDebugScreenPrintf("%s", buffer);
    printf("%s", buffer);
    fflush(stdout);

    fd = sceIoOpen("host0:/fzerox.log", flags, 0777);
    if (fd < 0) {
        fd = sceIoOpen("ms0:/fzerox.log", flags, 0777);
    }
    if (fd >= 0) {
        sceIoWrite(fd, buffer, strlen(buffer));
        sceIoClose(fd);
    }
}
