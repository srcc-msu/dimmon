include $(TOPDIR)/dmm.common.mk

SHLIB ?= $(MODULE)
SHLIB_NAME ?= lib$(SHLIB).so
SHLIB_COPY ?= $(LIBDIR)/$(notdir $(SHLIB_NAME))

CP ?= cp
SHELL = /bin/sh

OBJS ?= $(call sufsubst, $(SRCS), $(SRC_SUFFIXES), .o)

INT_HEADERS ?= $(wildcard $(MODULE).h)
INT_FILES = $(INT_HEADERS:.h=.i)
INTERFACE ?= $(INT_FILES)
INTERFACE_COPY ?= $(foreach f, $(INTERFACE), $(call stage_file_lib, $(f)))

LIB_SUPPL_COPY = $(foreach f, $(LIB_SUPPL), $(call stage_file_lib, $(f)))

MODULE_CFLAGS ?= -fvisibility=hidden
MODULE_CXXFLAGS ?= -fvisibility=hidden
MODULE_CPPFLAGS ?= -I $(TOPDIR) -I $(INCDIR)

SHARED_CFLAGS ?= -fpic
SHARED_CXXFLAGS ?= -fpic
SHARED_LDFLAGS ?= -shared -fpic

all:		module

# Make 'all' a default goal to allow rules in module Makefile
.DEFAULT_GOAL = all

module:		modlib modint stage-mod

modlib:		$(SHLIB_NAME)

$(SHLIB_NAME):	$(OBJS)
	$(CXX) $(OBJS) -o $(SHLIB_NAME) $(SHARED_LDFLAGS) $(LDFLAGS) $(LIBS)

%.o:		%.c
	$(CC) -MMD -MT '$@ $*.d' -c $(SHARED_CFLAGS) $(CFLAGS) $(MODULE_CFLAGS) $(CPPFLAGS) $(MODULE_CPPFLAGS) $< -o $@

%.o:		%.cc
	$(CXX) -MMD -MT '$@ $*.d' -c $(SHARED_CXXFLAGS) $(CXXFLAGS) $(MODULE_CXXFLAGS) $(CPPFLAGS) $(MODULE_CPPFLAGS) $< -o $@

DEPS ?= $(call sufsubst, $(SRCS), $(SRC_SUFFIXES), .d)

-include $(DEPS)

modint:		$(INTERFACE)

define make_interface_rule =
$(2):	$(1)
# Make dependecies for interface file
	$(CPP) -MM -MT '$(2) $(2).d' -MF $(2).d $(CPPFLAGS) $(MODULE_CPPFLAGS) $(1)
# Remove all #include as all needed definitions are
# there already (loaded by Lua dmm module)
	sed -e '/^\s*#\s*include.*$$$$/d' -- $(1) | $(CPP) $(CPPFLAGS) $(MODULE_CPPFLAGS) -P - -o $(2)
endef

$(foreach f, $(INT_HEADERS), $(eval $(call make_interface_rule, $(f), $(f:.h=.i))))

#$(INTERFACE):	$(INT_HEADERS)
# Make dependecies for interface file
#	$(CPP) -MM -MT '$@ $@.d' -MF '$@.d' $(CPPFLAGS) $(MODULE_CPPFLAGS) $<
# Remove all #include as all needed definitions are
# there already (loaded by Lua dmm module)
#	sed -e '/^\s*#\s*include.*$$/d' -- $< | $(CPP) $(CPPFLAGS) $(MODULE_CPPFLAGS) -P - -o $@

-include $(INTERFACE:=.d)

stage-mod:	stage-modlib stage-modint stage-mod-suppl

stage-modlib:	 modlib $(SHLIB_COPY)

$(SHLIB_COPY):	$(SHLIB_NAME)
	$(CP) -f $(SHLIB_NAME) $(SHLIB_COPY) 

stage-modint:	$(INTERFACE_COPY)

$(foreach f, $(INTERFACE), $(eval $(call stage_file_rule, $(f), $(call stage_file_lib, $(f)))))

stage-mod-suppl:	$(LIB_SUPPL_COPY)

$(foreach f, $(LIB_SUPPL), $(eval $(call stage_file_rule, $(f), $(call stage_file_lib, $(f))))) 

.PHONY:		distclean clean
distclean:	clean
clean:		clean-module clean-stage

clean-module:	clean-modlib clean-modint  clean-mod-suppl

clean-modlib:
	rm -f $(OBJS) $(DEPS) $(SHLIB_NAME)

clean-modint:
ifneq ($(strip $(INTERFACE)),)
	rm -f $(INTERFACE) $(INTERFACE:=.d) $(INT_HEADERS:.h=.i.d) $(INT_FILES)
endif

clean-mod-suppl:
ifneq ($(strip $(LIB_SUPPL_COPY)),)
	rm -f $(LIB_SUPPL_COPY)
endif

clean-stage:	clean-stage-modlib clean-stage-modint clean-stage-suppl

clean-stage-modlib:
	rm -f $(SHLIB_COPY)

clean-stage-modint:
ifneq ($(strip $(INTERFACE_COPY)),)
	rm -f $(INTERFACE_COPY)
endif

clean-stage-suppl:

.SUFFIXES:
.SUFFIXES:	$(SRC_SUFFIXES) .o
