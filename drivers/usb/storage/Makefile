#
# Makefile for the USB Mass Storage device drivers.
#

O_TARGET	:= usb-storage.o
M_OBJS		:= usb-storage.o
O_OBJS		:= scsiglue.o protocol.o transport.o debug.o usb.o
MOD_LIST_NAME	:= USB_STORAGE_MODULES

CFLAGS_scsiglue.o:= -I../../scsi/
CFLAGS_protocol.o:= -I../../scsi/
CFLAGS_transport.o:= -I../../scsi/
CFLAGS_debug.o:= -I../../scsi/
CFLAGS_usb.o:= -I../../scsi/

ifeq ($(CONFIG_USB_STORAGE_DEBUG),y)
	O_OBJS += debug.o
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
