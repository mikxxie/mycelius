# backboardd - low-level input daemon for mycelius (Linux-based mobile OS)
#
# Portable GNU Makefile. Builds on Linux with cc/clang; cross-compiles
# for aarch64-linux-gnu via `make cross`.
# BSD-style daemon: input evdev → Unix domain socket → LaunchPad.

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11 -I../include
LDLIBS ?=

PROG = backboardd
SRCS = backboardd.c

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
	$(CC) $(CFLAGS) -c $(SRCS) -o /tmp/$(PROG).aarch64.o
	file /tmp/$(PROG).aarch64.o | grep -q aarch64

clean:
	rm -f $(PROG)

install: all
	install -m 0755 -d $(DESTDIR)$(SBINDIR)
	install -m 0755 backboardd $(DESTDIR)$(SBINDIR)/backboardd
