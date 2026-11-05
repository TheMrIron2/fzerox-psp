# Curated source manifest for the PSP port.
#
# Keep this explicit so the PSP build does not silently pick up decomp files
# that are not portable yet.

# Platform layer: always compiled (bootstrap and full game).
PSP_PLATFORM_C_FILES := \
    src/psp/main.c \
    src/psp/platform.c \
    src/psp/ultra_reimpl.c \
    src/psp/os_stubs.c \
    src/psp/mio0.c \
    src/psp/segments.c \
    src/psp/log.c \
    src/psp/gfx/gfx_psp_dl.c \
    src/psp/gfx/gfx_psp_backend_gu.c

# Bootstrap EBOOT: platform layer plus the libultra OS self-test.
PSP_BOOTSTRAP_C_FILES := \
    $(PSP_PLATFORM_C_FILES) \
    src/psp/selftest.c

# Full game build.
include src/psp/game_sources.mk
PSP_GAME_C_FILES := $(PSP_PLATFORM_C_FILES) $(PSP_GAME_SOURCES)
