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

# Note: link order is important.  All raid personalities
# and xor.o must come before md.o, as they each initialise 
# themselves, and md.o may use the personalities when it 
# auto-initialised.
# The use of MIX_OBJS allows link order to be maintained even
# though some are export-objs and some aren't.

obj-$(CONFIG_MD_LINEAR)		+= linear.o
obj-$(CONFIG_MD_RAID0)		+= raid0.o
obj-$(CONFIG_MD_RAID1)		+= raid1.o
obj-$(CONFIG_MD_RAID5)		+= raid5.o xor.o
obj-$(CONFIG_BLK_DEV_MD)	+= md.o
obj-$(CONFIG_BLK_DEV_LVM)	+= lvm-mod.o

# Translate to Rules.make lists.
active-objs	:= $(sort $(obj-y) $(obj-m))

O_OBJS		:= $(obj-y)
M_OBJS		:= $(obj-m)
MIX_OBJS	:= $(filter $(export-objs), $(active-objs))

include $(TOPDIR)/Rules.make

lvm-mod.o: $(lvm-mod-objs)
	$(LD) -r -o $@ $(lvm-mod-objs)
