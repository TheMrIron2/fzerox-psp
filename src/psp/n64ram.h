#ifndef PSP_N64RAM_H
#define PSP_N64RAM_H

#include "PR/ultratypes.h"

/* Shadow of the N64 RDRAM address range 0x80000000..0x80800000 */
extern u8 gPspN64Ram[];

/* Size of that shadow; the host tests allocate a smaller one */
#ifdef __psp__
#define PSP_N64RAM_SIZE 0x800000u
#else
#define PSP_N64RAM_SIZE 0x40000u
#endif

#define N64_RAM_ADDR(off) ((u32) (gPspN64Ram + (off)))

#endif
