# tools/launcher/Makefile — build openlords2-launcher.exe.
#
# A Job-Object wrapper for the port's .exe and for the retail Lords2.exe
# under the harness — see openlords2-launcher.c for the why.  Built with
# the same mingw-w64 32-bit toolchain that builds src/, but as a
# separate binary because the launcher is a dev tool, not part of the
# distributable.
#
# Invoke from inside `nix develop`.  Same single-TU pattern as src/.

CC = i686-w64-mingw32-gcc

CFLAGS  ?= -std=c11 -O2 -g \
           -Wall -Wextra -Wno-unused-parameter \
           -DWINVER=0x0500 -D_WIN32_WINNT=0x0500
LDFLAGS ?= -static-libgcc
LIBS    ?= -lkernel32 -luser32

SRCS := openlords2-launcher.c

OUT ?= ../../build/openlords2-launcher.exe

.PHONY: all clean
all: $(OUT)

.PHONY: $(OUT)
$(OUT): $(SRCS)
	@mkdir -p $(dir $@)
	@rm -f $@
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(LIBS)

clean:
	rm -f $(OUT)
