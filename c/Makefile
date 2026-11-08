# Makefile for the PyVaders C port.
#
# Targets:
#   make            - build the SDL frontend binary (needs SDL2; see notes)
#   make test       - build & run the pure-logic Unity unit tests
#   make test-asan  - run tests under AddressSanitizer + UBSan
#   make valgrind   - run the test binary under valgrind
#   make clean      - remove build artifacts
#
# The C *core* (src/game.c) is SDL-free, so `make test` needs only a C
# compiler + libm. The `game` target additionally requires SDL2, SDL2_image,
# SDL_ttf and SDL_mixer.
#
# SDL flags come from pkg-config when available. This is required on macOS
# with Homebrew, where the headers live under /opt/homebrew/include and are
# not on the compiler's default search path.

CC         ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS     ?= -std=c11 -Wall -Wextra -Werror -pedantic
LDFLAGS    ?= -lm

SDL_CFLAGS := $(shell $(PKG_CONFIG) --cflags sdl2 SDL2_image SDL2_ttf SDL2_mixer 2>/dev/null)
SDL_LIBS   := $(shell $(PKG_CONFIG) --libs sdl2 SDL2_image SDL2_ttf SDL2_mixer 2>/dev/null || \
                  echo "-lSDL2 -lSDL2_image -lSDL2_ttf -lSDL2_mixer")

CORE_SRC   := src/game.c
TEST_SRC   := test/test_game.c third_party/unity/unity.c
FRONT_SRC  := src/render.c src/main.c

CORE_OBJECTS := $(CORE_SRC:.c=.o)
BUILD_DIR    := build
TEST_BIN     := $(BUILD_DIR)/test_game
GAME_BIN     := $(BUILD_DIR)/cvander

HEADERS := include/pyvaders.h
UNITY_INC := -Ithird_party/unity

.PHONY: all test test-asan valgrind clean help

all: $(GAME_BIN)

## Build the SDL frontend (requires SDL2 / SDL2_image / SDL_ttf / SDL_mixer)
$(GAME_BIN): $(FRONT_SRC) $(CORE_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SDL_CFLAGS) -Iinclude -Isrc $(FRONT_SRC) $(CORE_SRC) -o $@ \
		-pthread $(SDL_LIBS) $(LDFLAGS)

## Core object (SDL-free, leak-free by construction)
$(CORE_OBJECTS): $(CORE_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -Iinclude -c $(CORE_SRC) -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

## Unit tests for the pure logic core
$(TEST_BIN): $(TEST_SRC) $(CORE_SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(UNITY_INC) -Iinclude $(TEST_SRC) $(CORE_SRC) -o $@ $(LDFLAGS)

test: $(TEST_BIN)
	@$(TEST_BIN)

## AddressSanitizer + UndefinedBehaviourSanitizer
test-asan: | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(UNITY_INC) -fsanitize=address,undefined -fno-omit-frame-pointer \
		-Iinclude $(TEST_SRC) $(CORE_SRC) -o $(BUILD_DIR)/test_asan $(LDFLAGS)
	@$(BUILD_DIR)/test_asan

## Valgrind memory check on the test binary
valgrind: $(TEST_BIN)
	valgrind --error-exitcode=1 --leak-check=full \
		--show-leak-kinds=all $(TEST_BIN)

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Targets: all test test-asan valgrind clean"
