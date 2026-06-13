# Trace Studio v3 replay CLI — build via `nix develop --command make`.
# Pure C, no Windows/D3D deps, so the default target builds HOST-NATIVE (gcc) for
# the pytest cross-check; `make mingw` cross-builds the Win32 replay.exe.
CC      ?= cc
CFLAGS  := -O2 -g -std=c11 -Wall -Wextra
MINGW   := i686-w64-mingw32-gcc

all: replay
replay: replay.c replay_core.c replay_core.h ../format/olv3_format.h
	$(CC) $(CFLAGS) -o $@ replay.c replay_core.c

mingw: replay.c replay_core.c replay_core.h ../format/olv3_format.h
	$(MINGW) $(CFLAGS) -o replay.exe replay.c replay_core.c -static-libgcc

clean:
	rm -f replay replay.exe
.PHONY: all mingw clean
