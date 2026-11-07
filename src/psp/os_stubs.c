/* Stubs for libultra, Leo, and hardware symbols not yet ported to PSP.
 * Reached only by disk, rumble, unmatched-asm, or RSP microcode paths. */

#include "PR/ultratypes.h"
#include "PR/os.h"
#include "PR/gu.h"
#include "PR/leo.h"
#include "PR/ucode.h"

/* Data blobs referenced by generated code but not yet extracted natively */
u8 D_1000000[0x40000];
u8 D_2000000[0x40000];
u8 D_20284C0[0x100];
u8 D_2028480[0x40];
u8 D_A000000[0x1000];
u8 D_A001000[0x1000];
u8 D_A002000[0x1000];
u8 D_A003000[0x1000];
u8 D_A004000[0x1000];
u8 D_A005000[0x1000];
u8 D_A006000[0x1000];
u8 D_A007000[0x1000];
u8 D_A008000[0x1000];

LEODiskID leoBootID;
float __libm_qnan_f = 0.0f;

/* RSP microcode ranges: present but empty on the native build */
#define PSP_UCODE_RANGE_IMPL(a, b) \
    __asm__(".section .rodata\n.balign 8\n.globl " #a "\n" #a ":\n.globl " #b "\n" #b ":\n.previous\n")
#define PSP_UCODE_RANGE(a, b) PSP_UCODE_RANGE_IMPL(a, b)

PSP_UCODE_RANGE(rspbootTextStart, rspbootTextEnd);
PSP_UCODE_RANGE(aspMainTextStart, aspMainTextEnd);
PSP_UCODE_RANGE(aspMainDataStart, aspMainDataEnd);
PSP_UCODE_RANGE(gspF3DEX2_fifoTextStart, gspF3DEX2_fifoTextEnd);
PSP_UCODE_RANGE(gspF3DEX2_fifoDataStart, gspF3DEX2_fifoDataEnd);
PSP_UCODE_RANGE(gspF3DFLX2_Rej_fifoTextStart, gspF3DFLX2_Rej_fifoTextEnd);
PSP_UCODE_RANGE(gspF3DFLX2_Rej_fifoDataStart, gspF3DFLX2_Rej_fifoDataEnd);
PSP_UCODE_RANGE(gspF3DLX2_Rej_fifoTextStart, gspF3DLX2_Rej_fifoTextEnd);
PSP_UCODE_RANGE(gspF3DLX2_Rej_fifoDataStart, gspF3DLX2_Rej_fifoDataEnd);

/* libultra internals */
void __osSetSR(u32 value) {
    (void) value;
}

void __osSetCause(u32 value) {
    (void) value;
}

void __osSetCompare(u32 value) {
    (void) value;
}

void __osSetCount(u32 value) {
    (void) value;
}

void __osSetHWIntrRoutine(u32 interrupt, s32 (*handler)(void), void* stackEnd) {
    (void) interrupt;
    (void) handler;
    (void) stackEnd;
}

OSThread* __osGetActiveQueue(void) {
    return NULL;
}

/* GPU helper normally supplied by unmatched asm */
void guNormalize(float* x, float* y, float* z) {
    float len = (*x * *x) + (*y * *y) + (*z * *z);

    if (len > 0.0f) {
        float inv = 1.0f / __builtin_sqrtf(len);

        *x *= inv;
        *y *= inv;
        *z *= inv;
    }
}

/* Leo disk (US cartridge does not mount the drive) */
OSPiHandle* osLeoDiskInit(void) {
    return NULL;
}

int LeoGetAAdr2(u32 ccode, int* dx, int* dy, int* cy) {
    (void) ccode;
    (void) dx;
    (void) dy;
    (void) cy;
    return 0;
}

s32 __osLeoInterrupt(void) {
    return 0;
}

void __LeoBootGame3(void* entry) {
    (void) entry;
}

int LeoGetKAdr(int sjis) {
    (void) sjis;
    return 0;
}

int LeoGetAAdr(int code, int* dx, int* dy, int* cy) {
    (void) code;
    if (dx != NULL) {
        *dx = 0;
    }
    if (dy != NULL) {
        *dy = 0;
    }
    if (cy != NULL) {
        *cy = 0;
    }
    return 0;
}

/* Controller Pak and rumble */
s32 osMotorInit(OSMesgQueue* mq, OSPfs* pfs, int channel) {
    (void) mq;
    (void) pfs;
    (void) channel;
    return -1;
}

s32 __osMotorAccess(OSPfs* pfs, s32 flag) {
    (void) pfs;
    (void) flag;
    return -1;
}

s32 osPfsInitPak(OSMesgQueue* mq, OSPfs* pfs, int channel) {
    (void) mq;
    (void) pfs;
    (void) channel;
    return -1;
}

s32 osPfsAllocateFile(OSPfs* pfs, u16 companyCode, u32 gameCode, u8* gameName, u8* extName, int length,
                      s32* fileNo) {
    (void) pfs;
    (void) companyCode;
    (void) gameCode;
    (void) gameName;
    (void) extName;
    (void) length;
    (void) fileNo;
    return -1;
}

s32 osPfsFindFile(OSPfs* pfs, u16 companyCode, u32 gameCode, u8* gameName, u8* extName, s32* fileNo) {
    (void) pfs;
    (void) companyCode;
    (void) gameCode;
    (void) gameName;
    (void) extName;
    (void) fileNo;
    return -1;
}

s32 osPfsReadWriteFile(OSPfs* pfs, s32 fileNo, u8 flag, int offset, int nbytes, u8* data) {
    (void) pfs;
    (void) fileNo;
    (void) flag;
    (void) offset;
    (void) nbytes;
    (void) data;
    return -1;
}
