# File: drivers/irda/Makefile
#
# Makefile for the Linux IrDA infrared port device drivers.
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

L_TARGET := irda_drivers.a
L_OBJS   := 
M_OBJS   :=
MOD_LIST_NAME := IRDA_MODULES

ifeq ($(CONFIG_IRTTY_SIR),y)
L_OBJS += irtty.o
else
  ifeq ($(CONFIG_IRTTY_SIR),m)
  M_OBJS += irtty.o
  endif
endif

ifeq ($(CONFIG_NSC_FIR),y)
L_OBJS += pc87108.o
else
  ifeq ($(CONFIG_NSC_FIR),m)
  M_OBJS += pc87108.o
  endif
endif

ifeq ($(CONFIG_WINBOND_FIR),y)
L_OBJS += w83977af_ir.o
else
  ifeq ($(CONFIG_WINBOND_FIR),m)
  M_OBJS += w83977af_ir.o
  endif
endif

ifeq ($(CONFIG_SHARP_FIR),y)
L_OBJS += uircc.o irport.o
else
  ifeq ($(CONFIG_SHARP_FIR),m)
  M_OBJS += uircc.o irport.o
  endif
endif

ifeq ($(CONFIG_ESI_DONGLE),y)
L_OBJS += esi.o
else
  ifeq ($(CONFIG_ESI_DONGLE),m)
  M_OBJS += esi.o
  endif
endif

ifeq ($(CONFIG_TEKRAM_DONGLE),y)
L_OBJS += tekram.o
else
  ifeq ($(CONFIG_TEKRAM_DONGLE),m)
  M_OBJS += tekram.o
  endif
endif

ifeq ($(CONFIG_ACTISYS_DONGLE),y)
L_OBJS += actisys.o
else
  ifeq ($(CONFIG_ACTISYS_DONGLE),m)
  M_OBJS += actisys.o
  endif
endif

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s






