# Makefile for the Linux sound card driver
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes. (hopefully)
#
#

VERSION		= `cat .version`
TARGET_OS	= linux
USRINCDIR	= /usr/include

OBJS   = soundcard.o audio.o dmabuf.o sb_dsp.o dev_table.o \
	 opl3.o sequencer.o midibuf.o sb_card.o pas2_card.o adlib_card.o \
	 pas2_pcm.o pas2_mixer.o pas2_midi.o gus_card.o gus_wave.o mpu401.o \
	 gus_midi.o gus_vol.o patmgr.o sb_mixer.o sb16_dsp.o sb_midi.o \
	 sb16_midi.o sound_switch.o midi_synth.o uart6850.o sound_timer.o \
	 sys_timer.o ics2101.o ad1848.o pss.o sscape.o trix.o aedsp16.o \
	 mad16.o

all:	local.h sound.a

sound.a: $(OBJS) 
	-rm -f sound.a
	$(AR) rcs sound.a $(OBJS)
	sync

clean:
	rm -f core core.* *.o *.a tmp_make *~ x z *%
	rm -f configure
	for i in *.c;do rm -f `basename $$i .c`.s;done

indent:
	for n in *.c;do echo indent $$n;indent $$n;done

local.h:
	$(MAKE) clean
	$(MAKE) setup-$(TARGET_OS)
	$(MAKE) config
	$(MAKE) dep

config: configure
	@$(MAKE) setup-$(TARGET_OS)
	@./configure > local.h
	@echo \#define SOUND_CONFIG_DATE \"`date`\" >> local.h
	@echo \#define SOUND_CONFIG_BY \"`whoami`\" >> local.h
	@echo \#define SOUND_CONFIG_HOST \"`hostname`\" >> local.h
	@echo \#define SOUND_CONFIG_DOMAIN \"`hostname -d`\" >> local.h

clrconf:
	rm -f local.h .depend synth-ld.h trix_boot.h smw-midi0001.h

configure: configure.c
	$(HOSTCC) -o configure configure.c
	@cat .blurb

dep:
	$(CPP) -M *.c > .depend

setup-linux:
	@echo Compiling Sound Driver v $(VERSION) for Linux

sound.o: $(OBJS) 
	-rm -f sound.o
	$(LD) -r -o sound.o $(OBJS)

modules: sound.o
	(cd ../../modules; ln -fs ../drivers/sound/sound.o .)

include $(TOPDIR)/Rules.make
