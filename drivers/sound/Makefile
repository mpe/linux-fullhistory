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

else

include Defines

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
  L_OBJS += soundcard.o dev_table.o sequencer.o sys_timer.o sound_timer.o lowlevel/lowlevel.o midi_synth.o midibuf.o sound_firmware.o audio.o dmabuf.o
else
  ifeq ($(CONFIG_SOUND),m)
     M_OBJS += sound.o
     MIX_OBJS += sound_syms.o
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
  ifeq ($(CONFIG_GUSMAX),y)
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

sound.o: soundcard.o dev_table.o audio.o dmabuf.o sequencer.o sys_timer.o sound_timer.o lowlevel/lowlevel.o midi_synth.o midibuf.o sound_firmware.o
	ld -r -o sound.o soundcard.o dev_table.o audio.o dmabuf.o \
	       sequencer.o sys_timer.o sound_timer.o lowlevel/lowlevel.o \
	       midi_synth.o midibuf.o sound_firmware.o

gus.o: gus_card.o gus_midi.o gus_vol.o gus_wave.o ics2101.o
	ld -r -o gus.o gus_card.o gus_midi.o gus_vol.o gus_wave.o ics2101.o

# Firmware files that need translation
#
# The translated files are protected by a file that keeps track
# of what name was used to build them.  If the name changes, they
# will be forced to be remade.

bin2hex: bin2hex.c
	$(HOSTCC) -o bin2hex bin2hex.c

hex2hex: hex2hex.c
	$(HOSTCC) -o hex2hex hex2hex.c

ifeq ($(MAUI_HAVE_BOOT),y)

MAUI_BOOT_FILE := $(patsubst "%", %, $(MAUI_BOOT_FILE))

maui.o: maui_boot.h

maui_boot.h: $(MAUI_BOOT_FILE) bin2hex
	bin2hex maui_os < "$(MAUI_BOOT_FILE)" > $@
	@ ( \
	    echo 'ifeq ($(strip $(MAUI_BOOT_FILE)),$$(strip $$(MAUI_BOOT_FILE)))'; \
	    echo 'FILES_BOOT_UP_TO_DATE += $@'; \
	    echo 'endif' \
	) > .$@.boot

endif

ifeq ($(PSS_HAVE_BOOT),y)

PSS_BOOT_FILE := $(patsubst "%", %, $(PSS_BOOT_FILE))

pss.o: pss_boot.h

pss_boot.h: $(PSS_BOOT_FILE) bin2hex
	bin2hex pss_synth < "$(PSS_BOOT_FILE)" > $@
	@ ( \
	    echo 'ifeq ($(strip $(PSS_BOOT_FILE)),$$(strip $$(PSS_BOOT_FILE)))'; \
	    echo 'FILES_BOOT_UP_TO_DATE += $@'; \
	    echo 'endif' \
	) > .$@.boot

endif

ifeq ($(TRIX_HAVE_BOOT),y)

TRIX_BOOT_FILE := $(patsubst "%", %, $(TRIX_BOOT_FILE))

trix.o: trix_boot.h

trix_boot.h: $(TRIX_BOOT_FILE) hex2hex
	hex2hex trix_boot < "$(TRIX_BOOT_FILE)" > $@
	@ ( \
	    echo 'ifeq ($(strip $(TRIX_BOOT_FILE)),$$(strip $$(strip $$(TRIX_BOOT_FILE)))'; \
	    echo 'FILES_BOOT_UP_TO_DATE += $@'; \
	    echo 'endif' \
	) > .$@.boot

endif

# Find boot files whose source file names have changed and force rebuild.

FILES_BOOT_UP_TO_DATE :=

FILES_BOOT_EXIST := $(wildcard .*.boot)
ifneq ($(FILES_BOOT_EXIST),)
include $(FILES_BOOT_EXIST)
endif

FILES_BOOT_CHANGED := $(strip \
    $(filter-out $(FILES_BOOT_UP_TO_DATE), \
	maui_boot.h pss_boot.h trix_boot.h))

ifneq ($(FILES_BOOT_CHANGED),)
$(FILES_BOOT_CHANGED): dummy
endif
endif
