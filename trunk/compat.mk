#
# compat.mk - Kernel compatibility checks to be used from IET's main Makefile.
#             Taken from open-iscsi (http://www.open-iscsi.org).
#

KSUBLEVEL = $(shell cat $(KSRC)/Makefile | \
	awk -F= '/^SUBLEVEL =/ {print $$2}' | \
	sed 's/^[ \t]*//;s/[ \t]*$$//')

# generic check function
# syntax:
# @$(call generic_check_cmnd, CHECKING-COMMAND, PATCHNAME)
generic_check_cmnd = if $(1) ; then \
			echo " ... FAILED"; \
			echo "Apply $(2) first!"; exit 1; fi ; \
			echo " ... OK"

# the actual check for crypto_digest_*()
check_crypto_digest_cmnd = ! grep crypto_alloc_tfm kernel/digest.c > /dev/null

# see if we make use of the crypto_digest_*() family already
# (deprecated as of 2.6.19)
# syntax:
# @$(call check_crypto_digest, PATCHNAME)
check_crypto_digest = $(call generic_check_cmnd, $(check_crypto_digest_cmnd),\
			$(1))

# this if-else ladder is horrible
# but I don't know any quick way to clean it up
# since Make doesn't support a switch construct
.NOTPARALLEL: check_kernel_compat
check_kernel_compat:
	@echo -n "Checking kernel compatibility"
ifeq ($(KSUBLEVEL),14)
	@$(call check_crypto_digest,\
		patches/compat-2.6.14-2.6.18.patch)
else
ifeq ($(KSUBLEVEL),15)
	@$(call check_crypto_digest,\
		patches/compat-2.6.14-2.6.18.patch)
else
ifeq ($(KSUBLEVEL),16)
	@$(call check_crypto_digest,\
		patches/compat-2.6.14-2.6.18.patch)
else
ifeq ($(KSUBLEVEL),17)
	@$(call check_crypto_digest,\
		patches/compat-2.6.14-2.6.18.patch)
else
ifeq ($(KSUBLEVEL),18)
	@$(call check_crypto_digest,\
		patches/compat-2.6.14-2.6.18.patch)
else
ifeq ($(KSUBLEVEL),19)
# XXX: make sure no compat patch is applied here!?
	@echo " ... OK"
else
ifeq ($(KSUBLEVEL),20)
	@echo " ... OK"
else
	@echo "UNSUPPORTED KERNEL DETECTED"
	exit 1;
endif
endif
endif
endif
endif
endif
endif
