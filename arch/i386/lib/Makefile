#
# Makefile for i386-specific library files..
#

.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

OBJS  = checksum.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)
	sync

dep:

modules:

include $(TOPDIR)/Rules.make
