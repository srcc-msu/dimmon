SHELL = /bin/sh

TOPDIR := $(CURDIR)
export TOPDIR

include dmm.common.mk

CP ?= cp

PROG = dimmon
SRCS = dmm_main.c dmm_base.c dmm_log.c dmm_memman.c dmm_module.c dmm_event.c \
       dmm_timer.c dmm_sockevent.c dmm_wave.c

INT_HEADERS = dmm_message.h dmm_sockevent_types.h

MODULES =
MODULES += net/ip
MODULES += dbgprinter
MODULES += derivative
MODULES += avgprint
MODULES += aggregateall
MODULES += starter
MODULES += wavebuf
MODULES += prepend
MODULES += demux
MODULES += blackhole
MODULES += luacontrol
MODULES += sensors/cpuload
MODULES += sensors/memory
MODULES += sensors/dummy
MODULES += sensors/ifdata
MODULES += sensors/edac

LIBRARIES = dmm-lua.ll

MODDIRS = $(addprefix $(MODBASEDIR)/, $(MODULES))

LIBDIRS = $(addprefix $(LIBBASEDIR)/, $(LIBRARIES))

OBJS ?= $(call sufsubst, $(SRCS), $(SRC_SUFFIXES), .o)
INT_FILES = $(INT_HEADERS:.h=.i)
INT_COPY = $(foreach f, $(INT_FILES), $(call stage_file_lib, $(f)))

PROG_COPY ?= $(call stage_file_bin, $(PROG))

all: 		vendor main libraries modules build-tests

tests:		run-tests

vendor:		$(VENDORDIR)

.PHONY:		$(VENDORDIR)
$(VENDORDIR):
	$(MAKE) -C "$@" $(MAKECMDGOALS)

main:		prog interface stage-prog stage-interface

libraries:	$(LIBRARIES)

$(LIBRARIES):	$(LIBDIRS)

.PHONY:		$(LIBDIRS)
$(LIBDIRS):
	$(MAKE) -C "$@" $(MAKECMDGOALS)

modules:	$(MODULES)

$(MODULES):	$(MODDIRS)

.PHONY:		$(MODDIRS)
$(MODDIRS):
	$(MAKE) -C "$@" $(MAKECMDGOALS)

build-tests:
	$(MAKE) -C $(TESTDIR) "$@"

run-tests:
	$(MAKE) -C $(TESTDIR) "$@"

prog:		$(PROG)

stage-prog:	prog $(PROG_COPY)

$(PROG_COPY):	$(PROG)
	$(CP) -f $(PROG) $(PROG_COPY)

# Linking with C++ compiler to allow C++ code inside
$(PROG):	$(OBJS)
	$(CXX) $(LDFLAGS) -Wl,--export-dynamic -o $(PROG) $(OBJS) -ldl $(LIBS)

%.o:		%.c
	$(CC) -MMD -MT '$@ $*.d' -c $(SHARED_CFLAGS) $(CFLAGS) $(CPPFLAGS) $< -o $@

%.o:		%.cc
	$(CXX) -MMD -MT '$@ $*.d' -c $(SHARED_CXXFLAGS) $(CXXFLAGS) $(CPPFLAGS) $< -o $@

DEPS ?= $(call sufsubst, $(SRCS), $(SRC_SUFFIXES), .d)
-include $(DEPS)

interface:	$(INT_FILES)

$(INT_FILES):	%.i:	%.h
	$(CPP) -MMD -MT '$@ $@.d' -MF '$@.d' $(CPPFLAGS) $(MODULE_CPPFLAGS) -P $< -o $@

-include $(INT_FILES:=.d)

stage-interface:	$(INT_COPY)

$(foreach int_file, $(INT_FILES), $(eval $(call stage_file_rule, $(int_file), $(call stage_file_lib, $(int_file)))))

.PHONY:		clean
clean:		clean-interface clean-stage $(MODDIRS) $(LIBDIRS) clean-tests
	rm -f $(OBJS) $(DEPS) $(PROG)

clean-interface:
	rm -f $(INT_FILES) $(INT_FILES:=.d)

clean-stage:	clean-stage-prog clean-stage-interface

clean-stage-prog:
	rm -f $(PROG_COPY)

clean-stage-interface:
	rm -f $(INT_COPY)

.PHONY:		distclean
distclean:	vendor-clean clean

vendor-clean:	vendor

clean-tests:
	$(MAKE) -C $(TESTDIR) "$@"

.SUFFIXES:
.SUFFIXES:	$(SRC_SUFFIXES) .o
