#
# Makefile for the kernel usb device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#
# This isn't actually supported yet. Don't try to use it.

SUB_DIRS     :=
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

L_TARGET := usb.a
M_OBJS   :=
L_OBJS   := usb.o hub.o usb-debug.o
LX_OBJS  :=

ifeq ($(CONFIG_USB_UHCI),y)
  L_OBJS += uhci.o uhci-debug.o
else
  ifeq ($(CONFIG_USB_UHCI),m)
    MX_OBJS += usb-uhci.o uhci-debug.o
  endif
endif

ifeq ($(CONFIG_USB_OHCI),y)
  L_OBJS += ohci.o ohci-debug.o
else
  ifeq ($(CONFIG_USB_UHCI),m)
    MX_OBJS += ohci.o ohci-debug.o
  endif
endif

ifeq ($(CONFIG_USB_MOUSE),y)
  L_OBJS += mouse.o
else
  ifeq ($(CONFIG_USB_UHCI),m)
    MX_OBJS += mouse.o
  endif
endif

ifeq ($(CONFIG_USB_KBD),y)
  L_OBJS += keyboard.o keymap.o
else
  ifeq ($(CONFIG_USB_UHCI),m)
    MX_OBJS += keyboard.o
  endif
endif

include $(TOPDIR)/Rules.make

keymap.o: keymap.c

keymap.c: maps/serial.map maps/usb.map maps/fixup.map
	./mkmap > $@
