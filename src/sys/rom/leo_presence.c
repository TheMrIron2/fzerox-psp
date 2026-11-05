#include "global.h"

#ifdef PSP
/* No 64DD on PSP; the emulated PI always reads back zero */
bool LeoDD_CheckPresence(void) {
    return false;
}
#else
bool LeoDD_CheckPresence(void) {
    OSPiHandle* leoHandle = osLeoDiskInit();
    u32 status;

    osEPiReadIo(leoHandle, LEO_STATUS, &status);

    if (status & LEO_STATUS_PRESENCE_MASK) {
        return false;
    } else {
        return true;
    }
}
#endif
