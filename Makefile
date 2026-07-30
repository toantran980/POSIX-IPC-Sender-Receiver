#
# Makefile - POSIX IPC Sender & Receiver
# Author: Toan Tran
# Version: 1.0.0
#
# Targets:
#   all         - Build sender and receiver (release)
#   debug       - Build with debug flags and no optimization
#   release     - Build with optimization (default)
#   install     - Install binaries to /usr/local/bin
#   uninstall   - Remove binaries from /usr/local/bin
#   clean       - Remove build artifacts
#   dist        - Create source tarball
#

# ===================================================================
# Configuration
# ===================================================================

CC       := gcc
CFLAGS   := -Wall -Wextra -Wpedantic -Wshadow -Wformat=2 \
            -Wconversion -Wstrict-prototypes -Wold-style-definition \
            -D_POSIX_C_SOURCE=200112L
LDLIBS   := -lrt -lm

# Version
VERSION  := $(shell cat VERSION 2>/dev/null || echo "1.0.0")

# Directories
SRCDIR   := src
BUILDDIR := build

# Sources and targets
SRCS     := $(SRCDIR)/sender.c $(SRCDIR)/receiver.c
OBJS     := $(SRCDIR)/log.o $(SRCDIR)/crc32.o $(SRCDIR)/ipc_utils.o
TARGETS  := send_c recv_c

# Installation prefix
PREFIX   := /usr/local
BINDIR   := $(PREFIX)/bin

# ===================================================================
# Flags per build type
# ===================================================================

# Release (default)
CFLAGS_RELEASE := -O2 -DNDEBUG -D_FORTIFY_SOURCE=2

# Debug
CFLAGS_DEBUG   := -O0 -g -DDEBUG -fstack-protector-all -fsanitize=address
LDFLAGS_DEBUG  := -fsanitize=address

# ===================================================================
# Build Rules
# ===================================================================

.PHONY: all debug release install uninstall clean dist

# Default target
all: CFLAGS += $(CFLAGS_RELEASE)
all: $(TARGETS)

# Debug target
debug: CFLAGS += $(CFLAGS_DEBUG)
debug: LDFLAGS += $(LDFLAGS_DEBUG)
debug: $(TARGETS)

# Release target (explicit)
release: CFLAGS += $(CFLAGS_RELEASE)
release: $(TARGETS)

# ===================================================================
# Object file compilation
# ===================================================================

$(SRCDIR)/%.o: $(SRCDIR)/%.c $(SRCDIR)/msg.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRCDIR)/log.o: $(SRCDIR)/log.c $(SRCDIR)/log.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRCDIR)/crc32.o: $(SRCDIR)/crc32.c $(SRCDIR)/crc32.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SRCDIR)/ipc_utils.o: $(SRCDIR)/ipc_utils.c $(SRCDIR)/ipc_utils.h $(SRCDIR)/log.h $(SRCDIR)/msg.h
	$(CC) $(CFLAGS) -c $< -o $@

# ===================================================================
# Binary linking
# ===================================================================

send_c: $(SRCDIR)/sender.o $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

recv_c: $(SRCDIR)/receiver.o $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) $^ -o $@ $(LDLIBS)

# ===================================================================
# Installation
# ===================================================================

install: $(TARGETS)
	@echo "Installing to $(DESTDIR)$(BINDIR)..."
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 send_c $(DESTDIR)$(BINDIR)/ipc-send
	install -m 755 recv_c $(DESTDIR)$(BINDIR)/ipc-recv
	@echo "Installed: $(DESTDIR)$(BINDIR)/ipc-send, $(DESTDIR)$(BINDIR)/ipc-recv"

uninstall:
	@echo "Removing from $(DESTDIR)$(BINDIR)..."
	rm -f $(DESTDIR)$(BINDIR)/ipc-send
	rm -f $(DESTDIR)$(BINDIR)/ipc-recv
	@echo "Uninstalled successfully"

# ===================================================================
# Cleanup
# ===================================================================

clean:
	$(RM) $(TARGETS)
	$(RM) $(SRCDIR)/*.o
	$(RM) -r $(BUILDDIR)
	@echo "Cleanup complete"

# ===================================================================
# Distribution
# ===================================================================

dist:
	@echo "Creating distribution tarball..."
	$(eval DISTDIR := posix-ipc-$(VERSION))
	$(eval DISTFILE := $(DISTDIR).tar.gz)
	mkdir -p /tmp/$(DISTDIR)
	cp -r Makefile VERSION README.md TODO.md src /tmp/$(DISTDIR)/
	cd /tmp && tar czf $(DISTFILE) $(DISTDIR)
	mv /tmp/$(DISTFILE) .
	rm -rf /tmp/$(DISTDIR)
	@echo "Created $(DISTFILE)"

# ===================================================================
# Help
# ===================================================================

help:
	@echo "POSIX IPC Sender & Receiver - Makefile"
	@echo ""
	@echo "Targets:"
	@echo "  all       - Build sender and receiver (release mode, default)"
	@echo "  debug     - Build with debug flags and sanitizers"
	@echo "  release   - Build with optimization flags"
	@echo "  install   - Install to $(DESTDIR)$(BINDIR) as ipc-send, ipc-recv"
	@echo "  uninstall - Remove installed binaries"
	@echo "  clean     - Remove build artifacts"
	@echo "  dist      - Create source tarball"
	@echo "  help      - Show this message"
	@echo ""
	@echo "Variables:"
	@echo "  CC=$(CC)  CFLAGS=$(CFLAGS)  LDLIBS=$(LDLIBS)"
	@echo "  DESTDIR=$(DESTDIR)  PREFIX=$(PREFIX)"
