/* N64 segment symbols aliased into a writable PSP RDRAM shadow.
 * Offsets come from the US rev0 yaml VRAM layout.
 * Aliases are same-TU .set expressions so they stay section-relative. */

#include "PR/ultratypes.h"

u8 gPspN64Ram[0x800000] __attribute__((aligned(64)));

#define SEGALIAS_IMPL(sym, off) \
    __asm__(".globl " #sym "\n.set " #sym ", gPspN64Ram + " #off "\n")
#define SEGALIAS(sym, off) SEGALIAS_IMPL(sym, off)

#define SEGZERO_IMPL(sym) \
    __asm__(".globl " #sym "\n.set " #sym ", 0\n")
#define SEGZERO(sym) SEGZERO_IMPL(sym)

#define SEG_RANGE(name, start, end) \
    SEGALIAS(name##_VRAM, start); \
    SEGALIAS(name##_VRAM_END, end); \
    SEGALIAS(name##_TEXT_START, start); \
    SEGALIAS(name##_TEXT_END, start); \
    SEGALIAS(name##_DATA_START, start); \
    SEGALIAS(name##_DATA_END, start); \
    SEGALIAS(name##_RODATA_END, start); \
    SEGALIAS(name##_BSS_START, start); \
    SEGALIAS(name##_BSS_END, end); \
    SEGZERO(name##_DATA_SIZE); \
    SEGZERO(name##_ROM_START); \
    SEGZERO(name##_ROM_END)

#define SEG(name, off) SEG_RANGE(name, off, off)

SEG_RANGE(header, 0x600000, 0x600000);
SEG_RANGE(boot, 0x600100, 0x600100);
SEG_RANGE(framebuffer_unused, 0x400, 0x25C00);
SEG_RANGE(audio_context, 0x25C00, 0x2B3D0);
SEG_RANGE(audio_heap, 0x2B400, 0x66400);
SEG_RANGE(entry, 0x67000, 0x67000);
SEG_RANGE(main, 0x67060, 0x8B760);
SEG_RANGE(framebuffer1, 0x1D9800, 0x1FF000);
SEG_RANGE(framebuffer2, 0x200000, 0x225800);
SEG_RANGE(unk_gfx_segment, 0x225800, 0x2A6B40);
SEG_RANGE(unk_bss_segment, 0x225800, 0x24DCC0);
SEG_RANGE(gfxpool, 0x24DCE0, 0x2A6A40);
SEG_RANGE(game_context, 0x2A6B40, 0x2D5C40);
SEG_RANGE(buffers, 0x2D5C40, 0x32AC40);
SEG_RANGE(framebuffer3, 0x3DA800, 0x400000);
SEG_RANGE(cartridge_offsets, 0x32AC40, 0x32AC40);
SEG_RANGE(leo_font, 0x400000, 0x403010);
SEG_RANGE(leo, 0x403010, 0x405CE0);
SEG_RANGE(ovl_i2, 0xFC730, 0x104DB0);
SEG_RANGE(ovl_i3, 0x115DF0, 0x117EC0);
SEG_RANGE(ovl_i4, 0x115DF0, 0x115E20);
SEG_RANGE(course_select, 0x115DF0, 0x115E30);
SEG_RANGE(ovl_i6, 0x115DF0, 0x122CA0);
SEG_RANGE(ending, 0x1439D0, 0x1447A0);
SEG_RANGE(records, 0x1439D0, 0x143B00);
SEG_RANGE(ovl_i9, 0x115DF0, 0x115E00);
SEG_RANGE(ovl_i10, 0x115DF0, 0x115DF0);
SEG_RANGE(expansion_kit_textures_beta, 0x600200, 0x600200);
SEG_RANGE(course_edit_textures_beta, 0x600300, 0x600300);
SEG_RANGE(create_machine_textures, 0x600400, 0x600400);
SEG_RANGE(course_track_gfx, 0x600500, 0x600500);
SEG_RANGE(setup_gfx, 0x600600, 0x600600);
SEG_RANGE(machine_custom_gfx, 0x600700, 0x600700);
SEG_RANGE(hud_gfx, 0x600800, 0x600800);
SEG_RANGE(machine_global_gfx, 0x600900, 0x600900);
SEG_RANGE(machine_models, 0x600A00, 0x600A00);
SEG_RANGE(mute_city_textures, 0x600B00, 0x600B00);
SEG_RANGE(port_town_textures, 0x600C00, 0x600C00);
SEG_RANGE(big_blue_textures, 0x600D00, 0x600D00);
SEG_RANGE(sand_ocean_textures, 0x600E00, 0x600E00);
SEG_RANGE(devils_forest_textures, 0x600F00, 0x600F00);
SEG_RANGE(white_land_textures, 0x601000, 0x601000);
SEG_RANGE(sector_textures, 0x601100, 0x601100);
SEG_RANGE(red_canyon_textures, 0x601200, 0x601200);
SEG_RANGE(fire_field_textures, 0x601300, 0x601300);
SEG_RANGE(silence_textures, 0x601400, 0x601400);
SEG_RANGE(ending_venue_textures, 0x601500, 0x601500);
SEG_RANGE(podium_gfx, 0x601600, 0x601600);
SEG_RANGE(super_textures, 0x601700, 0x601700);
SEG_RANGE(boot_textures, 0x601800, 0x601800);
SEG_RANGE(staff_ghost_records, 0x601900, 0x601900);
SEG_RANGE(course_data, 0x601A00, 0x601A00);
SEG_RANGE(common_assets_compressed, 0x601B00, 0x601B00);
SEG_RANGE(ovl_i11, 0xFC730, 0xFC730);
SEG_RANGE(audio_bank, 0x601C00, 0x601C00);
SEG_RANGE(audio_seq, 0x601D00, 0x601D00);
SEG_RANGE(audio_table, 0x601E00, 0x601E00);
