#
# Makefile for the kernel software RAID and LVM drivers.
#

O_TARGET	:= mddev.o
SUB_DIRS	:=
ALL_SUB_DIRS	:=
MOD_SUB_DIRS	:=

export-objs	:= md.o xor.o
list-multi	:= lvm-mod.o
lvm-mod-objs	:= lvm.o lvm-snap.o

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

# NOTE: xor.o must link *before* md.o so that auto-detect
# of raid5 arrays works (and doesn't Oops).  Fortunately
# they are both export-objs, so setting the order here
# works.
obj-$(CONFIG_MD_LINEAR)		+= linear.o
obj-$(CONFIG_MD_RAID0)		+= raid0.o
obj-$(CONFIG_MD_RAID1)		+= raid1.o
obj-$(CONFIG_MD_RAID5)		+= raid5.o xor.o
obj-$(CONFIG_BLK_DEV_MD)	+= md.o
obj-$(CONFIG_BLK_DEV_LVM)	+= lvm-mod.o

# Translate to Rules.make lists.
O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS		:= $(filter     $(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter      $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make

lvm-mod.o: $(lvm-mod-objs)
	$(LD) -r -o $@ $(lvm-mod-objs)
