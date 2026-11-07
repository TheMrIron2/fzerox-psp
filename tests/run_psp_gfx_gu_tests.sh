#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
test_binary=$(mktemp /tmp/fzerox-gu-test.XXXXXX)
trap 'rm -f "$test_binary"' EXIT HUP INT TERM
pspsdk_headers=$(${PSP_CONFIG:-psp-config} --pspsdk-path)/include
${CC:-cc} -std=gnu99 -O2 -fno-pie -no-pie -ffunction-sections -fdata-sections -Wl,--gc-sections \
    -Wno-pointer-to-int-cast -Wno-int-to-pointer-cast -D_LANGUAGE_C -DF3DEX_GBI_2 \
    -Iinclude -I. -I"$pspsdk_headers" tests/psp_gfx_gu_test.c -o "$test_binary"
"$test_binary"
