# Makefile for the Linux sound card driver
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes. (hopefully)
#
# Stolen from the kernel Makefiles, Craig Metz - cmetz@thor.tjhsst.edu
#

.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) -c -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) $(SOUND_SUPPORT) -c $<

OBJS	= sound_stub.o

all:	sound.a

sound.a: $(OBJS) 
	$(AR) rcs sound.a $(OBJS)
	sync

clean:
	rm -f core *.o *.a *.BAK *.BA *.B
	rm -f soundload sounddrv
	for i in *.c;do rm -f `basename $$i .c`.s;done

indent:
	for n in *.c;do echo indent $$n;indent $$n;done

dep:
	$(CPP) -M $(SOUND_SUPPORT) *.c > .depend

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
