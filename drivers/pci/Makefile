#
# Makefile for the PCI bus specific drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now inherited from the
# parent makefile.
#

L_TARGET := pci.a

# Nasty trick as we need to link files with no references from the outside.
O_TARGET := pci_core.o
L_OBJS	 := pci_core.o
O_OBJS   := quirks.o
OX_OBJS   := pci.o

ifdef CONFIG_PROC_FS
O_OBJS   += proc.o
endif

L_OBJS   += compat.o names.o setup-res.o setup-bus.o setup-irq.o

ifndef CONFIG_X86
L_OBJS   += syscall.o
endif

include $(TOPDIR)/Rules.make

names.o: names.c devlist.h classlist.h

devlist.h classlist.h: pci.ids gen-devlist
	./gen-devlist <pci.ids

gen-devlist: gen-devlist.c
	$(HOSTCC) $(HOSTCFLAGS) -o gen-devlist gen-devlist.c
