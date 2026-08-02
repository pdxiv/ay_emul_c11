# Standalone C11 static library: superzazu/z80 + this milestone's z80_bus
# adapter and AY.pas port. No GTK/ALSA/Players.pas dependency - see the
# approved plan (please-port-ay-emul-to-vectorized-sutton.md) for scope.

CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g
INCLUDES = -Iinclude -Ithird_party/z80

SRC = src/ay.c src/z80_bus.c third_party/z80/z80.c
OBJ = $(SRC:.c=.o)
LIB = libayengine.a

.PHONY: all clean

all: $(LIB)

$(LIB): $(OBJ)
	ar rcs $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJ) $(LIB)
