#
# Makefile for Linux arch/m68k/atari source directory
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

EXTRA_CFLAGS := -Wa,-m68030

O_TARGET := atari.o
O_OBJS	 := config.o atakeyb.o ataints.o \
            atapart.o stdma.o atasound.o joystick.o stram.o atafb.o
OX_OBJS = ksyms.o

include $(TOPDIR)/Rules.make
