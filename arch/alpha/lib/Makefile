#
# Makefile for alpha-specific library files..
#

.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

OBJS  = __divqu.o __remqu.o __divlu.o __remlu.o memset.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)
	sync

__divqu.o: divide.S
	$(CC) -DDIV -c -o __divqu.o divide.S

__remqu.o: divide.S
	$(CC) -DREM -c -o __remqu.o divide.S

__divlu.o: divide.S
	$(CC) -DDIV -DINTSIZE -c -o __divlu.o divide.S

__remlu.o: divide.S
	$(CC) -DREM -DINTSIZE -c -o __remlu.o divide.S

dep:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif

