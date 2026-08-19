# Phase 5 (GUI) kickoff milestone: skinned GTK2 playback window. See
# PORTING_TO_C11_LINUX.md §4.1/§5 and migration_debt.yaml for scope and
# what's deferred.

CC ?= cc
# -Wno-deprecated-declarations: GTK2/glib's OWN system headers reference
# their own deprecated APIs internally (gtktypeutils.h's GTypeDebugFlags,
# gtktooltips.h's GTimeVal) - every single file in this directory
# transitively includes <gtk/gtk.h>, so without this every one of them
# would emit the same handful of warnings that have nothing to do with
# this project's own code (same rationale as engine/Makefile's separate
# per-file override for vendored/generated Musashi code, just scoped
# here via a flag instead of a separate pattern rule since the
# "foreign" header is pulled in by every single translation unit rather
# than a specific subset of files).
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Wno-deprecated-declarations -O2 -pthread
ENGINE_DIR = ../engine
AY_PLAYER_DIR = ../tools/ay_player
AY_EXPORT_DIR = ../tools/ay_export
LHASSA_DIR = $(ENGINE_DIR)/third_party/lhassa/Source
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-2.0)
GTK_LIBS = $(shell pkg-config --libs gtk+-2.0)
INCLUDES = -Iinclude -I$(ENGINE_DIR)/include \
           -I$(ENGINE_DIR)/third_party/z80 -I$(ENGINE_DIR)/third_party/musashi \
           -I$(AY_PLAYER_DIR)/include -I$(AY_EXPORT_DIR)/include \
           -I$(LHASSA_DIR)/include -I$(GEN_DIR) $(GTK_CFLAGS)
LIBS = $(ENGINE_DIR)/libayengine.a $(GTK_LIBS) -lasound -lm

# tools/ay_export/src/vtx_export.c (MIG-0010's VTX writer) needs LZH
# ("-lh5-") compression - see tools/ay_export/Makefile's own comment for
# why LhASsA is vendored and built this way (-DHOST=1, C89, its own
# relaxed warning set) rather than folded into engine/libayengine.a.
# MIG-0128 (playlist "Convert" menu) is this port's second real consumer
# of vtx_export.c, alongside the standalone ay_export CLI tool - same
# object list, same build rule, duplicated here rather than factored out
# since Make has no clean "reusable fragment" story across two totally
# separate top-level Makefiles.
LHASSA_SRC = $(LHASSA_DIR)/src/lh_error.c $(LHASSA_DIR)/src/lh_crc.c \
             $(LHASSA_DIR)/src/lh_crypto.c $(LHASSA_DIR)/src/lh_datetime.c \
             $(LHASSA_DIR)/src/lh_codec.c $(LHASSA_DIR)/src/lh_lh5dec.c \
             $(LHASSA_DIR)/src/lh_lh5enc.c $(LHASSA_DIR)/src/lh_lh1dec.c \
             $(LHASSA_DIR)/src/lh_charset.c $(LHASSA_DIR)/src/lh_header.c \
             $(LHASSA_DIR)/src/lh_archive.c $(LHASSA_DIR)/src/lh_buffer.c \
             $(LHASSA_DIR)/src/lh_lzhuf.c
LHASSA_OBJ = $(LHASSA_SRC:.c=.o)
LHASSA_CFLAGS = -std=c89 -pedantic -Wno-unused-function -Wno-unused-but-set-variable \
                -Wno-unused-variable -Wno-stringop-truncation -O2 -DHOST=1

BIN = ay_emul_gui

# Binary assets (skin, About-box bitmap) live as real files under
# assets/ - not hand-editable source, so diffing/reviewing/swapping
# them doesn't mean wading through a 6000+/39000+ line byte-array
# dump. gen_bin_c.sh turns each into the equivalent `const unsigned
# char gui_X[]`/`gui_X_len` C definition at build time, landing in
# GEN_DIR (gitignored - regenerated every build, never checked in).
GEN_DIR = build/generated
GEN_TOOL = tools/gen_bin_c.sh

# gui/src/mixer_win.c's widget CONSTRUCTION (MIG-0132) - regenerated from
# Mixer.lfm at build time via tools/lfm_gen/lfm_gen.py's --widgets-only
# mode, same GEN_DIR/"regenerated every build, never checked in"
# convention as GEN_TOOL above. Reads the checked-in copy under
# ../lfm_specs/ (see that directory's own README.md for provenance/sync
# convention) rather than the ay_emul/ reference submodule directly, so
# building gui/ needs python3 but NOT the ay_emul submodule checked out.
# See gui/src/mixer_win.c's own header comment and migration_debt.yaml's
# MIG-0132 entry for the full generator rationale (widget construction
# only - never layout, never signal-wiring, both stay 100% hand-written).
MIXER_LFM = ../lfm_specs/Mixer.lfm
LFM_GEN_TOOL = ../tools/lfm_gen/lfm_gen.py
GEN_SRC = $(GEN_DIR)/default_skin.c $(GEN_DIR)/about_bitmap.c \
          $(GEN_DIR)/mixer_win_gen.c

# alsa_output.c/wav.c are reused directly from tools/ay_player/src/, and
# vtx_export.c from tools/ay_export/src/ (MIG-0128's own "Convert"
# playlist menu, PSG via engine/psg_export.h needs no extra source at
# all) - no code duplication, no promotion to engine/ (engine/ is
# deliberately audio-output/export-agnostic, see PORTING_TO_C11_LINUX.
# md's phase-1 scope).
SRC = $(wildcard src/*.c) $(wildcard src/dialogs/*.c) \
      $(AY_PLAYER_DIR)/src/alsa_output.c $(AY_PLAYER_DIR)/src/wav.c \
      $(AY_EXPORT_DIR)/src/vtx_export.c $(GEN_SRC)
OBJ = $(SRC:.c=.o)

.PHONY: all clean
.DEFAULT_GOAL := all

all: $(BIN)

$(BIN): $(OBJ) $(LHASSA_OBJ) $(ENGINE_DIR)/libayengine.a
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LHASSA_OBJ) $(LIBS)

$(LHASSA_DIR)/src/%.o: $(LHASSA_DIR)/src/%.c
	$(CC) $(LHASSA_CFLAGS) -I$(LHASSA_DIR)/include -I$(LHASSA_DIR)/src -c $< -o $@

$(ENGINE_DIR)/libayengine.a:
	$(MAKE) -C $(ENGINE_DIR)

# Generated from assets/ at build time - see the GEN_DIR comment above.
$(GEN_DIR)/default_skin.c: assets/default_skin.ays $(GEN_TOOL)
	@mkdir -p $(GEN_DIR)
	$(GEN_TOOL) assets/default_skin.ays $@

$(GEN_DIR)/about_bitmap.c: assets/about.bmp $(GEN_TOOL)
	@mkdir -p $(GEN_DIR)
	$(GEN_TOOL) assets/about.bmp $@

# gui/src/mixer_win.c's widget construction (MIG-0132) - see the GEN_SRC
# comment above. One rule produces both the .c and the .h together (the
# generator writes them in one invocation); listing both as targets
# tells make a single recipe run satisfies either.
$(GEN_DIR)/mixer_win_gen.c $(GEN_DIR)/mixer_win_gen.h: $(MIXER_LFM) $(LFM_GEN_TOOL)
	@mkdir -p $(GEN_DIR)
	python3 $(LFM_GEN_TOOL) --widgets-only $(MIXER_LFM) $(GEN_DIR)/mixer_win_gen

# gui/src/mixer_win.c #includes "mixer_win_gen.h" directly (not just its
# own .o depending on the generated .c) - an order-only prerequisite on
# every object file guarantees the header exists before ANY compile that
# might need it, without forcing an unrelated file's .o to rebuild just
# because Mixer.lfm changed (real incremental correctness for that comes
# from -MMD -MP's own per-file .d tracking below, once a first build has
# run).
$(OBJ): | $(GEN_DIR)/mixer_win_gen.h

# -MMD -MP: see engine/Makefile's own comment - same rationale, same
# bug class this prevents (a shared header edit silently leaving a
# stale .o around instead of triggering a rebuild).
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

clean:
	rm -f $(BIN) $(OBJ) $(OBJ:.o=.d)
	rm -rf $(GEN_DIR)
