#
# Makefile for the Linux IEEE 1394 implementation
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile.
#

SUB_DIRS     :=
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

MOD_LIST_NAME := IEEE1394_MODULES

L_TARGET := ieee1394.a
L_OBJS   :=
LX_OBJS  :=
M_OBJS   :=
MX_OBJS  :=
MI_OBJS  :=
MIX_OBJS :=
O_OBJS   :=
OX_OBJS  :=

ifeq ($(CONFIG_IEEE1394),y)
L_OBJS  += ieee1394_core.o ieee1394_transactions.o hosts.o highlevel.o csr.o guid.o
LX_OBJS += ieee1394_syms.o
else
  ifeq ($(CONFIG_IEEE1394),m)
  M_OBJS  += ieee1394.o
  O_TARGET = ieee1394.o
  O_OBJS  += ieee1394_core.o ieee1394_transactions.o hosts.o highlevel.o csr.o guid.o
  OX_OBJS += ieee1394_syms.o
  endif
endif

ifeq ($(CONFIG_IEEE1394_PCILYNX),y)
L_OBJS += pcilynx.o
else
  ifeq ($(CONFIG_IEEE1394_PCILYNX),m)
  M_OBJS += pcilynx.o
  endif
endif

ifeq ($(CONFIG_IEEE1394_AIC5800),y)
L_OBJS += aic5800.o
else
  ifeq ($(CONFIG_IEEE1394_AIC5800),m)
  M_OBJS += aic5800.o
  endif
endif

ifeq ($(CONFIG_IEEE1394_OHCI1394),y)
LX_OBJS += ohci1394.o
else
  ifeq ($(CONFIG_IEEE1394_OHCI1394),m)
  MX_OBJS += ohci1394.o
  endif
endif

ifeq ($(CONFIG_IEEE1394_VIDEO1394),y)
L_OBJS += video1394.o
else
  ifeq ($(CONFIG_IEEE1394_VIDEO1394),m)
  M_OBJS += video1394.o
  endif
endif

ifeq ($(CONFIG_IEEE1394_RAWIO),y)
L_OBJS += raw1394.o
else
  ifeq ($(CONFIG_IEEE1394_RAWIO),m)
  M_OBJS += raw1394.o
  endif
endif


include $(TOPDIR)/Rules.make
