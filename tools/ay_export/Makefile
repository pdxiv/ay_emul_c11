# ay_export - MIG-0010's PSG/VTX register-write-log exporter. VTX export
# needs LZH ("-lh5-") compression - vendored via engine/third_party/lhassa
# (see migration_debt.yaml for why a third-party encoder was used instead
# of hand-porting lh5.pas's own Encode_Buffer_To_File) rather than added
# to engine/libayengine.a itself, keeping the core engine free of a
# compression dependency most of its consumers (playback) don't need.

CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -O2
ENGINE_DIR = ../../engine
LHASSA_DIR = $(ENGINE_DIR)/third_party/lhassa/Source
INCLUDES = -Iinclude -I$(ENGINE_DIR)/include -I$(ENGINE_DIR)/third_party/z80 \
           -I$(ENGINE_DIR)/third_party/musashi -I$(LHASSA_DIR)/include
LIBS = $(ENGINE_DIR)/libayengine.a -lm

BIN = ay_export
SRC = $(wildcard src/*.c)
OBJ = $(SRC:.c=.o)

# The full liblh.a source list (Source/Makefile's own LIBSRC, minus the
# two Amiga-only files) - see migration_debt.yaml's own investigation for
# why lh_lh5enc.c alone isn't enough (it depends on lh_codec.c/lh_lzhuf.c/
# lh_crc.c etc for shared bit-I/O and CRC helpers). Built with -DHOST=1,
# matching Source/Makefile's own `make HOST=1` (LhASsA's portable/non-
# Amiga build mode - see lh_platform.h).
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

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJ) $(LHASSA_OBJ) $(ENGINE_DIR)/libayengine.a
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(LHASSA_OBJ) $(LIBS)

$(ENGINE_DIR)/libayengine.a:
	$(MAKE) -C $(ENGINE_DIR)

%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

$(LHASSA_DIR)/src/%.o: $(LHASSA_DIR)/src/%.c
	$(CC) $(LHASSA_CFLAGS) -I$(LHASSA_DIR)/include -I$(LHASSA_DIR)/src -c $< -o $@

clean:
	rm -f $(BIN) $(OBJ) $(LHASSA_OBJ)
