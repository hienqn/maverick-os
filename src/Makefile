# -*- makefile -*-
# =============================================================================
# PintOS Top-Level Makefile
# =============================================================================
# This Makefile provides project-wide utilities (clean, tags, format).
# Actual kernel builds are done in subdirectories (threads/, userprog/, etc.)
# =============================================================================

# Subdirectories containing kernel builds (each has its own Makefile)
BUILD_SUBDIRS = threads userprog vm filesys

# Default target: remind user to build in subdirectories
# :: syntax creates a "double-colon rule" allowing multiple definitions
all::
	@echo "Run 'make' in subdirectories: $(BUILD_SUBDIRS)."
	@echo "This top-level make has only 'clean' targets."

# =============================================================================
# Clean Targets
# =============================================================================

# All directories to clean (includes examples and utils)
CLEAN_SUBDIRS = $(BUILD_SUBDIRS) examples utils

# Recursively clean all subdirectories
# $(MAKE) -C $$d $@ = run "make clean" in each directory
# $$d escapes the $ for shell expansion in the for loop
clean::
	for d in $(CLEAN_SUBDIRS); do $(MAKE) -C $$d $@; done
	rm -f TAGS tags

# Deep clean: also remove editor backup files (*~)
distclean:: clean
	find . -name '*~' -exec rm '{}' \;

# =============================================================================
# Code Navigation Tags (for editors like Vim/Emacs)
# =============================================================================

# Directories to index for code navigation
TAGS_SUBDIRS = $(BUILD_SUBDIRS) devices lib

# Command to find all C, header, and assembly source files
TAGS_SOURCES = find $(TAGS_SUBDIRS) -name \*.[chS] -print

# Generate Emacs TAGS file
TAGS::
	etags --members `$(TAGS_SOURCES)`

# Generate Vim tags file
tags::
	ctags -T --no-warn `$(TAGS_SOURCES)`

# Generate cscope database (another code navigation tool)
cscope.files::
	$(TAGS_SOURCES) > cscope.files

cscope:: cscope.files
	cscope -b -q -k

# =============================================================================
# Code Formatting
# =============================================================================

# Format all C source and header files using clang-format
# -prune skips the build/ directory to avoid formatting generated files
format::
	find . -name build -prune -o -type f -name '*.[ch]' -print | \
	xargs clang-format -i -style=file
