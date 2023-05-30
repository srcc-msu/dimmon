include $(TOPDIR)/dmm.common.mk

SHLIB ?= $(LIB)
SHLIB_NAME ?= lib$(SHLIB).so
SHLIB_COPY ?= $(LIBDIR)/$(notdir $(SHLIB_NAME))

CP ?= cp
SHELL = /bin/sh

OBJS ?= $(call sufsubst, $(SRCS), $(SRC_SUFFIXES), .o)

INT_HEADER ?= $(wildcard $(LIB).h)
INTERFACE = $(INT_HEADER)
INTERFACE_COPY ?= $(if $(INTERFACE), $(call stage_file_inc, $(INTERFACE)))

LIB_SUPPL_COPY = $(foreach f, $(LIB_SUPPL), $(call stage_file_lib, $(f)))

LIB_CPPFLAGS ?= -I $(TOPDIR)

SHARED_CFLAGS ?= -fpic
SHARED_CXXFLAGS ?= -fpic
SHARED_LDFLAGS ?= -shared -fpic

all:		library

# Make 'all' a default goal to allow rules in library Makefile
.DEFAULT_GOAL = all

library:	liblib stage-lib

liblib:		$(SHLIB_NAME)

$(SHLIB_NAME):	$(OBJS)
	$(CXX) $(OBJS) -o $(SHLIB_NAME) $(SHARED_LDFLAGS) $(LDFLAGS) $(LIBS)

%.o:		%.c
	$(CC) -MMD -MT '$@ $*.d' -c $(SHARED_CFLAGS) $(CFLAGS) $(LIB_CFLAGS) $(CPPFLAGS) $(LIB_CPPFLAGS) $< -o $@

%.o:		%.cc
	$(CXX) -MMD -MT '$@ $*.d' -c $(SHARED_CXXFLAGS) $(CXXFLAGS) $(LIB_CXXFLAGS) $(CPPFLAGS) $(LIB_CPPFLAGS) $< -o $@

DEPS ?= $(call sufsubst, $(SRCS), $(SRC_SUFFIXES), .d)

-include $(DEPS)

stage-lib:	stage-liblib stage-libint stage-lib-suppl

stage-liblib:	 liblib $(SHLIB_COPY)

$(SHLIB_COPY):	$(SHLIB_NAME)
	$(CP) -f $(SHLIB_NAME) $(SHLIB_COPY) 

stage-libint:	$(INTERFACE_COPY)

$(INTERFACE_COPY):	$(INTERFACE)
	$(CP) -f $(INTERFACE) $(INTERFACE_COPY)

stage-lib-suppl:	$(LIB_SUPPL_COPY)

$(foreach f, $(LIB_SUPPL), $(eval $(call stage_file_rule, $(f), $(call stage_file_lib, $(f))))) 

.PHONY:		distclean clean
distclean:	clean
clean:		clean-library clean-stage

clean-library:	clean-liblib

clean-liblib:
	rm -f $(OBJS) $(DEPS) $(SHLIB_NAME)

clean-stage:	clean-stage-liblib clean-stage-libint clean-stage-suppl

clean-stage-liblib:
	rm -f $(SHLIB_COPY)

clean-stage-libint:
ifneq ($(strip $(INTERFACE_COPY)),)
	rm -f $(INTERFACE_COPY)
endif

clean-stage-suppl:
ifneq ($(strip $(LIB_SUPPL_COPY)),)
	rm -f $(LIB_SUPPL_COPY)
endif

.SUFFIXES:
.SUFFIXES:	$(SRC_SUFFIXES) .o
