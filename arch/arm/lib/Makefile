#
# linux/arch/arm/lib/Makefile
#
# Copyright (C) 1995-1999 Russell King
#

USE_STANDARD_AS_RULE := true

L_TARGET := lib.a
L_OBJS   := changebit.o csumipv6.o csumpartial.o csumpartialcopy.o \
	    csumpartialcopyuser.o clearbit.o copy_page.o findbit.o \
	    memchr.o memcpy.o memset.o memzero.o setbit.o          \
	    strncpy_from_user.o strnlen_user.o strchr.o strrchr.o  \
	    testchangebit.o testclearbit.o testsetbit.o uaccess.o

O_TARGET := lib.o
O_OBJS   := backtrace.o delay.o

L_OBJS_arc		:= io-acorn.o floppydma.o
L_OBJS_rpc		:= io-acorn.o floppydma.o
L_OBJS_clps7500		:= io-acorn.o
L_OBJS_ebsa110		:= io-ebsa110.o
L_OBJS_footbridge	:= io-footbridge.o
L_OBJS_l7200     	:= io-acorn.o
L_OBJS_nexuspci		:= io-footbridge.o
L_OBJS_sa1100		:= io-footbridge.o
L_OBJS_shark		:= io-shark.o
L_OBJS_integrator	:= io-shark.o

ifeq ($(PROCESSOR),armo)
  L_OBJS += uaccess-armo.o
endif

ifneq ($(MACHINE),ebsa110)
  OX_OBJS += io.o
endif

L_OBJS	+= $(L_OBJS_$(MACHINE))

include $(TOPDIR)/Rules.make

constants.h: getconsdata.o extractconstants.pl
	$(PERL) extractconstants.pl $(OBJDUMP) > $@

getconsdata.o: getconsdata.c
	$(CC) $(CFLAGS) -c getconsdata.c
