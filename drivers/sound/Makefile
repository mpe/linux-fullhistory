# Makefile for the Linux sound card driver
#
# 18 Apr 1998, Michael Elizabeth Chastain, <mailto:mec@shout.net>
# Rewritten to use lists instead of if-statements.



# My subdirectories.

SUB_DIRS	:= 
MOD_SUB_DIRS	:=
MOD_IN_SUB_DIRS	:=
ALL_SUB_DIRS	:= $(SUB_DIRS) lowlevel

ifeq ($(CONFIG_LOWLEVEL_SOUND),y)
    SUB_DIRS		+= lowlevel
    MOD_IN_SUB_DIRS	+= lowlevel
endif



# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs	:=  ad1848.o audio_syms.o midi_syms.o mpu401.o \
		    msnd.o opl3.o sb_card.o sequencer_syms.o \
		    sound_core.o sound_syms.o uart401.o ad1816.o



# Object file lists.

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=



# Each configuration option enables a list of files.

obj-$(CONFIG_SOUND)		+= soundcore.o
obj-$(CONFIG_DMASOUND)		+= dmasound.o
obj-$(CONFIG_SOUND_OSS)		+= sound.o
obj-$(CONFIG_SOUND_ADLIB)	+= adlib_card.o opl3.o
obj-$(CONFIG_SOUND_CS4232)	+= cs4232.o ad1848.o 

# In theory, there's probably no reason to include the uart401 code
# to support a WaveFront card's CS4232 module. However, it makes
# reconfiguring things require a recompile, so just leave this
# here and try not to worry about the extra uart401 module.

obj-$(CONFIG_SOUND_CS4232)	+= uart401.o
obj-$(CONFIG_SOUND_GUS)		+= gus.o ad1848.o
obj-$(CONFIG_SOUND_MAD16)	+= mad16.o ad1848.o sb.o uart401.o
obj-$(CONFIG_SOUND_MAUI)	+= maui.o mpu401.o
obj-$(CONFIG_SOUND_MPU401)	+= mpu401.o
obj-$(CONFIG_SOUND_MSNDCLAS)	+= msnd.o msnd_classic.o
obj-$(CONFIG_SOUND_MSNDPIN)	+= msnd.o msnd_pinnacle.o
obj-$(CONFIG_SOUND_MSS)		+= ad1848.o
obj-$(CONFIG_SOUND_OPL3SA1)	+= opl3sa.o ad1848.o uart401.o
obj-$(CONFIG_SOUND_OPL3SA2)	+= opl3sa2.o ad1848.o uart401.o mpu401.o
obj-$(CONFIG_SOUND_PAS)		+= pas2.o sb.o uart401.o
obj-$(CONFIG_SOUND_PSS)		+= pss.o ad1848.o mpu401.o
obj-$(CONFIG_SOUND_SB)		+= sb.o uart401.o
obj-$(CONFIG_SOUND_SOFTOSS)	+= softoss2.o
obj-$(CONFIG_SOUND_SGALAXY)	+= sgalaxy.o
obj-$(CONFIG_SOUND_AD1816)      += ad1816.o
obj-$(CONFIG_SOUND_SSCAPE)	+= sscape.o ad1848.o mpu401.o
obj-$(CONFIG_SOUND_TRIX)	+= trix.o ad1848.o sb.o uart401.o
obj-$(CONFIG_SOUND_UART6850)	+= uart6850.o
obj-$(CONFIG_SOUND_VMIDI)	+= v_midi.o
obj-$(CONFIG_SOUND_YM3812)	+= adlib_card.o opl3.o
obj-$(CONFIG_SOUND_VIDC)	+= vidc_mod.o
obj-$(CONFIG_SOUND_WAVEARTIST)	+= waveartist.o
obj-$(CONFIG_SOUND_WAVEFRONT)   += wavefront.o

#jnx
obj-$(CONFIG_SOUND_ES1370)	+= es1370.o
obj-$(CONFIG_SOUND_ES1371)	+= es1371.o
obj-$(CONFIG_SOUND_SONICVIBES)	+= sonicvibes.o

# Declare multi-part drivers.

list-multi	:= sound.o gus.o pas2.o sb.o softoss2.o vidc_mod.o \
    soundcore.o wavefront.o

sound-objs	:= 							\
    dev_table.o soundcard.o sound_syms.o		\
    audio.o audio_syms.o dmabuf.o					\
    midi_syms.o midi_synth.o midibuf.o					\
    sequencer.o sequencer_syms.o sound_timer.o sys_timer.o

soundcore-objs	:= sound_core.o sound_firmware.o

gus-objs	:= gus_card.o gus_midi.o gus_vol.o gus_wave.o ics2101.o
pas2-objs	:= pas2_card.o pas2_midi.o pas2_mixer.o pas2_pcm.o
sb-objs		:= sb_audio.o sb_card.o sb_common.o sb_midi.o sb_mixer.o	\
			   sb_ess.o
softoss2-objs	:= softoss.o softoss_rs.o
vidc_mod-objs	:= vidc.o vidc_audio.o vidc_fill.o vidc_mixer.o vidc_synth.o
wavefront-objs  := wavfront.o wf_midi.o yss225.o


# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.

multi-y		:= $(filter $(list-multi), $(obj-y))
multi-m		:= $(filter $(list-multi), $(obj-m))
int-y		:= $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m		:= $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))



# Files that are both resident and modular: remove from modular.

obj-m		:= $(filter-out $(obj-y), $(obj-m))
int-m		:= $(filter-out $(int-y), $(int-m))



# Set flags for secondary drivers.
# I have to do this before I reduce obj-y to components.

EXTRA_CFLAGS	:= $(sort					\
    $(patsubst ad1848.o,  -DCONFIG_SOUND_AD1848,		\
    $(patsubst mpu401.o,  -DCONFIG_SOUND_MPU_EMU,		\
    $(patsubst sb.o,      -DCONFIG_SOUND_SBDSP,			\
    $(patsubst uart401.o, -DCONFIG_SOUND_UART401,		\
	$(filter ad1848.o mpu401.o sb.o uart401.o, $(obj-y))	\
    )))))



# Take multi-part drivers out of obj-y and put components in.

obj-y		:= $(filter-out $(list-multi), $(obj-y)) $(int-y)



# Translate to Rules.make lists.

L_TARGET	:= sound.a
# This is a nice idea but needs depmod altering
#MOD_LIST_NAME	:= SOUND_MODULES

L_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-y)))
LX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-y)))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))
MI_OBJS		:= $(sort $(filter-out $(export-objs), $(int-m)))
MIX_OBJS	:= $(sort $(filter     $(export-objs), $(int-m)))

ifeq ($(CONFIG_LOWLEVEL_SOUND),y)
    L_OBJS	+= lowlevel/lowlevel.o
endif

include $(TOPDIR)/Rules.make



# Link rules for multi-part drivers.

sound.o: $(sound-objs)
	$(LD) -r -o $@ $(sound-objs)

soundcore.o: $(soundcore-objs)
	$(LD) -r -o $@ $(soundcore-objs)

gus.o: $(gus-objs)
	$(LD) -r -o $@ $(gus-objs)

pas2.o: $(pas2-objs)
	$(LD) -r -o $@ $(pas2-objs)

sb.o: $(sb-objs)
	$(LD) -r -o $@ $(sb-objs)

softoss2.o: $(softoss2-objs)
	$(LD) -r -o $@ $(softoss2-objs)

vidc_mod.o: $(vidc_mod-objs)
	$(LD) -r -o $@ $(vidc_mod-objs)

wavefront.o: $(wavefront-objs)
	$(LD) -r -o $@ $(wavefront-objs)

# Firmware files that need translation
#
# The translated files are protected by a file that keeps track
# of what name was used to build them.  If the name changes, they
# will be forced to be remade.
#
# First make the utilities.

bin2hex: bin2hex.c
	$(HOSTCC) $(HOSTCFLAGS) -o bin2hex bin2hex.c

hex2hex: hex2hex.c
	$(HOSTCC) $(HOSTCFLAGS) -o hex2hex hex2hex.c




# Turtle Beach Maui / Tropez

maui.o: maui_boot.h

ifeq ($(CONFIG_MAUI_HAVE_BOOT),y)
    maui_boot.h: $(patsubst "%", %, $(CONFIG_MAUI_BOOT_FILE)) bin2hex
	./bin2hex -i maui_os < $(CONFIG_MAUI_BOOT_FILE) > $@
else
    maui_boot.h:
	(							\
	    echo 'static unsigned char * maui_os = NULL;';	\
	    echo 'static int maui_osLen = 0;';			\
	) > $@
endif
	@ ( \
	    echo 'ifeq ($(strip $(CONFIG_MAUI_HAVE_BOOT) $(CONFIG_MAUI_BOOT_FILE)),$$(strip $$(CONFIG_MAUI_HAVE_BOOT) $$(CONFIG_MAUI_BOOT_FILE)))'; \
	    echo 'FILES_BOOT_UP_TO_DATE += $@'; \
	    echo 'endif' \
	) > .$@.boot



# Turtle Beach MultiSound

ifeq ($(CONFIG_MSNDCLAS_HAVE_BOOT),y)
    msnd_classic.o: msndperm.c msndinit.c

    msndperm.c: $(patsubst "%", %, $(CONFIG_MSNDCLAS_PERM_FILE)) bin2hex
	./bin2hex msndperm < $(CONFIG_MSNDCLAS_PERM_FILE) > $@
	@ ( \
	    echo 'ifeq ($(strip $(CONFIG_MSNDCLAS_HAVE_BOOT) $(CONFIG_MSNDCLAS_PERM_FILE)),$$(strip $$(CONFIG_MSNDCLAS_HAVE_BOOT) $$(CONFIG_MSNDCLAS_PERM_FILE)))'; \
	    echo 'FILES_BOOT_UP_TO_DATE += $@'; \
	    echo 'endif' \
	) > .$@.boot

    msndinit.c: $(patsubst "%", %, $(CONFIG_MSNDCLAS_INIT_FILE)) bin2hex
	./bin2hex msndinit < $(CONFIG_MSNDCLAS_INIT_FILE) > $@
	@ ( \
	    echo 'ifeq ($(strip $(CONFIG_MSNDCLAS_HAVE_BOOT) $(CONFIG_MSNDCLAS_INIT_FILE)),$$(strip $$(CONFIG_MSNDCLAS_HAVE_BOOT) $$(CONFIG_MSNDCLAS_INIT_FILE)))'; \
	    echo 'FILES_BOOT_UP_TO_DATE += $@'; \
	    echo 'endif' \
	) > .$@.boot
endif

ifeq ($(CONFIG_MSNDPIN_HAVE_BOOT),y)
    msnd_pinnacle.o: pndsperm.c pndspini.c

    pndsperm.c: $(patsubst "%", %, $(CONFIG_MSNDPIN_PERM_FILE)) bin2hex
	./bin2hex pndsperm < $(CONFIG_MSNDPIN_PERM_FILE) > $@
	@ ( \
	    echo 'ifeq ($(strip $(CONFIG_MSNDPIN_HAVE_BOOT) $(CONFIG_MSNDPIN_PERM_FILE)),$$(strip $$(CONFIG_MSNDPIN_HAVE_BOOT) $$(CONFIG_MSNDPIN_PERM_FILE)))'; \
	    echo 'FILES_BOOT_UP_TO_DATE += $@'; \
	    echo 'endif' \
	) > .$@.boot

    pndspini.c: $(patsubst "%", %, $(CONFIG_MSNDPIN_INIT_FILE)) bin2hex
	./bin2hex pndspini < $(CONFIG_MSNDPIN_INIT_FILE) > $@
	@ ( \
	    echo 'ifeq ($(strip $(CONFIG_MSNDPIN_HAVE_BOOT) $(CONFIG_MSNDPIN_INIT_FILE)),$$(strip $$(CONFIG_MSNDPIN_HAVE_BOOT) $$(CONFIG_MSNDPIN_INIT_FILE)))'; \
	    echo 'FILES_BOOT_UP_TO_DATE += $@'; \
	    echo 'endif' \
	) > .$@.boot
endif



# PSS (ECHO-ADI2111)

pss.o: pss_boot.h

ifeq ($(CONFIG_PSS_HAVE_BOOT),y)
    pss_boot.h: $(patsubst "%", %, $(CONFIG_PSS_BOOT_FILE)) bin2hex
	./bin2hex pss_synth < $(CONFIG_PSS_BOOT_FILE) > $@
else
    pss_boot.h:
	(							\
	    echo 'static unsigned char * pss_synth = NULL;';	\
	    echo 'static int pss_synthLen = 0;';		\
	) > $@
endif
	@ ( \
	    echo 'ifeq ($(strip $(CONFIG_PSS_HAVE_BOOT) $(CONFIG_PSS_BOOT_FILE)),$$(strip $$(CONFIG_PSS_HAVE_BOOT) $$(CONFIG_PSS_BOOT_FILE)))'; \
	    echo 'FILES_BOOT_UP_TO_DATE += $@'; \
	    echo 'endif' \
	) > .$@.boot



# MediaTrix AudioTrix Pro

trix.o: trix_boot.h

ifeq ($(CONFIG_TRIX_HAVE_BOOT),y)
    trix_boot.h: $(patsubst "%", %, $(CONFIG_TRIX_BOOT_FILE)) hex2hex
	./hex2hex -i trix_boot < $(CONFIG_TRIX_BOOT_FILE) > $@
else
    trix_boot.h:
	(							\
	    echo 'static unsigned char * trix_boot = NULL;';	\
	    echo 'static int trix_boot_len = 0;';		\
	) > $@
endif
	@ ( \
	    echo 'ifeq ($(strip $(CONFIG_TRIX_HAVE_BOOT) $(CONFIG_TRIX_BOOT_FILE)),$$(strip $$(CONFIG_TRIX_HAVE_BOOT) $$(CONFIG_TRIX_BOOT_FILE)))'; \
	    echo 'FILES_BOOT_UP_TO_DATE += $@'; \
	    echo 'endif' \
	) > .$@.boot



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
