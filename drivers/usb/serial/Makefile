#
# Makefile for the USB serial device drivers.
#

O_TARGET	:= usb-serial.o
M_OBJS		:=
O_OBJS		:=

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

obj-$(CONFIG_USB_SERIAL)                        += usbserial.o
obj-$(CONFIG_USB_SERIAL_VISOR)			+= visor.o
obj-$(CONFIG_USB_SERIAL_WHITEHEAT)		+= whiteheat.o
obj-$(CONFIG_USB_SERIAL_FTDI_SIO)		+= ftdi_sio.o
obj-$(CONFIG_USB_SERIAL_KEYSPAN_PDA)		+= keyspan_pda.o
obj-$(CONFIG_USB_SERIAL_KEYSPAN)		+= keyspan.o
obj-$(CONFIG_USB_SERIAL_OMNINET)		+= omninet.o
obj-$(CONFIG_USB_SERIAL_DIGI_ACCELEPORT)	+= digi_acceleport.o
obj-$(CONFIG_USB_SERIAL_BELKIN)			+= belkin_sa.o
obj-$(CONFIG_USB_SERIAL_EMPEG)			+= empeg.o
 
# Objects that export symbols.
export-objs	:= usbserial.o

# Translate to Rules.make lists.

O_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-y)))
OX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-y)))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make

