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

ifdef CONFIG_BLK_DEV_FD
L_OBJS += floppy.o
else
M_OBJS += floppy.o
endif

ifdef CONFIG_AZTCD
L_OBJS += aztcd.o
else
M_OBJS += aztcd.o
endif #CONFIG_AZTCD

ifdef CONFIG_CDU31A
L_OBJS += cdu31a.o
else
M_OBJS += cdu31a.o
endif #CONFIG_CDU31A

ifdef CONFIG_MCD
L_OBJS += mcd.o
else
M_OBJS += mcd.o
endif #CONFIG_MCD

ifdef CONFIG_MCDX
L_OBJS += mcdx.o
else
M_OBJS += mcdx.o
endif #CONFIG_MCDX

ifdef CONFIG_SBPCD
L_OBJS += sbpcd.o
else
M_OBJS += sbpcd.o
endif #CONFIG_SBPCD

ifdef CONFIG_SBPCD2
L_OBJS += sbpcd2.o
endif #CONFIG_SBPCD2

ifdef CONFIG_SBPCD3
L_OBJS += sbpcd3.o
endif #CONFIG_SBPCD3

ifdef CONFIG_SBPCD4
L_OBJS += sbpcd4.o
endif #CONFIG_SBPCD4

ifdef CONFIG_CDU535
L_OBJS += sonycd535.o
else
M_OBJS += sonycd535.o
endif #CONFIG_CDU535

ifdef CONFIG_GSCD
L_OBJS += gscd.o
else
M_OBJS += gscd.o
endif #CONFIG_GSCD

ifdef CONFIG_CM206
L_OBJS += cm206.o
else
M_OBJS += cm206.o
endif #CONFIG_CM206

ifdef CONFIG_OPTCD
L_OBJS += optcd.o
else
M_OBJS += optcd.o
endif #CONFIG_OPTCD

ifdef CONFIG_SJCD
L_OBJS += sjcd.o
#else
#M_OBJS += sjcd.o
endif #CONFIG_SJCD

ifdef CONFIG_BLK_DEV_HD
L_OBJS += hd.o
endif

ifdef CONFIG_BLK_DEV_IDE
L_OBJS += ide.o
endif

ifdef CONFIG_BLK_DEV_TRITON
L_OBJS += triton.o
endif

ifdef CONFIG_BLK_DEV_IDECD
L_OBJS += ide-cd.o
endif

ifdef CONFIG_BLK_DEV_XD
L_OBJS += xd.o
endif

include $(TOPDIR)/Rules.make
