VERSION = 2
PATCHLEVEL = 5
SUBLEVEL = 19
EXTRAVERSION =

# We are using a recursive build, so we need to do a little thinking
# to get the ordering right.
#
# Most importantly: sub-Makefiles should only ever modify files in
# their own directory. If in some directory we have a dependency on
# a file in another dir (which doesn't happen often, but it's of
# unavoidable when linking the built-in.o targets which finally
# turn into vmlinux), we will call a sub make in that other dir, and
# after that we are sure that everything which is in that other dir
# is now up to date.
#
# The only cases where we need to modify files which have global
# effects are thus separated out and done before the recursive
# descending is started. They are now explicitly listed as the
# prepare rule.

KERNELRELEASE=$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)

ARCH := $(shell uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ -e s/arm.*/arm/ -e s/sa110/arm/)
KERNELPATH=kernel-$(shell echo $(KERNELRELEASE) | sed -e "s/-//g")

CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
	  else if [ -x /bin/bash ]; then echo /bin/bash; \
	  else echo sh; fi ; fi)
TOPDIR	:= $(CURDIR)

HPATH   	= $(TOPDIR)/include
FINDHPATH	= $(HPATH)/asm $(HPATH)/linux $(HPATH)/scsi $(HPATH)/net

HOSTCC  	= gcc
HOSTCFLAGS	= -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer

CROSS_COMPILE 	=

#
# Include the make variables (CC, etc...)
#

AS		= $(CROSS_COMPILE)as
LD		= $(CROSS_COMPILE)ld
CC		= $(CROSS_COMPILE)gcc
CPP		= $(CC) -E
AR		= $(CROSS_COMPILE)ar
NM		= $(CROSS_COMPILE)nm
STRIP		= $(CROSS_COMPILE)strip
OBJCOPY		= $(CROSS_COMPILE)objcopy
OBJDUMP		= $(CROSS_COMPILE)objdump
MAKEFILES	= $(TOPDIR)/.config
GENKSYMS	= /sbin/genksyms
DEPMOD		= /sbin/depmod
PERL		= perl
MODFLAGS	= -DMODULE
CFLAGS_MODULE   = $(MODFLAGS)
AFLAGS_MODULE   =
CFLAGS_KERNEL	=
AFLAGS_KERNEL	=
EXPORT_FLAGS    =

export	VERSION PATCHLEVEL SUBLEVEL EXTRAVERSION KERNELRELEASE ARCH \
	CONFIG_SHELL TOPDIR HPATH HOSTCC HOSTCFLAGS CROSS_COMPILE AS LD CC \
	CPP AR NM STRIP OBJCOPY OBJDUMP MAKE MAKEFILES GENKSYMS PERL

export CPPFLAGS EXPORT_FLAGS
export CFLAGS CFLAGS_KERNEL CFLAGS_MODULE 
export AFLAGS AFLAGS_KERNEL AFLAGS_MODULE

all:	do-it-all

#
# Make "config" the default target if there is no configuration file or
# "depend" the target if there is no top-level dependency information.
#

ifeq (.config,$(wildcard .config))
include .config
do-it-all:	vmlinux
else
CONFIGURATION = config
do-it-all:	config
endif

#
# INSTALL_PATH specifies where to place the updated kernel and system map
# images.  Uncomment if you want to place them anywhere other than root.
#

#export	INSTALL_PATH=/boot

#
# INSTALL_MOD_PATH specifies a prefix to MODLIB for module directory
# relocations required by build roots.  This is not defined in the
# makefile but the arguement can be passed to make if needed.
#

MODLIB	:= $(INSTALL_MOD_PATH)/lib/modules/$(KERNELRELEASE)
export MODLIB

#
# standard CFLAGS
#

CPPFLAGS := -D__KERNEL__ -I$(HPATH)

CFLAGS := $(CPPFLAGS) -Wall -Wstrict-prototypes -Wno-trigraphs -O2 \
	  -fomit-frame-pointer -fno-strict-aliasing -fno-common
AFLAGS := -D__ASSEMBLY__ $(CPPFLAGS)

ifdef CONFIG_MODULES
EXPORT_FLAGS := -DEXPORT_SYMTAB
endif

INIT		=init/init.o
CORE_FILES	=kernel/kernel.o mm/mm.o fs/fs.o ipc/ipc.o
NETWORKS	=net/network.o

LIBS		=$(TOPDIR)/lib/lib.a
SUBDIRS		=init kernel lib drivers mm fs net ipc sound

DRIVERS-y 	= drivers/built-in.o
DRIVERS-$(CONFIG_SOUND) += sound/sound.o

DRIVERS := $(DRIVERS-y)


include arch/$(ARCH)/Makefile

export	NETWORKS DRIVERS LIBS HEAD LDFLAGS LINKFLAGS MAKEBOOT ASFLAGS

# boot target
# ---------------------------------------------------------------------------

boot: vmlinux
	@$(MAKE) -C arch/$(ARCH)/boot

# Build vmlinux
# ---------------------------------------------------------------------------

#	This is a bit tricky: If we need to relink vmlinux, we want
#	the version number incremented, which means recompile init/version.o
#	and relink init/init.o. However, we cannot do this during the
#       normal descending-into-subdirs phase, since at that time
#       we cannot yet know if we will need to relink vmlinux.
#	So we descend into init/ inside the rule for vmlinux again.

vmlinux-objs := $(HEAD) $(INIT) $(CORE_FILES) $(LIBS) $(DRIVERS) $(NETWORKS)

cmd_link_vmlinux = $(LD) $(LINKFLAGS) $(HEAD) $(INIT) \
		--start-group \
		$(CORE_FILES) \
		$(LIBS) \
		$(DRIVERS) \
		$(NETWORKS) \
		--end-group \
		-o vmlinux

#	set -e makes the rule exit immediately on error

define rule_link_vmlinux
	set -e
	echo Generating build number
	. scripts/mkversion > .tmpversion
	mv -f .tmpversion .version
	$(MAKE) -C init
	echo $(cmd_link_vmlinux)
	$(cmd_link_vmlinux)
	echo 'cmd_$@ := $(cmd_link_vmlinux)' > $(@D)/.$(@F).cmd
	$(NM) vmlinux | grep -v '\(compiled\)\|\(\.o$$\)\|\( [aUw] \)\|\(\.\.ng$$\)\|\(LASH[RL]DI\)' | sort > System.map
endef

vmlinux: $(CONFIGURATION) $(vmlinux-objs) FORCE
	$(call if_changed_rule,link_vmlinux)

#	The actual objects are generated when descending, 
#	make sure no implicit rule kicks in

$(sort $(vmlinux-objs)): $(SUBDIRS) ;

# 	Handle descending into subdirectories listed in $(SUBDIRS)

.PHONY: $(SUBDIRS)
$(SUBDIRS): prepare
	@$(MAKE) -C $@

#	Things we need done before we even start the actual build.
#	The dependency on .hdepend will in turn take care of
#	include/asm, include/linux/version etc.

.PHONY: prepare
prepare: .hdepend include/config/MARKER

# Single targets
# ---------------------------------------------------------------------------

%.s: %.c FORCE
	@$(MAKE) -C $(@D) $(@F)
%.i: %.c FORCE
	@$(MAKE) -C $(@D) $(@F)
%.o: %.c FORCE
	@$(MAKE) -C $(@D) $(@F)
%.s: %.S FORCE
	@$(MAKE) -C $(@D) $(@F)
%.o: %.S FORCE
	@$(MAKE) -C $(@D) $(@F)

# 	FIXME: The asm symlink changes when $(ARCH) changes. That's
#	hard to detect, but I suppose "make mrproper" is a good idea
#	before switching between archs anyway.

include/asm:
	@echo 'Making asm->asm-$(ARCH) symlink'
	@ln -s asm-$(ARCH) $@
	@echo 'Making directory include/linux/modules'
	@mkdir include/linux/modules

# 	Split autoconf.h into include/linux/config/*

include/config/MARKER: scripts/split-include include/linux/autoconf.h
	scripts/split-include include/linux/autoconf.h include/config
	@ touch include/config/MARKER

# Generate some files
# ---------------------------------------------------------------------------

#	version.h changes when $(KERNELRELEASE) etc change, as defined in
#	this Makefile

include/linux/version.h: Makefile
	@echo Generating $@
	@. scripts/mkversion_h $@ $(KERNELRELEASE) $(VERSION) $(PATCHLEVEL) $(SUBLEVEL)

# Helpers built in scripts/
# ---------------------------------------------------------------------------

scripts/mkdep scripts/split-include : FORCE
	@$(MAKE) -C scripts

# Generate dependencies
# ---------------------------------------------------------------------------

# 	In the same pass, generate module versions, that's why it's
#	all mixed up here.

.PHONY: depend dep $(patsubst %,_sfdep_%,$(SUBDIRS))

depend dep: .hdepend

#	.hdepend is missing prerequisites - in fact dependencies need
#	to be redone basically each time anything changed - since
#	that's too expensive, we traditionally rely on the user to
#	run "make dep" manually whenever necessary. In this case,
#	we make "FORCE" a prequisite, to force redoing the
#	dependencies. Yeah, that's ugly, and it'll go away soon.

.hdepend: scripts/mkdep include/linux/version.h include/asm \
	  $(if $(filter dep depend,$(MAKECMDGOALS)),FORCE)
	scripts/mkdep -- `find $(FINDHPATH) -name SCCS -prune -o -follow -name \*.h ! -name modversions.h -print` > $@
	@$(MAKE) $(patsubst %,_sfdep_%,$(SUBDIRS))
ifdef CONFIG_MODVERSIONS
	@$(MAKE) include/linux/modversions.h
endif
	@$(MAKE) archdep

$(patsubst %,_sfdep_%,$(SUBDIRS)): FORCE
	@$(MAKE) -C $(patsubst _sfdep_%, %, $@) fastdep

# 	Update modversions.h, but only if it would change.

include/linux/modversions.h: FORCE
	@(echo "#ifndef _LINUX_MODVERSIONS_H";\
	  echo "#define _LINUX_MODVERSIONS_H"; \
	  echo "#include <linux/modsetver.h>"; \
	  cd $(TOPDIR)/include/linux/modules; \
	  for f in *.ver; do \
	    if [ -f $$f ]; then echo "#include <linux/modules/$${f}>"; fi; \
	  done; \
	  echo "#endif"; \
	) > $@.tmp
	@if [ -r $@ ] && cmp -s $@ $@.tmp; then \
		echo $@ was not updated; \
		rm -f $@.tmp; \
	else \
		echo $@ was updated; \
		mv -f $@.tmp $@; \
	fi

# ---------------------------------------------------------------------------
# Modules

ifdef CONFIG_MODULES

#	Build modules

ifdef CONFIG_MODVERSIONS
MODFLAGS += -include $(HPATH)/linux/modversions.h
endif

.PHONY: modules
modules: $(patsubst %, _mod_%, $(SUBDIRS))

.PHONY: $(patsubst %, _mod_%, $(SUBDIRS))
$(patsubst %, _mod_%, $(SUBDIRS)) : include/linux/version.h include/config/MARKER
	@$(MAKE) -C $(patsubst _mod_%, %, $@) modules

#	Install modules

.PHONY: modules_install
modules_install: _modinst_ $(patsubst %, _modinst_%, $(SUBDIRS)) _modinst_post

.PHONY: _modinst_
_modinst_:
	@rm -rf $(MODLIB)/kernel
	@rm -f $(MODLIB)/build
	@mkdir -p $(MODLIB)/kernel
	@ln -s $(TOPDIR) $(MODLIB)/build

# If System.map exists, run depmod.  This deliberately does not have a
# dependency on System.map since that would run the dependency tree on
# vmlinux.  This depmod is only for convenience to give the initial
# boot a modules.dep even before / is mounted read-write.  However the
# boot script depmod is the master version.
ifeq "$(strip $(INSTALL_MOD_PATH))" ""
depmod_opts	:=
else
depmod_opts	:= -b $(INSTALL_MOD_PATH) -r
endif
.PHONY: _modinst_post
_modinst_post:
	if [ -r System.map ]; then $(DEPMOD) -ae -F System.map $(depmod_opts) $(KERNELRELEASE); fi

.PHONY: $(patsubst %, _modinst_%, $(SUBDIRS))
$(patsubst %, _modinst_%, $(SUBDIRS)) :
	@$(MAKE) -C $(patsubst _modinst_%, %, $@) modules_install

else # CONFIG_MODULES

# ---------------------------------------------------------------------------
# Modules not configured

modules modules_install: FORCE
	@echo
	@echo "The present kernel configuration has modules disabled."
	@echo "Type 'make config' and enable loadable module support."
	@echo "Then build a kernel with module support enabled."
	@echo
	@exit 1

endif # CONFIG_MODULES

# Scripts to check various things for consistency
# ---------------------------------------------------------------------------

checkconfig:
	find * -name '*.[hcS]' -type f -print | sort | xargs $(PERL) -w scripts/checkconfig.pl

checkhelp:
	find * -name [cC]onfig.in -print | sort | xargs $(PERL) -w scripts/checkhelp.pl

checkincludes:
	find * -name '*.[hcS]' -type f -print | sort | xargs $(PERL) -w scripts/checkincludes.pl

# Generate tags for editors
# ---------------------------------------------------------------------------

TAGS: FORCE
	{ find include/asm-${ARCH} -name '*.h' -print ; \
	find include -type d \( -name "asm-*" -o -name config \) -prune -o -name '*.h' -print ; \
	find $(SUBDIRS) init -name '*.[ch]' ; } | grep -v SCCS | etags -

# 	Exuberant ctags works better with -I
tags: FORCE
	CTAGSF=`ctags --version | grep -i exuberant >/dev/null && echo "-I __initdata,__exitdata,EXPORT_SYMBOL,EXPORT_SYMBOL_NOVERS"`; \
	ctags $$CTAGSF `find include/asm-$(ARCH) -name '*.h'` && \
	find include -type d \( -name "asm-*" -o -name config \) -prune -o -name '*.h' -print | xargs ctags $$CTAGSF -a && \
	find $(SUBDIRS) init -name '*.[ch]' | xargs ctags $$CTAGSF -a

# Assorted miscellaneous targets
# ---------------------------------------------------------------------------

# Documentation targets

sgmldocs psdocs pdfdocs htmldocs:
	@$(MAKE) -C Documentation/DocBook $@


# RPM target
#
#	If you do a make spec before packing the tarball you can rpm -ta it

spec:
	. scripts/mkspec >kernel.spec

#	Build a tar ball, generate an rpm from it and pack the result
#	There arw two bits of magic here
#	1) The use of /. to avoid tar packing just the symlink
#	2) Removing the .dep files as they have source paths in them that
#	   will become invalid

rpm:	clean spec
	find . \( -size 0 -o -name .depend -o -name .hdepend \) -type f -print | xargs rm -f
	set -e; \
	cd $(TOPDIR)/.. ; \
	ln -sf $(TOPDIR) $(KERNELPATH) ; \
	tar -cvz --exclude CVS -f $(KERNELPATH).tar.gz $(KERNELPATH)/. ; \
	rm $(KERNELPATH) ; \
	cd $(TOPDIR) ; \
	. scripts/mkversion > .version ; \
	rpm -ta $(TOPDIR)/../$(KERNELPATH).tar.gz ; \
	rm $(TOPDIR)/../$(KERNELPATH).tar.gz

# Targets which don't need .config
# ===========================================================================

# Kernel configuration
# ---------------------------------------------------------------------------

.PHONY: oldconfig xconfig menuconfig config

oldconfig:
	$(CONFIG_SHELL) scripts/Configure -d arch/$(ARCH)/config.in

xconfig:
	@$(MAKE) -C scripts kconfig.tk
	wish -f scripts/kconfig.tk

menuconfig:
	@$(MAKE) -C scripts/lxdialog all
	$(CONFIG_SHELL) scripts/Menuconfig arch/$(ARCH)/config.in

config:
	$(CONFIG_SHELL) scripts/Configure arch/$(ARCH)/config.in


# Cleaning up
# ---------------------------------------------------------------------------

#	files removed with 'make clean'
CLEAN_FILES += \
	kernel/ksyms.lst include/linux/compile.h \
	vmlinux System.map \
	.tmp* \
	drivers/char/consolemap_deftbl.c drivers/video/promcon_tbl.c \
	drivers/char/conmakehash \
	drivers/char/drm/*-mod.c \
	drivers/pci/devlist.h drivers/pci/classlist.h drivers/pci/gen-devlist \
	drivers/zorro/devlist.h drivers/zorro/gen-devlist \
	sound/oss/bin2hex sound/oss/hex2hex \
	drivers/atm/fore200e_mkfirm drivers/atm/{pca,sba}*{.bin,.bin1,.bin2} \
	drivers/scsi/aic7xxx/aic7xxx_seq.h \
	drivers/scsi/aic7xxx/aic7xxx_reg.h \
	drivers/scsi/aic7xxx/aicasm/aicasm_gram.c \
	drivers/scsi/aic7xxx/aicasm/aicasm_scan.c \
	drivers/scsi/aic7xxx/aicasm/y.tab.h \
	drivers/scsi/aic7xxx/aicasm/aicasm \
	drivers/scsi/53c700_d.h \
	net/khttpd/make_times_h \
	net/khttpd/times.h \
	submenu*

# 	directories removed with 'make clean'
CLEAN_DIRS += \
	modules

# 	files removed with 'make mrproper'
MRPROPER_FILES += \
	include/linux/autoconf.h include/linux/version.h \
	drivers/net/hamradio/soundmodem/sm_tbl_{afsk1200,afsk2666,fsk9600}.h \
	drivers/net/hamradio/soundmodem/sm_tbl_{hapn4800,psk4800}.h \
	drivers/net/hamradio/soundmodem/sm_tbl_{afsk2400_7,afsk2400_8}.h \
	drivers/net/hamradio/soundmodem/gentbl \
	sound/oss/*_boot.h sound/oss/.*.boot \
	sound/oss/msndinit.c \
	sound/oss/msndperm.c \
	sound/oss/pndsperm.c \
	sound/oss/pndspini.c \
	drivers/atm/fore200e_*_fw.c drivers/atm/.fore200e_*.fw \
	.version .config* config.in config.old \
	scripts/tkparse scripts/kconfig.tk scripts/kconfig.tmp \
	scripts/lxdialog/*.o scripts/lxdialog/lxdialog \
	.menuconfig.log \
	include/asm \
	.hdepend scripts/mkdep scripts/split-include scripts/docproc \
	$(TOPDIR)/include/linux/modversions.h \
	kernel.spec

# 	directories removed with 'make mrproper'
MRPROPER_DIRS += \
	include/config \
	$(TOPDIR)/include/linux/modules


clean:	archclean
	find . \( -name '*.[oas]' -o -name core -o -name '.*.cmd' \) -type f -print \
		| grep -v lxdialog/ | xargs rm -f
	rm -f $(CLEAN_FILES)
	rm -rf $(CLEAN_DIRS)
	@$(MAKE) -C Documentation/DocBook clean

mrproper: clean archmrproper
	find . \( -size 0 -o -name .depend \) -type f -print | xargs rm -f
	rm -f $(MRPROPER_FILES)
	rm -rf $(MRPROPER_DIRS)
	@$(MAKE) -C Documentation/DocBook mrproper

distclean: mrproper
	rm -f core `find . \( -not -type d \) -and \
		\( -name '*.orig' -o -name '*.rej' -o -name '*~' \
		-o -name '*.bak' -o -name '#*#' -o -name '.*.orig' \
		-o -name '.*.rej' -o -name '.SUMS' -o -size 0 \) -type f -print` TAGS tags

# FIXME Should go into a make.lib or something 
# ===========================================================================

# read all saved command lines

cmd_files := $(wildcard .*.cmd)
ifneq ($(cmd_files),)
  include $(cmd_files)
endif

# Usage: $(call if_changed_rule,foo)
# will check if $(cmd_foo) changed, or any of the prequisites changed,
# and if so will execute $(rule_foo)

if_changed_rule = $(if $(strip $? \
		               $(filter-out $(cmd_$(1)),$(cmd_$(@F)))\
			       $(filter-out $(cmd_$(@F)),$(cmd_$(1)))),\
	               @$(rule_$(1)))

FORCE:
