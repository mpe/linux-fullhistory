#
# Makefile for alpha-specific library files..
#

OBJS  = __divqu.o __remqu.o __divlu.o __remlu.o memset.o memcpy.o io.o \
	checksum.o strlen.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)

memset.o: memset.S
	$(CC) -c -o memset.o memset.S

__divqu.o: divide.S
	$(CC) -DDIV -c -o __divqu.o divide.S

__remqu.o: divide.S
	$(CC) -DREM -c -o __remqu.o divide.S

__divlu.o: divide.S
	$(CC) -DDIV -DINTSIZE -c -o __divlu.o divide.S

__remlu.o: divide.S
	$(CC) -DREM -DINTSIZE -c -o __remlu.o divide.S

dep:

include $(TOPDIR)/Rules.make
