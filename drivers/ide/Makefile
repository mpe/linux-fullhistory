#
# Makefile for the kernel ata, atapi, and ide block device drivers.
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
ALL_SUB_DIRS := $(SUB_DIRS)

L_TARGET := ide.a
L_OBJS   := ide-geometry.o
M_OBJS   :=
MOD_LIST_NAME := IDE_MODULES
LX_OBJS :=
MX_OBJS :=

ifeq ($(CONFIG_BLK_DEV_AEC6210),y)
IDE_OBJS += aec6210.o
endif

ifeq ($(CONFIG_BLK_DEV_ALI14XX),y)
IDE_OBJS += ali14xx.o
endif

ifeq ($(CONFIG_BLK_DEV_ALI15X3),y)
IDE_OBJS += alim15x3.o
endif

ifeq ($(CONFIG_BLK_DEV_AMD7409),y)
IDE_OBJS += amd7409.o
endif

ifeq ($(CONFIG_BLK_DEV_BUDDHA),y)
IDE_OBJS += buddha.o
endif

ifeq ($(CONFIG_BLK_DEV_CMD640),y)
IDE_OBJS += cmd640.o
endif

ifeq ($(CONFIG_BLK_DEV_CMD64X),y)
IDE_OBJS += cmd64x.o
endif

ifeq ($(CONFIG_BLK_DEV_CS5530),y)
IDE_OBJS += cs5530.o
endif

ifeq ($(CONFIG_BLK_DEV_CY82C693),y)
IDE_OBJS += cy82c693.o
endif

ifeq ($(CONFIG_BLK_DEV_DTC2278),y)
IDE_OBJS += dtc2278.o
endif

ifeq ($(CONFIG_BLK_DEV_FALCON_IDE),y)
IDE_OBJS += falconide.o
endif

ifeq ($(CONFIG_BLK_DEV_GAYLE),y)
IDE_OBJS += gayle.o
endif

ifeq ($(CONFIG_BLK_DEV_Q40IDE),y)
IDE_OBJS += q40ide.o
endif

ifeq ($(CONFIG_BLK_DEV_HD),y)
L_OBJS += hd.o
endif

ifeq ($(CONFIG_BLK_DEV_HPT34X),y)
IDE_OBJS += hpt34x.o
endif

ifeq ($(CONFIG_BLK_DEV_HPT366),y)
IDE_OBJS += hpt366.o
endif

ifeq ($(CONFIG_BLK_DEV_HT6560B),y)
IDE_OBJS += ht6560b.o
endif

ifeq ($(CONFIG_BLK_DEV_IDE_ICSIDE),y)
IDE_OBJS += icside.o
endif

ifeq ($(CONFIG_BLK_DEV_IDEDMA),y)
IDE_OBJS += ide-dma.o
endif

ifeq ($(CONFIG_BLK_DEV_IDEPCI),y)
IDE_OBJS += ide-pci.o
endif

ifeq ($(CONFIG_BLK_DEV_ISAPNP),y)
IDE_OBJS += ide-pnp.o
endif

ifeq ($(CONFIG_BLK_DEV_IDE_PMAC),y)
IDE_OBJS += ide-pmac.o
endif

ifeq ($(CONFIG_BLK_DEV_MAC_IDE),y)
IDE_OBJS += macide.o
endif

ifeq ($(CONFIG_BLK_DEV_NS87415),y)
IDE_OBJS += ns87415.o
endif

ifeq ($(CONFIG_BLK_DEV_OPTI621),y)
IDE_OBJS += opti621.o
endif

ifeq ($(CONFIG_BLK_DEV_PDC202XX),y)
IDE_OBJS += pdc202xx.o
endif

ifeq ($(CONFIG_BLK_DEV_PDC4030),y)
IDE_OBJS += pdc4030.o
endif

ifeq ($(CONFIG_BLK_DEV_PIIX),y)
IDE_OBJS += piix.o
endif

ifeq ($(CONFIG_BLK_DEV_QD6580),y)
IDE_OBJS += qd6580.o
endif

ifeq ($(CONFIG_BLK_DEV_IDE_RAPIDE),y)
IDE_OBJS += rapide.o
endif

ifeq ($(CONFIG_BLK_DEV_RZ1000),y)
IDE_OBJS += rz1000.o
endif

ifeq ($(CONFIG_BLK_DEV_SIS5513),y)
IDE_OBJS += sis5513.o
endif

ifeq ($(CONFIG_BLK_DEV_SL82C105),y)
IDE_OBJS += sl82c105.o
endif

ifeq ($(CONFIG_BLK_DEV_TRM290),y)
IDE_OBJS += trm290.o
endif

ifeq ($(CONFIG_BLK_DEV_UMC8672),y)
IDE_OBJS += umc8672.o
endif

ifeq ($(CONFIG_BLK_DEV_VIA82CXXX),y)
IDE_OBJS += via82cxxx.o
endif

### if CONFIG_BLK_DEV_IDE is n, IDE_OBJS will be ignored

ifeq ($(CONFIG_PROC_FS),y)
IDE_OBJS += ide-proc.o
endif
  
###Collect

ifeq ($(CONFIG_BLK_DEV_IDE),y)
  LX_OBJS += ide.o ide-features.o
  L_OBJS += ide-probe.o $(IDE_OBJS)
else
  ifeq ($(CONFIG_BLK_DEV_IDE),m)
  MIX_OBJS += ide.o ide-features.o $(IDE_OBJS)
  M_OBJS += ide-mod.o ide-probe-mod.o
  endif
endif

############

ifeq ($(CONFIG_BLK_DEV_IDECS),y)
L_OBJS += ide-cs.o
else
  ifeq ($(CONFIG_BLK_DEV_IDECS),m)
  M_OBJS += ide-cs.o
  endif
endif

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

include $(TOPDIR)/Rules.make

ide-mod.o: ide.o ide-features.o $(IDE_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ ide.o ide-features.o $(IDE_OBJS)

ide-probe-mod.o: ide-probe.o ide-geometry.o
	$(LD) $(LD_RFLAG) -r -o $@ ide-probe.o ide-geometry.o
