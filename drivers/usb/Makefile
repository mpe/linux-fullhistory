#
# Makefile for the kernel USB device drivers.
#

SUB_DIRS     :=
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

L_TARGET := usb.a
MOD_LIST_NAME := USB_MODULES

ifeq ($(CONFIG_USB),y)
  L_OBJS += usb-debug.o usb-core.o hub.o
  LX_OBJS += usb.o
  ifeq ($(CONFIG_USB_PROC),y)
    L_OBJS += proc_usb.o
  endif
endif
ifeq ($(CONFIG_USB),m)
  M_OBJS += usbcore.o
  MI_OBJS += usb-debug.o usb-core.o hub.o
  MIX_OBJS += usb.o
  ifeq ($(CONFIG_USB_PROC),y)
    MI_OBJS += proc_usb.o
  endif
endif

ifeq ($(CONFIG_USB_UHCI),y)
    L_OBJS += uhci.o uhci-debug.o
endif

ifeq ($(CONFIG_USB_UHCI),m)
      M_OBJS += usb-uhci.o
      MI_OBJS += uhci.o uhci-debug.o
endif

ifeq ($(CONFIG_USB_OHCI_HCD),y)
    L_OBJS += ohci-hcd.o
endif
ifeq ($(CONFIG_USB_OHCI_HCD),m)
      M_OBJS += usb-ohci-hcd.o
      MI_OBJS += ohci-hcd.o
endif

ifeq ($(CONFIG_USB_SCANNER),y)
  L_OBJS += scanner.o
endif
ifeq ($(CONFIG_USB_SCANNER),m)
  M_OBJS  +=scanner.o
endif

ifeq ($(CONFIG_USB_ACM),y)
  L_OBJS += acm.o
endif
ifeq ($(CONFIG_USB_ACM),m)
  M_OBJS += acm.o
endif

ifeq ($(CONFIG_USB_PRINTER),y)
  L_OBJS += printer.o
endif

ifeq ($(CONFIG_USB_PRINTER),m)
  M_OBJS += printer.o
endif

ifeq ($(CONFIG_USB_SERIAL),y)
  L_OBJS += usb-serial.o
endif

ifeq ($(CONFIG_USB_SERIAL),m)
  M_OBJS += usb-serial.o
endif

ifeq ($(CONFIG_USB_AUDIO),y)
  L_OBJS += audio.o
endif

ifeq ($(CONFIG_USB_AUDIO),m)
  M_OBJS += audio.o
endif

ifeq ($(CONFIG_USB_CPIA),y)
  L_OBJS += cpia.o
endif

ifeq ($(CONFIG_USB_CPIA),m)
  M_OBJS += cpia.o
endif

ifeq ($(CONFIG_USB_OV511),y)
  L_OBJS += ov511.o
endif

ifeq ($(CONFIG_USB_OV511),m)
  M_OBJS += ov511.o
endif

ifeq ($(CONFIG_USB_DC2XX),y)
  L_OBJS += dc2xx.o
endif
ifeq ($(CONFIG_USB_DC2XX),m)
  M_OBJS += dc2xx.o
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
endif

ifeq ($(CONFIG_USB_HID),y)
  L_OBJS += hid.o
  ILX_OBJS := input.o
endif

ifeq ($(CONFIG_USB_HID),m)
  M_OBJS += hid.o
  IMX_OBJS := input.o
endif

ifeq ($(CONFIG_USB_KBD),y)
  L_OBJS += usbkbd.o
  ILX_OBJS := input.o
endif

ifeq ($(CONFIG_USB_KBD),m)
  M_OBJS += usbkbd.o
  IMX_OBJS := input.o
endif

ifeq ($(CONFIG_USB_MOUSE),y)
  L_OBJS += usbmouse.o
  ILX_OBJS := input.o
endif

ifeq ($(CONFIG_USB_MOUSE),m)
  M_OBJS += usbmouse.o
  IMX_OBJS := input.o
endif

LX_OBJS += $(ILX_OBJS)
MX_OBJS += $(IMX_OBJS)

ifeq ($(CONFIG_INPUT_KEYBDEV),y)
  L_OBJS += keybdev.o
endif

ifeq ($(CONFIG_INPUT_KEYBDEV),m)
  M_OBJS += keybdev.o
endif

ifeq ($(CONFIG_INPUT_MOUSEDEV),y)
  L_OBJS += mousedev.o
endif

ifeq ($(CONFIG_INPUT_MOUSEDEV),m)
  M_OBJS += mousedev.o
endif

ifeq ($(CONFIG_INPUT_JOYDEV),y)
  L_OBJS += joydev.o
endif

ifeq ($(CONFIG_INPUT_JOYDEV),m)
  M_OBJS += joydev.o
endif

ifeq ($(CONFIG_INPUT_EVDEV),y)
  L_OBJS += evdev.o
endif

ifeq ($(CONFIG_INPUT_EVDEV),m)
  M_OBJS += evdev.o
endif

ifeq ($(CONFIG_USB_USS720),y)
  L_OBJS += uss720.o
endif

ifeq ($(CONFIG_USB_USS720),m)
  M_OBJS += uss720.o
endif

ifeq ($(CONFIG_USB_DABUSB),y)
  L_OBJS += dabusb.o
endif

ifeq ($(CONFIG_USB_DABUSB),m)
  M_OBJS += dabusb.o
endif

include $(TOPDIR)/Rules.make

ifeq ($(CONFIG_USB_SCSI_DEBUG),y)
usb-scsi.o: usb_scsi.o usb_scsi_debug.o
	$(LD) $(LD_RFLAG) -r -o $@  usb_scsi.o usb_scsi_debug.o 
else
usb-scsi.o: usb_scsi.o
	$(LD) $(LD_RFLAG) -r -o $@  usb_scsi.o
endif

usb-uhci.o: uhci.o uhci-debug.o 
	$(LD) $(LD_RFLAG) -r -o $@  uhci.o uhci-debug.o 

usb-ohci-hcd.o: ohci-hcd.o
	$(LD) $(LD_RFLAG) -r -o $@ ohci-hcd.o

ifeq ($(CONFIG_USB_PROC),y)
usbcore.o: usb.o usb-debug.o usb-core.o proc_usb.o hub.o
	$(LD) $(LD_RFLAG) -r -o $@ usb.o usb-debug.o usb-core.o proc_usb.o \
	hub.o
else
usbcore.o: usb.o usb-debug.o usb-core.o hub.o
	$(LD) $(LD_RFLAG) -r -o $@ usb.o usb-debug.o usb-core.o \
	hub.o
endif
