# winman - iOS-like window manager for mycelius (FreeBSD-based mobile OS)
#
# Portable GNU Makefile. Builds on Linux CI (clang/gcc) and cross-compiles
# for aarch64-unknown-freebsd via clang-18/lld-18. On FreeBSD base this can be
# converted to bsd.prog.mk (PROG=winman SRCS=winman.c).

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
LDLIBS ?= -lxcb

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

PROG = winman launcher
SRCS_winman = winman.c
SRCS_launcher = launcher.c

.PHONY: all clean install cross

all: $(PROG)

winman: winman.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

launcher: launcher.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

# Cross-build for aarch64-unknown-freebsd using clang-18/lld-18.
# Usage: make cross CC=aarch64-unknown-freebsd-clang-18
cross: CFLAGS += -target aarch64-unknown-freebsd
cross: LDLIBS += -lxcb
cross: $(PROG)

clean:
	rm -f $(PROG)

install: all
	install -m 0755 -d $(DESTDIR)$(BINDIR)
	install -m 0755 winman $(DESTDIR)$(BINDIR)/winman
	install -m 0755 launcher $(DESTDIR)$(BINDIR)/launcher
