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

L_OBJS   := pci.o
L_TARGET := pci.a

ifeq ($(CONFIG_PCI_OPTIMIZE),y)
L_OBJS   += quirks.o
endif

include $(TOPDIR)/Rules.make
