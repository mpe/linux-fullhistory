BUILDCODE=s
# Makefile for the Linux sound card driver
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes. (hopefully)
#
#
#
#
ifeq ($(ARCH),m68k)
  L_TARGET := sound.a
  L_OBJS   :=
  M_OBJS   :=
  ifeq ($(CONFIG_DMASOUND),y)
    L_OBJS += dmasound.o
  else
    ifeq ($(CONFIG_DMASOUND),m)
    M_OBJS += dmasound.o
    endif
  endif

  include $(TOPDIR)/Rules.make

  clean:
	rm -f core *.o *.a *.s

  # dummy rule to keep 'make xconfig' happy
  mkscript:

# More dummy targets for make [menu]config
mkscript:
kernelconfig:

else

ifeq (.defines,$(wildcard .defines))
include .defines
#include .objects
endif

TARGET_OS=linux

ifndef TOPDIR
TOPDIR=/usr/src/linux
endif

SUB_DIRS     := lowlevel
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

L_TARGET := sound.a
M_OBJS   :=
L_OBJS   :=

ifeq ($(CONFIG_SOUND),y)
  L_OBJS += soundcard.o dev_table.o sound_switch.o sequencer.o sys_timer.o sound_timer.o lowlevel/lowlevel.o midi_synth.o midibuf.o sound_firmware.o audio.o dmabuf.o
else
  ifeq ($(CONFIG_SOUND),m)
     M_OBJS += sound.o
     MX_OBJS += sound_syms.o
  endif
endif

ifeq ($(CONFIG_MIDI),y)
  L_OBJS += midibuf.o
  LX_OBJS += midi_synth.o
endif

#ifeq ($(CONFIG_AUDIO),y)
#L_OBJS += dmabuf.o
#endif

ifeq ($(CONFIG_YM3812),y)
LX_OBJS += adlib_card.o opl3.o
else
  ifeq ($(CONFIG_YM3812),m)
  MX_OBJS += adlib_card.o opl3.o
  endif
endif

ifeq ($(CONFIG_PAS),y)
L_OBJS += pas2.o
else
  ifeq ($(CONFIG_PAS),m)
  M_OBJS += pas2.o
  endif
endif

ifeq ($(CONFIG_GUS),y)
L_OBJS += gus.o
else
  ifeq ($(CONFIG_GUS),m)
    M_OBJS += gus.o
  endif
  ifeq ($(CONFIG_GUSMAX),m)
    ifneq ($(CONFIG_MSS),y)
      CONFIG_MSS = m
    endif
  endif
endif

ifeq ($(CONFIG_SB),y)
L_OBJS += sb_audio.o sb_common.o sb_midi.o sb_mixer.o
LX_OBJS += sb_card.o
CONFIG_UART401 = y
else
  ifeq ($(CONFIG_SB),m)
  M_OBJS += sb.o
  MIX_OBJS += sb_card.o
  ifneq ($(CONFIG_UART401),y)
    CONFIG_UART401 = m
  endif
  endif
endif

ifeq ($(CONFIG_MSS),y)
LX_OBJS += ad1848.o
else
  ifeq ($(CONFIG_MSS),m)
  MX_OBJS += ad1848.o
  endif
endif

ifeq ($(CONFIG_ADLIB),y)
LX_OBJS += adlib_card.o
else
  ifeq ($(CONFIG_ADLIB),m)
  MX_OBJS += adlib_card.o
  endif
endif

ifeq ($(CONFIG_MPU401),y)
LX_OBJS += mpu401.o
else
  ifeq ($(CONFIG_MPU401),m)
  MX_OBJS += mpu401.o
  else
    ifeq ($(CONFIG_MPU_EMU),y)
    LX_OBJS += mpu401.o
    else
      ifeq ($(CONFIG_MPU_EMU),m)
      MX_OBJS += mpu401.o
      endif
    endif
  endif
endif

ifeq ($(CONFIG_UART401),y)
LX_OBJS += uart401.o
else
  ifeq ($(CONFIG_UART401),m)
  MX_OBJS += uart401.o
  endif
endif

ifeq ($(CONFIG_UART6850),y)
LX_OBJS += uart6850.o
else
  ifeq ($(CONFIG_UART6850),m)
  MX_OBJS += uart6850.o
  endif
endif

ifeq ($(CONFIG_PSS),y)
L_OBJS += pss.o
else
  ifeq ($(CONFIG_PSS),m)
  M_OBJS += pss.o
  endif
endif

ifeq ($(CONFIG_SSCAPE),y)
L_OBJS += sscape.o
else
  ifeq ($(CONFIG_SSCAPE),m)
  M_OBJS += sscape.o
  endif
endif

ifeq ($(CONFIG_TRIX),y)
L_OBJS += trix.o
else
  ifeq ($(CONFIG_TRIX),m)
  M_OBJS += trix.o
  endif
endif

ifeq ($(CONFIG_MAD16),y)
L_OBJS += mad16.o
else
  ifeq ($(CONFIG_MAD16),m)
  M_OBJS += mad16.o sb.o uart401.o
  MX_OBJS += sb_card.o ad1848.o
  endif
endif

ifeq ($(CONFIG_CS4232),y)
LX_OBJS += cs4232.o
else
  ifeq ($(CONFIG_CS4232),m)
  MX_OBJS += cs4232.o
  endif
endif

ifeq ($(CONFIG_MAUI),y)
L_OBJS += maui.o
else
  ifeq ($(CONFIG_MAUI),m)
    M_OBJS += maui.o
  endif
endif

ifeq ($(CONFIG_SOFTOSS),y)
L_OBJS += softoss.o softoss_rs.o
else
   ifeq ($(CONFIG_SOFTOSS),m)
      M_OBJS += softoss2.o
   endif
endif

ifeq ($(CONFIG_OPL3SA1),y)
L_OBJS += opl3sa.o
LX_OBJS += ad1848.o
else
  ifeq ($(CONFIG_OPL3SA1),m)
  M_OBJS += opl3sa.o
  MX_OBJS += ad1848.o
  endif
endif

ifeq ($(CONFIG_VMIDI),y)
L_OBJS += v_midi.o
else
   ifeq ($(CONFIG_VMIDI),m)
      M_OBJS += v_midi.o
   endif
endif

include $(TOPDIR)/Rules.make

softoss2.o:	softoss.o softoss_rs.o
	ld -r -o softoss2.o softoss.o softoss_rs.o

pas2.o:       pas2_card.o pas2_midi.o pas2_mixer.o pas2_pcm.o
	ld -r -o pas2.o pas2_card.o pas2_midi.o pas2_mixer.o pas2_pcm.o

sb.o: sb_audio.o sb_card.o sb_common.o sb_midi.o sb_mixer.o
	ld -r -o sb.o sb_audio.o sb_card.o sb_common.o sb_midi.o sb_mixer.o

lowlevel/lowlevel.o:
	cd lowlevel; make

sound.o: soundcard.o dev_table.o sound_switch.o audio.o dmabuf.o sequencer.o sys_timer.o sound_timer.o lowlevel/lowlevel.o midi_synth.o midibuf.o sound_firmware.o
	ld -r -o sound.o soundcard.o dev_table.o sound_switch.o audio.o dmabuf.o \
	       sequencer.o sys_timer.o sound_timer.o lowlevel/lowlevel.o \
	       midi_synth.o midibuf.o sound_firmware.o
	rm sound_syms.o

gus.o: gus_card.o gus_midi.o gus_vol.o gus_wave.o ics2101.o
	ld -r -o gus.o gus_card.o gus_midi.o gus_vol.o gus_wave.o ics2101.o

kernelconfig: setup
	rm -f configure
	$(HOSTCC) -o configure configure.c
	./configure fixedlocal > local.h
	./configure fixeddefines > .defines
	@echo \#define SOUND_CONFIG_DATE \"`date`\" >> local.h
	@echo \#define SOUND_CONFIG_BY \"`whoami`\" >> local.h
#	@echo \#define SOUND_CONFIG_HOST \"`hostname`\" >> local.h 2>/dev/null
#	@echo \#define SOUND_CONFIG_DOMAIN \"`hostname -d`\" >> local.h 2>/dev/null
	@echo \#define SOUND_UNAME_A \"`uname -a`\" >> local.h

configure: configure.c
	$(HOSTCC) -o configure configure.c
	@cat .blurb

setup:
	@echo Compiling Sound Driver v $(VERSION) for Linux

mkscript:

config: configure
	@$(MAKE) setup
	@./configure > local.h
	@echo \#define SOUND_CONFIG_DATE \"`date`\" >> local.h
	@echo \#define SOUND_CONFIG_BY \"`whoami`\" >> local.h
#	@echo \#define SOUND_CONFIG_HOST \"`hostname`\" >> local.h 2>/dev/null
#	@echo \#define SOUND_CONFIG_DOMAIN \"`hostname -d`\" >> local.h 2>/dev/null
	@echo \#define SOUND_UNAME_A \"`uname -a`\" >> local.h

endif
