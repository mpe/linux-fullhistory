VERSION = 1
PATCHLEVEL = 1
SUBLEVEL = 57

ARCH = i386

all:	Version zImage

.EXPORT_ALL_VARIABLES:

CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
	  else if [ -x /bin/bash ]; then echo /bin/bash; \
	  else echo sh; fi ; fi)
TOPDIR	:= $(shell if [ "$$PWD" != "" ]; then echo $$PWD; else pwd; fi)

#
# Make "config" the default target if there is no configuration file or
# "depend" the target if there is no top-level dependency information.
#
ifeq (.config,$(wildcard .config))
include .config
ifeq (.depend,$(wildcard .depend))
include .depend
else
CONFIGURATION = depend
endif
else
CONFIGURATION = config
endif

#
# ROOT_DEV specifies the default root-device when making the image.
# This can be either FLOPPY, CURRENT, /dev/xxxx or empty, in which case
# the default of FLOPPY is used by 'build'.
#

ROOT_DEV = CURRENT

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

CFLAGS = -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -pipe

ifdef CONFIG_CPP
CFLAGS := $(CFLAGS) -x c++
endif

#
# if you want the ram-disk device, define this to be the
# size in blocks.
#

#RAMDISK = -DRAMDISK=512

# Include the make variables (CC, etc...)
#

include arch/$(ARCH)/Makefile

ARCHIVES	=kernel/kernel.o mm/mm.o fs/fs.o net/net.o ipc/ipc.o
FILESYSTEMS	=fs/filesystems.a
DRIVERS		=drivers/block/block.a \
		 drivers/char/char.a \
		 drivers/net/net.a \
		 ibcs/ibcs.o
LIBS		=lib/lib.a
SUBDIRS		=kernel drivers mm fs net ipc ibcs lib

ifdef CONFIG_SCSI
DRIVERS := $(DRIVERS) drivers/scsi/scsi.a
endif

ifdef CONFIG_SOUND
DRIVERS := $(DRIVERS) drivers/sound/sound.a
endif

ifdef CONFIG_MATH_EMULATION
DRIVERS := $(DRIVERS) drivers/FPU-emu/math.a
endif

.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $<

Version: dummy
	rm -f tools/version.h

boot:
	ln -sf arch/$(ARCH)/boot boot

include/asm:
	( cd include ; ln -sf asm-$(ARCH) asm)

kernel/entry.S:
	ln -sf ../arch/$(ARCH)/entry.S kernel/entry.S

symlinks: boot include/asm kernel/entry.S

config.in: arch/$(ARCH)/config.in
	cp $< $@

oldconfig: symlinks config.in
	$(CONFIG_SHELL) Configure -d $(OPTS)

config: symlinks config.in
	$(CONFIG_SHELL) Configure $(OPTS)

linuxsubdirs: dummy
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i; done

tools/./version.h: tools/version.h

tools/version.h: $(CONFIGURE) Makefile
	@./makever.sh
	@echo \#define UTS_RELEASE \"$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)\" > tools/version.h
	@echo \#define UTS_VERSION \"\#`cat .version` `date`\" >> tools/version.h
	@echo \#define LINUX_COMPILE_TIME \"`date +%T`\" >> tools/version.h
	@echo \#define LINUX_COMPILE_BY \"`whoami`\" >> tools/version.h
	@echo \#define LINUX_COMPILE_HOST \"`hostname`\" >> tools/version.h
	@if [ -x /bin/dnsdomainname ]; then \
	   echo \#define LINUX_COMPILE_DOMAIN \"`dnsdomainname`\"; \
	 else \
	   echo \#define LINUX_COMPILE_DOMAIN \"`domainname`\"; \
	 fi >> tools/version.h
	@echo \#define LINUX_COMPILER \"`$(HOSTCC) -v 2>&1 | tail -1`\" >> tools/version.h

tools/build: tools/build.c $(CONFIGURE)
	$(HOSTCC) $(CFLAGS) -o $@ $<

boot/head.o: $(CONFIGURE) boot/head.s

boot/head.s: boot/head.S $(CONFIGURE) include/linux/tasks.h
	$(CPP) -traditional $< -o $@

tools/version.o: tools/version.c tools/version.h

init/main.o: $(CONFIGURE) init/main.c
	$(CC) $(CFLAGS) $(PROFILING) -c -o $*.o $<

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

clean:	archclean
	rm -f kernel/ksyms.lst
	rm -f core `find . -name '*.[oas]' -print`
	rm -f core `find . -name 'core' -print`
	rm -f zImage zSystem.map tools/zSystem tools/system
	rm -f Image System.map tools/build
	rm -f zBoot/zSystem zBoot/xtract zBoot/piggyback
	rm -f .tmp* drivers/sound/configure

mrproper: clean
	rm -f include/linux/autoconf.h tools/version.h
	rm -f drivers/sound/local.h
	rm -f .version .config* config.in config.old
	rm -f boot include/asm kernel/entry.S
	rm -f .depend `find . -name .depend -print`

distclean: mrproper

backup: mrproper
	cd .. && tar cf - linux | gzip -9 > backup.gz
	sync

depend dep:
	touch tools/version.h
	for i in init/*.c;do echo -n "init/";$(CPP) -M $$i;done > .tmpdepend
	for i in tools/*.c;do echo -n "tools/";$(CPP) -M $$i;done >> .tmpdepend
	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i dep; done
	rm -f tools/version.h
	mv .tmpdepend .depend

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

dummy: ..$(CONFIGURATION)

else

dummy:

endif

#
# Leave these dummy entries for now to tell people that they are going away..
#
Image:
	@echo
	@echo Uncompressed kernel images no longer supported. Use
	@echo \"make zImage\" instead.
	@echo
	@exit 1

disk:
	@echo
	@echo Uncompressed kernel images no longer supported. Use
	@echo \"make zdisk\" instead.
	@echo
	@exit 1
