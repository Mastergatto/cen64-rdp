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

ifeq ($(OS),windows)
OBJECTS = $(addprefix $(OBJECT_DIR)\, $(notdir $(SOURCES:.c=.o)))
else
OBJECTS = $(addprefix $(OBJECT_DIR)/, $(notdir $(SOURCES:.c=.o)))
endif

# =============================================================================
#  Build variables and settings.
# =============================================================================
OBJECT_DIR=Objects

# ============================================================================
#  Build rules and flags.
# ============================================================================
BLUE=$(shell tput setaf 4)
PURPLE=$(shell tput setaf 5)
TEXTRESET=$(shell tput sgr0)
YELLOW=$(shell tput setaf 3)

ECHO=/usr/bin/printf "%s\n"
MKDIR = /bin/mkdir -p

ifeq ($(OS),windows)
CC=gcc.exe
CXX=g++.exe

BLUE=
PURPLE=
TEXTRESET=
YELLOW=

ECHO=echo
MAYBE=if not exist
MKDIR=mkdir
RM=del /q /s 1>NUL 2>NUL
endif

AR = ar
DOXYGEN = doxygen

# Remove these flags when Core.c is cleaned up...
RDP_FLAGS = -DLITTLE_ENDIAN -Wno-unused-parameter -Wno-unused-variable \
  -Wno-sign-compare -Wno-unused-but-set-variable -Wno-unused-function \
  -Wno-maybe-uninitialized

WARNINGS = -Wall -Wextra -pedantic

COMMON_CFLAGS = $(WARNINGS) $(RDP_FLAGS) -std=c99 -march=native -I.
COMMON_CXXFLAGS = $(WARNINGS) $(RDP_FLAGS) -std=c++0x -march=native -I.
OPTIMIZATION_FLAGS = -flto -fuse-linker-plugin -fdata-sections \
	-ffunction-sections -funsafe-loop-optimizations

ARFLAGS = rcs
RELEASE_CFLAGS = -DNDEBUG -O3 $(OPTIMIZATION_FLAGS)
DEBUG_CFLAGS = -DDEBUG -O0 -ggdb -g3

# ============================================================================
#  Build targets.
# ============================================================================
.PHONY: all all-cpp clean debug debug-cpp

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

clean:
ifeq ($(OS),windows)
	@$(ECHO) $(BLUE)Cleaning librdp...$(TEXTRESET)
else
	@$(ECHO) "$(BLUE)Cleaning librdp...$(TEXTRESET)"
endif
	@$(RM) $(OBJECTS) $(TARGET)

# ============================================================================
#  Build rules.
# ============================================================================
ifeq ($(OS),windows)
$(TARGET): $(OBJECTS)
	@$(ECHO) $(BLUE)Linking$(YELLOW): $(PURPLE)$(PREFIXDIR)$@$(TEXTRESET)
	@$(AR) $(ARFLAGS) $@ $^

$(OBJECT_DIR)\\%.o: %.c %.h Common.h
	@$(MAYBE) $(OBJECT_DIR) $(MKDIR) $(OBJECT_DIR)
	@$(ECHO) $(BLUE)Compiling$(YELLOW): $(PURPLE)$(PREFIXDIR)$<$(TEXTRESET)
	@$(CC) $(CFLAGS) $< -c -o $@
else
$(TARGET): $(OBJECTS)
	@$(ECHO) "$(BLUE)Linking$(YELLOW): $(PURPLE)$(PREFIXDIR)$@$(TEXTRESET)"
	@$(AR) $(ARFLAGS) $@ $^

$(OBJECT_DIR)/%.o: %.c %.h Common.h
	@$(MKDIR) $(OBJECT_DIR)
	@$(ECHO) "$(BLUE)Compiling$(YELLOW): $(PURPLE)$(PREFIXDIR)$<$(TEXTRESET)"
	@$(CC) $(CFLAGS) $< -c -o $@
endif

