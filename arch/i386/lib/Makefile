#
# Makefile for i386-specific library files..
#

.c.s:
	$(CC) $(CFLAGS) -S $<
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

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif

