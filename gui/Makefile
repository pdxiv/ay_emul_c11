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
GTK_CFLAGS = $(shell pkg-config --cflags gtk+-2.0)
GTK_LIBS = $(shell pkg-config --libs gtk+-2.0)
INCLUDES = -Iinclude -Idialogs -I$(ENGINE_DIR)/include \
           -I$(ENGINE_DIR)/third_party/z80 -I$(ENGINE_DIR)/third_party/musashi \
           -I$(AY_PLAYER_DIR)/include $(GTK_CFLAGS)
LIBS = $(ENGINE_DIR)/libayengine.a $(GTK_LIBS) -lasound -lm

BIN = ay_emul_gui
# alsa_output.c is reused directly from tools/ay_player/src/ - no
# code duplication, no promotion to engine/ (engine/ is deliberately
# audio-output-agnostic, see PORTING_TO_C11_LINUX.md's phase-1 scope).
SRC = $(wildcard src/*.c) $(wildcard dialogs/*.c) $(AY_PLAYER_DIR)/src/alsa_output.c
OBJ = $(SRC:.c=.o)

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ) $(ENGINE_DIR)/libayengine.a
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

$(ENGINE_DIR)/libayengine.a:
	$(MAKE) -C $(ENGINE_DIR)

# -MMD -MP: see engine/Makefile's own comment - same rationale, same
# bug class this prevents (a shared header edit silently leaving a
# stale .o around instead of triggering a rebuild).
%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -MMD -MP -c $< -o $@

-include $(OBJ:.o=.d)

clean:
	rm -f $(BIN) $(OBJ) $(OBJ:.o=.d)
