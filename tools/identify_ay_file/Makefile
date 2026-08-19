# Build for identify_ay_file - a from-scratch reimplementation of
# ay_emul's file-format identification logic (see identify_ay_file.md).
#
# Links against engine/libayengine.a for the IntegrityCheck confirmation
# step (migration_debt.yaml MIG-0023b/MIG-0102-family): Pascal's real
# Module_Detector rejects a structural match unless LoadTrackerModule +
# GetTimeXXX also produce a nonzero playable duration, and engine/'s
# per-format loaders already carry oracle-validated GetTimeXXX ports
# (global_tick_max) for 11 of the 15 structural-tracker formats. This
# was a deliberate reversal of this tool's earlier "no engine dependency"
# design (see git history) - reusing already-validated engine code for
# both the structural match and the IntegrityCheck step was judged better
# than re-deriving/maintaining independent logic for the same Pascal
# behavior a second time.
CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
ENGINE_DIR = ../../engine
INCLUDES = -Iinclude -I$(ENGINE_DIR)/include -I$(ENGINE_DIR)/third_party/z80 \
           -I$(ENGINE_DIR)/third_party/musashi
LIBS = $(ENGINE_DIR)/libayengine.a -lm

BIN = identify_ay_file
SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ) $(ENGINE_DIR)/libayengine.a
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LIBS)

$(ENGINE_DIR)/libayengine.a:
	$(MAKE) -C $(ENGINE_DIR)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(BIN) $(OBJ)
