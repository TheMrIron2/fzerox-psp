# Curated source manifest for the PSP port.
#
# Keep this explicit so the PSP build does not silently pick up decomp files
# that are not portable yet.

# Bootstrap EBOOT: PSP toolchain, n64psp runtime, and libultra OS self-test.
PSP_BOOTSTRAP_C_FILES := \
    src/psp/main.c \
    src/psp/platform.c \
    src/psp/ultra_reimpl.c \
    src/psp/selftest.c

# Full game build. Filled in as the port comes up.
PSP_GAME_C_FILES :=
