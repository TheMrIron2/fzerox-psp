# PSP build for the F-Zero X port. This is the primary Makefile.
#
# Default target builds the bootstrap EBOOT:
#   make
#   make psp
#
# Full-game target, once the game links through its boot chain:
#   make PSP_FULL=1
#
# The original decomp build is preserved in Makefile.decomp:
#   make -f Makefile.decomp

-include .make_options

MAKEFLAGS += --no-builtin-rules --no-print-directory
.DEFAULT_GOAL := psp

TARGET  := fzerox
VERSION ?= us
REV     ?= rev0

PSP_FULL ?= 0
PSP_TITLE ?= F-Zero X
BUILD_DIR ?= $(if $(filter 1,$(PSP_FULL)),build/psp-full,build/psp)

PSP_CONFIG ?= psp-config
PSP_PRXGEN ?= psp-prxgen
PSP_FIXUP_IMPORTS ?= psp-fixup-imports
MKSFOEX ?= mksfoex
PACK_PBP ?= pack-pbp

CC := psp-gcc
AR := psp-ar

PSPSDK ?= $(shell $(PSP_CONFIG) --pspsdk-path 2>/dev/null)
PSPDEV ?= $(shell $(PSP_CONFIG) --psp-prefix 2>/dev/null | sed 's,/psp$$,,')

ifeq ($(strip $(PSPSDK)),)
$(error psp-config --pspsdk-path returned nothing; ensure PSPDEV is on PATH)
endif

PSP_ELF := $(BUILD_DIR)/$(TARGET).psp.elf
PSP_PRX := $(BUILD_DIR)/$(TARGET).psp.prx
PSP_MAP := $(BUILD_DIR)/$(TARGET).psp.map
PSP_SFO := $(BUILD_DIR)/PARAM.SFO
PSP_EBOOT := $(BUILD_DIR)/EBOOT.PBP

# Reusable n64psp runtime. Archives are built by the submodule Makefile.
N64PSP_DIR := lib/n64psp
N64PSP_BUILD := build-psp-release
N64PSP_ARCHIVES := \
    $(N64PSP_DIR)/$(N64PSP_BUILD)/libn64psp_runtime.a \
    $(N64PSP_DIR)/$(N64PSP_BUILD)/libn64psp_trace_backend.a \
    $(N64PSP_DIR)/$(N64PSP_BUILD)/libn64psp_platform_psp.a \
    $(N64PSP_DIR)/$(N64PSP_BUILD)/libn64psp_math.a
N64PSP_TARGETS := $(patsubst $(N64PSP_DIR)/%,%,$(N64PSP_ARCHIVES))
N64PSP_STAMP := $(N64PSP_DIR)/$(N64PSP_BUILD)/.fzerox-n64psp-build.stamp

CFLAGS := -std=gnu89 -G0 -O2 -g
CFLAGS += -DPSP -D__PSP__ -D_PSP_FW_VERSION=600
CFLAGS += -D_LANGUAGE_C -DF3DEX_GBI_2 -D_MIPS_SZLONG=32
CFLAGS += -fno-builtin -fno-common -fno-strict-aliasing -fwrapv -funsigned-char
CFLAGS += -ffunction-sections -fdata-sections

ifeq ($(PSP_FULL),1)
CFLAGS += -DVERSION_US=1 -DNDEBUG -DNON_MATCHING -DAVOID_UB -DCOMPILER_GCC -DPSP_FULL=1
CFLAGS += -DASSET_VERSION=$(VERSION) -DASSET_REVISION=$(REV)
endif

IINC := -Iinclude -I. -Ibin/$(VERSION)/$(REV) -Ilib/n64psp/include
IINC += -I$(PSPDEV)/psp/include -I$(PSPSDK)/include

PSP_WARNINGS := -Wall -Wextra -Wno-unused-parameter
$(BUILD_DIR)/src/psp/%.o: CFLAGS += $(PSP_WARNINGS)

LDFLAGS := -L$(PSPDEV)/psp/lib -L$(PSPSDK)/lib
LDFLAGS += -Wl,-zmax-page-size=128 -Wl,-Map,$(PSP_MAP)
LDFLAGS += -specs=$(PSPSDK)/lib/prxspecs
LDFLAGS += -Wl,-q,-T$(PSPSDK)/lib/linkfile.prx
LDFLAGS += $(PSPSDK)/lib/prxexports.o

PSP_LIBS := -lm -lpspdebug -lpspdisplay -lpspgu -lpspge -lpspctrl -lpspaudio -lpsprtc -lpsppower -lpspsdk

include src/psp/sources.mk

ifeq ($(PSP_FULL),1)
C_FILES := $(PSP_GAME_C_FILES)
else
C_FILES := $(PSP_BOOTSTRAP_C_FILES)
endif

O_FILES := $(patsubst %.c,$(BUILD_DIR)/%.o,$(C_FILES))
DEP_FILES := $(O_FILES:.o=.d)

.PHONY: all psp clean FORCE

all: psp

psp: $(PSP_EBOOT)
	@printf 'EBOOT: %s\n' '$(PSP_EBOOT)'

$(PSP_EBOOT): $(PSP_ELF) $(PSP_SFO)
	$(PSP_PRXGEN) $(PSP_ELF) $(PSP_PRX)
	$(PACK_PBP) $@ $(PSP_SFO) NULL NULL NULL NULL NULL $(PSP_PRX) NULL

$(PSP_SFO):
	@mkdir -p $(dir $@)
	$(MKSFOEX) -d MEMSIZE=1 '$(PSP_TITLE)' $@

$(PSP_ELF): $(O_FILES) $(N64PSP_ARCHIVES) Makefile src/psp/sources.mk
	@mkdir -p $(dir $@)
	$(CC) $(O_FILES) $(LDFLAGS) $(N64PSP_ARCHIVES) $(PSP_LIBS) -o $@
	$(PSP_FIXUP_IMPORTS) $@

$(N64PSP_STAMP): FORCE
	$(MAKE) -C $(N64PSP_DIR) BUILD_PSP=$(N64PSP_BUILD) PSP_CC=$(CC) PSP_AR=$(AR) $(N64PSP_TARGETS)
	@touch $@

$(N64PSP_ARCHIVES): $(N64PSP_STAMP)

$(BUILD_DIR)/%.o: %.c Makefile src/psp/sources.mk
	@mkdir -p $(dir $@)
	$(CC) -c $(CFLAGS) $(IINC) -I$(dir $*) -MMD -MP -o $@ $<

clean:
	$(RM) -r $(BUILD_DIR)

FORCE:

-include $(DEP_FILES)
