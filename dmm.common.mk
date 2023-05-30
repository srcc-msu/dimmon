MODBASEDIR = modules
LIBBASEDIR = src/lib

BINDIR ?= $(TOPDIR)/bin
LIBDIR ?= $(TOPDIR)/lib
INCDIR ?= $(TOPDIR)/include
VENDORDIR = $(TOPDIR)/vendor
TESTDIR = $(TOPDIR)/tests

include $(VENDORDIR)/dmm.vendor.mk

SRC_SUFFIXES ?= .c .cc

# _GNU_SOURCE is defined as stdlib in C++ in GCC is unconditionally
# compiled with _GNU_SOURCE, so let's two standard libraries be the same
DEFAULT_CFLAGS ?= -O -D_GNU_SOURCE
DEBUG_CFLAGS ?= -Og -g -Wall -Wextra -Werror -DDEBUG -D_GNU_SOURCE
DEFAULT_CXXFLAGS ?= -std=c++11 -O
DEBUG_CXXFLAGS ?= -std=c++11 -Og -g -Wall -Wextra -Werror -DDEBUG
DEBUG_CPPFLAGS ?= -DDEBUG

ifdef DEBUG
CFLAGS ?= $(DEBUG_CFLAGS) $(COMMON_CFLAGS)
CXXFLAGS ?= $(DEBUG_CXXFLAGS) $(COMMON_CXXFLAGS)
CPPFLAGS ?= $(DEBUG_CPPFLAGS) $(VENDOR_CPPFLAGS) $(COMMON_CPPFLAGS)
else
CFLAGS ?= $(DEFAULT_CFLAGS) $(COMMON_CFLAGS)
CXXFLAGS ?= $(DEFAULT_CXXFLAGS) $(COMMON_CXXFLAGS)
CPPFLAGS ?= $(DEFAULT_CPPFLAGS) $(VENDOR_CPPFLAGS) $(COMMON_CPPFLAGS)
endif

LDFLAGS ?= -L $(LIBDIR) $(VENDOR_LDFLAGS)

# Auxiliary macros
stage_file_dir = $(2)/$(notdir $(1))

stage_file_lib = $(call stage_file_dir, $(1), $(LIBDIR))
stage_file_bin = $(call stage_file_dir, $(1), $(BINDIR))
stage_file_inc = $(call stage_file_dir, $(1), $(INCDIR))

define stage_file_rule =
$(2):	$(1)
	cp $(1) $(2) 
endef

# sufsubst macro is a generalization of $(VARL:.suf1=.suf2) substitution
# Usage: $(call subsubst, $(FILES), $(SRC_SUFFIXES), $(DST_SUFFIX))
# It scans through $(FILES) and replaces every occurence of
# any suffix from $(SRC_SUFFIXES) to $(DST_SUFFIX) (a single suffix)
# For example, $(call sufsubst, a.c b.cc, .c .cc, .o) returns 'a.o b.o'
#
# NO SPACE AFTER '='
sufsubst =$(strip                                     \
            $(if                                      \
              $(strip $(2)),                          \
              $(call                                  \
                sufsubst,                             \
                $(1:$(firstword $(2))=$(strip $(3))), \
                $(wordlist 2, $(words $(2)), $(2)),   \
                $(3)                                  \
              ),                                      \
              $(1)                                    \
            )                                         \
          )
