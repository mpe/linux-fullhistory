#
# Makefile for Linux arch/m68k/mac source directory
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

EXTRA_CFLAGS := -Wa,-m68020

O_TARGET := mac.o
O_OBJS	 := config.o bootparse.o macints.o via6522.o \
		mackeyb.o adb-bus.o macboing.o debug.o
OX_OBJS  := mac_ksyms.o

include $(TOPDIR)/Rules.make
