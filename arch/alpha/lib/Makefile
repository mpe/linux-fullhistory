#
# Makefile for alpha-specific library files..
#

.S.s:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -E -o $*.s $<
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -c -o $*.o $<

OBJS  = __divqu.o __remqu.o __divlu.o __remlu.o memset.o memcpy.o io.o \
	checksum.o csum_partial_copy.o strlen.o \
	strcat.o strcpy.o strncat.o strncpy.o stxcpy.o stxncpy.o \
	strchr.o strrchr.o memchr.o \
	copy_user.o clear_user.o strncpy_from_user.o strlen_user.o \
	csum_ipv6_magic.o strcasecmp.o fpreg.o \
	callback_srm.o srm_puts.o srm_printk.o

lib.a: $(OBJS)
	$(AR) rcs lib.a $(OBJS)

__divqu.o: divide.S
	$(CC) $(AFLAGS) -DDIV -c -o __divqu.o divide.S

__remqu.o: divide.S
	$(CC) $(AFLAGS) -DREM -c -o __remqu.o divide.S

__divlu.o: divide.S
	$(CC) $(AFLAGS) -DDIV -DINTSIZE -c -o __divlu.o divide.S

__remlu.o: divide.S
	$(CC) $(AFLAGS) -DREM -DINTSIZE -c -o __remlu.o divide.S

dep:

include $(TOPDIR)/Rules.make
