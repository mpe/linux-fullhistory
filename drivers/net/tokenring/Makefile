#
# Makefile for drivers/net/tokenring
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

SUB_DIRS     :=
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

obj-y   :=
obj-n   :=
obj-m   :=
obj-    :=
export-objs := tms380tr.o

obj-$(CONFIG_IBMTR) 	+= ibmtr.o
obj-$(CONFIG_IBMOL) 	+= olympic.o
obj-$(CONFIG_IBMLS) 	+= lanstreamer.o
obj-$(CONFIG_TMS380TR) 	+= tms380tr.o
obj-$(CONFIG_ABYSS) 	+= abyss.o
obj-$(CONFIG_MADGEMC) 	+= madgemc.o
obj-$(CONFIG_TMSPCI) 	+= tmspci.o
obj-$(CONFIG_SMCTR) 	+= smctr.o

L_TARGET := tr.a
L_OBJS   := $(filter-out $(export-objs), $(obj-y))
LX_OBJS  := $(filter     $(export-objs), $(obj-y))
M_OBJS   := $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS  := $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make
