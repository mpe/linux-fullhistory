#
# Makefile for the FPU instruction emulation.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := math-emu.o
O_OBJS   := math.o fabsq.o faddq.o fdivq.o fdmulq.o fitoq.o 		\
		fmovq.o fmulq.o fnegq.o fqtoi.o fqtox.o fsubq.o		\
		fxtoq.o fdtoq.o fstoq.o fqtos.o fqtod.o fsqrtq.o	\
		fcmpq.o fcmpeq.o udivmodti4.o				\
		fsqrts.o fsqrtd.o fadds.o faddd.o fsubs.o fsubd.o	\
		fmuls.o fmuld.o fdivs.o fdivd.o fsmuld.o		\
		fstoi.o fdtoi.o fstox.o fdtox.o fstod.o fdtos.o

include $(TOPDIR)/Rules.make
