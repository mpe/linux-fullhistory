#
# Makefile for the USB Mass Storage device drivers.
#

O_TARGET	:= usb-storage.o
M_OBJS		:= usb-storage.o
O_OBJS		:= scsiglue.o protocol.o transport.o usb.o 

CFLAGS_scsiglue.o:= -I../../scsi/
CFLAGS_protocol.o:= -I../../scsi/
CFLAGS_transport.o:= -I../../scsi/
CFLAGS_debug.o:= -I../../scsi/
CFLAGS_usb.o:= -I../../scsi/
CFLAGS_shuttle_usbat.o:= -I../../scsi/
CFLAGS_sddr09.o:= -I../../scsi/

ifeq ($(CONFIG_USB_STORAGE_DEBUG),y)
	O_OBJS += debug.o
endif

ifeq ($(CONFIG_USB_STORAGE_HP8200e),y)
	O_OBJS += shuttle_usbat.o
endif

ifeq ($(CONFIG_USB_STORAGE_SDDR09),y)
	O_OBJS += sddr09.o
endif

ifeq ($(CONFIG_USB_STORAGE_FREECOM),y)
	O_OBJS += freecom.o
endif

ifeq ($(CONFIG_USB_STORAGE_SHUTTLE_SMARTMEDIA),y)
	O_OBJS += shuttle_sm.o
endif

ifeq ($(CONFIG_USB_STORAGE_SHUTTLE_COMPACTFLASH),y)
	O_OBJS += shuttle_cf.o
endif
 
include $(TOPDIR)/Rules.make
