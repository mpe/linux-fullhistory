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

MOD_LIST_NAME := PCMCIA_MODULES

ifeq ($(CONFIG_PCMCIA),y)
  O_OBJS   := i82365.o tcic.o cistpl.o rsrc_mgr.o bulkmem.o
  OX_OBJS  := ds.o cs.o
  O_TARGET := pcmcia.o
  ifeq ($(CONFIG_CARDBUS),y)
    O_OBJS += cardbus.o
  endif
else
  ifeq ($(CONFIG_PCMCIA),m)
     M_OBJS   := i82365.o tcic.o pcmcia_core.o 
     MX_OBJS  := ds.o
     MIX_OBJS  := cs.o
     CORE_OBJS := cistpl.o rsrc_mgr.o bulkmem.o cs.o
     ifeq ($(CONFIG_CARDBUS),y)
	CORE_OBJS += cardbus.o
     endif
  endif
endif

include $(TOPDIR)/Rules.make

pcmcia_core.o:  $(CORE_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ $(CORE_OBJS)
