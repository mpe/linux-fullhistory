#
# Makefile for the soundmodem device driver.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

O_TARGET	:= soundmodem.o

O_OBJS		:= sm.o
ifeq ($(CONFIG_SOUNDMODEM_SBC),y)
O_OBJS		+= sm_sbc.o
endif
ifeq ($(CONFIG_SOUNDMODEM_WSS),y)
O_OBJS		+= sm_wss.o
endif
ifeq ($(CONFIG_SOUNDMODEM_AFSK1200),y)
O_OBJS		+= sm_afsk1200.o
endif
ifeq ($(CONFIG_SOUNDMODEM_AFSK2400_7),y)
O_OBJS		+= sm_afsk2400_7.o
endif
ifeq ($(CONFIG_SOUNDMODEM_AFSK2400_8),y)
O_OBJS		+= sm_afsk2400_8.o
endif
ifeq ($(CONFIG_SOUNDMODEM_AFSK2666),y)
O_OBJS		+= sm_afsk2666.o
endif
ifeq ($(CONFIG_SOUNDMODEM_HAPN4800),y)
O_OBJS		+= sm_hapn4800.o
endif
ifeq ($(CONFIG_SOUNDMODEM_PSK4800),y)
O_OBJS		+= sm_psk4800.o
endif
ifeq ($(CONFIG_SOUNDMODEM_FSK9600),y)
O_OBJS		+= sm_fsk9600.o
endif

M_OBJS		:= $(O_TARGET)

all:		all_targets
.PHONY:		all

gentbl:		gentbl.c
		$(HOSTCC) $(HOSTCFLAGS) $< -o $@ -lm

TBLHDR		:= sm_tbl_afsk1200.h sm_tbl_afsk2400_8.h 
TBLHDR		+= sm_tbl_afsk2666.h sm_tbl_psk4800.h
TBLHDR		+= sm_tbl_hapn4800.h sm_tbl_fsk9600.h

$(TBLHDR):	gentbl
		./gentbl

fastdep:	$(TBLHDR)

include $(TOPDIR)/Rules.make
