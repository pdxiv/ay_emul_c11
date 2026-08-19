CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2 -g
ENGINE = ../../engine
GUI = ../../gui
INCLUDES = -I$(GUI)/include -I$(ENGINE)/include -I$(ENGINE)/third_party/z80 \
           -I$(ENGINE)/third_party/musashi $(shell pkg-config --cflags glib-2.0)
LIBS = $(shell pkg-config --libs glib-2.0) -lm
LIB = $(ENGINE)/libayengine.a

.PHONY: all clean

all: test_playlist_ayl

$(LIB):
	$(MAKE) -C $(ENGINE)

test_playlist_ayl: test_playlist_ayl.c $(LIB) $(GUI)/src/playlist.c
	$(CC) $(CFLAGS) $(INCLUDES) test_playlist_ayl.c $(GUI)/src/playlist.c $(LIB) -o $@ $(LIBS)

clean:
	rm -f test_playlist_ayl
