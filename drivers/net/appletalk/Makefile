#
# Makefile for drivers/net/appletalk
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
export-objs :=

obj-$(CONFIG_IPDDP) += ipddp.o
obj-$(CONFIG_COPS) += cops.o
obj-$(CONFIG_LTPC) += ltpc.o

O_TARGET := appletalk.o
O_OBJS   := $(filter-out $(export-objs), $(obj-y))
OX_OBJS  := $(filter     $(export-objs), $(obj-y))
M_OBJS   := $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS  := $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make

