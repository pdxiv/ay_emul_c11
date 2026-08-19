CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g
ENGINE = ../../engine
AY_EXPORT = ../../tools/ay_export
LHASSA_DIR = $(ENGINE)/third_party/lhassa/Source
INCLUDES = -I$(ENGINE)/include -I$(ENGINE)/third_party/z80 -I$(ENGINE)/third_party/musashi \
           -I$(AY_EXPORT)/include -I$(LHASSA_DIR)/include
LIB = $(ENGINE)/libayengine.a
LHASSA_OBJ = $(wildcard $(LHASSA_DIR)/src/*.o)

.PHONY: all clean

all: test_vtx_export

$(LIB):
	$(MAKE) -C $(ENGINE)

$(LHASSA_DIR)/src/lh_lh5enc.o:
	$(MAKE) -C $(AY_EXPORT)

test_vtx_export: test_vtx_export.c $(AY_EXPORT)/src/vtx_export.c $(LIB) $(LHASSA_DIR)/src/lh_lh5enc.o
	$(CC) $(CFLAGS) $(INCLUDES) test_vtx_export.c $(AY_EXPORT)/src/vtx_export.c \
	  $(LHASSA_DIR)/src/*.o $(LIB) -o $@ -lm

clean:
	rm -f test_vtx_export
