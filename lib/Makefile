#
# Makefile for some libs needed in the kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

L_TARGET := lib.a
L_OBJS   := errno.o ctype.o string.o vsprintf.o brlock.o
LX_OBJS  := cmdline.o

ifneq ($(CONFIG_HAVE_DEC_LOCK),y) 
  L_OBJS += dec_and_lock.o
endif

include $(TOPDIR)/Rules.make
