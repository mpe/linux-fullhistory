#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

# Dave Redman Frame Buffer tuning support.
# OK this is kind of ugly but it does allow drivers to be added fairly
# easily. and you can even choose what sort of support you want.

O_TARGET := sunchar.o
O_OBJS   := ${O_OBJ} sunkbd.o sunkbdmap.o sunmouse.o sunserial.o zs.o
M_OBJS   :=

ifeq ($(ARCH),sparc64)

ifeq ($(CONFIG_PCI),y)

O_OBJS += su.o pcikbd.o

ifeq ($(CONFIG_SAB82532),y)
O_OBJS += sab82532.o
else
  ifeq ($(CONFIG_SAB82532),m)
  M_OBJS += sab82532.o
  endif
endif

ifeq ($(CONFIG_ENVCTRL),y)
O_OBJS += envctrl.o
else
  ifeq ($(CONFIG_ENVCTRL),m)
  M_OBJS += envctrl.o
  endif
endif

endif # eq($(CONFIG_PCI,y)

ifeq ($(CONFIG_OBP_FLASH),y)
O_OBJS += flash.o
else
  ifeq ($(CONFIG_OBP_FLASH),m)
  M_OBJS += flash.o
  endif
endif

else # !eq($(ARCH),sparc64)

ifeq ($(CONFIG_PCI),y)
O_OBJS += su.o pcikbd.o
endif

endif # !eq($(ARCH),sparc64)

ifeq ($(CONFIG_SUN_OPENPROMIO),y)
O_OBJS += openprom.o
else
  ifeq ($(CONFIG_SUN_OPENPROMIO),m)
  M_OBJS += openprom.o
  endif
endif

ifeq ($(CONFIG_SUN_MOSTEK_RTC),y)
O_OBJS += rtc.o
else
  ifeq ($(CONFIG_SUN_MOSTEK_RTC),m)
  M_OBJS += rtc.o
  endif
endif

ifeq ($(CONFIG_SUN_BPP),y)
O_OBJS += bpp.o
else
  ifeq ($(CONFIG_SUN_BPP),m)
  M_OBJS += bpp.o
  endif
endif

ifeq ($(CONFIG_SUN_VIDEOPIX),y)
O_OBJS += vfc.o
else
  ifeq ($(CONFIG_SUN_VIDEOPIX),m)
  M_OBJS += vfc.o
  endif
endif

include $(TOPDIR)/Rules.make

sunkbdmap.o: sunkeymap.c

vfc.o: vfc_dev.o vfc_i2c.o
	$(LD) -r -o vfc.o vfc_dev.o vfc_i2c.o
