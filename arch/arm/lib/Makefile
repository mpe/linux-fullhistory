#
# linux/arch/arm/lib/Makefile
#
# Copyright (C) 1995-1999 Russell King
#

L_TARGET := lib.a
L_OBJS   := backtrace.o bitops.o checksum.o delay.o \
	    string.o system.o uaccess.o

ifeq ($(PROCESSOR),armo)
  L_OBJS += uaccess-armo.o
endif

ifdef CONFIG_ARCH_ACORN
  L_OBJS += io-acorn.o
  ifdef CONFIG_ARCH_A5K
    L_OBJS += floppydma.o
  endif
  ifdef CONFIG_ARCH_RPC
    L_OBJS += floppydma.o
  endif
endif

ifeq ($(MACHINE),ebsa110)
  L_OBJS += io-ebsa110.o
else
  LX_OBJS += io.o
endif

ifeq ($(MACHINE),footbridge)
  L_OBJS += io-footbridge.o
endif

include $(TOPDIR)/Rules.make

.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -traditional -c -o $*.o $<

constants.h: getconsdata.o extractconstants.pl
	$(PERL) extractconstants.pl $(OBJDUMP) > $@

getconsdata.o: getconsdata.c
	$(CC) $(CFLAGS) -c getconsdata.c

checksum.o string.o: constants.h
