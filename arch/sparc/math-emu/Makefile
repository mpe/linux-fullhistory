#
# Makefile for the FPU instruction emulation.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := math-emu.o
O_OBJS   := math.o ashldi3.o fabss.o faddd.o faddq.o fadds.o 		\
		fcmpd.o fcmped.o fcmpeq.o fcmpes.o fcmpq.o fcmps.o	\
		fdivd.o fdivq.o fdivs.o fdmulq.o fdtoi.o fdtoq.o	\
		fdtos.o fitoq.o fmovs.o fmuld.o fmulq.o fmuls.o		\
		fnegs.o fqtod.o fqtoi.o fqtos.o fsmuld.o fsqrtd.o	\
		fsqrtq.o fsqrts.o fstod.o fstoi.o fstoq.o fsubd.o 	\
		fsubq.o	fsubs.o udivmodti4.o

LINKS	 := double.h faddd.c faddq.c fadds.c fdivd.c fdivq.c fdivs.c	\
		fdtoi.c fitoq.c fmuld.c fmulq.c fmuls.c fqtoi.c		\
		fsqrtd.c fsqrtq.c fsqrts.c fstoi.c fsubd.c 		\
		fsubq.c	fsubs.c op-1.h op-2.h op-4.h op-common.h quad.h	\
		single.h soft-fp.h udivmodti4.c

.S.s:
	$(CPP) -D__ASSEMBLY__ -ansi $< -o $*.s

.S.o:
	$(CC) -D__ASSEMBLY__ -ansi -c $< -o $*.o

include $(TOPDIR)/Rules.make

symlinks:
	ln -sf $(patsubst %,../../sparc64/math-emu/%,$(LINKS)) .

cleansymlinks:
	rm -f $(LINKS)
