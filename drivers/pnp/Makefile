#
# Makefile for the kernel Plug-and-Play device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#
# Note 3! Plug and Play is the Borg.  We have assimilated some other
# drivers in the `char', `net' and `scsi' directories, but left them
# there to allay suspicion.

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

L_TARGET := pnp.a
MX_OBJS  :=
LX_OBJS  := 
MI_OBJS  :=
MIX_OBJS :=

ifeq ($(CONFIG_PNP_PARPORT),y)
  LX_OBJS += parport_probe.o
else
  ifeq ($(CONFIG_PNP_PARPORT),m)
    MX_OBJS += parport_probe.o
  endif
endif

include $(TOPDIR)/Rules.make
