#
# Makefile for the FPU instruction emulation.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := math-emu.o
O_OBJS   := math.o
CFLAGS += -I. -I$(TOPDIR)/include/math-emu -w

ifeq ($(CONFIG_MATHEMU),m)
M_OBJS   := $(O_TARGET)
endif

include $(TOPDIR)/Rules.make
