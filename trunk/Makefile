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

ifneq ($(wildcard $(KSRC)/include/linux/utsrelease.h),)
	VERSION_FILE := $(KSRC)/include/linux/utsrelease.h
else
	VERSION_FILE := $(KSRC)/include/linux/version.h
endif

KVER := $(shell $(CC) $(CFLAGS) -E -dM $(VERSION_FILE) | \
	grep UTS_RELEASE | awk '{ print $$3 }' | sed 's/\"//g')

KMOD := /lib/modules/$(KVER)/kernel

KMAJ := $(shell echo $(KVER) | \
	sed -e 's/^\([0-9][0-9]*\)\.[0-9][0-9]*\.[0-9][0-9]*.*/\1/')
KMIN := $(shell echo $(KVER) | \
	sed -e 's/^[0-9][0-9]*\.\([0-9][0-9]*\)\.[0-9][0-9]*.*/\1/')
KREV := $(shell echo $(KVER) | \
	sed -e 's/^[0-9][0-9]*\.[0-9][0-9]*\.\([0-9][0-9]*\).*/\1/')

kver_eq = $(shell [ $(KMAJ)$(KMIN)$(KREV) -eq $(1)$(2)$(3) ] && \
	echo 1 || echo 0)
kver_lt = $(shell [ $(KMAJ)$(KMIN)$(KREV) -lt $(1)$(2)$(3) ] && \
	echo 1 || echo 0)
kver_le = $(shell [ $(KMAJ)$(KMIN)$(KREV) -le $(1)$(2)$(3) ] && \
	echo 1 || echo 0)
kver_gt = $(shell [ $(KMAJ)$(KMIN)$(KREV) -gt $(1)$(2)$(3) ] && \
	echo 1 || echo 0)
kver_ge = $(shell [ $(KMAJ)$(KMIN)$(KREV) -ge $(1)$(2)$(3) ] && \
	echo 1 || echo 0)
kver_lk = $(shell [ `echo $(KVER) | egrep $(1)` ] && echo 1 || echo 0)

#
# Please when adding patch sets start with the latest to the earliest
# the idea behind this is that by properly patching the latest code
# base first the earlier patch sets will not need to be modified.
#

# Compatibility patch for kernels >= 2.6.19 and <= 2.6.21
ifeq ($(call kver_le,2,6,21),1)
	PATCHES := $(PATCHES) compat-2.6.19-2.6.21.patch
endif

# Compatibility patch for kernels >= 2.6.14 and <= 2.6.18
ifeq ($(call kver_le,2,6,18),1)
	PATCHES := $(PATCHES) compat-2.6.14-2.6.18.patch
endif

# We don't support kernels < 2.6.14 except for explicit distros
ifeq ($(call kver_lt,2,6,14),1)
	UNSUPPORTED := true
endif

# Compatibility patch for RHEL4/CentOS4
ifeq ($(call kver_lk,"2\.6\.9-.*\.(EL|plus\.c4)"),1)
	PATCHES += compat-rhel4.patch
	UNSUPPORTED :=
endif

MANPAGES:= ietadm.8 ietd.8 ietd.conf.5

ifeq ($(MANDIR),)
	MANPATH := $(shell (manpath 2>/dev/null || \
		echo $MANPATH) | sed 's/:/ /g')
	ifneq ($(MANPATH),)
		test_dir = $(findstring $(dir), $(MANPATH))
	else
		test_dir = $(shell [ -e $(dir) ] && echo $(dir))
	endif
	MANDIR := /usr/share/man /usr/man
	MANDIR := $(foreach dir, $(MANDIR), $(test_dir))
	MANDIR := $(firstword $(MANDIR))
endif

ifeq ($(MANDIR),)
	MANDIR := /usr/share/man
endif

DOCS:= ChangeLog COPYING README README.vmware

ifeq ($(DOCDIR),)
	DOCDIR := /usr/share/doc/iscsitarget
endif

all: usr kernel

usr: patch
	$(MAKE) -C usr

kernel: patch
	$(MAKE) -C $(KSRC) SUBDIRS=$(shell pwd)/kernel modules

patch: $(UNSUPPORTED) integ_check $(PATCHES)

$(UNSUPPORTED):
	@echo "Sorry, your kernel version and/or distribution is currently"
	@echo "not supported."
	@echo ""
	@echo "Please read the README file for information on how you can"
	@echo "contribute compatibility/bug fixes to the IET project."
	@exit 1

integ_check:
	@if [ -e .patched.* -a ! -e .patched.$(KVER) ]; then \
		$(MAKE) unpatch; \
	fi

$(PATCHES): .patched.$(KVER)

.patched.$(KVER):
	@set -e; \
	if [ ! -e .patched.* ]; then \
		for p in $(PATCHES); do \
			echo "Applying Patch $$p"; \
			patch -p0 < patches/$$p; \
			echo $$p >>.patched.$(KVER); \
		done; \
	fi

unpatch:
	@set -e; \
	if [ -e .patched.* ]; then \
		for p in `cat .patched.*`; do \
			reverse="$$p $$reverse"; \
		done; \
		for r in $$reverse; do \
			echo "Reversing patch $$r"; \
			patch -p0 -R < patches/$$r; \
		done; \
		rm -f .patched.*; \
	fi

depmod:
	@echo "Running depmod"
	@if [ x$(DISTDIR) != x -o x$(INSTALL_MOD_PATH) != x ]; then \
		depmod -aq -b $(DISTDIR)$(INSTALL_MOD_PATH) $(KVER); \
	else \
		depmod -aq $(KVER); \
	fi

install: install-usr install-etc install-doc install-kernel depmod

install-kernel: kernel/iscsi_trgt.ko
	@install -vD kernel/iscsi_trgt.ko \
		$(DISTDIR)$(INSTALL_MOD_PATH)$(KMOD)/iscsi/iscsi_trgt.ko

install-usr: usr/ietd usr/ietadm
	@install -vD usr/ietd $(DISTDIR)/usr/sbin/ietd
	@install -vD usr/ietadm $(DISTDIR)/usr/sbin/ietadm

install-etc: install-initd
	@if [ ! -e $(DISTDIR)/etc/ietd.conf ]; then \
		install -vD -m 644 etc/ietd.conf \
			$(DISTDIR)/etc/ietd.conf; \
	fi
	@if [ ! -e $(DISTDIR)/etc/initiators.allow ]; then \
		install -vD -m 644 etc/initiators.allow \
			$(DISTDIR)/etc/initiators.allow; \
	fi
	@if [ ! -e $(DISTDIR)/etc/initiators.deny ]; then \
		install -vD -m 644 etc/initiators.deny \
			$(DISTDIR)/etc/initiators.deny; \
	fi

install-initd:
	@if [ -f /etc/debian_version ]; then \
		install -vD -m 755 etc/initd/initd.debian \
			$(DISTDIR)/etc/init.d/iscsi-target; \
	elif [ -f /etc/redhat-release ]; then \
		install -vD -m 755 etc/initd/initd.redhat \
			$(DISTDIR)/etc/init.d/iscsi-target; \
	elif [ -f /etc/gentoo-release ]; then \
		install -vD -m 755 etc/initd/initd.gentoo \
			$(DISTDIR)/etc/init.d/iscsi-target; \
	elif [ -f /etc/slackware-version ]; then \
		install -vD -m 755 etc/initd/initd \
			$(DISTDIR)/etc/rc.d/iscsi-target; \
	else \
		install -vD -m 755 etc/initd/initd \
			$(DISTDIR)/etc/init.d/iscsi-target; \
	fi

install-doc: install-man
	@ok=true; for f in $(DOCS) ; \
		do [ -e $$f ] || \
			{ echo $$f missing ; ok=false; } ; \
	done ; $$ok
	@set -e; for f in $(DOCS) ; do \
		install -v -D -m 644 $$f \
			$(DISTDIR)$(DOCDIR)/$$f ; \
	done

install-man:
	@ok=true; for f in $(MANPAGES) ; \
		do [ -e doc/manpages/$$f ] || \
			{ echo doc/manpages/$$f missing ; ok=false; } ; \
	done ; $$ok
	@set -e; for f in $(MANPAGES) ; do \
		s=$${f##*.}; \
		install -v -D -m 644 doc/manpages/$$f \
			$(DISTDIR)$(MANDIR)/man$$s/$$f ; \
	done

uninstall: uninstall-kernel depmod uninstall-usr uninstall-etc uninstall-doc

uninstall-kernel:
	rm -f $(DISTDIR)$(INSTALL_MOD_PATH)$(KMOD)/iscsi/iscsi_trgt.ko

uninstall-usr:
	@rm -f $(DISTDIR)/usr/sbin/ietd
	@rm -f $(DISTDIR)/usr/sbin/ietadm

uninstall-etc: uninstall-initd

uninstall-initd:
	if [ -f /etc/debian_version ]; then \
		rm -f $(DISTDIR)/etc/init.d/iscsi-target; \
	elif [ -f /etc/redhat-release ]; then \
		rm -f $(DISTDIR)/etc/init.d/iscsi-target; \
	elif [ -f /etc/gentoo-release ]; then \
		rm -f $(DISTDIR)/etc/init.d/iscsi-target; \
	elif [ -f /etc/slackware-version ]; then \
		rm -f $(DISTDIR)/etc/rc.d/iscsi-target; \
	else \
		rm -f $(DISTDIR)/etc/init.d/iscsi-target; \
	fi

uninstall-doc: uninstall-man
	rm -rf $(DISTDIR)$(DOCDIR)

uninstall-man:
	set -e; for f in $(MANPAGES) ; do \
		s=$${f##*.}; \
		rm -f $(DISTDIR)$(MANDIR)/man$$s/$$f ; \
	done

clean:
	$(MAKE) -C usr clean
	$(MAKE) -C $(KSRC) SUBDIRS=$(shell pwd)/kernel clean

distclean: unpatch clean
	find . -name \*.orig -exec rm -f \{\} \;
	find . -name \*.rej -exec rm -f \{\} \;
	find . -name \*~ -exec rm -f \{\} \;
	find . -name Module.symvers -exec rm -f \{\} \;

