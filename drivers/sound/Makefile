# Makefile for the Linux sound card driver
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes. (hopefully)
#
#

.PHONY: dummy
SUB_DIRS	= lowlevel
VERSION		= `head -1 .version`
TARGET_OS	= linux
USRINCDIR	= /usr/include
MODULEDIR	= /lib/modules/misc

FIXEDOBJS	= soundcard.o dev_table.o sound_switch.o

ifndef NO_LOWLEVEL
	FIXEDOBJS := $(FIXEDOBJS) lowlevel/lowlevel.o
endif

ifeq (.defines,$(wildcard .defines))
include .defines
include .objects
else
OBJS   = `cat .object_files`
endif

ifndef TOPDIR
TOPDIR=/usr/src/linux
endif


ifndef HOSTCC
build:
	@echo Compiling modularized sound driver
	@make sound.o
	@echo Sound module compiled.

install:	sound.o
	cp sound.o $(MODULEDIR)
endif

.c.o:
	$(CC) $(CFLAGS) -c $<

ifeq ($(CONFIG_SOUND),y)

all:	local.h sound.a

OBJS += $(FIXEDOBJS)

else
all:
endif

ifndef HOSTCC
#
#	Running outside the kernel build.
#
CC	= gcc
HOSTCC	= gcc
CFLAGS	= -O2 -D__KERNEL__ -DMODULE -I/usr/src/linux/include -Wall -Wstrict-prototypes -fomit-frame-pointer -pipe -m486
USE_DEPEND=y
else
include $(TOPDIR)/Rules.make
endif

sound.a: $(OBJS) 
	-rm -f sound.a
	$(AR) rcs sound.a $(OBJS)
	sync

clean:
	rm -f core core.* *.o *.a tmp_make *~ x y z *%
	rm -f configure sound_stub.c objects/*.o 
	cd lowlevel;make clean

indent:
	for n in *.c;do echo indent $$n;indent $$n;done

local.h: 
	$(MAKE) clean
	$(MAKE) setup-$(TARGET_OS)
	$(MAKE) oldconfig
	$(MAKE) dep
	@echo
	@echo
	@echo
	@echo NOTE! Object file dependencies may not be up to date. Run
	@echo make again if kernel/driver doesn''t link properly. Restarting
	@echo it now may save some time.
	@echo
	@echo

config: configure
	@$(MAKE) setup-$(TARGET_OS)
	@./configure > local.h
	@echo \#define SOUND_CONFIG_DATE \"`date`\" >> local.h
	@echo \#define SOUND_CONFIG_BY \"`whoami`\" >> local.h
#	@echo \#define SOUND_CONFIG_HOST \"`hostname`\" >> local.h 2>/dev/null
#	@echo \#define SOUND_CONFIG_DOMAIN \"`hostname -d`\" >> local.h 2>/dev/null
	@echo \#define SOUND_UNAME_A \"`uname -a`\" >> local.h

oldconfig: setup-$(TARGET_OS) configure
	@./configure -o > local.h
	@echo \#define SOUND_CONFIG_DATE \"`date`\" >> local.h
	@echo \#define SOUND_CONFIG_BY \"`whoami`\" >> local.h
#	@echo \#define SOUND_CONFIG_HOST \"`hostname`\" >> local.h 2>/dev/null
#	@echo \#define SOUND_CONFIG_DOMAIN \"`hostname -d`\" >> local.h 2>/dev/null
	@echo \#define SOUND_UNAME_A \"`uname -a`\" >> local.h

kernelconfig: setup-$(TARGET_OS)
	rm -f configure
	$(HOSTCC) -o configure configure.c
	./configure fixedlocal > local.h
	./configure fixeddefines > .defines
	@echo \#define SOUND_CONFIG_DATE \"`date`\" >> local.h
	@echo \#define SOUND_CONFIG_BY \"`whoami`\" >> local.h
#	@echo \#define SOUND_CONFIG_HOST \"`hostname`\" >> local.h 2>/dev/null
#	@echo \#define SOUND_CONFIG_DOMAIN \"`hostname -d`\" >> local.h 2>/dev/null
	@echo \#define SOUND_UNAME_A \"`uname -a`\" >> local.h

mkscript: setup-$(TARGET_OS)
	rm -f configure
	$(HOSTCC) -o configure configure.c
	./configure script > Config.in
	cat lowlevel/Config.tmpl >> Config.in
	./configure fixedlocal > local.h
	./configure fixeddefines > .defines

clrconf:
	rm -f local.h .depend synth-ld.h trix_boot.h smw-midi0001.h maui_boot.h .defines

configure: configure.c
	$(HOSTCC) -o configure configure.c
	@cat .blurb

dep:
	$(CPP) -M *.c > .depend

setup-linux:
	@echo Compiling Sound Driver v $(VERSION) for Linux

sound.o: local.h $(FIXEDOBJS) sound.a
	-rm -f sound.o
	$(LD) -r -o sound.o $(FIXEDOBJS) sound.a

modules: local.h sound.o
	ln -fs `pwd`/sound.o $(TOPDIR)/modules/sound.o


lowlevel/lowlevel.o: dummy
		cd lowlevel;make

ifdef USE_DEPEND
#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
endif
