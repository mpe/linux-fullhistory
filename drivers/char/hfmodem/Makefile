#
# Makefile for the hfmodem device driver.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

O_TARGET	:= hfmodem.o

O_OBJS		:= refclock.o modem.o main.o
ifeq ($(CONFIG_HFMODEM_SBC),y)
O_OBJS		+= sbc.o
endif
ifeq ($(CONFIG_HFMODEM_WSS),y)
O_OBJS		+= wss.o
endif

M_OBJS		:= $(O_TARGET)

all:		all_targets
.PHONY:		all

gentbl:		gentbl.c
		$(HOSTCC) $(HOSTCFLAGS) $< -o $@ -lm

TBLHDR		:= tables.h

tables.h:	gentbl
		./gentbl > $@

fastdep:	$(TBLHDR)

include $(TOPDIR)/Rules.make
