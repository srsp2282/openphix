# Plain Makefile for POSIX systems (Linux, macOS). CMake is the primary build.
#
#   make                build openphix-tool
#   make test           run the unit tests
#   sudo make install   install the binary and, on Linux, the udev rule
#   sudo make uninstall remove both again
#
# Overridable: PREFIX (default /usr/local), BINDIR, UDEVDIR, DESTDIR and
# INSTALL_UDEV. A user install needs no root:
#   make install PREFIX=$HOME/.local INSTALL_UDEV=0

CC ?= cc
CFLAGS ?= -O2 -Wall -Wextra -Wshadow -std=c99 -D_POSIX_C_SOURCE=200809L -D_DARWIN_C_SOURCE
LIBUSB_CFLAGS ?= $(shell pkg-config --cflags libusb-1.0 2>/dev/null)
LIBUSB_LIBS ?= $(shell pkg-config --libs libusb-1.0 2>/dev/null || echo -lusb-1.0)
CORE = src/protocol.o src/device.o src/package.o src/fake.o src/crypto.o

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin
UDEVDIR ?= /etc/udev/rules.d
UDEV_RULES = 99-openphix.rules
INSTALL ?= install
UNAME_S := $(shell uname -s)

# The udev rule is a Linux thing and needs root. Set INSTALL_UDEV=0 to skip it,
# which is what you want for a user install into $HOME.
INSTALL_UDEV ?= 1
ifeq ($(UNAME_S),Linux)
DO_UDEV := $(INSTALL_UDEV)
else
DO_UDEV := 0
endif

all: openphix-tool

openphix-tool: src/main.o src/usb_transport.o $(CORE)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBUSB_LIBS)

src/usb_transport.o: src/usb_transport.c
	$(CC) $(CFLAGS) $(LIBUSB_CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

test: tests/test_protocol tests/test_device tests/test_crypto
	./tests/test_protocol && ./tests/test_device && ./tests/test_crypto

tests/test_protocol: tests/test_protocol.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^
tests/test_device: tests/test_device.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^
tests/test_crypto: tests/test_crypto.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^

install: all
	@$(INSTALL) -d "$(DESTDIR)$(BINDIR)" && \
	  $(INSTALL) -m 755 openphix-tool "$(DESTDIR)$(BINDIR)/openphix-tool" || \
	  { echo "openphix-tool: cannot install into $(BINDIR)." >&2; \
	    echo "  try 'sudo make install', or 'make install PREFIX=\$$HOME/.local'." >&2; exit 1; }
	@echo "installed $(DESTDIR)$(BINDIR)/openphix-tool"
ifeq ($(DO_UDEV),1)
	@$(INSTALL) -d "$(DESTDIR)$(UDEVDIR)" && \
	  $(INSTALL) -m 644 $(UDEV_RULES) "$(DESTDIR)$(UDEVDIR)/$(UDEV_RULES)" || \
	  { echo "openphix-tool: cannot install the udev rule into $(UDEVDIR)." >&2; \
	    echo "  the binary is installed. Run 'sudo make install' to finish, or pass" >&2; \
	    echo "  INSTALL_UDEV=0 to skip it and call the tool with sudo instead." >&2; exit 1; }
	@echo "installed $(DESTDIR)$(UDEVDIR)/$(UDEV_RULES)"
	@if [ -z "$(DESTDIR)" ] && command -v udevadm >/dev/null 2>&1; then \
	  udevadm control --reload-rules && udevadm trigger --subsystem-match=usb && \
	  echo "udev rules reloaded. Replug the tool and it works without sudo."; \
	else \
	  echo "staged only, udev not reloaded."; \
	fi
endif

uninstall:
	rm -f "$(DESTDIR)$(BINDIR)/openphix-tool"
	@echo "removed $(DESTDIR)$(BINDIR)/openphix-tool"
ifeq ($(DO_UDEV),1)
	rm -f "$(DESTDIR)$(UDEVDIR)/$(UDEV_RULES)"
	@echo "removed $(DESTDIR)$(UDEVDIR)/$(UDEV_RULES)"
	@if [ -z "$(DESTDIR)" ] && command -v udevadm >/dev/null 2>&1; then \
	  udevadm control --reload-rules; \
	fi
endif

clean:
	rm -f openphix-tool src/*.o tests/test_protocol tests/test_device tests/test_crypto

.PHONY: all test install uninstall clean
