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


L_TARGET := block.a
L_OBJS   := ll_rw_blk.o genhd.o 
M_OBJS   :=
MOD_LIST_NAME := BLOCK_MODULES
LX_OBJS :=

ifeq ($(CONFIG_BLK_DEV_FD),y)
L_OBJS += floppy.o
else
  ifeq ($(CONFIG_BLK_DEV_FD),m)
  M_OBJS += floppy.o
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
L_OBJS += loop.o
else
  ifeq ($(CONFIG_BLK_DEV_LOOP),m)
  M_OBJS += loop.o
  endif
endif

ifeq ($(CONFIG_BLK_DEV_HD),y)
L_OBJS += hd.o
endif

ifeq ($(CONFIG_BLK_DEV_IDE),y)
L_OBJS += ide.o
endif

ifeq ($(CONFIG_BLK_DEV_RZ1000),y)
L_OBJS += rz1000.o
endif

ifeq ($(CONFIG_BLK_DEV_CMD640),y)
L_OBJS += cmd640.o
endif

ifeq ($(CONFIG_BLK_DEV_TRITON),y)
L_OBJS += triton.o
endif

ifeq ($(CONFIG_BLK_DEV_DTC2278),y)
L_OBJS += dtc2278.o
endif

ifeq ($(CONFIG_BLK_DEV_HT6560B),y)
L_OBJS += ht6560b.o
endif

ifeq ($(CONFIG_BLK_DEV_QD6580),y)
L_OBJS += qd6580.o
endif

ifeq ($(CONFIG_BLK_DEV_UMC8672),y)
L_OBJS += umc8672.o
endif

ifeq ($(CONFIG_BLK_DEV_ALI14XX),y)
L_OBJS += ali14xx.o
endif

ifeq ($(CONFIG_BLK_DEV_PROMISE),y)
L_OBJS += promise.o
endif

ifeq ($(CONFIG_BLK_DEV_IDECD),y)
L_OBJS += ide-cd.o
endif

ifeq ($(CONFIG_BLK_DEV_IDETAPE),y)
L_OBJS += ide-tape.o
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

#ifeq ($(CONFIG_MD_RAID1),y)
#L_OBJS += raid1.o
#else
#  ifeq ($(CONFIG_MD_SUPPORT_RAID1),y)
#    ifeq ($(CONFIG_MD_RAID1),m)
#    M_OBJS += raid1.o
#    endif
#  endif
#endif
#
#ifeq ($(CONFIG_MD_RAID5),y)
#L_OBJS += raid5.o
#else
#  ifeq ($(CONFIG_MD_SUPPORT_RAID5),y)
#    ifeq ($(CONFIG_MD_RAID5),m)
#    M_OBJS += raid5.o
#    endif
#  endif
#endif

endif

include $(TOPDIR)/Rules.make
