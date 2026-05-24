# tests/Makefile — host-gcc unit tests for the portable parsers.
#
# Built with HOST gcc (not the mingw32 cross-compiler) + ASan + UBSan
# so we can exercise the pure-C code paths under sanitizers.  Run with
#   make -C tests run
# from inside `nix develop`.
#
# Shape mirrored from /opt/src/openrecet/tests/Makefile — the
# canonical test-harness style for our sibling project.  Only the
# Win32-independent code is tested here: src/main.c, src/gfx.c,
# src/audio.c, src/io.c, src/dev_hooks.c all touch the Win32 API
# and are excluded.

CC      ?= gcc
SAN     := -fsanitize=address,undefined -fno-sanitize-recover=all \
           -fno-omit-frame-pointer
CFLAGS  := -std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter \
           -I../src \
           -DOPENLORDS2_ROOT=\"$(abspath ..)\" \
           $(SAN) \
           -MMD -MP
LDFLAGS := $(SAN)

# Pure-C source under test.  As more portable modules land
# (e.g. localization-blob decoder, sprite-format reader), add them
# here.
SRCS_UNDER_TEST := ../src/strutil.c ../src/config.c \
                   ../src/engine_rate.c ../src/engine_phase.c \
                   ../src/engine_delta.c ../src/engine_anim_cycle.c \
                   ../src/engine_input.c ../src/engine_fps.c

# Test files — each defines one or more `int test_<name>(void)`
# functions registered in test_main.c.
SRCS_TESTS      := test_main.c test_strutil.c test_config.c \
                   test_engine_rate.c test_engine_phase.c \
                   test_engine_delta.c test_engine_anim_cycle.c \
                   test_engine_input.c test_engine_fps.c

OBJS := $(addprefix build/,$(notdir $(SRCS_UNDER_TEST:.c=.o)) \
                            $(SRCS_TESTS:.c=.o))
DEPS := $(OBJS:.o=.d)
OUT  := build/run_tests

.PHONY: all run clean
all: $(OUT)

run: $(OUT)
	./$(OUT)

build/%.o: ../src/%.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

build/%.o: %.c | build
	$(CC) $(CFLAGS) -c -o $@ $<

$(OUT): $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

build:
	@mkdir -p $@

clean:
	rm -rf build/

-include $(DEPS)
