# Host build of the game-logic unit tests. No ESP-IDF / hardware needed:
#   make -C test/host run
#
# A sourced ESP-IDF shell puts a cross-compiler 'ld' on PATH ahead of the system
# linker; keep the host build on system tools so it works in any shell.
export PATH := /usr/bin:/bin:$(PATH)

CC      ?= cc
CFLAGS  ?= -std=c11 -Wall -Wextra -O1 -g
ROOT    := ../..
MAIN    := $(ROOT)/main
INCS       := -I$(MAIN) -Imock
GAME_SRCS  := $(MAIN)/pick.c $(MAIN)/proto.c $(MAIN)/pong.c $(MAIN)/survivor.c $(MAIN)/descender.c $(MAIN)/puzzler.c $(MAIN)/runner.c $(MAIN)/gfx.c stubs.c test_games.c
PROTO_SRCS := $(MAIN)/proto.c test_proto.c
GFX_SRCS   := $(MAIN)/gfx.c test_gfx.c
BINS       := game_tests proto_tests gfx_tests

all: $(BINS)

game_tests: $(GAME_SRCS) mock.h
	$(CC) $(CFLAGS) $(INCS) $(GAME_SRCS) -lm -o $@

proto_tests: $(PROTO_SRCS)
	$(CC) $(CFLAGS) $(INCS) $(PROTO_SRCS) -lm -o $@

gfx_tests: $(GFX_SRCS)
	$(CC) $(CFLAGS) $(INCS) $(GFX_SRCS) -o $@

run: $(BINS)
	./game_tests
	./proto_tests
	./gfx_tests

clean:
	rm -f $(BINS)

.PHONY: all run clean
