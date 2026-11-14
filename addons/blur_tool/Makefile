# Build:  make
# Output: ../../build/addons/blur_tool.so

CC      ?= cc
CFLAGS  ?= -O2 -g -Wall -Wextra -fPIC
GTK_CFLAGS := $(shell pkg-config --cflags gtk4)
GTK_LIBS   := $(shell pkg-config --libs gtk4)

blur_tool.so: blur_tool.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -I../../src -shared -o $@ blur_tool.c $(GTK_LIBS)

install: blur_tool.so
	mkdir -p ../../build/addons
	cp blur_tool.so ../../build/addons/

clean:
	rm -f blur_tool.so

.PHONY: install clean
