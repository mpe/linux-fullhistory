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

# Nasty trick as nobody references pcisyms.o, but we still want it linked.
ifeq ($(CONFIG_MODULES),y)
O_TARGET = pci_syms.o
OX_OBJS  = pcisyms.o
O_OBJS   = pci.o
L_OBJS   := pci_syms.o
else
L_OBJS   := pci.o
endif

ifdef CONFIG_PROC_FS
L_OBJS   += proc.o
ifdef CONFIG_PCI_OLD_PROC
L_OBJS   += oldproc.o
endif
endif

ifdef CONFIG_PCI_QUIRKS
L_OBJS   += quirks.o
endif

L_OBJS   += compat.o

include $(TOPDIR)/Rules.make
