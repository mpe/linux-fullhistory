#
# linux/arch/arm/lib/Makefile
#
# Copyright (C) 1995-1998 Russell King
#

L_TARGET := lib.a
L_OBJS   := backtrace.o bitops.o delay.o fp_support.o \
	    loaders.o memcpy.o memfastset.o system.o string.o uaccess.o

ifeq ($(PROCESSOR),armo)
  L_OBJS += uaccess-armo.o
endif

ifdef CONFIG_INET
  L_OBJS += checksum.o
endif

ifdef CONFIG_ARCH_ACORN
  L_OBJS += ll_char_wr.o io-acorn.o
  ifdef CONFIG_ARCH_A5K
    L_OBJS += floppydma.o
  endif
  ifdef CONFIG_ARCH_RPC
    L_OBJS += floppydma.o
  endif
endif

ifdef CONFIG_ARCH_EBSA110
  L_OBJS += io-ebsa110.o
endif

include $(TOPDIR)/Rules.make

constants.h: getconstants
	./getconstants > constants.h

getconstants: getconstants.c getconstants.h
	$(HOSTCC) -D__KERNEL__ -o getconstants getconstants.c

getconstants.h: getconsdata.c
	$(CC) $(CFLAGS) -c getconsdata.c
	$(PERL) extractinfo.perl $(OBJDUMP) > $@

%.o: %.S
ifndef $(CONFIG_BINUTILS_NEW)
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -E $< | tr ';$$' '\n#' > ..tmp.$<.s
	$(CC) $(CFLAGS:-pipe=) -c -o $@ ..tmp.$<.s
	$(RM) ..tmp.$<.s
else
	$(CC) $(CFLAGS) -D__ASSEMBLY__ -c -o $@ $<
endif

clean:
	$(RM) getconstants constants.h getconstants.h
