#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_binary=$(mktemp /tmp/fzerox-gfx-test.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
${CC:-cc} -std=gnu99 -O2 -fno-pie -no-pie -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast \
    -D_LANGUAGE_C -DF3DEX_GBI_2 -Iinclude -I. -Ilib/n64psp/include tests/psp_gfx_dl_test.c \
    lib/n64psp/src/math/math_api.c lib/n64psp/src/math/math_scalar.c -lm -o "$test_binary"
"$test_binary"
