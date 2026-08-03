# Standalone build for identify_ay_file - a from-scratch reimplementation
# of ay_emul's file-format identification logic (see identify_ay_file.md).
# Deliberately has no dependency on engine/'s player port: identification
# and playback are different concerns, ported independently from the
# Pascal source.

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
INCLUDES = -Iinclude

BIN = identify_ay_file
SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(BIN) $(OBJ)
