TOPDIR ?= $(CURDIR)/../../..
export TOPDIR

MODULE = net_ip
SRCS = main.c recv.c send.c common.c

INT_HEADERS = common.h recv.h send.h

INTERFACE = net_ip.i

$(INTERFACE):	$(INT_HEADERS:.h=.i)
	cat $+ > $@

LIB_SUPPL = net_ip.lua

include $(TOPDIR)/dmm.module.mk
