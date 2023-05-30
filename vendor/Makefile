TOPDIR ?= ${CURDIR}/..
export TOPDIR

VENDORDIR ?= $(TOPDIR)/vendor

include $(VENDORDIR)/dmm.vendor.mk

VENDOR_PROJECTS = luajit luaposix cpputest sysfsutils libedac
VENDOR_VARS_FILE = $(VENDORDIR)/vars.sh

VENDOR_BUILD_COOKIE = $(VENDORDIR)/.vendor-build-done

all:	vendor

.PHONY:	vendor
vendor:	vendor-build vendor-vars

vendor-build:	$(VENDOR_BUILD_COOKIE)

.PHONY:	$(VENDOR_PROJECTS)
$(VENDOR_BUILD_COOKIE):	$(if $(wildcard $(VENDOR_BUILD_COOKIE)),, $(VENDOR_PROJECTS))
	touch $(VENDOR_BUILD_COOKIE)

vendor-vars:	$(VENDOR_VARS_FILE)

$(VENDOR_VARS_FILE):	clean-vendor-vars $(addprefix vars-, $(VENDOR_PROJECTS))

clean-vendor-vars:
	@-rm -f $(VENDOR_VARS_FILE)

.PHONY:	$(addprefix vars-, $(VENDOR_PROJECTS))

distclean:	clean

clean:	$(addprefix clean-, $(VENDOR_PROJECTS)) clean-vendor-vars
	@-rm -f $(VENDOR_BUILD_COOKIE)

vendor-clean:	clean

# Here start rules for individual vendor projects
luajit:
	cd $(LUAJIT_HOME) && make
	cd $(LUAJIT_HOME) && [ ! -e src/luajit-2.1 -o -L src/luajit-2.1 ] &&  rm -f src/luajit-2.1 && ln -s . src/luajit-2.1

vars-luajit:
	@echo LUAJIT_HOME=$(LUAJIT_HOME) >> $(VENDOR_VARS_FILE)
	@echo export LUAJIT_HOME >> $(VENDOR_VARS_FILE)
	@echo LUAJIT=$(LUAJIT) >> $(VENDOR_VARS_FILE)
	@echo export LUAJIT >> $(VENDOR_VARS_FILE)
	@echo LUA_PATH= >> $(VENDOR_VARS_FILE)
	@echo export LUA_PATH >> $(VENDOR_VARS_FILE)
	@echo LUA_CPATH= >> $(VENDOR_VARS_FILE)
	@echo export LUA_CPATH >> $(VENDOR_VARS_FILE)

clean-luajit:
	cd $(LUAJIT_HOME) && make clean && rm -f src/luajit-2.1

LUAPOSIX_BUILD_COOKIE = $(VENDORDIR)/.luaposix-build-done
luaposix: $(LUAPOSIX_BUILD_COOKIE)

$(LUAPOSIX_BUILD_COOKIE):
	cd $(LUAPOSIX_HOME) && LUA_PATH="" LUA_CPATH="" $(LUAJIT) build-aux/luke
	touch $(LUAPOSIX_BUILD_COOKIE)

vars-luaposix:
	@echo LUA_PATH=\"$$\{LUA_PATH:+$$\{LUA_PATH\}\;\}'$(LUAPOSIX_LUA_PATH)'\" >> $(VENDOR_VARS_FILE)
	@echo export LUA_PATH >> $(VENDOR_VARS_FILE)
	@echo LUA_CPATH=\"$$\{LUA_CPATH:+$$\{LUA_CPATH\}\;\}'$(LUAPOSIX_LUA_CPATH)'\" >> $(VENDOR_VARS_FILE)
	@echo export LUA_CPATH >> $(VENDOR_VARS_FILE)

clean-luaposix:
	rm -f $(LUAPOSIX_HOME)/build-aux/config.ld $(LUAPOSIX_HOME)/lib/posix/version.lua
	rm -Rf $(LUAPOSIX_HOME)/linux
	rm -f $(LUAPOSIX_BUILD_COOKIE)

SYSFSUTILS_BUILD_COOKIE = $(VENDORDIR)/.sysfsutils-build-done
sysfsutils:	$(SYSFSUTILS_BUILD_COOKIE)

$(SYSFSUTILS_BUILD_COOKIE):
	cd $(SYSFSUTILS_HOME) && [ -f $(SYSFSUTILS_HOME)/Makefile ] || ./configure
	cd $(SYSFSUTILS_HOME) && make
	cd $(SYSFSUTILS_HOME) && [ ! -e include/sysfs -o -L include/sysfs ] &&  rm -f include/sysfs && ln -s . include/sysfs
	touch $(SYSFSUTILS_BUILD_COOKIE)

vars-sysfsutils:
	@echo LD_LIBRARY_PATH=\"'$(SYSFSUTILS_LIB)':$$\{LD_LIBRARY_PATH\}\" >> $(VENDOR_VARS_FILE)

clean-sysfsutils:
	cd $(SYSFSUTILS_HOME) && [ -f $(SYSFSUTILS_HOME)/Makefile ] && make distclean || /bin/true
	cd $(SYSFSUTILS_HOME) && rm -f include/sysfs
	rm -f $(SYSFSUTILS_BUILD_COOKIE)

libedac:
	cd $(LIBEDAC_HOME) && [ -f $(LIBEDAC_HOME)/Makefile ] || CPPFLAGS="$(VENDOR_CPPFLAGS)" LIBRARY_PATH="$(SYSFSUTILS_LIB)" ./configure
	cd $(LIBEDAC_HOME) && LIBRARY_PATH="$(SYSFSUTILS_LIB)" make

vars-libedac:
	@echo LD_LIBRARY_PATH=\"'$(LIBEDAC_LIB)':$$\{LD_LIBRARY_PATH\}\" >> $(VENDOR_VARS_FILE)

clean-libedac:
	cd $(LIBEDAC_HOME) && [ -f $(LIBEDAC_HOME)/Makefile ] && make distclean || /bin/true

cpputest:
	cd $(CPPUTEST_HOME)/cpputest_build && [ -x $(CPPUTEST_HOME)/configure ] || autoreconf .. -i
	cd $(CPPUTEST_HOME)/cpputest_build && [ -f $(CPPUTEST_HOME)/cpputest_build/Makefile ] || ../configure
	cd $(CPPUTEST_HOME)/cpputest_build && make

vars-cpputest:

clean-cpputest:
	cd $(CPPUTEST_HOME)/cpputest_build && [ -f $(CPPUTEST_HOME)/cpputest_build/Makefile ] && make distclean || /bin/true

