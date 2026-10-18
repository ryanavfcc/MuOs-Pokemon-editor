CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra
all: pokemon-browser
pokemon-browser: src/main.c
	$(CC) $(CFLAGS) -o $@ $< $(shell sdl2-config --cflags --libs)
clean:
	rm -f pokemon-browser
