#
# Makefile for the kernel USB device drivers.
#

# Subdirs.

SUB_DIRS	:=
MOD_SUB_DIRS	:= $(SUB_DIRS)
ALL_SUB_DIRS	:= $(SUB_DIRS) serial storage

# The target object and module list name.

O_TARGET	:= usbdrv.o
M_OBJS		:=
O_OBJS		:=

# Objects that export symbols.

export-objs		:= usb.o

# Multipart objects.

list-multi		:= usbcore.o
usbcore-objs		:= usb.o usb-debug.o hub.o

# Optional parts of multipart objects.

ifeq ($(CONFIG_USB_DEVICEFS),y)
	usbcore-objs	+= devio.o inode.o drivers.o devices.o
endif

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Each configuration option enables a list of files.

obj-$(CONFIG_USB)		+= usbcore.o
obj-$(CONFIG_USB_UHCI)		+= usb-uhci.o
obj-$(CONFIG_USB_UHCI_ALT)	+= uhci.o
obj-$(CONFIG_USB_OHCI)		+= usb-ohci.o

obj-$(CONFIG_USB_MOUSE)		+= usbmouse.o
obj-$(CONFIG_USB_HID)		+= hid.o
obj-$(CONFIG_USB_KBD)		+= usbkbd.o
obj-$(CONFIG_USB_WACOM)		+= wacom.o

obj-$(CONFIG_USB_SCANNER)	+= scanner.o
obj-$(CONFIG_USB_ACM)		+= acm.o
obj-$(CONFIG_USB_PRINTER)	+= printer.o
obj-$(CONFIG_USB_AUDIO)		+= audio.o
obj-$(CONFIG_USB_IBMCAM)	+= ibmcam.o
obj-$(CONFIG_USB_DC2XX)		+= dc2xx.o
obj-$(CONFIG_USB_MDC800)	+= mdc800.o
obj-$(CONFIG_USB_USS720)	+= uss720.o
obj-$(CONFIG_USB_DABUSB)	+= dabusb.o
obj-$(CONFIG_USB_PLUSB)		+= plusb.o
obj-$(CONFIG_USB_OV511)		+= ov511.o
obj-$(CONFIG_USB_PEGASUS)	+= pegasus.o
obj-$(CONFIG_USB_RIO500)	+= rio500.o
obj-$(CONFIG_USB_DSBR)		+= dsbr100.o
obj-$(CONFIG_USB_MICROTEK)	+= microtek.o
obj-$(CONFIG_USB_BLUETOOTH)	+= bluetooth.o
obj-$(CONFIG_USB_NET1080)	+= net1080.o

# Object files in subdirectories

ifeq ($(CONFIG_USB_SERIAL),y)
	SUB_DIRS += serial
	obj-y += serial/usb-serial.o
else
	ifeq ($(CONFIG_USB_SERIAL),m)
		MOD_SUB_DIRS += serial
	endif
endif

ifeq ($(CONFIG_USB_STORAGE),y)
	SUB_DIRS += storage
	obj-y += storage/storage.o
else
	ifeq ($(CONFIG_USB_STORAGE),m)
		MOD_SUB_DIRS += storage
	endif
endif

# Translate to Rules.make lists.
multi-used	:= $(filter $(list-multi), $(obj-y) $(obj-m))
multi-objs	:= $(foreach m, $(multi-used), $($(basename $(m))-objs))
active-objs	:= $(sort $(multi-objs) $(obj-y) $(obj-m))

O_OBJS		:= $(obj-y)
M_OBJS		:= $(obj-m)
MIX_OBJS	:= $(filter $(export-objs), $(active-objs))

# The global Rules.make.

include $(TOPDIR)/Rules.make

# Link rules for multi-part drivers.

usbcore.o: $(usbcore-objs)
	$(LD) -r -o $@ $(usbcore-objs)

