# SYNTHRLN - Multi-Platform BBS-to-TCP Bridge
# Makefile - Linux native and cross-compilation targets
#
# Targets:
#   make                  - Linux native (default)
#   make linux            - Linux native (explicit)
#   make windows          - Cross-compile for Windows (requires mingw-w64)
#   make windows-local    - Windows local-mode only build
#   make clean            - Remove build artifacts

# ============================================================================
# Compiler & flags
# ============================================================================

CC_LINUX   := gcc
CC_WIN     := x86_64-w64-mingw32-gcc

CFLAGS_COMMON := -std=c99 -Wall -Wextra -pedantic \
                 -Wno-unused-parameter \
                 -O2

CFLAGS_LINUX  := $(CFLAGS_COMMON) -D__linux__
CFLAGS_WIN    := $(CFLAGS_COMMON) -D_WIN32 -DWIN32

LDFLAGS_LINUX :=
LDFLAGS_WIN   := -lws2_32

SRCDIR := src
OUTDIR := build

# ============================================================================
# Source file lists (platform-specific selection)
# ============================================================================

# Sources common to all platforms
SRC_COMMON := \
    $(SRCDIR)/main.c \
    $(SRCDIR)/config.c \
    $(SRCDIR)/dropfile.c

# Linux: stdio pipes + BSD sockets (bbs_posix.c handles both BBS and --local mode)
SRC_LINUX := \
    $(SRC_COMMON) \
    $(SRCDIR)/bbs_posix.c \
    $(SRCDIR)/net_posix.c

# Windows: Winsock handle inheritance + Winsock outbound
SRC_WIN := \
    $(SRC_COMMON) \
    $(SRCDIR)/bbs_posix.c \
    $(SRCDIR)/net_posix.c

# ============================================================================
# Targets
# ============================================================================

.PHONY: all linux windows windows-local clean install

all: linux

linux: $(OUTDIR)/synthrln

$(OUTDIR)/synthrln: $(SRC_LINUX) | $(OUTDIR)
	$(CC_LINUX) $(CFLAGS_LINUX) $(SRC_LINUX) -o $@ $(LDFLAGS_LINUX)
	@echo "Built: $@"

windows: $(OUTDIR)/synthrln.exe

$(OUTDIR)/synthrln.exe: $(SRC_WIN) | $(OUTDIR)
	$(CC_WIN) $(CFLAGS_WIN) $(SRC_WIN) -o $@ $(LDFLAGS_WIN)
	@echo "Built: $@"

$(OUTDIR):
	mkdir -p $(OUTDIR)

# ============================================================================
# Install (Linux only)
# ============================================================================

PREFIX ?= /usr/local

install: $(OUTDIR)/synthrln
	install -D -m 755 $(OUTDIR)/synthrln $(DESTDIR)$(PREFIX)/bin/synthrln
	@echo "Installed to $(DESTDIR)$(PREFIX)/bin/synthrln"

# ============================================================================
# Clean
# ============================================================================

clean:
	rm -rf $(OUTDIR)
	@echo "Cleaned build directory."

# ============================================================================
# Debug builds
# ============================================================================

debug-linux: CFLAGS_LINUX := $(CFLAGS_COMMON) -D__linux__ -DDEBUG -g -O0 -fsanitize=address
debug-linux: $(OUTDIR)/synthrln-debug

$(OUTDIR)/synthrln-debug: $(SRC_LINUX) | $(OUTDIR)
	$(CC_LINUX) $(CFLAGS_LINUX) $(SRC_LINUX) -o $@ $(LDFLAGS_LINUX) -fsanitize=address
	@echo "Built debug: $@"
