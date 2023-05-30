TOPDIR ?= ${CURDIR}/..
export TOPDIR

all:

# List of tests
TESTS = dmm_module

# Rules for individual tests for here either
# as includes or in this file
include dmm_module.files/Rules.mk

include $(TOPDIR)/dmm.common.mk

all:	tests

tests:	build-tests run-tests

build:	build-tests

run:	run-tests

distclean:	clean
clean:	clean-tests

build-tests:	$(addprefix build-,$(TESTS))

run-tests:	$(addprefix run-,$(TESTS))

clean-tests:	$(addprefix clean-,$(TESTS))

define make_test_rules =
EXE_$(1) ?= $(1).test.out

.PHONY:		run-$(1) clean-$(1)

build-$(1):	$$(EXE_$(1)) $$(DEPS_$(1))

$$(EXE_$(1)):	$$(SRC_$(1))
	$(CXX) $(CPPFLAGS) $(CPPUTEST_CPPFLAGS) $(CXXFLAGS) $(CPPUTEST_CXXFLAGS) \
	       $(CPPUTEST_CFLAGS) -o $$@ $$^ $(LDFLAGS) $(CPPUTEST_LDFLAGS) $$(FLAGS_$(1))

run-$(1):	$$(EXE_$(1))
	-./$$(EXE_$(1)) $(TEST_CL_FLAGS)

clean-$(1):	clean-$$(EXE_$(1)) $$(CLEAN_DEPS_$(1))

clean-$$(EXE_$(1)):
	-rm -f $$(EXE_$(1))
endef

$(foreach t, $(TESTS), $(eval $(call make_test_rules,$(t))))

