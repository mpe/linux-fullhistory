# Makefile for the Linux low-level sound card drivers.
#
# 11 Feb 1998, Michael Elizabeth Chastain, <mailto:mec@shout.net>
# Rewritten to use lists instead of if statements.

export-objs			:= soundlow.o

list-y				:=
list-m				:=
list-n				:=
list-				:=

obj-$(CONFIG_SOUND_OSS)		+= soundlow.o
obj-$(CONFIG_ACI_MIXER)		+= aci.o
obj-$(CONFIG_AEDSP16)		+= aedsp16.o
obj-$(CONFIG_AWE32_SYNTH)	+= awe_wave.o

O_TARGET	:= lowlevel.o
O_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-y)))
OX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-y)))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make
