CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -fPIC
GTK_CFLAGS := $(shell pkg-config --cflags gtk4 gmodule-2.0)
GTK_LIBS   := $(shell pkg-config --libs gtk4 gmodule-2.0)

blend_brush.so: blend_brush.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -I../../src -shared -o $@ blend_brush.c $(GTK_LIBS)

install: blend_brush.so
	mkdir -p ../../build/addons
	cp blend_brush.so ../../build/addons/

clean:
	rm -f blend_brush.so

.PHONY: install clean
