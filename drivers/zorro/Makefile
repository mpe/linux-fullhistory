#
# Makefile for the Zorro bus specific drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now inherited from the
# parent makefile.
#

L_TARGET := zorro.a

# Nasty trick as we need to link files with no references from the outside.
O_TARGET := zorro_core.o
L_OBJS   := zorro_core.o
OX_OBJS   := zorro.o

ifdef CONFIG_PROC_FS
O_OBJS   += proc.o
endif

L_OBJS   += names.o

include $(TOPDIR)/Rules.make

names.o: names.c devlist.h

devlist.h: zorro.ids gen-devlist
	./gen-devlist <zorro.ids

gen-devlist: gen-devlist.c
	$(HOSTCC) $(HOSTCFLAGS) -o gen-devlist gen-devlist.c
