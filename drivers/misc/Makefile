#
# Makefile for misc devices that really don't fit anywhere else.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

O_TARGET := misc.o
M_OBJS   :=
O_OBJS   :=
OX_OBJS  :=

ifeq ($(CONFIG_ACPI),y)
  O_OBJS += acpi.o
else
  ifeq ($(CONFIG_ACPI),m)
    M_OBJS += acpi.o
  endif
endif

include $(TOPDIR)/Rules.make

fastdep:
