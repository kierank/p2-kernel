# Makefile for P2PF infomation module.
## $Id: Makefile 1290 2009-01-27 02:30:05Z isao $

ifdef CONFIG_P2PF_VERINFO
VERINFO_FILE = $(shell echo $(TOPDIR)/$(CONFIG_P2PF_VERINFO_FILE))
ifeq ($(wildcard $(VERINFO_FILE)),$(VERINFO_FILE))
EXTRA_CFLAGS += -DVERINFO=\"$(shell cat $(VERINFO_FILE) 2>/dev/null)\"
endif
endif

obj-$(CONFIG_PROC_P2PF_TARGET)	:=	p2pfinfo.o
