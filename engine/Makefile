# Standalone C11 static library: superzazu/z80 + Musashi (68000) + this
# project's z80_bus/m68k_bus adapters and AY.pas port. No GTK/ALSA/
# Players.pas dependency - see the approved plans
# (please-port-ay-emul-to-vectorized-sutton.md) for scope.

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g
INCLUDES = -Iinclude -Ithird_party/z80 -Ithird_party/musashi

MUSASHI_DIR = third_party/musashi
MUSASHI_GEN = $(MUSASHI_DIR)/m68kops.c $(MUSASHI_DIR)/m68kops.h

SRC = src/ay.c src/z80_bus.c src/m68k_bus.c \
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

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(LIB) $(MUSASHI_GEN) $(MUSASHI_DIR)/m68kmake
