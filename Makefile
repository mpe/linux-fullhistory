VERSION = 2
PATCHLEVEL = 2
SUBLEVEL = 0
EXTRAVERSION =

ARCH := $(shell uname -m | sed -e s/i.86/i386/ -e s/sun4u/sparc64/ -e s/arm.*/arm/ -e s/sa110/arm/)

.EXPORT_ALL_VARIABLES:

CONFIG_SHELL := $(shell if [ -x "$$BASH" ]; then echo $$BASH; \
	  else if [ -x /bin/bash ]; then echo /bin/bash; \
	  else echo sh; fi ; fi)
TOPDIR	:= $(shell if [ "$$PWD" != "" ]; then echo $$PWD; else pwd; fi)

HPATH   	= $(TOPDIR)/include
FINDHPATH	= $(HPATH)/asm $(HPATH)/linux $(HPATH)/scsi $(HPATH)/net

HOSTCC  	=gcc
HOSTCFLAGS	=-Wall -Wstrict-prototypes -O2 -fomit-frame-pointer

CROSS_COMPILE 	=

AS	=$(CROSS_COMPILE)as
LD	=$(CROSS_COMPILE)ld
CC	=$(CROSS_COMPILE)gcc -D__KERNEL__ -I$(HPATH)
CPP	=$(CC) -E
AR	=$(CROSS_COMPILE)ar
NM	=$(CROSS_COMPILE)nm
STRIP	=$(CROSS_COMPILE)strip
OBJDUMP	=$(CROSS_COMPILE)objdump
MAKE	=make
GENKSYMS=/sbin/genksyms

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

KERNELRELEASE=$(VERSION).$(PATCHLEVEL).$(SUBLEVEL)$(EXTRAVERSION)

#
# INSTALL_PATH specifies where to place the updated kernel and system map
# images.  Uncomment if you want to place them anywhere other than root.

#INSTALL_PATH=/boot

#
# INSTALL_MOD_PATH specifies a prefix to MODLIB for module directory 
# relocations required by build roots.  This is not defined in the
# makefile but the arguement can be passed to make if needed.
#

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

ifdef CONFIG_SMP
CFLAGS += -D__SMP__
AFLAGS += -D__SMP__
endif

#
# if you want the RAM disk device, define this to be the
# size in blocks.
#

#RAMDISK = -DRAMDISK=512

# Include the make variables (CC, etc...)
#

CORE_FILES	=kernel/kernel.o mm/mm.o fs/fs.o ipc/ipc.o
FILESYSTEMS	=fs/filesystems.a
NETWORKS	=net/network.a
DRIVERS		=drivers/block/block.a \
		 drivers/char/char.a \
	         drivers/misc/misc.a
LIBS		=$(TOPDIR)/lib/lib.a
SUBDIRS		=kernel drivers mm fs net ipc lib

ifdef CONFIG_NUBUS
DRIVERS := $(DRIVERS) drivers/nubus/nubus.a
endif

ifeq ($(CONFIG_ISDN),y)
DRIVERS := $(DRIVERS) drivers/isdn/isdn.a
endif

DRIVERS := $(DRIVERS) drivers/net/net.a

ifeq ($(CONFIG_SCSI),y)
DRIVERS := $(DRIVERS) drivers/scsi/scsi.a
endif

ifneq ($(CONFIG_CD_NO_IDESCSI)$(CONFIG_BLK_DEV_IDECD)$(CONFIG_BLK_DEV_SR)$(CONFIG_PARIDE_PCD),)
DRIVERS := $(DRIVERS) drivers/cdrom/cdrom.a
endif

ifeq ($(CONFIG_SOUND),y)
DRIVERS := $(DRIVERS) drivers/sound/sound.a
endif

ifdef CONFIG_PCI
DRIVERS := $(DRIVERS) drivers/pci/pci.a
endif

ifdef CONFIG_DIO
DRIVERS := $(DRIVERS) drivers/dio/dio.a
endif

ifdef CONFIG_SBUS
DRIVERS := $(DRIVERS) drivers/sbus/sbus.a
endif

ifdef CONFIG_ZORRO
DRIVERS := $(DRIVERS) drivers/zorro/zorro.a
endif

ifeq ($(CONFIG_FC4),y)
DRIVERS := $(DRIVERS) drivers/fc4/fc4.a
endif

ifdef CONFIG_PPC
DRIVERS := $(DRIVERS) drivers/macintosh/macintosh.a
endif

ifdef CONFIG_PNP
DRIVERS := $(DRIVERS) drivers/pnp/pnp.a
endif

ifdef CONFIG_VT
DRIVERS := $(DRIVERS) drivers/video/video.a
endif

ifeq ($(CONFIG_PARIDE),y)
DRIVERS := $(DRIVERS) drivers/block/paride/paride.a
endif

ifdef CONFIG_HAMRADIO
DRIVERS := $(DRIVERS) drivers/net/hamradio/hamradio.a
endif

ifeq ($(CONFIG_USB),y)
DRIVERS := $(DRIVERS) drivers/uusbd/usb.a
endif

ifeq ($(CONFIG_I2O),y)
DRIVERS := $(DRIVERS) drivers/i2o/i2o.a
endif

ifeq ($(CONFIG_IRDA),y)
DRIVERS := $(DRIVERS) drivers/net/irda/irda_drivers.a
endif

include arch/$(ARCH)/Makefile

.S.s:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -E -o $*.s $<
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c -o $*.o $<

Version: dummy
	@rm -f include/linux/compile.h

boot: vmlinux
	@$(MAKE) -C arch/$(ARCH)/boot

vmlinux: $(CONFIGURATION) init/main.o init/version.o linuxsubdirs
	$(LD) $(LINKFLAGS) $(HEAD) init/main.o init/version.o \
		--start-group \
		$(CORE_FILES) \
		$(FILESYSTEMS) \
		$(NETWORKS) \
		$(DRIVERS) \
		$(LIBS) \
		--end-group \
		-o vmlinux
	$(NM) vmlinux | grep -v '\(compiled\)\|\(\.o$$\)\|\( [aU] \)\|\(\.\.ng$$\)\|\(LASH[RL]DI\)' | sort > System.map

symlinks:
	rm -f include/asm
	( cd include ; ln -sf asm-$(ARCH) asm)
	@if [ ! -d modules ]; then \
		mkdir modules; \
	fi
	@if [ ! -d include/linux/modules ]; then \
		mkdir include/linux/modules; \
	fi

oldconfig: symlinks scripts/split-include
	$(CONFIG_SHELL) scripts/Configure -d arch/$(ARCH)/config.in
	if [ -r include/linux/autoconf.h ]; then \
	    scripts/split-include include/linux/autoconf.h include/config; \
	fi

xconfig: symlinks scripts/split-include
	$(MAKE) -C scripts kconfig.tk
	wish -f scripts/kconfig.tk
	if [ -r include/linux/autoconf.h ]; then \
	    scripts/split-include include/linux/autoconf.h include/config; \
	fi

menuconfig: include/linux/version.h symlinks scripts/split-include
	$(MAKE) -C scripts/lxdialog all
	$(CONFIG_SHELL) scripts/Menuconfig arch/$(ARCH)/config.in
	if [ -r include/linux/autoconf.h ]; then \
	    scripts/split-include include/linux/autoconf.h include/config; \
	fi

config: symlinks scripts/split-include
	$(CONFIG_SHELL) scripts/Configure arch/$(ARCH)/config.in
	if [ -r include/linux/autoconf.h ]; then \
	    scripts/split-include include/linux/autoconf.h include/config; \
	fi

linuxsubdirs: $(patsubst %, _dir_%, $(SUBDIRS))

$(patsubst %, _dir_%, $(SUBDIRS)) : dummy
	$(MAKE) -C $(patsubst _dir_%, %, $@)

$(TOPDIR)/include/linux/version.h: include/linux/version.h
$(TOPDIR)/include/linux/compile.h: include/linux/compile.h

newversion:
	@if [ ! -f .version ]; then \
		echo 1 > .version; \
	else \
		expr 0`cat .version` + 1 > .version; \
	fi

include/linux/compile.h: $(CONFIGURATION) include/linux/version.h newversion
	@echo -n \#define UTS_VERSION \"\#`cat .version` > .ver
	@if [ -n "$(CONFIG_SMP)" ] ; then echo -n " SMP" >> .ver; fi
	@if [ -f .name ]; then  echo -n \-`cat .name` >> .ver; fi
	@echo ' '`date`'"' >> .ver
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
	@echo \#define LINUX_COMPILER \"`$(CC) $(CFLAGS) -v 2>&1 | tail -1`\" >> .ver
	@mv -f .ver $@

include/linux/version.h: ./Makefile
	@echo \#define UTS_RELEASE \"$(KERNELRELEASE)\" > .ver
	@echo \#define LINUX_VERSION_CODE `expr $(VERSION) \\* 65536 + $(PATCHLEVEL) \\* 256 + $(SUBLEVEL)` >> .ver
	@echo '#define KERNEL_VERSION(a,b,c) (((a) << 16) + ((b) << 8) + (c))' >>.ver
	@mv -f .ver $@

init/version.o: init/version.c include/linux/compile.h
	$(CC) $(CFLAGS) -DUTS_MACHINE='"$(ARCH)"' -c -o init/version.o init/version.c

init/main.o: init/main.c
	$(CC) $(CFLAGS) $(PROFILING) -c -o $*.o $<

fs lib mm ipc kernel drivers net: dummy
	$(MAKE) $(subst $@, _dir_$@, $@)

MODFLAGS = -DMODULE
ifdef CONFIG_MODULES
ifdef CONFIG_MODVERSIONS
MODFLAGS += -DMODVERSIONS -include $(HPATH)/linux/modversions.h
endif

modules: $(patsubst %, _mod_%, $(SUBDIRS))

$(patsubst %, _mod_%, $(SUBDIRS)) : include/linux/version.h
	$(MAKE) -C $(patsubst _mod_%, %, $@) CFLAGS="$(CFLAGS) $(MODFLAGS)" MAKING_MODULES=1 modules

modules_install:
	@( \
	MODLIB=$(INSTALL_MOD_PATH)/lib/modules/$(KERNELRELEASE); \
	cd modules; \
	MODULES=""; \
	inst_mod() { These="`cat $$1`"; MODULES="$$MODULES $$These"; \
		mkdir -p $$MODLIB/$$2; cp $$These $$MODLIB/$$2; \
		echo Installing modules under $$MODLIB/$$2; \
	}; \
	mkdir -p $$MODLIB; \
	\
	if [ -f BLOCK_MODULES ]; then inst_mod BLOCK_MODULES block; fi; \
	if [ -f NET_MODULES   ]; then inst_mod NET_MODULES   net;   fi; \
	if [ -f IPV4_MODULES  ]; then inst_mod IPV4_MODULES  ipv4;  fi; \
	if [ -f IPV6_MODULES  ]; then inst_mod IPV6_MODULES  ipv6;  fi; \
	if [ -f SCSI_MODULES  ]; then inst_mod SCSI_MODULES  scsi;  fi; \
	if [ -f FS_MODULES    ]; then inst_mod FS_MODULES    fs;    fi; \
	if [ -f NLS_MODULES   ]; then inst_mod NLS_MODULES   fs;    fi; \
	if [ -f CDROM_MODULES ]; then inst_mod CDROM_MODULES cdrom; fi; \
	if [ -f HAM_MODULES   ]; then inst_mod HAM_MODULES   net;   fi; \
	if [ -f SOUND_MODULES ]; then inst_mod SOUND_MODULES sound; fi; \
	if [ -f VIDEO_MODULES ]; then inst_mod VIDEO_MODULES video; fi; \
	if [ -f FC4_MODULES   ]; then inst_mod FC4_MODULES   fc4;   fi; \
	if [ -f IRDA_MODULES  ]; then inst_mod IRDA_MODULES  net;   fi; \
	\
	ls *.o > $$MODLIB/.allmods; \
	echo $$MODULES | tr ' ' '\n' | sort | comm -23 $$MODLIB/.allmods - > $$MODLIB/.misc; \
	if [ -s $$MODLIB/.misc ]; then inst_mod $$MODLIB/.misc misc; fi; \
	rm -f $$MODLIB/.misc $$MODLIB/.allmods; \
	)

# modules disabled....

else
modules modules_install: dummy
	@echo
	@echo "The present kernel configuration has modules disabled."
	@echo "Type 'make config' and enable loadable module support."
	@echo "Then build a kernel with module support enabled."
	@echo
	@exit 1
endif

clean:	archclean
	rm -f kernel/ksyms.lst include/linux/compile.h
	rm -f core `find . -name '*.[oas]' ! -regex '.*lxdialog/.*' -print`
	rm -f core `find . -type f -name 'core' -print`
	rm -f core `find . -name '.*.flags' -print`
	rm -f vmlinux System.map
	rm -f .tmp*
	rm -f drivers/char/consolemap_deftbl.c drivers/video/promcon_tbl.c
	rm -f drivers/char/conmakehash
	rm -f drivers/sound/bin2hex drivers/sound/hex2hex
	if [ -d modules ]; then \
		rm -f core `find modules/ -type f -print`; \
	fi
	rm -f submenu*

mrproper: clean archmrproper
	rm -f include/linux/autoconf.h include/linux/version.h
	rm -f drivers/net/hamradio/soundmodem/sm_tbl_{afsk1200,afsk2666,fsk9600}.h
	rm -f drivers/net/hamradio/soundmodem/sm_tbl_{hapn4800,psk4800}.h
	rm -f drivers/net/hamradio/soundmodem/sm_tbl_{afsk2400_7,afsk2400_8}.h
	rm -f drivers/net/hamradio/soundmodem/gentbl
	rm -f drivers/char/hfmodem/gentbl drivers/char/hfmodem/tables.h
	rm -f drivers/sound/*_boot.h drivers/sound/.*.boot
	rm -f .version .config* config.in config.old
	rm -f scripts/tkparse scripts/kconfig.tk scripts/kconfig.tmp
	rm -f scripts/lxdialog/*.o scripts/lxdialog/lxdialog
	rm -f .menuconfig.log
	rm -f include/asm
	rm -rf include/config
	rm -f .depend `find . -name .depend -print`
	rm -f core `find . -size 0 -print`
	rm -f .hdepend scripts/mkdep scripts/split-include
	rm -f $(TOPDIR)/include/linux/modversions.h
	rm -rf $(TOPDIR)/include/linux/modules
	rm -rf modules

distclean: mrproper
	rm -f core `find . \( -name '*.orig' -o -name '*.rej' -o -name '*~' \
                -o -name '*.bak' -o -name '#*#' -o -name '.*.orig' \
                -o -name '.*.rej' -o -name '.SUMS' -o -size 0 \) -print` TAGS

backup: mrproper
	cd .. && tar cf - linux/ | gzip -9 > backup.gz
	sync

sums:
	find . -type f -print | sort | xargs sum > .SUMS

dep-files: scripts/mkdep archdep include/linux/version.h
	scripts/mkdep init/*.c > .depend
	scripts/mkdep `find $(FINDHPATH) -follow -name \*.h ! -name modversions.h -print` > .hdepend
#	set -e; for i in $(SUBDIRS); do $(MAKE) -C $$i fastdep ;done
# let this be made through the fastdep rule in Rules.make
	$(MAKE) $(patsubst %,_sfdep_%,$(SUBDIRS)) _FASTDEP_ALL_SUB_DIRS="$(SUBDIRS)"

MODVERFILE :=

ifdef CONFIG_MODVERSIONS
MODVERFILE := $(TOPDIR)/include/linux/modversions.h
endif

depend dep: dep-files $(MODVERFILE)

# make checkconfig: Prune 'scripts' directory to avoid "false positives".
checkconfig:
	perl -w scripts/checkconfig.pl `find * -path 'scripts' -prune -o -name '*.[hcS]' -print | sort`

checkhelp:
	perl -w scripts/checkhelp.pl `find * -name [cC]onfig.in -print`

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

scripts/mkdep: scripts/mkdep.c
	$(HOSTCC) $(HOSTCFLAGS) -o scripts/mkdep scripts/mkdep.c

scripts/split-include: scripts/split-include.c
	$(HOSTCC) $(HOSTCFLAGS) -o scripts/split-include scripts/split-include.c
