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
L_OBJS   := ll_rw_blk.o ramdisk.o genhd.o 
M_OBJS   :=
MOD_LIST_NAME := BLOCK_MODULES

ifeq ($(CONFIG_BLK_DEV_FD),y)
L_OBJS += floppy.o
else
  ifeq ($(CONFIG_BLK_DEV_FD),m)
  M_OBJS += floppy.o
  endif
endif

ifeq ($(CONFIG_AZTCD),y)
L_OBJS += aztcd.o
else
  ifeq ($(CONFIG_AZTCD),m)
  M_OBJS += aztcd.o
  endif
endif #CONFIG_AZTCD

ifeq ($(CONFIG_CDU31A),y)
L_OBJS += cdu31a.o
else
  ifeq ($(CONFIG_CDU31A),m)
  M_OBJS += cdu31a.o
  endif
endif #CONFIG_CDU31A

ifeq ($(CONFIG_MCD),y)
L_OBJS += mcd.o
else
  ifeq ($(CONFIG_MCD),m)
  M_OBJS += mcd.o
  endif
endif #CONFIG_MCD

ifeq ($(CONFIG_MCDX),y)
L_OBJS += mcdx.o
else
  ifeq ($(CONFIG_MCDX),m)
  M_OBJS += mcdx.o
  endif
endif #CONFIG_MCDX

ifeq ($(CONFIG_SBPCD),y)
L_OBJS += sbpcd.o
else
  ifeq ($(CONFIG_SBPCD),m)
  M_OBJS += sbpcd.o
  endif
endif #CONFIG_SBPCD

ifeq ($(CONFIG_SBPCD2),y)
L_OBJS += sbpcd2.o
endif #CONFIG_SBPCD2

ifeq ($(CONFIG_SBPCD3),y)
L_OBJS += sbpcd3.o
endif #CONFIG_SBPCD3

ifeq ($(CONFIG_SBPCD4),y)
L_OBJS += sbpcd4.o
endif #CONFIG_SBPCD4

ifeq ($(CONFIG_CDU535),y)
L_OBJS += sonycd535.o
else
  ifeq ($(CONFIG_CDU535),m)
  M_OBJS += sonycd535.o
  endif
endif #CONFIG_CDU535

ifeq ($(CONFIG_GSCD),y)
L_OBJS += gscd.o
else
  ifeq ($(CONFIG_GSCD),m)
  M_OBJS += gscd.o
  endif
endif #CONFIG_GSCD

ifeq ($(CONFIG_CM206),y)
L_OBJS += cm206.o
else
  ifeq ($(CONFIG_CM206),m)
  M_OBJS += cm206.o
  endif
endif #CONFIG_CM206

ifeq ($(CONFIG_OPTCD),y)
L_OBJS += optcd.o
else
  ifeq ($(CONFIG_OPTCD),m)
  M_OBJS += optcd.o
  endif
endif #CONFIG_OPTCD

ifeq ($(CONFIG_SJCD),y)
L_OBJS += sjcd.o
else
  ifeq ($(CONFIG_SJCD),m)
  M_OBJS += sjcd.o
  endif
endif #CONFIG_SJCD

ifeq ($(CONFIG_BLK_DEV_HD),y)
L_OBJS += hd.o
endif

ifeq ($(CONFIG_BLK_DEV_IDE),y)
L_OBJS += ide.o
endif

ifeq ($(CONFIG_BLK_DEV_TRITON),y)
L_OBJS += triton.o
endif

ifeq ($(CONFIG_BLK_DEV_IDECD),y)
L_OBJS += ide-cd.o
endif

ifeq ($(CONFIG_BLK_DEV_XD),y)
L_OBJS += xd.o
endif

include $(TOPDIR)/Rules.make
