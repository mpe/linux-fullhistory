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

.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

#
# Note : at this point, these files are compiled on all systems. 
# In the future, some of these should be built conditionally.
#

OBJS := ll_rw_blk.o ramdisk.o genhd.o 
SRCS := ll_rw_blk.c ramdisk.c genhd.c 
BLOCK_MODULE_OBJS =

ifdef CONFIG_BLK_DEV_FD
OBJS := $(OBJS) floppy.o
SRCS := $(SRCS) floppy.c
endif

ifdef CONFIG_CDU31A
OBJS := $(OBJS) cdu31a.o
SRCS := $(SRCS) cdu31a.c
endif

ifdef CONFIG_MCD
OBJS := $(OBJS) mcd.o
SRCS := $(SRCS) mcd.c
endif

ifdef CONFIG_AZTCD
OBJS := $(OBJS) aztcd.o
SRCS := $(SRCS) aztcd.c
else
BLOCK_MODULE_OBJS := $(BLOCK_MODULE_OBJS) aztcd.o
endif

ifdef CONFIG_SBPCD
OBJS := $(OBJS) sbpcd.o
SRCS := $(SRCS) sbpcd.c
else
BLOCK_MODULE_OBJS := $(BLOCK_MODULE_OBJS) sbpcd.o
endif #CONFIG_SBPCD

ifdef CONFIG_SBPCD2
OBJS := $(OBJS) sbpcd2.o
SRCS := $(SRCS) sbpcd2.c
endif #CONFIG_SBPCD2

ifdef CONFIG_SBPCD3
OBJS := $(OBJS) sbpcd3.o
SRCS := $(SRCS) sbpcd3.c
endif #CONFIG_SBPCD3

ifdef CONFIG_SBPCD4
OBJS := $(OBJS) sbpcd4.o
SRCS := $(SRCS) sbpcd4.c
endif #CONFIG_SBPCD4

ifdef CONFIG_BLK_DEV_HD
OBJS := $(OBJS) hd.o
SRCS := $(SRCS) hd.c
endif

ifdef CONFIG_BLK_DEV_IDE
OBJS := ide.o $(OBJS)
SRCS := ide.c $(SRCS)
endif

ifdef CONFIG_BLK_DEV_XD
OBJS := $(OBJS) xd.o
SRCS := $(SRCS) xd.c
endif

ifdef CONFIG_CDU535
OBJS := $(OBJS) sonycd535.o
SRCS := $(SRCS) sonycd535.c
else
BLOCK_MODULE_OBJS := $(BLOCK_MODULE_OBJS) sonycd535.o
endif

all: block.a

block.a: $(OBJS)
	rm -f block.a
	$(AR) rcs block.a $(OBJS)
	sync

dep:
	$(CPP) -M $(SRCS) > .depend
ifdef BLOCK_MODULE_OBJS
	$(CPP) -M -DMODULE $(BLOCK_MODULE_OBJS:.o=.c) >> .depend
endif

modules: $(BLOCK_MODULE_OBJS)
	echo $(BLOCK_MODULE_OBJS) > ../../modules/BLOCK_MODULES
	(cd ../../modules;for i in $(BLOCK_MODULE_OBJS); do ln -sf ../drivers/block/$$i .; done)

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
