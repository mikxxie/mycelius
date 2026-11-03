# launchpad - SpringBoard-style UI shell / render server for mycelius (Linux)
#
# Portable GNU Makefile. Builds on Linux with cc/clang; cross-compiles
# for aarch64-linux-gnu via `make cross`.
# BSD-style compositor: Unix domain socket input → scene graph → framebuffer.

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -I../include
LDLIBS ?=

PROG = launchpad
SRCS = launchpad.c render.c

PREFIX ?= /usr/local
SBINDIR ?= $(PREFIX)/sbin

.PHONY: all clean install cross

all: $(PROG)

$(PROG): $(SRCS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

# Cross-build for aarch64-linux-gnu.
#   SYSROOT=/path/to/linux make cross CC=aarch64-linux-gnu-gcc
cross: CC = aarch64-linux-gnu-gcc
cross: CFLAGS += -target aarch64-linux-gnu --sysroot=$(SYSROOT) \
                 -I$(SYSROOT)/usr/include -I../include
cross:
	@if [ -z "$(SYSROOT)" ]; then echo "SYSROOT required"; exit 1; fi
	for src in $(SRCS); do \
	  $(CC) $(CFLAGS) -c $$src -o /tmp/$$(basename $$src .c).aarch64.o; \
	done
	file /tmp/*.aarch64.o | grep -q aarch64

clean:
	rm -f $(PROG)

install: all
	install -m 0755 -d $(DESTDIR)$(SBINDIR)
	install -m 0755 launchpad $(DESTDIR)$(SBINDIR)/launchpad
