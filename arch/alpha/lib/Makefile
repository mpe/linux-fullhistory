#
# Makefile for alpha-specific library files..
#

OBJS  = __divqu.o __remqu.o __divlu.o __remlu.o memset.o memcpy.o io.o \
	checksum.o csum_partial_copy.o strlen.o \
	strcat.o strcpy.o strncat.o strncpy.o stxcpy.o stxncpy.o \
	strchr.o strrchr.o \
	copy_user.o clear_user.o strncpy_from_user.o strlen_user.o \
	csum_ipv6_magic.o strcasecmp.o semaphore.o \
	srm_dispatch.o srm_fixup.o srm_puts.o srm_printk.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)

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
