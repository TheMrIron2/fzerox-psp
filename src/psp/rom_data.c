#include "PR/ultratypes.h"

/* Fixed cartridge words read by Racer_InitMachineStats. The original reads
 * these boot-code words through the ROM window; embed the values used. */
typedef struct {
    u32 addr;
    u32 value;
} PspRomWord;

static const PspRomWord sRomWords[] = {
    { 0x078u, 0x3C08A470u }, { 0x0A0u, 0x2231FFFFu }, { 0x1BCu, 0x0330C820u }, { 0x1C8u, 0x02C8B020u },
    { 0x248u, 0x01F07820u }, { 0x250u, 0x2DA80008u }, { 0x2C4u, 0x01C87020u }, { 0x2FCu, 0x01074020u },
    { 0x36Cu, 0x3C0AA470u }, { 0x374u, 0x3C090006u }, { 0x384u, 0x8D490010u }, { 0x38Cu, 0x35080300u },
    { 0x390u, 0x3C090FFFu }, { 0x50Cu, 0x3D3261E1u }, { 0x540u, 0x93B3831Au }, { 0x550u, 0x3C0E25EAu },
    { 0x57Cu, 0x3A009D62u }, { 0x5D8u, 0x3AD8321Bu }, { 0x5E4u, 0xB31A7D37u }, { 0x60Cu, 0x4F12AFDDu },
    { 0x660u, 0xEB4298EBu }, { 0x664u, 0x2A5C9012u }, { 0x6A0u, 0xF600FC42u }, { 0x6D8u, 0x4523C00Au },
    { 0x6E8u, 0x662F997Au },
    { 0x718u, 0x81E1E442u }, { 0x740u, 0x200B8F0Au }, { 0x780u, 0x3066028Au }, { 0x95Cu, 0x241AFFFFu },
    { 0x978u, 0x307A0001u }, { 0x988u, 0x279C0001u }, { 0xA20u, 0x07430003u },
};

u32 PspRomRead(u32 addr) {
    u32 i;

    for (i = 0; i < (sizeof(sRomWords) / sizeof(sRomWords[0])); i++) {
        if (sRomWords[i].addr == addr) {
            return sRomWords[i].value;
        }
    }
    return 0;
}
