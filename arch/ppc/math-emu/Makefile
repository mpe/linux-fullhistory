#
#
#

O_TARGET := math-emu.o

O_OBJS   := math.o fmr.o lfd.o stfd.o

ifdef CONFIG_MATH_EMULATION
O_OBJS +=   fabs.o fadd.o fadds.o fcmpo.o fcmpu.o fctiw.o fctiwz.o	\
	    fdiv.o fdivs.o fmadd.o fmadds.o fmsub.o fmsubs.o 		\
	    fmul.o fmuls.o fnabs.o fneg.o fnmadd.o fnmadds.o		\
	    fnmsub.o fnmsubs.o fres.o frsp.o frsqrte.o fsel.o		\
	    fsqrt.o fsqrts.o fsub.o fsubs.o lfs.o			\
	    mcrfs.o mffs.o mtfsb0.o mtfsb1.o mtfsf.o mtfsfi.o		\
	    stfiwx.o stfs.o udivmodti4.o types.o
endif

include $(TOPDIR)/Rules.make

