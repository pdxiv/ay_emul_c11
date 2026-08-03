CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g
ENGINE = ../../engine
INCLUDES = -I$(ENGINE)/include -I$(ENGINE)/third_party/z80 -I$(ENGINE)/third_party/musashi
LIB = $(ENGINE)/libayengine.a

.PHONY: all clean

all: test_z80_bus test_ay test_m68k_bus test_atari test_ay_file test_ym_file test_pt3_file test_sndh_file test_vtx_file

$(LIB):
	$(MAKE) -C $(ENGINE)

test_z80_bus: test_z80_bus.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) test_z80_bus.c $(LIB) -o $@

test_ay: test_ay.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) test_ay.c $(LIB) -o $@ -lm

test_m68k_bus: test_m68k_bus.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) test_m68k_bus.c $(LIB) -o $@ -lm

test_atari: test_atari.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) test_atari.c $(LIB) -o $@ -lm

test_ay_file: test_ay_file.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) test_ay_file.c $(LIB) -o $@ -lm

test_ym_file: test_ym_file.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) test_ym_file.c $(LIB) -o $@ -lm

test_pt3_file: test_pt3_file.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) test_pt3_file.c $(LIB) -o $@ -lm

test_sndh_file: test_sndh_file.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) test_sndh_file.c $(LIB) -o $@ -lm

test_vtx_file: test_vtx_file.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) test_vtx_file.c $(LIB) -o $@ -lm

clean:
	rm -f test_z80_bus test_ay test_m68k_bus test_atari test_ay_file test_ym_file test_pt3_file test_sndh_file test_vtx_file
