#
# Makefile for the USB serial device drivers.
#

O_TARGET	:= usb-serial.o
M_OBJS		:= usb-serial.o
O_OBJS		:= usbserial.o
MOD_LIST_NAME	:= USB_SERIAL_MODULES

ifeq ($(CONFIG_USB_SERIAL_VISOR),y)
	O_OBJS += visor.o
endif
ifeq ($(CONFIG_USB_SERIAL_WHITEHEAT),y)
	O_OBJS += whiteheat.o
endif
ifeq ($(CONFIG_USB_SERIAL_FTDI_SIO),y)
	O_OBJS += ftdi_sio.o
endif
ifeq ($(CONFIG_USB_SERIAL_KEYSPAN_PDA),y)
	O_OBJS += keyspan_pda.o
endif
ifeq ($(CONFIG_USB_SERIAL_KEYSPAN),y)
	O_OBJS += keyspan.o
endif
ifeq ($(CONFIG_USB_SERIAL_OMNINET),y)
	O_OBJS += omninet.o
endif
ifeq ($(CONFIG_USB_SERIAL_DIGI_ACCELEPORT),y)
	O_OBJS += digi_acceleport.o
endif
 
include $(TOPDIR)/Rules.make
