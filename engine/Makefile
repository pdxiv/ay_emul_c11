# Standalone C11 static library: superzazu/z80 + Musashi (68000) + this
# project's z80_bus/m68k_bus adapters and AY.pas port. No GTK/ALSA/
# Players.pas dependency - see the approved plans
# (please-port-ay-emul-to-vectorized-sutton.md) for scope.

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g
INCLUDES = -Iinclude -Ithird_party/z80 -Ithird_party/musashi
# -MMD -MP: emit a per-.o .d file listing the headers each translation
# unit actually included, so editing a shared header (e.g. ay.h, which
# every one of the 18 format structs embeds via `ay_engine ay;`)
# correctly triggers a rebuild of every .o that includes it - not just
# the .o whose own .c file changed. Without this, make's plain `%.o:
# %.c` rule below only tracks the .c file itself, so a header-only
# change can silently leave stale .o files in libayengine.a: different
# translation units then disagree about struct layout (an ABI
# mismatch), which is exactly the bug MIG-0094 hit and fixed with a
# full `make clean` - this dependency tracking makes that whole class
# of bug impossible instead of relying on remembering to clean.

MUSASHI_DIR = third_party/musashi
MUSASHI_GEN = $(MUSASHI_DIR)/m68kops.c $(MUSASHI_DIR)/m68kops.h

SRC = src/hw/ay.c src/hw/z80_bus.c src/hw/m68k_bus.c src/hw/mfp.c src/hw/dma_sound.c \
      src/hw/atari_emulate.c src/util/trace_log.c src/util/lh5.c \
      src/formats/ay_file.c src/formats/ym_file.c src/formats/pt3_file.c \
      src/formats/sndh_file.c src/formats/vtx_file.c src/formats/pt1_file.c src/formats/gtr_file.c \
      src/formats/fls_file.c src/formats/stc_file.c src/formats/stp_file.c src/formats/pt2_file.c \
      src/formats/fxm_file.c src/formats/psm_file.c src/formats/asc_file.c src/formats/ftc_file.c \
      src/formats/psc_file.c src/formats/sqt_file.c src/player.c \
      third_party/z80/z80.c \
      $(MUSASHI_DIR)/m68kcpu.c $(MUSASHI_DIR)/m68kdasm.c \
      $(MUSASHI_DIR)/m68kops.c $(MUSASHI_DIR)/softfloat/softfloat.c
OBJ = $(SRC:.c=.o)
LIB = libayengine.a

.PHONY: all clean

all: $(LIB)

$(LIB): $(MUSASHI_GEN) $(OBJ)
	ar rcs $@ $(OBJ)

# m68kops.c/.h are code-generated from m68k_in.c by Musashi's own
# m68kmake tool - not checked into the vendored submodule, built here.
$(MUSASHI_GEN): $(MUSASHI_DIR)/m68k_in.c
	$(CC) -O2 -o $(MUSASHI_DIR)/m68kmake $(MUSASHI_DIR)/m68kmake.c
	cd $(MUSASHI_DIR) && ./m68kmake . m68k_in.c

# Vendored/generated Musashi 68000 core (git submodule + m68kmake's
# generated m68kops.c) - not this project's own code (README.md
# already exempts m68kops.c from this project's own conventions on
# that basis). Built without -Wall -Wextra: the handful of genuinely-
# unused-variable warnings this produces (m68kcpu.h's own `t`,
# m68kops.c's generated bitfield-instruction temporaries) come from
# the third-party source/generator itself, not anything fixable here
# without hand-patching vendored/generated code - these more specific
# pattern rules take priority over the generic %.o: %.c one below for
# anything under third_party/musashi/.
third_party/musashi/%.o: third_party/musashi/%.c
	$(CC) -std=c11 -O2 -g $(INCLUDES) -MMD -MP -c $< -o $@

third_party/musashi/softfloat/%.o: third_party/musashi/softfloat/%.c
	$(CC) -std=c11 -O2 -g $(INCLUDES) -MMD -MP -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

clean:
	rm -f $(OBJ) $(OBJ:.o=.d) $(LIB) $(MUSASHI_GEN) $(MUSASHI_DIR)/m68kmake
