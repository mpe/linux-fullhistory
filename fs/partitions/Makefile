#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := partitions.o
O_OBJS   := check.o

ifeq ($(CONFIG_ACORN_PARTITION),y)
O_OBJS += acorn.o
endif

ifeq ($(CONFIG_AMIGA_PARTITION),y)
O_OBJS += amiga.o
endif

ifeq ($(CONFIG_ATARI_PARTITION),y)
O_OBJS += atari.o
endif

ifeq ($(CONFIG_MAC_PARTITION),y)
O_OBJS += mac.o
endif

ifeq ($(CONFIG_MSDOS_PARTITION),y)
O_OBJS += msdos.o
endif

ifeq ($(CONFIG_OSF_PARTITION),y)
O_OBJS += osf.o
endif

ifeq ($(CONFIG_SGI_PARTITION),y)
O_OBJS += sgi.o
endif

ifeq ($(CONFIG_SUN_PARTITION),y)
O_OBJS += sun.o
endif

ifeq ($(CONFIG_ULTRIX_PARTITION),y)
O_OBJS += ultrix.o
endif

include $(TOPDIR)/Rules.make
