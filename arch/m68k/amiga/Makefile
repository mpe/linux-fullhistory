#
# Makefile for Linux arch/m68k/amiga source directory
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := amiga.o
O_OBJS   := config.o amiints.o cia.o chipram.o amisound.o
OX_OBJS  := amiga_ksyms.o

ifdef CONFIG_AMIGA_PCMCIA
O_OBJS := $(O_OBJS) pcmcia.o
endif

include $(TOPDIR)/Rules.make
