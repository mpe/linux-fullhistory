# File: drivers/hamradio/Makefile
#
# Makefile for the Linux AX.25 and HFMODEM device drivers.
#
# 19971130 	Moved the amateur radio related network drivers from 
#		drivers/net/ to drivers/hamradio for easier maintainance.
#               Joerg Reuter DL1BKE <jreuter@poboxes.com>


SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

L_TARGET := hamradio.a
L_OBJS   := 
M_OBJS   :=
MOD_LIST_NAME := HAM_MODULES

# Need these to keep track of whether the hdlc module should
# really go in the kernel or a module.
CONFIG_HDLCDRV_BUILTIN :=
CONFIG_HDLCDRV_MODULE  :=

ifeq ($(CONFIG_DMASCC),y)
L_OBJS += dmascc.o
else
  ifeq ($(CONFIG_DMASCC),m)
  M_OBJS += dmascc.o
  endif
endif

ifeq ($(CONFIG_SCC),y)
L_OBJS += scc.o
else
  ifeq ($(CONFIG_SCC),m)
  M_OBJS += scc.o
  endif
endif

ifeq ($(CONFIG_MKISS),y)
L_OBJS += mkiss.o
else
  ifeq ($(CONFIG_MKISS),m)
  M_OBJS += mkiss.o
  endif
endif

ifeq ($(CONFIG_6PACK),y)
L_OBJS += 6pack.o
else
  ifeq ($(CONFIG_6PACK),m)
  M_OBJS += 6pack.o
  endif
endif

ifeq ($(CONFIG_PI),y)
L_OBJS += pi2.o
else
  ifeq ($(CONFIG_PI),m)
  M_OBJS += pi2.o
  endif
endif

ifeq ($(CONFIG_PT),y)
L_OBJS += pt.o
else
  ifeq ($(CONFIG_PT),m)
  M_OBJS += pt.o
  endif
endif

ifeq ($(CONFIG_BPQETHER),y)
L_OBJS += bpqether.o
else
  ifeq ($(CONFIG_BPQETHER),m)
  M_OBJS += bpqether.o
  endif
endif

ifeq ($(CONFIG_BAYCOM_SER_FDX),y)
L_OBJS += baycom_ser_fdx.o
CONFIG_HDLCDRV_BUILTIN = y
else
  ifeq ($(CONFIG_BAYCOM_SER_FDX),m)
  CONFIG_HDLCDRV_MODULE = y
  M_OBJS += baycom_ser_fdx.o
  endif
endif

ifeq ($(CONFIG_BAYCOM_SER_HDX),y)
L_OBJS += baycom_ser_hdx.o
CONFIG_HDLCDRV_BUILTIN = y
else
  ifeq ($(CONFIG_BAYCOM_SER_HDX),m)
  CONFIG_HDLCDRV_MODULE = y
  M_OBJS += baycom_ser_hdx.o
  endif
endif

ifeq ($(CONFIG_BAYCOM_PAR),y)
L_OBJS += baycom_par.o
CONFIG_HDLCDRV_BUILTIN = y
else
  ifeq ($(CONFIG_BAYCOM_PAR),m)
  CONFIG_HDLCDRV_MODULE = y
  M_OBJS += baycom_par.o
  endif
endif

ifeq ($(CONFIG_BAYCOM_EPP),y)
L_OBJS += baycom_epp.o
CONFIG_HDLCDRV_BUILTIN = y
else
  ifeq ($(CONFIG_BAYCOM_EPP),m)
  CONFIG_HDLCDRV_MODULE = y
  M_OBJS += baycom_epp.o
  endif
endif

ifeq ($(CONFIG_SOUNDMODEM),y)
ALL_SUB_DIRS += soundmodem
SUB_DIRS += soundmodem
L_OBJS += soundmodem/soundmodem.o
CONFIG_HDLCDRV_BUILTIN = y
else
  ifeq ($(CONFIG_SOUNDMODEM),m)
  CONFIG_HDLCDRV_MODULE = y
  ALL_SUB_DIRS += soundmodem
  MOD_SUB_DIRS += soundmodem
  endif
endif

# If anything built-in uses the hdlcdrv, then build it into the kernel also.
# If not, but a module uses it, build as a module.
ifdef CONFIG_HDLCDRV_BUILTIN
LX_OBJS += hdlcdrv.o
else
  ifdef CONFIG_HDLCDRV_MODULE
  MX_OBJS += hdlcdrv.o
  endif
endif

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s
