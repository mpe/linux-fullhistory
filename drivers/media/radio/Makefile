#
# Makefile for the kernel character device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

O_OBJS		:=
OX_OBJS		:=
M_OBJS		:=
MX_OBJS		:=

# Object file lists.

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

O_TARGET := radio.o

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs     :=	

list-multi	:=	

obj-$(CONFIG_RADIO_AZTECH) += radio-aztech.o
obj-$(CONFIG_RADIO_RTRACK2) += radio-rtrack2.o
obj-$(CONFIG_RADIO_SF16FMI) += radio-sf16fmi.o
obj-$(CONFIG_RADIO_CADET) += radio-cadet.o
obj-$(CONFIG_RADIO_TYPHOON) += radio-typhoon.o
obj-$(CONFIG_RADIO_TERRATEC) += radio-terratec.o
obj-$(CONFIG_RADIO_RTRACK) += radio-aimslab.o
obj-$(CONFIG_RADIO_ZOLTRIX) += radio-zoltrix.o
obj-$(CONFIG_RADIO_MIROPCM20) += radio-miropcm20.o
obj-$(CONFIG_RADIO_GEMTEK) += radio-gemtek.o
obj-$(CONFIG_RADIO_TRUST) += radio-trust.o
obj-$(CONFIG_RADIO_MAESTRO) += radio-maestro.o

# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.

multi-y         := $(filter $(list-multi), $(obj-y))
multi-m         := $(filter $(list-multi), $(obj-m))
int-y           := $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m           := $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Files that are both resident and modular: remove from modular.

obj-m           := $(filter-out $(obj-y), $(obj-m))
int-m           := $(filter-out $(int-y), $(int-m))

# Take multi-part drivers out of obj-y and put components in.

obj-y           := $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.

O_OBJS          := $(filter-out $(export-objs), $(obj-y))
OX_OBJS         := $(filter     $(export-objs), $(obj-y))
M_OBJS          := $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS         := $(sort $(filter     $(export-objs), $(obj-m)))
MI_OBJS		:= $(sort $(filter-out $(export-objs), $(int-m)))
MIX_OBJS	:= $(sort $(filter     $(export-objs), $(int-m)))

include $(TOPDIR)/Rules.make
