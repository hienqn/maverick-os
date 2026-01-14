# -*- makefile -*-
# =============================================================================
# PintOS Example Programs Makefile
# =============================================================================
# Builds sample user programs that demonstrate PintOS functionality.
# These are useful for testing and as templates for new programs.
# =============================================================================

SRCDIR = ..

# =============================================================================
# Program Definitions
# =============================================================================
# To add a new program:
#   1. Add program name to PROGS list
#   2. Add programname_SRC = programname.c line

PROGS = cat cmp cp echo halt hex-dump ls mcat mcp mkdir pwd rm shell \
	bubsort lineup matmult recursor

# -----------------------------------------------------------------------------
# Project 2 (User Programs) - Basic utilities
# -----------------------------------------------------------------------------
cat_SRC = cat.c          # Print file contents
cmp_SRC = cmp.c          # Compare two files
cp_SRC = cp.c            # Copy file
echo_SRC = echo.c        # Print arguments
halt_SRC = halt.c        # Halt the system
hex-dump_SRC = hex-dump.c # Hex dump of file
lineup_SRC = lineup.c    # Print numbered lines
ls_SRC = ls.c            # List directory contents
recursor_SRC = recursor.c # Recursive process spawning
rm_SRC = rm.c            # Remove file

# -----------------------------------------------------------------------------
# Project 3 (Virtual Memory) - Memory-intensive programs
# -----------------------------------------------------------------------------
bubsort_SRC = bubsort.c  # Bubble sort (tests large memory)
matmult_SRC = matmult.c  # Matrix multiplication (tests memory access)
mcat_SRC = mcat.c        # Cat using mmap
mcp_SRC = mcp.c          # Copy using mmap

# -----------------------------------------------------------------------------
# Project 4 (File System) - Directory operations
# -----------------------------------------------------------------------------
mkdir_SRC = mkdir.c      # Create directory
pwd_SRC = pwd.c          # Print working directory
shell_SRC = shell.c      # Interactive shell

# =============================================================================
# Build System
# =============================================================================

# Include shared configuration and user program build rules
include $(SRCDIR)/Make.config
include $(SRCDIR)/Makefile.userprog
