#
# Makefile for the Linux IrDA protocol layer.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

ALL_SUB_DIRS := irlan ircomm irlpt compressors
SUB_DIRS :=
MOD_SUB_DIRS :=

O_TARGET := irda.o
O_OBJS	 := irmod.o iriap.o iriap_event.o irlmp.o irlmp_event.o irlmp_frame.o \
            irlap.o irlap_event.o irlap_frame.o timer.o qos.o irqueue.o \
            irttp.o irda_device.o irias_object.o crc.o wrapper.o af_irda.o
MOD_LIST_NAME := IRDA_MODULES
M_OBJS   := $(O_TARGET)

ifeq ($(CONFIG_IRDA_COMPRESSION),y)
O_OBJS += irlap_comp.o
endif

ifeq ($(CONFIG_PROC_FS),y)
O_OBJS += irproc.o
endif

ifeq ($(CONFIG_SYSCTL),y)
O_OBJS += irsysctl.o
endif

ifeq ($(CONFIG_IRLAN),y)
SUB_DIRS += irlan
O_OBJS += irlan/irlan.o
else
  ifeq ($(CONFIG_IRLAN),m)
  MOD_SUB_DIRS += irlan
  endif
endif

ifeq ($(CONFIG_IRLPT),y)
SUB_DIRS += irlpt
O_OBJS += irlpt/irlpt.o
else
  ifeq ($(CONFIG_IRLPT),m)
  MOD_IN_SUB_DIRS += irlpt
  endif
endif

ifeq ($(CONFIG_IROBEX),y)
SUB_DIRS += irobex
O_OBJS += irobex/irobex.o
else
  ifeq ($(CONFIG_IROBEX),m)
  MOD_SUB_DIRS += irobex
  endif
endif

ifeq ($(CONFIG_IRDA_COMPRESSION),y)
SUB_DIRS += compressors
MOD_IN_SUB_DIRS += compressors
endif

ifeq ($(CONFIG_IRDA_DEFLATE),y)
O_OBJS += compressors/irda_deflate.o
else
  ifeq ($(CONFIG_IRDA_DEFLATE),m)
  MOD_TO_LIST += irda_deflate.o
  endif
endif

ifeq ($(CONFIG_IRCOMM),y)
SUB_DIRS += ircomm
O_OBJS += ircomm/ircomm_n_vtd.o
else
  ifeq ($(CONFIG_IRCOMM),m)
  MOD_SUB_DIRS += ircomm
  endif
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .




