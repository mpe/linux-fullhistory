#
# Makefile for the Linux AX.25 and HFMODEM device drivers.
#
#
# 19971130 	Moved the amateur radio related network drivers from 
#		drivers/net/ to drivers/hamradio for easier maintainance.
#               Joerg Reuter DL1BKE <jreuter@yaina.de>
#
# 20000806	Rewritten to use lists instead of if-statements.
#		Christoph Hellwig <hch@caldera.de>
#


SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS) soundmodem

O_TARGET := hamradio.o

export-objs	= hdlcdrv.o


obj-$(CONFIG_DMASCC)		+= dmascc.o
obj-$(CONFIG_SCC)		+= scc.o
obj-$(CONFIG_MKISS)		+= mkiss.o
obj-$(CONFIG_6PACK)		+= 6pack.o
obj-$(CONFIG_YAM)		+= yam.o
obj-$(CONFIG_PI)		+= pi2.o
obj-$(CONFIG_PT)		+= pt.o
obj-$(CONFIG_BPQETHER)		+= bpqether.o
obj-$(CONFIG_BAYCOM_SER_FDX)	+= baycom_ser_fdx.o	hdlcdrv.o
obj-$(CONFIG_BAYCOM_SER_HDX)	+= baycom_ser_hdx.o	hdlcdrv.o
obj-$(CONFIG_BAYCOM_PAR)	+= baycom_par.o		hdlcdrv.o
obj-$(CONFIG_BAYCOM_EPP)	+= baycom_epp.o		hdlcdrv.o
obj-$(CONFIG_SOUNDMODEM)	+= 			hdlcdrv.o

ifeq ($(CONFIG_SOUNDMODEM),y)
SUB_DIRS += soundmodem
O_OBJS += soundmodem/soundmodem.o
else
  ifeq ($(CONFIG_SOUNDMODEM),m)
  MOD_SUB_DIRS += soundmodem
  endif
endif

# Files that are both resident and modular: remove from modular.
obj-m		:= $(filter-out $(obj-y), $(obj-m))

# Translate to Rules.make lists.
O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS		:= $(filter     $(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make
