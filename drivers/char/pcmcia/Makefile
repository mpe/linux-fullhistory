#
# drivers/char/pcmcia/Makefile
#
# Makefile for the Linux PCMCIA char device drivers.
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

O_TARGET := pcmcia_char.o

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

obj-$(CONFIG_PCMCIA_SERIAL_CS)	+= serial_cs.o
obj-$(CONFIG_PCMCIA_SERIAL_CB)	+= serial_cb.o

O_OBJS		:= $(obj-y)
OX_OBJS		:=
M_OBJS		:= $(obj-m)
MX_OBJS		:=

include $(TOPDIR)/Rules.make
