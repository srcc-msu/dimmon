VENDORDIR ?= $(TOPDIR)/vendor

LUAJIT_HOME = $(VENDORDIR)/luajit
LUAJIT_INCLUDE = $(LUAJIT_HOME)/src
LUAJIT_LIB = $(LUAJIT_HOME)/src
LUAJIT = $(LUAJIT_HOME)/src/luajit
VENDOR_CPPFLAGS += -I$(LUAJIT_INCLUDE)
VENDOR_LDFLAGS  += -L$(LUAJIT_LIB)

LUAPOSIX_HOME = $(VENDORDIR)/luaposix
LUAPOSIX_LUA_PATH = $(LUAPOSIX_HOME)/lib/?.lua;$(LUAPOSIX_HOME)/lib/?/init.lua
LUAPOSIX_LUA_CPATH = $(LUAPOSIX_HOME)/linux/?.so

SYSFSUTILS_HOME = $(VENDORDIR)/sysfsutils
SYSFSUTILS_INCLUDE = $(SYSFSUTILS_HOME)/include
SYSFSUTILS_LIB = $(SYSFSUTILS_HOME)/lib/.libs
VENDOR_CPPFLAGS += -I$(SYSFSUTILS_INCLUDE)
VENDOR_LDFLAGS  += -L$(SYSFSUTILS_LIB)

LIBEDAC_HOME = $(VENDORDIR)/libedac
LIBEDAC_INCLUDE = $(LIBEDAC_HOME)/src/lib
LIBEDAC_LIB = $(LIBEDAC_HOME)/src/lib/.libs
VENDOR_CPPFLAGS += -I$(LIBEDAC_INCLUDE)
VENDOR_LDFLAGS  += -L$(LIBEDAC_LIB)

CPPUTEST_HOME = $(VENDORDIR)/cpputest
CPPUTEST_INCLUDE = $(CPPUTEST_HOME)/include
CPPUTEST_LIB = $(CPPUTEST_HOME)/cpputest_build/lib
CPPUTEST_CPPFLAGS += -I$(CPPUTEST_INCLUDE)
CPPUTEST_CXXFLAGS += -include $(CPPUTEST_INCLUDE)/CppUTest/MemoryLeakDetectorNewMacros.h
CPPUTEST_CFLAGS   += -include $(CPPUTEST_INCLUDE)/CppUTest/MemoryLeakDetectorMallocMacros.h
CPPUTEST_LDFLAGS  += -L$(CPPUTEST_LIB) -lCppUTest -lCppUTestExt
