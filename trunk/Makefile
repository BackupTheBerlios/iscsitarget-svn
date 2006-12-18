#
# Makefile for the Linux kernel device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile.

#export KSRC := /usr/src/linux

SUBDIRS := $(shell pwd)

ifeq ($(KSRC),)
	KSRC ?= /lib/modules/$(shell uname -r)/build
endif

all: progs mods

include compat.mk

mods: check_kernel_compat
	$(MAKE) -C $(KSRC) SUBDIRS=$(shell pwd)/kernel modules

progs:
	$(MAKE) -C usr

install: kernel/iscsi_trgt.ko usr/ietd usr/ietadm
	@install -vD usr/ietd $(DISTDIR)/usr/sbin/ietd
	@install -vD usr/ietadm $(DISTDIR)/usr/sbin/ietadm
	if [ -f /etc/debian_version ]; then \
		install -vD -m 755 etc/initd/initd.debian $(DISTDIR)/etc/init.d/iscsi-target; \
	elif [ -f /etc/redhat-release ]; then \
		install -vD -m 755 etc/initd/initd.redhat $(DISTDIR)/etc/init.d/iscsi-target; \
	elif [ -f /etc/gentoo-release ]; then \
		install -vD -m 755 etc/initd/initd.gentoo $(DISTDIR)/etc/init.d/iscsi-target; \
	elif [ -f /etc/slackware-version ]; then \
		install -vD -m 755 etc/initd/initd $(DISTDIR)/etc/rc.d/iscsi-target; \
	else \
		install -vD -m 755 etc/initd/initd $(DISTDIR)/etc/init.d/iscsi-target; \
	fi
	@eval `sed -n 's/#define UTS_RELEASE /KERNELRELEASE=/p' $(KSRC)/include/linux/version.h $(KSRC)/include/linux/utsrelease.h 2>/dev/null`; \
	install -vD kernel/iscsi_trgt.ko \
	$(DISTDIR)$(INSTALL_MOD_PATH)/lib/modules/$$KERNELRELEASE/kernel/iscsi/iscsi_trgt.ko
	-depmod -aq

clean:
	$(MAKE) -C usr clean
	$(MAKE) -C $(KSRC) SUBDIRS=$(shell pwd)/kernel clean

