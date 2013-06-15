#  ============================================================================
#   Makefile for *NIX.
#
#   RDPSIM: Reality Display Processor SIMulator.
#   Copyright (C) 2013, Tyler J. Stachecki.
#   All rights reserved.
#
#   This file is subject to the terms and conditions defined in
#   file 'LICENSE', which is part of this source code package.
#  ============================================================================
TARGET = librdp.a

# ============================================================================
#  A list of files to link into the library.
# ============================================================================
SOURCES := $(wildcard *.c)
OBJECTS = $(addprefix $(OBJECT_DIR)/, $(notdir $(SOURCES:.c=.o)))

# =============================================================================
#  Build variables and settings.
# =============================================================================
OBJECT_DIR=Objects

BLUE=$(shell tput setaf 4)
PURPLE=$(shell tput setaf 5)
TEXTRESET=$(shell tput sgr0)
YELLOW=$(shell tput setaf 3)

# ============================================================================
#  General build rules.
# ============================================================================
ECHO=/usr/bin/printf "%s\n"
MKDIR = /bin/mkdir -p

AR = ar
DOXYGEN = doxygen

WARNINGS = -Wall -Wextra -pedantic

# Remove these flags when Core.c is cleaned up...
RDP_FLAGS = -Wno-unused-parameter -Wno-unused-variable \
  -Wno-sign-compare -Wno-unused-but-set-variable -Wno-unused-function \
  -Wno-maybe-uninitialized

COMMON_CFLAGS = $(WARNINGS) $(RDP_FLAGS) -std=c99 -march=native -I.
COMMON_CXXFLAGS = $(WARNINGS) $(RDP_FLAGS) -std=c++0x -march=native -I.
OPTIMIZATION_FLAGS = -flto -fwhole-program -fuse-linker-plugin \
	-fdata-sections -ffunction-sections -funsafe-loop-optimizations

ARFLAGS = rcs
RELEASE_CFLAGS = -DNDEBUG -O3 $(OPTIMIZATION_FLAGS)
DEBUG_CFLAGS = -DDEBUG -O0 -ggdb -g3

$(OBJECT_DIR)/%.o: %.c %.h Common.h
	@$(MKDIR) $(OBJECT_DIR)
	@$(ECHO) "$(BLUE)Compiling$(YELLOW): $(PURPLE)$(PREFIXDIR)$<$(TEXTRESET)"
	@$(CC) $(CFLAGS) $< -c -o $@

# ============================================================================
#  Targets.
# ============================================================================
all: CFLAGS = $(COMMON_CFLAGS) $(RELEASE_CFLAGS) $(RDP_FLAGS)
all: $(TARGET)

debug: CFLAGS = $(COMMON_CFLAGS) $(DEBUG_CFLAGS) $(RDP_FLAGS)
debug: $(TARGET)

all-cpp: CFLAGS = $(COMMON_CXXFLAGS) $(RELEASE_CFLAGS) $(RDP_FLAGS)
all-cpp: $(TARGET)
all-cpp: CC = $(CXX)

debug-cpp: CFLAGS = $(COMMON_CXXFLAGS) $(DEBUG_CFLAGS) $(RDP_FLAGS)
debug-cpp: $(TARGET)
debug-cpp: CC = $(CXX)

$(TARGET): $(OBJECTS)
	@$(ECHO) "$(BLUE)Linking$(YELLOW): $(PURPLE)$(PREFIXDIR)$@$(TEXTRESET)"
	@$(AR) $(ARFLAGS) $@ $^

.PHONY: clean documentation inspect inspect-cpp

clean:
	@$(ECHO) "$(BLUE)Cleaning librdp...$(TEXTRESET)"
	@$(RM) $(OBJECTS) $(TARGET)

inspect: $(TARGET)
	objdump -d $< | less

inspect-cpp: $(TARGET)
	objdump -d $< | c++filt | less

