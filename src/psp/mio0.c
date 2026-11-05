#include "PR/ultratypes.h"

/* MIO0 big-endian decoder, matches the original N64 routine */
static u32 mio0_read_u32(const u8* p) {
    return ((u32) p[0] << 24) | ((u32) p[1] << 16) | ((u32) p[2] << 8) | (u32) p[3];
}

void mio0Decode(u8* src, void* dst) {
    u8* out = (u8*) dst;
    const u8* bits;
    u32 destSize;
    u32 compOffset;
    u32 uncompOffset;
    u32 written;
    u32 bitIndex;
    u32 compIndex;
    u32 uncompIndex;
    u32 srcAddr = (u32) src;
    u32 dstAddr = (u32) dst;

    if ((src == NULL) || (dst == NULL)) {
        return;
    }
    /* Reject wild pointers from missing/relocated segment data */
    if ((srcAddr < 0x08000000u) || (srcAddr >= 0x0A000000u)) {
        return;
    }
    if ((dstAddr < 0x08000000u) || (dstAddr >= 0x0A000000u)) {
        return;
    }
    if ((src[0] != 'M') || (src[1] != 'I') || (src[2] != 'O') || (src[3] != '0')) {
        return;
    }

    destSize = mio0_read_u32(src + 4);
    compOffset = mio0_read_u32(src + 8);
    uncompOffset = mio0_read_u32(src + 12);

    if ((destSize == 0) || (destSize > 0x800000u) || (compOffset < 16) || (uncompOffset < 16)) {
        return;
    }

    bits = src + 16;
    written = 0;
    bitIndex = 0;
    compIndex = 0;
    uncompIndex = 0;

    while (written < destSize) {
        if (bits[bitIndex >> 3] & (0x80 >> (bitIndex & 7))) {
            out[written] = src[uncompOffset + uncompIndex];
            written++;
            uncompIndex++;
        } else {
            const u8* vals = src + compOffset + compIndex;
            u32 length = ((u32) (vals[0] >> 4) & 0xF) + 3;
            u32 distance = ((((u32) vals[0] & 0xF) << 8) | (u32) vals[1]) + 1;
            u32 i;

            compIndex += 2;
            for (i = 0; (i < length) && (written < destSize); i++) {
                out[written] = out[written - distance];
                written++;
            }
        }
        bitIndex++;
    }
}

/* Returns the uncompressed size stored in the MIO0 header */
s32 func_800AA6BC(u8* data) {
    if (data == NULL) {
        return 0;
    }
    return (s32) mio0_read_u32(data + 4);
}
