#
# Makefile for the Linux IRDA compression protocols
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET :=
O_OBJS	 := 
MOD_LIST_NAME := IRDA_MODULES
M_OBJS   := 

ifeq ($(CONFIG_IRDA_DEFLATE),y)
O_OBJS += irda_deflate.o
else
  ifeq ($(CONFIG_IRDA_DEFLATE),m)
  M_OBJS += irda_deflate.o
  endif
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
