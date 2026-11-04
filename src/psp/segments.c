/* Zero-size N64 segment placeholders for the native build.
 * Overlay and asset data are compiled into the ELF, so the ROM/VRAM
 * ranges only need to exist for address bookkeeping. */

#include "PR/ultratypes.h"

#define PSP_SEG_RANGE_IMPL(s, e) \
    __asm__(".section .rodata\n.balign 16\n.globl " #s "\n" #s ":\n.globl " #e "\n" #e ":\n.previous\n")
#define PSP_SEG_RANGE(s, e) PSP_SEG_RANGE_IMPL(s, e)

#define PSP_SEG_DATA_IMPL(s, e, r) \
    __asm__(".section .rodata\n.balign 16\n.globl " #s "\n" #s ":\n.globl " #e "\n" #e ":\n.globl " #r "\n" #r ":\n.previous\n")
#define PSP_SEG_DATA(s, e, r) PSP_SEG_DATA_IMPL(s, e, r)

#define PSP_SEG_SIZE_IMPL(sym) \
    __asm__(".globl " #sym "\n.set " #sym ", 0\n")
#define PSP_SEG_SIZE(sym) PSP_SEG_SIZE_IMPL(sym)

#define PSP_SEGMENT(name) \
    PSP_SEG_RANGE(name##_ROM_START, name##_ROM_END); \
    PSP_SEG_RANGE(name##_VRAM, name##_VRAM_END); \
    PSP_SEG_RANGE(name##_TEXT_START, name##_TEXT_END); \
    PSP_SEG_DATA(name##_DATA_START, name##_DATA_END, name##_RODATA_END); \
    PSP_SEG_SIZE(name##_DATA_SIZE); \
    PSP_SEG_RANGE(name##_BSS_START, name##_BSS_END)

PSP_SEGMENT(framebuffer_unused);
PSP_SEGMENT(audio_context);
PSP_SEGMENT(main);
PSP_SEGMENT(cartridge_offsets);
PSP_SEGMENT(leo);
PSP_SEGMENT(ovl_i2);
PSP_SEGMENT(ovl_i3);
PSP_SEGMENT(ovl_i4);
PSP_SEGMENT(course_select);
PSP_SEGMENT(ovl_i6);
PSP_SEGMENT(ending);
PSP_SEGMENT(records);
PSP_SEGMENT(ovl_i9);
PSP_SEGMENT(ovl_i10);
PSP_SEGMENT(framebuffer1);
PSP_SEGMENT(framebuffer2);
PSP_SEGMENT(unk_gfx_segment);
PSP_SEGMENT(unk_bss_segment);
PSP_SEGMENT(gfxpool);
PSP_SEGMENT(buffers);
PSP_SEGMENT(framebuffer3);
PSP_SEGMENT(expansion_kit_textures_beta);
PSP_SEGMENT(course_edit_textures_beta);
PSP_SEGMENT(create_machine_textures);
PSP_SEGMENT(course_track_gfx);
PSP_SEGMENT(setup_gfx);
PSP_SEGMENT(machine_custom_gfx);
PSP_SEGMENT(hud_gfx);
PSP_SEGMENT(machine_global_gfx);
PSP_SEGMENT(machine_models);
PSP_SEGMENT(mute_city_textures);
PSP_SEGMENT(port_town_textures);
PSP_SEGMENT(big_blue_textures);
PSP_SEGMENT(sand_ocean_textures);
PSP_SEGMENT(devils_forest_textures);
PSP_SEGMENT(white_land_textures);
PSP_SEGMENT(sector_textures);
PSP_SEGMENT(red_canyon_textures);
PSP_SEGMENT(fire_field_textures);
PSP_SEGMENT(silence_textures);
PSP_SEGMENT(ending_venue_textures);
PSP_SEGMENT(podium_gfx);
PSP_SEGMENT(super_textures);
PSP_SEGMENT(boot_textures);
PSP_SEGMENT(staff_ghost_records);
PSP_SEGMENT(course_data);
PSP_SEGMENT(common_assets_compressed);
PSP_SEGMENT(ovl_i11);
PSP_SEGMENT(audio_seq);
PSP_SEGMENT(audio_bank);
PSP_SEGMENT(audio_table);
PSP_SEGMENT(game_context);
