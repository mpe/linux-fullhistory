#
# Makefile for ia64-specific library routines..
#

.S.o:
	$(CC) $(AFLAGS) -c $< -o $@

L_TARGET = lib.a

L_OBJS  = __divdi3.o __divsi3.o __udivdi3.o __udivsi3.o		\
	__moddi3.o __modsi3.o __umoddi3.o __umodsi3.o		\
	checksum.o clear_page.o csum_partial_copy.o copy_page.o	\
	copy_user.o clear_user.o memset.o strncpy_from_user.o	\
	strlen.o strlen_user.o strnlen_user.o			\
	flush.o do_csum.o

LX_OBJS = io.o

IGNORE_FLAGS_OBJS = __divdi3.o __divsi3.o __udivdi3.o __udivsi3.o \
		    __moddi3.o __modsi3.o __umoddi3.o __umodsi3.o

include $(TOPDIR)/Rules.make

__divdi3.o: idiv.S
	$(CC) $(AFLAGS) -c -o $@ $<

__divsi3.o: idiv.S
	$(CC) $(AFLAGS) -c -DSINGLE -c -o $@ $<

__udivdi3.o: idiv.S
	$(CC) $(AFLAGS) -c -DUNSIGNED -c -o $@ $<

__udivsi3.o: idiv.S
	$(CC) $(AFLAGS) -c -DUNSIGNED -DSINGLE -c -o $@ $<

__moddi3.o: idiv.S
	$(CC) $(AFLAGS) -c -DMODULO -c -o $@ $<

__modsi3.o: idiv.S
	$(CC) $(AFLAGS) -c -DMODULO -DSINGLE -c -o $@ $<

__umoddi3.o: idiv.S
	$(CC) $(AFLAGS) -c -DMODULO -DUNSIGNED -c -o $@ $<

__umodsi3.o: idiv.S
	$(CC) $(AFLAGS) -c -DMODULO -DUNSIGNED -DSINGLE -c -o $@ $<
