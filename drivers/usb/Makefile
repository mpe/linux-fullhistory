#
# Makefile for the kernel usb device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..

SUB_DIRS     :=
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

L_TARGET := usb.a
M_OBJS   :=
L_OBJS   :=

ifeq ($(CONFIG_USB),y)
  L_OBJS +=usbcore.o
endif
ifeq ($(CONFIG_USB),m)
  M_OBJS +=usbcore.o
  MIX_OBJS +=usb.o usb-debug.o usb-core.o
endif

ifeq ($(CONFIG_USB_UHCI),y)
    L_OBJS += uhci.o uhci-debug.o
endif

ifeq ($(CONFIG_USB_UHCI),m)
      M_OBJS += usb-uhci.o
    MIX_OBJS += uhci.o uhci-debug.o
endif

ifeq ($(CONFIG_USB_OHCI),y)
    L_OBJS += ohci.o ohci-debug.o
endif
ifeq ($(CONFIG_USB_OHCI),m)
      M_OBJS += usb-ohci.o
      MIX_OBJS += ohci.o ohci-debug.o
endif

ifeq ($(CONFIG_USB_OHCI_HCD),y)
    L_OBJS += ohci-hcd.o ohci-root-hub.o
endif
ifeq ($(CONFIG_USB_OHCI_HCD),m)
      M_OBJS += usb-ohci-hcd.o
      MIX_OBJS += ohci-hcd.o ohci-root-hub.o
endif

ifeq ($(CONFIG_USB_MOUSE),y)
  L_OBJS += mouse.o
endif
ifeq ($(CONFIG_USB_MOUSE),m)
  M_OBJS  +=mouse.o	
  MIX_OBJS +=mouse.o
endif

ifeq ($(CONFIG_USB_HUB),y)
  L_OBJS += hub.o
endif
ifeq ($(CONFIG_USB_HUB),m)
  M_OBJS  +=hub.o
  MIX_OBJS +=hub.o	
endif

ifeq ($(CONFIG_USB_ACM),y)
  L_OBJS += acm.o
endif
ifeq ($(CONFIG_USB_ACM),m)
  M_OBJS += acm.o
  MIX_OBJS +=acm.o
endif

ifeq ($(CONFIG_USB_PRINTER),y)
  L_OBJS += printer.o
endif

ifeq ($(CONFIG_USB_PRINTER),m)
  M_OBJS += printer.o
  MIX_OBJS += printer.o  
endif

ifeq ($(CONFIG_USB_KBD),y)
  L_OBJS += keyboard.o keymap.o
endif

ifeq ($(CONFIG_USB_KBD),m)
  M_OBJS += usb-keyboard.o
  MIX_OBJS += keyboard.o keymap.o
endif

ifeq ($(CONFIG_USB_AUDIO),y)
  L_OBJS += audio.o
endif

ifeq ($(CONFIG_USB_AUDIO),m)
  M_OBJS += audio.o
  MIX_OBJS += audio.o
endif

ifeq ($(CONFIG_USB_CPIA),y)
  L_OBJS += cpia.o
endif

ifeq ($(CONFIG_USB_CPIA),m)
  M_OBJS += cpia.o
  MIX_OBJS += cpia.o
endif

ifeq ($(CONFIG_USB_SCSI),y)
  L_OBJS += usb_scsi.o
  ifeq ($(CONFIG_USB_SCSI_DEBUG),y)
    L_OBJS += usb_scsi_debug.o
  endif
endif

include $(TOPDIR)/Rules.make

keymap.o: keymap.c

keymap.c: maps/serial.map maps/usb.map maps/fixup.map
	./mkmap > $@

keymap-mac.o: keymap-mac.c
keymap-mac.c: maps/mac.map maps/usb.map
	./mkmap.adb > $@

ifneq ($(CONFIG_MAC_KEYBOARD),y)
usb-keyboard.o: keymap.o keyboard.o
	$(LD) $(LD_RFLAG) -r -o $@  keymap.o keyboard.o 
else
usb-keyboard.o: keymap-mac.o keyboard.o
	$(LD) $(LD_RFLAG) -r -o $@  keymap-mac.o keyboard.o 
endif

usb-uhci.o: uhci.o uhci-debug.o 
	$(LD) $(LD_RFLAG) -r -o $@  uhci.o uhci-debug.o 

usb-ohci.o: ohci.o ohci-debug.o 
	$(LD) $(LD_RFLAG) -r -o $@ ohci.o ohci-debug.o 
       
usb-ohci-hcd.o: ohci-hcd.o ohci-root-hub.o 
	$(LD) $(LD_RFLAG) -r -o $@ ohci-hcd.o ohci-root-hub.o 
       
usbcore.o: usb.o usb-debug.o usb-core.o
	$(LD) $(LD_RFLAG) -r -o $@ usb.o usb-debug.o usb-core.o

       
