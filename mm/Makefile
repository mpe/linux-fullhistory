#
# Makefile for the linux memory manager.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := mm.o
O_OBJS	 := memory.o mmap.o filemap.o mprotect.o mlock.o mremap.o \
	    vmalloc.o slab.o \
	    swap.o vmscan.o page_io.o page_alloc.o swap_state.o swapfile.o

include $(TOPDIR)/Rules.make
