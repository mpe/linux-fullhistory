#
# Makefile for Linux arch/m68k/hp300 source directory
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := hp300.o
O_OBJS	 := ksyms.o config.o ints.o time.o reboot.o

ifdef CONFIG_VT
O_OBJS += hil.o
endif

include $(TOPDIR)/Rules.make
