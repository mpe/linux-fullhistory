#
# Makefile for the kernel block device drivers.
#
# 12 June 2000, Christoph Hellwig <schch@pe.tu-clausthal.de>
# Rewritten to use lists instead of if-statements.
# 
# Note : at this point, these files are compiled on all systems.
# In the future, some of these should be built conditionally.
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS) paride

O_TARGET := block.o
MOD_LIST_NAME	:= BLOCK_MODULES


export-objs	:= ll_rw_blk.o blkpg.o loop.o DAC960.o md.o xor.o
list-multi	:= lvm-mod.o
lvm-mod-objs	:= lvm.o lvm-snap.o

obj-y	:= ll_rw_blk.o blkpg.o genhd.o elevator.o

obj-$(CONFIG_MAC_FLOPPY)	+= swim3.o
obj-$(CONFIG_BLK_DEV_FD)	+= floppy.o
obj-$(CONFIG_AMIGA_FLOPPY)	+= amiflop.o
obj-$(CONFIG_ATARI_FLOPPY)	+= ataflop.o
obj-$(CONFIG_BLK_DEV_SWIM_IOP)	+= swim_iop.o
obj-$(CONFIG_ATARI_ACSI)	+= acsi.o
obj-$(CONFIG_ATARI_SLM)		+= acsi_slm.o
obj-$(CONFIG_AMIGA_Z2RAM)	+= z2ram.o
obj-$(CONFIG_BLK_DEV_RAM)	+= rd.o
obj-$(CONFIG_BLK_DEV_LOOP)	+= loop.o
obj-$(CONFIG_BLK_DEV_PS2)	+= ps2esdi.o
obj-$(CONFIG_BLK_DEV_XD)	+= xd.o
obj-$(CONFIG_BLK_CPQ_DA)	+= cpqarray.o
obj-$(CONFIG_BLK_DEV_DAC960)	+= DAC960.o
obj-$(CONFIG_BLK_DEV_LVM)	+= lvm-mod.o

obj-$(CONFIG_BLK_DEV_MD)	+= md.o
obj-$(CONFIG_MD_LINEAR)		+= linear.o
obj-$(CONFIG_MD_RAID0)		+= raid0.o
obj-$(CONFIG_MD_RAID1)		+= raid1.o

ifeq ($(CONFIG_MD_RAID5),m)
	obj-y += xor.o
else
	obj-$(CONFIG_MD_RAID5)	+= xor.o
endif
obj-$(CONFIG_MD_RAID5)		+= raid5.o

obj-$(CONFIG_BLK_DEV_NBD)	+= nbd.o

ifeq ($(CONFIG_PARIDE),y)
SUB_DIRS	+= paride
MOD_IN_SUB_DIRS	+= paride
else
  ifeq ($(CONFIG_PARIDE),m)
  MOD_IN_SUB_DIRS	+= paride
  endif
endif


# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.
multi-y		:= $(filter $(list-multi), $(obj-y))
multi-m		:= $(filter $(list-multi), $(obj-m))
int-y		:= $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m		:= $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Take multi-part drivers out of obj-y and put components in.
obj-y		:= $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.
O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS		:= $(filter	$(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter	$(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make

lvm-mod.o: $(lvm-mod-objs)
	$(LD) -r -o $@ $(lvm-mod-objs)
