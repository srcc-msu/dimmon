SRC_dmm_module = dmm_module.test.cc
FLAGS_dmm_module = -ldl
DEPS_dmm_module = dmm_module.MODULES
CLEAN_DEPS_dmm_module = clean-dmm_module.MODULES

dmm_module.MODULES:	dmm_module_empty_module \
			dmm_module_module_wrong_abi \
			dmm_module_module_one_type \
			dmm_module_module_two_types

dmm_module_empty_module:
	$(MAKE) -C $(CURDIR)/dmm_module.files -f Makefile.dmm_module_empty_module modlib modint

dmm_module_module_wrong_abi:
	$(MAKE) -C $(CURDIR)/dmm_module.files -f Makefile.dmm_module_module_wrong_abi modlib modint

dmm_module_module_one_type:
	$(MAKE) -C $(CURDIR)/dmm_module.files -f Makefile.dmm_module_module_one_type modlib modint

dmm_module_module_two_types:
	$(MAKE) -C $(CURDIR)/dmm_module.files -f Makefile.dmm_module_module_two_types modlib modint

clean-dmm_module.MODULES:	clean-dmm_module_empty_module \
				clean-dmm_module_module_wrong_abi \
				clean-dmm_module_module_one_type \
				clean-dmm_module_module_two_types

clean-dmm_module_empty_module:
	$(MAKE) -C $(CURDIR)/dmm_module.files -f Makefile.dmm_module_empty_module clean-module

clean-dmm_module_module_wrong_abi:
	$(MAKE) -C $(CURDIR)/dmm_module.files -f Makefile.dmm_module_module_wrong_abi clean-module

clean-dmm_module_module_one_type:
	$(MAKE) -C $(CURDIR)/dmm_module.files -f Makefile.dmm_module_module_one_type clean-module

clean-dmm_module_module_two_types:
	$(MAKE) -C $(CURDIR)/dmm_module.files -f Makefile.dmm_module_module_two_types clean-module

