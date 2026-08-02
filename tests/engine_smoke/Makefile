CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g
ENGINE = ../../engine
INCLUDES = -I$(ENGINE)/include -I$(ENGINE)/third_party/z80
LIB = $(ENGINE)/libayengine.a

.PHONY: all clean

all: test_z80_bus test_ay

$(LIB):
	$(MAKE) -C $(ENGINE)

test_z80_bus: test_z80_bus.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) test_z80_bus.c $(LIB) -o $@

test_ay: test_ay.c $(LIB)
	$(CC) $(CFLAGS) $(INCLUDES) test_ay.c $(LIB) -o $@ -lm

clean:
	rm -f test_z80_bus test_ay
