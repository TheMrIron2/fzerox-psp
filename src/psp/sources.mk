# Curated source manifest for the PSP port.
#
# Keep this explicit so the PSP build does not silently pick up decomp files
# that are not portable yet.

# Bootstrap EBOOT: proves the PSP toolchain and n64psp runtime link.
PSP_BOOTSTRAP_C_FILES := \
    src/psp/main.c

# Full game build. Filled in as the port comes up.
PSP_GAME_C_FILES :=
