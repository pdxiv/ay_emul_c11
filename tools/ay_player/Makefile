# Minimal CLI player/WAV exporter (Phase 4) driving engine/libayengine.a
# (Phases 0-2) through ALSA (Phase 3) or a hand-rolled WAV writer. See
# PORTING_TO_C11_LINUX.md §8 and migration_debt.yaml MIG-0025/0026/0027.

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
ENGINE_DIR = ../../engine
INCLUDES = -Iinclude -I$(ENGINE_DIR)/include -I$(ENGINE_DIR)/third_party/z80 \
           -I$(ENGINE_DIR)/third_party/musashi
LIBS = $(ENGINE_DIR)/libayengine.a -lasound -lm

BIN = ay_player
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
