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

.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

OBJS := pci.o
SRCS := pci.c

all: pci.a

pci.a: $(OBJS)
	rm -f pci.a
	$(AR) rcs pci.a $(OBJS)
	sync

dep:
	$(CPP) -M $(SRCS) > .depend

modules:

include $(TOPDIR)/Rules.make
