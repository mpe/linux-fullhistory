# Makefile for the Linux sound card driver
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes. (hopefully)
#
#

VERSION		= `head -1 .version`
TARGET_OS	= linux
USRINCDIR	= /usr/include
MODULEDIR	= /lib/modules/misc

FIXEDOBJS	= soundcard.o dev_table.o sound_switch.o

ifeq (.defines,$(wildcard .defines))
include .defines
include .objects
else
OBJS   = audio.o dmabuf.o sb_dsp.o \
	 opl3.o sequencer.o midibuf.o sb_card.o pas2_card.o adlib_card.o \
	 pas2_pcm.o pas2_mixer.o pas2_midi.o gus_card.o gus_wave.o mpu401.o \
	 gus_midi.o gus_vol.o patmgr.o sb_mixer.o sb16_dsp.o sb_midi.o \
	 sb16_midi.o midi_synth.o uart6850.o sound_timer.o \
	 sys_timer.o ics2101.o ad1848.o pss.o sscape.o trix.o aedsp16.o \
	 mad16.o mad16_sb_midi.o cs4232.o maui.o sound_pnp.o
endif


build:
	@echo Compiling modularized sound driver
	@make sound.o
	@echo Sound module compiled.

install:	sound.o
	cp sound.o $(MODULEDIR)

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
CFLAGS	= -D__KERNEL__ -DMODULE -DMODVERSIONS -I/usr/src/linux/include -Wall -Wstrict-prototypes -O2 -fomit-frame-pointer -pipe -m486
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
	@echo make again if kernel/driver doesn't link properly. Restarting
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

script: setup-$(TARGET_OS)
	rm -f configure
	$(HOSTCC) -o configure configure.c
	./configure script > Config.in
	./configure fixedlocal > local.h
	./configure fixeddefines > .defines

clrconf:
	rm -f local.h .depend synth-ld.h trix_boot.h smw-midi0001.h .defines

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
	ln -fs `pwd`/sound.o /usr/src/linux/modules/sound.o


#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
