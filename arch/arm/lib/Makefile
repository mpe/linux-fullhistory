#
# linux/arch/arm/lib/Makefile
#
# Copyright (C) 1995-1998 Russell King
#

L_TARGET := lib.a
L_OBJS   := backtrace.o bitops.o checksum.o delay.o io.o memcpy.o \
	    system.o string.o uaccess.o

ifeq ($(PROCESSOR),armo)
  L_OBJS += uaccess-armo.o
endif

ifdef CONFIG_ARCH_ACORN
  L_OBJS += loaders.o ll_char_wr.o io-acorn.o
  ifdef CONFIG_ARCH_A5K
    L_OBJS += floppydma.o
  endif
  ifdef CONFIG_ARCH_RPC
    L_OBJS += floppydma.o
  endif
endif

ifeq ($(MACHINE),ebsa110)
  L_OBJS += io-ebsa110.o
endif

ifeq ($(MACHINE),vnc)
  L_OBJS += io-ebsa285.o
endif

ifeq ($(MACHINE),ebsa285)
  L_OBJS += io-ebsa285.o
endif

include $(TOPDIR)/Rules.make

constants.h: getconsdata.o extractconstants.pl
	$(PERL) extractconstants.pl $(OBJDUMP) > $@

getconsdata.o: getconsdata.c
	$(CC) $(CFLAGS) -c getconsdata.c

checksum.o: constants.h

%.o: %.S
ifneq ($(CONFIG_BINUTILS_NEW),y)
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -E $< | tr ';$$' '\n#' > ..tmp.$<.s
	$(CC) $(CFLAGS:-pipe=) -c -o $@ ..tmp.$<.s
	$(RM) ..tmp.$<.s
else
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -c -o $@ $<
endif
