#
# Makefile for the kernel miscellaneous SPARC device drivers.
#
# Dave Redman Frame Buffer tuning support.
#
# 7 October 2000, Bartlomiej Zolnierkiewicz <bkz@linux-ide.org>
# Rewritten to use lists instead of if-statements.
#

O_TARGET := sunchar.o
O_OBJS   := ${O_OBJ} sunkbd.o sunkbdmap.o sunmouse.o sunserial.o zs.o

vfc-objs := vfc_dev.o vfc_i2c.o

ifeq ($(CONFIG_PCI),y)
OX_OBJS	+= su.o
O_OBJS	+= pcikbd.o
endif

ifeq ($(ARCH),sparc64)

ifeq ($(CONFIG_PCI),y)
obj-$(CONFIG_SAB82532)			+= sab82532.o
obj-$(CONFIG_ENVCTRL)			+= envctrl.o
obj-$(CONFIG_DISPLAY7SEG)		+= display7seg.o
endif

obj-$(CONFIG_OBP_FLASH)			+= flash.o
endif

obj-$(CONFIG_SUN_OPENPROMIO)		+= openprom.o
obj-$(CONFIG_SUN_MOSTEK_RTC)		+= rtc.o
obj-$(CONFIG_SUN_BPP)			+= bpp.o
obj-$(CONFIG_SUN_VIDEOPIX)		+= vfc.o
obj-$(CONFIG_SUN_AURORA)		+= aurora.o
obj-$(CONFIG_TADPOLE_TS102_UCTRL)	+= uctrl.o
obj-$(CONFIG_SUN_JSFLASH)		+= jsflash.o

O_OBJS	+= $(obj-y)
M_OBJS	:= $(obj-m)

include $(TOPDIR)/Rules.make

sunkbdmap.o: sunkeymap.c

vfc.o: $(vfc-objs)
	$(LD) -r -o $@ $(vfc-objs)
