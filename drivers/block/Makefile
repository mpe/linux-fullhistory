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
L_OBJS   := genhd.o
M_OBJS   :=
MOD_LIST_NAME := BLOCK_MODULES
LX_OBJS := ll_rw_blk.o
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

ifeq ($(CONFIG_BLK_DEV_HD),y)
L_OBJS += hd.o
endif

ifeq ($(CONFIG_BLK_DEV_RZ1000),y)
IDE_OBJS += rz1000.o
endif

ifeq ($(CONFIG_BLK_DEV_CMD640),y)
IDE_OBJS += cmd640.o
endif

ifeq ($(CONFIG_BLK_DEV_IDE_PMAC),y)
IDE_OBJS += ide-pmac.o
endif

ifeq ($(CONFIG_BLK_DEV_IDEPCI),y)
IDE_OBJS += ide-pci.o
endif

ifeq ($(CONFIG_BLK_DEV_IDEDMA),y)
IDE_OBJS += ide-dma.o
endif

ifeq ($(CONFIG_BLK_DEV_DTC2278),y)
IDE_OBJS += dtc2278.o
endif

ifeq ($(CONFIG_BLK_DEV_HT6560B),y)
IDE_OBJS += ht6560b.o
endif

ifeq ($(CONFIG_BLK_DEV_QD6580),y)
IDE_OBJS += qd6580.o
endif

ifeq ($(CONFIG_BLK_DEV_UMC8672),y)
IDE_OBJS += umc8672.o
endif

ifeq ($(CONFIG_BLK_DEV_ALI14XX),y)
IDE_OBJS += ali14xx.o
endif

ifeq ($(CONFIG_BLK_DEV_PDC4030),y)
IDE_OBJS += pdc4030.o
endif

ifeq ($(CONFIG_BLK_DEV_TRM290),y)
IDE_OBJS += trm290.o
endif

ifeq ($(CONFIG_BLK_DEV_OPTI621),y)
IDE_OBJS += opti621.o
endif

ifeq ($(CONFIG_BLK_DEV_NS87415),y)
IDE_OBJS += ns87415.o
endif

ifeq ($(CONFIG_BLK_DEV_VIA82C586),y)
IDE_OBJS += via82c586.o
endif

ifeq ($(CONFIG_BLK_DEV_CMD646),y)
IDE_OBJS += cmd646.o
endif

ifeq ($(CONFIG_BLK_DEV_SL82C105),y)
IDE_OBJS += sl82c105.o
endif

### if CONFIG_BLK_DEV_IDE is n, IDE_OBJS will be ignored

ifeq ($(CONFIG_PROC_FS),y)
IDE_OBJS += ide-proc.o
endif
  
###Collect

ifeq ($(CONFIG_BLK_DEV_IDE),y)
  LX_OBJS += ide.o
  L_OBJS += ide-probe.o $(IDE_OBJS)
else
  ifeq ($(CONFIG_BLK_DEV_IDE),m)
  MIX_OBJS += ide.o $(IDE_OBJS)
  M_OBJS += ide-mod.o ide-probe.o
  endif
endif

############

ifeq ($(CONFIG_BLK_DEV_IDEDISK),y)
L_OBJS += ide-disk.o
else
  ifeq ($(CONFIG_BLK_DEV_IDEDISK),m)
  M_OBJS += ide-disk.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_IDECD),y)
L_OBJS += ide-cd.o
else
  ifeq ($(CONFIG_BLK_DEV_IDECD),m)
  M_OBJS += ide-cd.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_IDETAPE),y)
L_OBJS += ide-tape.o
else
  ifeq ($(CONFIG_BLK_DEV_IDETAPE),m)
  M_OBJS += ide-tape.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_IDEFLOPPY),y)
L_OBJS += ide-floppy.o
else
  ifeq ($(CONFIG_BLK_DEV_IDEFLOPPY),m)
  M_OBJS += ide-floppy.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_PS2),y)
L_OBJS += ps2esdi.o
endif

ifeq ($(CONFIG_BLK_DEV_XD),y)
L_OBJS += xd.o
else
  ifeq ($(CONFIG_BLK_DEV_XD),m)
  M_OBJS += xd.o
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

ide-mod.o: ide.o $(IDE_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ ide.o $(IDE_OBJS)
