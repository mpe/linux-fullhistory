# Makefile for the Linux sound card driver
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes. (hopefully)
#
#

VERSION		= 2.0
USRINCDIR	= /usr/include

.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

OBJS   = soundcard.o dsp.o audio.o dmabuf.o sb_dsp.o dev_table.o \
	 opl3.o sequencer.o midibuf.o sb_card.o pas2_card.o adlib_card.o \
	 pas2_pcm.o pas2_mixer.o pas2_midi.o gus_card.o gus_wave.o mpu401.o \
	 gus_midi.o gus_vol.o patmgr.o

all:	local.h sound.a

sound.a: $(OBJS) 
	-rm -f sound.a
	$(AR) rcs sound.a $(OBJS)
	sync

indent:
	for n in *.c;do echo indent $$n;indent $$n;done

local.h:
	rm -f configure
	$(MAKE) config
	$(MAKE) dep

config: configure
	@./configure > local.h
	@echo \#define SOUND_VERSION_STRING \"$(VERSION)\" >> local.h
	@echo \#define SOUND_CONFIG_DATE \"`date`\" >> local.h
	@echo \#define SOUND_CONFIG_BY \"`whoami`\" >> local.h
	@echo \#define SOUND_CONFIG_HOST \"`hostname`\" >> local.h
	@echo \#define SOUND_CONFIG_DOMAIN \"`domainname`\" >> local.h

clrconf:
	rm -f local.h .depend os.h soundcard.c

configure: configure.c
	$(HOSTCC) -o configure configure.c
	@cat .blurb

dep:
	$(CPP) -M *.c > .depend

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
