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
L_OBJS   :=
LX_OBJS  :=
USBX_OBJS := usb.o hub.o usb-debug.o

ifeq ($(CONFIG_USB_MOUSE),y)
  USBX_OBJS += mouse.o
endif

ifeq ($(CONFIG_USB_KBD),y)
  USBX_OBJS += keyboard.o keymap.o
endif

ifeq ($(CONFIG_USB_AUDIO),y)
  USBX_OBJS += audio.o
endif

ifeq ($(CONFIG_USB), y)
  L_OBJS += $(USBX_OBJS)
endif

ifeq ($(CONFIG_USB_UHCI),y)
  ifeq ($(CONFIG_USB), y)
    L_OBJS += uhci.o uhci-debug.o
  else
    ifeq ($(CONFIG_USB),m)
      M_OBJS += usb-uhci.o
      MIX_OBJS += $(USBX_OBJS)
    endif
  endif
endif

ifeq ($(CONFIG_USB_OHCI),y)
  ifeq ($(CONFIG_USB), y)
    L_OBJS += ohci.o ohci-debug.o
  else
    ifeq ($(CONFIG_USB),m)
      USBO_OBJS += ohci.o ohci-debug.o
      M_OBJS += usb-ohci.o
      MIX_OBJS += $(USBX_OBJS)
    endif
  endif
endif

ifeq ($(CONFIG_USB_OHCI_HCD),y)
  ifeq ($(CONFIG_USB), y)
    L_OBJS += ohci-hcd.o ohci-root-hub.o
  else
    ifeq ($(CONFIG_USB),m)
      USBO_OBJS += ohci-hcd.o ohci-root-hub.o
      M_OBJS += usb-ohci-hcd.o
      MIX_OBJS += $(USBX_OBJS)
    endif
  endif
endif
include $(TOPDIR)/Rules.make

keymap.o: keymap.c

keymap.c: maps/serial.map maps/usb.map maps/fixup.map
	./mkmap > $@

usb-uhci.o: uhci.o uhci-debug.o $(USBX_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@  uhci.o uhci-debug.o $(USBX_OBJS)

usb-ohci.o: ohci.o ohci-debug.o $(USBX_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ ohci.o ohci-debug.o $(USBX_OBJS)

usb-ohci-hcd.o: ohci-hcd.o ohci-root-hub.o $(USBX_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ ohci-hcd.o ohci-root-hub.o $(USBX_OBJS)
       
