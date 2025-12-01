##############################################################################
# Configuration for Makefile
#

.DEFAULT_GOAL := all

PROJECT := {{patch_name}}
PROJECT_TYPE := {{unit_type}}
PROJECT_DEV_ID := 0x0U
PROJECT_UNIT_ID := 0x0U
MAX_UNIT_SIZE := {{max_unit_size}}
SDRAM_ALLOC_THRESHOLD := {{sdram_alloc_threshold}}

ifeq ($(strip $(SDRAM_SIZE)),)
SDRAM_SIZE = 0
endif
ifeq ($(shell [ $(SDRAM_SIZE) -gt {{max_sdram_size}} ] && echo yes || echo no),yes)
$(error Required SDRAM size($(SDRAM_SIZE)bytes) exceeds {{max_sdram_size}}bytes)
endif

##############################################################################
# Sources
#

# C sources 
UCSRC = header.c {{heavy_files_c}}

UCSRC += _unit_base.c

# C++ sources 
UCXXSRC = logue_heavy.cpp {{heavy_files_cpp}}

# List ASM source files here
UASMSRC = 

UASMXSRC = 

##############################################################################
# Include Paths
#

UINCDIR  = $(PLATFORMDIR)/../common

##############################################################################
# Library Paths
#

ULIBDIR = 

##############################################################################
# Libraries
#

ULIBS  = -lm
ULIBS  += -lstdc++
ULIBS  += -Wl,--gc-sections

##############################################################################
# Macros
#

UDEFS = -DNDEBUG -fvisibility=hidden

UDEFS += -DPROJECT_DEV_ID=$(PROJECT_DEV_ID) -DPROJECT_UNIT_ID=$(PROJECT_UNIT_ID)

# Assume Unix-like to suppress warning messages
UDEFS += -U_WIN32 -U_WIN64 -U_MSC_VER -D__unix

# Try disabling this option when the results are inaccurate.
# UDEFS += -DLOGUE_FAST_MATH

# Enable this to reduce the processing load
# UDEFS += -DRENDER_HALF

