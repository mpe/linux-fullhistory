#
# Makefile for the kernel pcmcia subsystem (c/o David Hinds)
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..

SUB_DIRS     :=
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

O_TARGET := pcmcia.o

O_OBJS   := i82365.o tcic.o cistpl.o rsrc_mgr.o bulkmem.o
OX_OBJS  := ds.o cs.o

ifeq ($(CONFIG_CARDBUS),y)
  O_OBJS += cardbus.o
else
  ifeq ($(CONFIG_CARDBUS),m)
    MX_OBJS += cardbus.o
  endif
endif

include $(TOPDIR)/Rules.make
