#
# Makefile for the Linux/MIPS-specific parts of the memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := mm.o
O_OBJS	 := extable.o init.o fault.o r4xx0.o r2300.o r6000.o tfp.o \
            andes.o loadmmu.o

ifdef CONFIG_SGI
O_OBJS   += umap.o
endif

include $(TOPDIR)/Rules.make
