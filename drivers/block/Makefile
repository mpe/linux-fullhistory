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
L_OBJS   := ll_rw_blk.o rd.o genhd.o 
M_OBJS   :=
MOD_LIST_NAME := BLOCK_MODULES

ifeq ($(CONFIG_BLK_DEV_FD),y)
L_OBJS += floppy.o
else
  ifeq ($(CONFIG_BLK_DEV_FD),m)
  M_OBJS += floppy.o
  endif
endif

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

ifeq ($(CONFIG_BLK_DEV_IDETAPE),y)
L_OBJS += ide-tape.o
endif

ifeq ($(CONFIG_BLK_DEV_XD),y)
L_OBJS += xd.o
endif

include $(TOPDIR)/Rules.make
