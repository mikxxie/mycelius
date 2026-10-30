# launchpad - SpringBoard-style UI shell / render server for mycelius
#
# Portable GNU Makefile. Builds on Linux CI with cc/clang; cross-compiles
# for aarch64-unknown-freebsd with clang-18/lld-18 via `make cross`.
# On FreeBSD base this becomes a bsd.prog.mk port (PROG=launchpad).

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

# Cross-build for aarch64-unknown-freebsd (clang-18/lld-18).
#   SYSROOT=/path/to/freebsd make cross CC=clang-18
# Compiles to object files only (linking needs full FreeBSD libs).
cross: CC = clang-18
cross: CFLAGS += -target aarch64-unknown-freebsd --sysroot=$(SYSROOT) \
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
