# Makefile for the Linux video drivers.
# 5 Aug 1999, James Simmons, <mailto:jsimmons@edgeglobal.com>
# Rewritten to use lists instead of if-statements.

SUB_DIRS        :=
MOD_SUB_DIRS    := $(SUB_DIRS)
MOD_IN_SUB_DIRS :=
ALL_SUB_DIRS    := $(SUB_DIRS)

O_TARGET := matrox.o
O_OBJS   :=
M_OBJS   :=
# This is a nice idea but needs depmod altering
# MOD_LIST_NAME := VIDEO_MODULES

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs    :=  matroxfb_base.o matroxfb_accel.o matroxfb_DAC1064.o matroxfb_Ti3026.o matroxfb_misc.o

# Object file lists.
obj-y          :=
obj-m          :=
obj-n          :=
obj-           :=

# Each configuration option enables a list of files.

obj-$(CONFIG_FB_MATROX)           += matroxfb_base.o matroxfb_accel.o matroxfb_DAC1064.o matroxfb_Ti3026.o matroxfb_misc.o
obj-$(CONFIG_FB_MATROX_I2C)       += i2c-matroxfb.o
obj-$(CONFIG_FB_MATROX_MAVEN)     += matroxfb_maven.o matroxfb_crtc2.o

# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.

multi-y         := $(filter $(list-multi), $(obj-y))
multi-m         := $(filter $(list-multi), $(obj-m))
int-y           := $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m           := $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Files that are both resident and modular: remove from modular.

obj-m          := $(filter-out $(obj-y), $(obj-m))
int-m          := $(filter-out $(int-y), $(int-m))

# Take multi-part drivers out of obj-y and put components in.

obj-y          := $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.

O_OBJS         := $(filter-out $(export-objs), $(obj-y))
OX_OBJS        := $(filter     $(export-objs), $(obj-y))
M_OBJS         := $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS        := $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s
