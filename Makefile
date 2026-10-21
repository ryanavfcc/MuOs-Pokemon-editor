CC ?= gcc
CFLAGS ?= -O2 -std=c11 -D_DEFAULT_SOURCE -Wall -Wextra
SDL_CFLAGS := $(shell pkg-config --cflags sdl2)
SDL_LIBS := $(shell pkg-config --libs sdl2)
all: pokemon-browser
pokemon-browser: src/main.c
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -o $@ $< $(SDL_LIBS)
clean:
	rm -f pokemon-browser
.PHONY: all clean
