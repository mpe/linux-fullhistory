#
# Makefile for the kernel block device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now inherited from the
# parent makefile.
#

#
# Note : at this point, these files are compiled on all systems.
# In the future, some of these should be built conditionally.
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS) paride


L_TARGET := block.a
L_OBJS   := genhd.o elevator.o
M_OBJS   :=
MOD_LIST_NAME := BLOCK_MODULES
LX_OBJS := ll_rw_blk.o blkpg.o
MX_OBJS :=

ifeq ($(CONFIG_MAC_FLOPPY),y)
L_OBJS += swim3.o
endif

ifeq ($(CONFIG_BLK_DEV_FD),y)
L_OBJS += floppy.o
else
  ifeq ($(CONFIG_BLK_DEV_FD),m)
  M_OBJS += floppy.o
  endif
endif

ifeq ($(CONFIG_AMIGA_FLOPPY),y)
  L_OBJS += amiflop.o
else
  ifeq ($(CONFIG_AMIGA_FLOPPY),m)
    M_OBJS += amiflop.o
  endif
endif

ifeq ($(CONFIG_ATARI_FLOPPY),y)
  L_OBJS += ataflop.o
else
  ifeq ($(CONFIG_ATARI_FLOPPY),m)
  M_OBJS += ataflop.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_SWIM_IOP),y)
  L_OBJS += swim_iop.o
endif

ifeq ($(CONFIG_ATARI_ACSI),y)
  LX_OBJS += acsi.o
else
  ifeq ($(CONFIG_ATARI_ACSI),m)
  MX_OBJS += acsi.o
  endif
endif

ifeq ($(CONFIG_ATARI_SLM),y)
  L_OBJS += acsi_slm.o
else
  ifeq ($(CONFIG_ATARI_SLM),m)
  M_OBJS += acsi_slm.o
  endif
endif

ifeq ($(CONFIG_AMIGA_Z2RAM),y)
L_OBJS += z2ram.o
else
  ifeq ($(CONFIG_AMIGA_Z2RAM),m)
  M_OBJS += z2ram.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_RAM),y)
L_OBJS += rd.o
else
  ifeq ($(CONFIG_BLK_DEV_RAM),m)
  M_OBJS += rd.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_LOOP),y)
LX_OBJS += loop.o
else
  ifeq ($(CONFIG_BLK_DEV_LOOP),m)
  MX_OBJS += loop.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_PS2),y)
L_OBJS += ps2esdi.o
else
  ifeq ($(CONFIG_BLK_DEV_PS2),m)
  M_OBJS += ps2esdi.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_XD),y)
L_OBJS += xd.o
else
  ifeq ($(CONFIG_BLK_DEV_XD),m)
  M_OBJS += xd.o
  endif
endif

ifeq ($(CONFIG_BLK_CPQ_DA),y)
L_OBJS += cpqarray.o
else
  ifeq ($(CONFIG_BLK_CPQ_DA),m)
  M_OBJS += cpqarray.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_DAC960),y)
LX_OBJS += DAC960.o
else
  ifeq ($(CONFIG_BLK_DEV_DAC960),m)
  MX_OBJS += DAC960.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_LVM),y)
L_OBJS += lvm.o lvm-snap.o
else
   ifeq ($(CONFIG_BLK_DEV_LVM),m)
   M_OBJS += lvm-mod.o
   endif
endif

ifeq ($(CONFIG_BLK_DEV_MD),y)
LX_OBJS += md.o

ifeq ($(CONFIG_MD_LINEAR),y)
L_OBJS += linear.o
else
  ifeq ($(CONFIG_MD_LINEAR),m)
  M_OBJS += linear.o
  endif
endif

ifeq ($(CONFIG_MD_STRIPED),y)
L_OBJS += raid0.o
else
  ifeq ($(CONFIG_MD_STRIPED),m)
  M_OBJS += raid0.o
  endif
endif

ifeq ($(CONFIG_MD_MIRRORING),y)
L_OBJS += raid1.o
else
  ifeq ($(CONFIG_MD_MIRRORING),m)
  M_OBJS += raid1.o
  endif
endif

ifeq ($(CONFIG_MD_RAID5),y)
L_OBJS += raid5.o
else
  ifeq ($(CONFIG_MD_RAID5),m)
  M_OBJS += raid5.o
  endif
endif

ifeq ($(CONFIG_MD_TRANSLUCENT),y)
L_OBJS += translucent.o
else
  ifeq ($(CONFIG_MD_TRANSLUCENT),m)
  M_OBJS += translucent.o
  endif
endif

ifeq ($(CONFIG_MD_HSM),y)
L_OBJS += hsm.o
else
  ifeq ($(CONFIG_MD_HSM),m)
  M_OBJS += hsm.o
  endif
endif

endif

ifeq ($(CONFIG_BLK_DEV_NBD),y)
L_OBJS += nbd.o
else
  ifeq ($(CONFIG_BLK_DEV_NBD),m)
  M_OBJS += nbd.o
  endif
endif

ifeq ($(CONFIG_PARIDE),y)
SUB_DIRS	+= paride
MOD_IN_SUB_DIRS	+= paride
else
  ifeq ($(CONFIG_PARIDE),m)
  MOD_IN_SUB_DIRS	+= paride
  endif
endif

include $(TOPDIR)/Rules.make

lvm-mod.o:	lvm.o lvm-snap.o
	$(LD) -r -o $@ lvm.o lvm-snap.o
