VERSION = 1
PATCHLEVEL = 3
SUBLEVEL = 44

ARCH = i386

#
# For SMP kernels, set this. We don't want to have this in the config file
# because it makes re-config very ugly and too many fundamental files depend
# on "CONFIG_SMP"
#
# NOTE! SMP is experimental. See the file Documentation/SMP.txt
#
# SMP = 1

.EXPORT_ALL_VARIABLES:

CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
	  else if [ -x /bin/bash ]; then echo /bin/bash; \
	  else echo sh; fi ; fi)
TOPDIR	:= $(shell if [ "$$PWD" != "" ]; then echo $$PWD; else pwd; fi)

HPATH   = $(TOPDIR)/include
AS	=as
LD	=ld
HOSTCC	=gcc -I$(HPATH)
CC	=gcc -D__KERNEL__ -I$(HPATH)
MAKE	=make
CPP	=$(CC) -E
AR	=ar
NM	=nm
STRIP	=strip
AWK	=awk

all:	do-it-all

#
# Make "config" the default target if there is no configuration file or
# "depend" the target if there is no top-level dependency information.
#
ifeq (.config,$(wildcard .config))
include .config
ifeq (.depend,$(wildcard .depend))
include .depend
do-it-all:	Version vmlinux
else
CONFIGURATION = depend
do-it-all:	depend
endif
else
CONFIGURATION = config
do-it-all:	config
endif

#
# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, CURRENT, /dev/xxxx or empty, in which case
# the default of FLOPPY is used by 'build'.
#

ROOT_DEV = CURRENT

#
# NFS_ROOT_NAME specifies the default name of the directory to mount
# as root via NFS, if the kernel does not get the "root=" option from
# the boot loader. The "%s" will be replaced by the IP-number of the
# local system.
#

NFS_ROOT = -DNFS_ROOT="\"/tftpboot/%s\""

#
# INSTALL_PATH specifies where to place the updated kernel and system map
# images.  Uncomment if you want to place them anywhere other than root.

#INSTALL_PATH=/boot

#
# If you want to preset the SVGA mode, uncomment the next line and
# set SVGA_MODE to whatever number you want.
# Set it to -DSVGA_MODE=NORMAL_VGA if you just want the EGA/VGA mode.
# The number is the same as you would ordinarily press at bootup.
#

SVGA_MODE=	-DSVGA_MODE=NORMAL_VGA

#
# standard CFLAGS
#

CFLAGS = -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer

ifdef CONFIG_CPP
CFLAGS := $(CFLAGS) -x c++
endif

ifdef SMP
CFLAGS += -D__SMP__
endif

#
# if you want the ram-disk device, define this to be the
# size in blocks.
#

#RAMDISK = -DRAMDISK=512

# Include the make variables (CC, etc...)
#

ARCHIVES	=kernel/kernel.o mm/mm.o fs/fs.o ipc/ipc.o net/network.a
FILESYSTEMS	=fs/filesystems.a
DRIVERS		=drivers/block/block.a \
		 drivers/char/char.a \
		 drivers/net/net.a
LIBS		=$(TOPDIR)/lib/lib.a
SUBDIRS		=kernel drivers mm fs net ipc lib

ifdef CONFIG_CD_NO_IDESCSI
DRIVERS := $(DRIVERS) drivers/cdrom/cdrom.a
endif

ifeq ($(CONFIG_SCSI),y)
DRIVERS := $(DRIVERS) drivers/scsi/scsi.a
endif

ifeq ($(CONFIG_SOUND),y)
DRIVERS := $(DRIVERS) drivers/sound/sound.a
endif

ifdef CONFIG_PCI
DRIVERS := $(DRIVERS) drivers/pci/pci.a
endif

include arch/$(ARCH)/Makefile

ifdef SMP

.S.s:
	$(CC) -D__ASSEMBLY__ -D__SMP__ -traditional -E -o $*.s $<
.S.o:
	$(CC) -D__ASSEMBLY__ -D__SMP__ -traditional -c -o $*.o $<

else

.S.s:
	$(CC) -D__ASSEMBLY__ -traditional -E -o $*.s $<
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c -o $*.o $<

endif

Version: dummy
	@rm -f include/linux/compile.h

boot: vmlinux
	@$(MAKE) -C arch/$(ARCH)/boot

vmlinux: $(CONFIGURATION) init/main.o init/version.o linuxsubdirs
	$(LD) $(LINKFLAGS) $(HEAD) init/main.o init/version.o \
		$(ARCHIVES) \
		$(FILESYSTEMS) \
		$(DRIVERS) \
		$(LIBS) -o vmlinux
	$(NM) vmlinux | grep -v '\(compiled\)\|\(\.o$$\)\|\( a \)' | sort > System.map

symlinks:
	rm -f include/asm
	( cd include ; ln -sf asm-$(ARCH) asm)

oldconfig: symlinks
	$(CONFIG_SHELL) scripts/Configure -d arch/$(ARCH)/config.in

xconfig: symlinks
	( cd scripts ; make kconfig.tk)
	./scripts/kconfig.tk

config: symlinks
	$(CONFIG_SHELL) scripts/Configure arch/$(ARCH)/config.in

linuxsubdirs: dummy
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i; done

$(TOPDIR)/include/linux/version.h: include/linux/version.h
$(TOPDIR)/include/linux/compile.h: include/linux/compile.h

newversion:
	@if [ ! -f .version ]; then \
		echo 1 > .version; \
	else \
		expr `cat .version` + 1 > .version; \
	fi

include/linux/compile.h: $(CONFIGURATION) include/linux/version.h newversion
	@if [ -f .name ]; then \
	   echo \#define UTS_VERSION \"\#`cat .version`-`cat .name` `date`\"; \
	 else \
	   echo \#define UTS_VERSION \"\#`cat .version` `date`\";  \
	 fi >> .ver
	@echo \#define LINUX_COMPILE_TIME \"`date +%T`\" >> .ver
	@echo \#define LINUX_COMPILE_BY \"`whoami`\" >> .ver
	@echo \#define LINUX_COMPILE_HOST \"`hostname`\" >> .ver
	@if [ -x /bin/dnsdomainname ]; then \
	   echo \#define LINUX_COMPILE_DOMAIN \"`dnsdomainname`\"; \
	 elif [ -x /bin/domainname ]; then \
	   echo \#define LINUX_COMPILE_DOMAIN \"`domainname`\"; \
	 else \
	   echo \#define LINUX_COMPILE_DOMAIN ; \
	 fi >> .ver
	@echo \#define LINUX_COMPILER \"`$(HOSTCC) -v 2>&1 | tail -1`\" >> .ver
	@mv -f .ver $@

include/linux/version.h: ./Makefile
	@echo \#define UTS_RELEASE \"$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\" > .ver
	@echo \#define LINUX_VERSION_CODE `expr $(VERSION) \\* 65536 + $(PATCHLEVEL) \\* 256 + $(SUBLEVEL)` >> .ver
	@mv -f .ver $@

init/version.o: init/version.c include/linux/compile.h
	$(CC) $(CFLAGS) -DUTS_MACHINE='"$(ARCH)"' -c -o init/version.o init/version.c

init/main.o: init/main.c
	$(CC) $(CFLAGS) $(PROFILING) $(NFS_ROOT) -c -o $*.o $<

fs: dummy
	$(MAKE) linuxsubdirs SUBDIRS=fs

lib: dummy
	$(MAKE) linuxsubdirs SUBDIRS=lib

mm: dummy
	$(MAKE) linuxsubdirs SUBDIRS=mm

ipc: dummy
	$(MAKE) linuxsubdirs SUBDIRS=ipc

kernel: dummy
	$(MAKE) linuxsubdirs SUBDIRS=kernel

drivers: dummy
	$(MAKE) linuxsubdirs SUBDIRS=drivers

net: dummy
	$(MAKE) linuxsubdirs SUBDIRS=net

MODFLAGS = -DMODULE

ifdef CONFIG_MODVERSIONS
MODFLAGS += -DMODVERSIONS -include $(HPATH)/linux/modversions.h
endif

modules: include/linux/version.h
	@set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i CFLAGS="$(CFLAGS) $(MODFLAGS)" modules; done

modules_install:
	@( \
	MODLIB=/lib/modules/$(VERSION).$(PATCHLEVEL).$(SUBLEVEL); \
	cd modules; \
	MODULES=""; \
	inst_mod() { These="`cat $$1`"; MODULES="$$MODULES $$These"; \
		mkdir -p $$MODLIB/$$2; cp -p $$These $$MODLIB/$$2; \
		echo Installing modules under $$MODLIB/$$2; \
	}; \
	\
	if [ -f NET_MODULES  ]; then inst_mod NET_MODULES  net;  fi; \
	if [ -f IPV4_MODULES ]; then inst_mod IPV4_MODULES ipv4; fi; \
	if [ -f SCSI_MODULES ]; then inst_mod SCSI_MODULES scsi; fi; \
	if [ -f FS_MODULES   ]; then inst_mod FS_MODULES   fs;   fi; \
	\
	ls *.o > .allmods; \
	echo $$MODULES | tr ' ' '\n' | sort | comm -23 .allmods - > .misc; \
	if [ -s .misc ]; then inst_mod .misc misc; fi; \
	rm -f .misc .allmods; \
	)

clean:	archclean
	rm -f kernel/ksyms.lst include/linux/compile.h
	rm -f core `find . -name '*.[oas]' -print`
	rm -f core `find . -type f -name 'core' -print`
	rm -f vmlinux System.map
	rm -f .tmp* drivers/sound/configure
	rm -fr modules/*

mrproper: clean
	rm -f include/linux/autoconf.h include/linux/version.h
	rm -f drivers/sound/local.h drivers/sound/.defines
	rm -f drivers/scsi/aic7xxx_asm drivers/scsi/aic7xxx_seq.h
	rm -f drivers/char/uni_hash.tbl drivers/char/conmakehash
	rm -f .version .config* config.in config.old
	rm -f scripts/tkparse scripts/kconfig.tk scripts/kconfig.tmp
	rm -f include/asm
	rm -f .depend `find . -name .depend -print`
	rm -f .hdepend
	rm -f $(TOPDIR)/include/linux/modversions.h
	rm -f $(TOPDIR)/include/linux/modules/*

distclean: mrproper
	rm -f core `find . -name '*.orig' -print`


backup: mrproper
	cd .. && tar cf - linux | gzip -9 > backup.gz
	sync

dep-files: archdep .hdepend include/linux/version.h
	$(AWK) -f scripts/depend.awk init/*.c > .tmpdepend
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i fastdep; done
	mv .tmpdepend .depend

MODVERFILE :=

ifdef CONFIG_MODVERSIONS
MODVERFILE := $(TOPDIR)/include/linux/modversions.h
endif

depend dep: dep-files $(MODVERFILE)

ifdef CONFIGURATION
..$(CONFIGURATION):
	@echo
	@echo "You have a bad or nonexistent" .$(CONFIGURATION) ": running 'make" $(CONFIGURATION)"'"
	@echo
	$(MAKE) $(CONFIGURATION)
	@echo
	@echo "Successful. Try re-making (ignore the error that follows)"
	@echo
	exit 1

#dummy: ..$(CONFIGURATION)
dummy:

else

dummy:

endif

include Rules.make

#
# This generates dependencies for the .h files.
#

.hdepend: dummy
	rm -f $@
	$(AWK) -f scripts/depend.awk `find $(HPATH) -name \*.h ! -name modversions.h -print` > .$@
	mv .$@ $@
