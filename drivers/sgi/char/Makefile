#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

O_TARGET := sgichar.o
O_OBJS   := graphics.o streamable.o newport.o cons_newport.o sgicons.o \
	vga_font.o rrm.o shmiq.o usema.o

ifeq ($(CONFIG_SGI_SERIAL),y)
  O_OBJS += sgiserial.o
endif

include $(TOPDIR)/Rules.make
