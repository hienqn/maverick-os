# -*- makefile -*-
# =============================================================================
# PintOS Utility Programs Makefile
# =============================================================================
# Builds host-side helper utilities used by the PintOS test infrastructure.
# These run on the development machine, NOT inside PintOS.
# =============================================================================

# Default: build all utilities
all: setitimer-helper squish-pty squish-unix

# Use host compiler (not cross-compiler)
CC = gcc
CFLAGS = -Wall -W
LOADLIBES = -lm

# -----------------------------------------------------------------------------
# Utility Programs
# -----------------------------------------------------------------------------

# setitimer-helper: Sets interval timers for test timeout enforcement
setitimer-helper: setitimer-helper.o

# squish-pty: PTY (pseudo-terminal) handling for interactive tests
squish-pty: squish-pty.o

# squish-unix: Unix domain socket handling for test communication
squish-unix: squish-unix.o

# -----------------------------------------------------------------------------
# Clean Target
# -----------------------------------------------------------------------------

clean:
	rm -f *.o setitimer-helper squish-pty squish-unix
