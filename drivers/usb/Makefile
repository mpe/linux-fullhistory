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
MOD_LIST_NAME := USB_MODULES

ifeq ($(CONFIG_USB),y)
  L_OBJS += usbcore.o
  ifeq ($(CONFIG_USB_PROC),y)
    L_OBJS += proc_usb.o
  endif
endif
ifeq ($(CONFIG_USB),m)
  M_OBJS += usbcore.o
  MIX_OBJS += usb.o usb-debug.o usb-core.o
  ifeq ($(CONFIG_USB_PROC),y)
    MIX_OBJS += proc_usb.o
  endif
endif

ifeq ($(CONFIG_USB_UHCI),y)
    L_OBJS += uhci.o uhci-debug.o
endif

ifeq ($(CONFIG_USB_UHCI),m)
      M_OBJS += usb-uhci.o
      MI_OBJS += uhci.o uhci-debug.o
endif

ifeq ($(CONFIG_USB_OHCI),y)
    L_OBJS += ohci.o ohci-debug.o
endif
ifeq ($(CONFIG_USB_OHCI),m)
      M_OBJS += usb-ohci.o
      MI_OBJS += ohci.o ohci-debug.o
endif

ifeq ($(CONFIG_USB_OHCI_HCD),y)
    L_OBJS += ohci-hcd.o ohci-root-hub.o
endif
ifeq ($(CONFIG_USB_OHCI_HCD),m)
      M_OBJS += usb-ohci-hcd.o
      MI_OBJS += ohci-hcd.o ohci-root-hub.o
endif

ifeq ($(CONFIG_USB_MOUSE),y)
  L_OBJS += mouse.o
endif
ifeq ($(CONFIG_USB_MOUSE),m)
  M_OBJS += mouse.o	
  MI_OBJS += mouse.o
endif

ifeq ($(CONFIG_USB_HP_SCANNER),y)
  L_OBJS += hp_scanner.o
endif
ifeq ($(CONFIG_USB_HP_SCANNER),m)
  M_OBJS  +=hp_scanner.o
  MI_OBJS +=hp_scanner.o
endif

ifeq ($(CONFIG_USB_ACM),y)
  L_OBJS += acm.o
endif
ifeq ($(CONFIG_USB_ACM),m)
  M_OBJS += acm.o
  MI_OBJS += acm.o
endif

ifeq ($(CONFIG_USB_PRINTER),y)
  L_OBJS += printer.o
endif

ifeq ($(CONFIG_USB_PRINTER),m)
  M_OBJS += printer.o
  MI_OBJS += printer.o  
endif

ifeq ($(CONFIG_USB_SERIAL),y)
  L_OBJS += usb-serial.o
endif

ifeq ($(CONFIG_USB_SERIAL),m)
  M_OBJS += usb-serial.o
  MI_OBJS += usb-serial.o  
endif

ifneq ($(CONFIG_ADB_KEYBOARD),y)
KEYMAP=keymap
else
KEYMAP=keymap-mac
endif

ifeq ($(CONFIG_USB_KBD),y)
  L_OBJS += keyboard.o $(KEYMAP).o
endif

ifeq ($(CONFIG_USB_KBD),m)
  M_OBJS += usb-keyboard.o
  MI_OBJS += keyboard.o $(KEYMAP).o
endif

ifeq ($(CONFIG_USB_AUDIO),y)
  L_OBJS += audio.o
endif

ifeq ($(CONFIG_USB_AUDIO),m)
  M_OBJS += audio.o
  MI_OBJS += audio.o
endif

ifeq ($(CONFIG_USB_CPIA),y)
  L_OBJS += cpia.o
endif

ifeq ($(CONFIG_USB_CPIA),m)
  M_OBJS += cpia.o
  MI_OBJS += cpia.o
endif

ifeq ($(CONFIG_USB_DC2XX),y)
  L_OBJS += dc2xx.o
endif
ifeq ($(CONFIG_USB_DC2XX),m)
  M_OBJS += dc2xx.o
  MI_OBJS += dc2xx.o
endif

ifeq ($(CONFIG_USB_SCSI),y)
  L_OBJS += usb_scsi.o
  ifeq ($(CONFIG_USB_SCSI_DEBUG),y)
    L_OBJS += usb_scsi_debug.o
  endif
endif

ifeq ($(CONFIG_USB_SCSI),m)
  M_OBJS += usb-scsi.o
  MI_OBJS += usb_scsi.o
  ifeq ($(CONFIG_USB_SCSI_DEBUG),y)
    MI_OBJS += usb_scsi_debug.o
  endif
endif

ifeq ($(CONFIG_USB_EZUSB),y)
  L_OBJS += ezusb.o
endif

ifeq ($(CONFIG_USB_EZUSB),m)
  M_OBJS += ezusb.o
  MI_OBJS += ezusb.o
endif

ifeq ($(CONFIG_USB_USS720),y)
  L_OBJS += uss720.o
endif

ifeq ($(CONFIG_USB_USS720),m)
  M_OBJS += uss720.o
  MI_OBJS += uss720.o
endif

include $(TOPDIR)/Rules.make

keymap.o: keymap.c

keymap.c: maps/serial.map maps/usb.map maps/fixup.map
	./mkmap > $@

keymap-mac.o: keymap-mac.c
keymap-mac.c: maps/mac.map maps/usb.map
	./mkmap.adb > $@

usb-keyboard.o: $(KEYMAP).o keyboard.o
	$(LD) $(LD_RFLAG) -r -o $@  $(KEYMAP).o keyboard.o 

ifeq ($(CONFIG_USB_SCSI_DEBUG),y)
usb-scsi.o: usb_scsi.o usb_scsi_debug.o
	$(LD) $(LD_RFLAG) -r -o $@  usb_scsi.o usb_scsi_debug.o 
else
usb-scsi.o: usb_scsi.o
	$(LD) $(LD_RFLAG) -r -o $@  usb_scsi.o
endif

usb-uhci.o: uhci.o uhci-debug.o 
	$(LD) $(LD_RFLAG) -r -o $@  uhci.o uhci-debug.o 

usb-ohci.o: ohci.o ohci-debug.o 
	$(LD) $(LD_RFLAG) -r -o $@ ohci.o ohci-debug.o 

usb-ohci-hcd.o: ohci-hcd.o ohci-root-hub.o 
	$(LD) $(LD_RFLAG) -r -o $@ ohci-hcd.o ohci-root-hub.o 

ifeq ($(CONFIG_USB_PROC),y)
usbcore.o: usb.o usb-debug.o usb-core.o proc_usb.o hub.o
	$(LD) $(LD_RFLAG) -r -o $@ usb.o usb-debug.o usb-core.o proc_usb.o \
	hub.o
else
usbcore.o: usb.o usb-debug.o usb-core.o hub.o
	$(LD) $(LD_RFLAG) -r -o $@ usb.o usb-debug.o usb-core.o \
	hub.o
endif
