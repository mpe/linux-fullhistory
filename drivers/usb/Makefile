#
# Makefile for the kernel USB device drivers.
#

# Subdirs.

SUB_DIRS	:=
MOD_SUB_DIRS	:= $(SUB_DIRS)
ALL_SUB_DIRS	:= $(SUB_DIRS)

# The target object and module list name.

O_TARGET	:= usbdrv.o
M_OBJS		:=
O_OBJS		:=
MOD_LIST_NAME	:= USB_MODULES

# Objects that export symbols.

export-objs		:= usb.o input.o

# Multipart objects.

list-multi		:= usbcore.o
usbcore-objs		:= usb.o usb-debug.o usb-core.o hub.o

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

obj-$(CONFIG_USB_MOUSE)		+= usbmouse.o input.o
obj-$(CONFIG_USB_HID)		+= hid.o input.o
obj-$(CONFIG_USB_KBD)		+= usbkbd.o input.o
obj-$(CONFIG_USB_GRAPHIRE)	+= graphire.o input.o
obj-$(CONFIG_USB_WMFORCE)	+= wmforce.o input.o
obj-$(CONFIG_INPUT_KEYBDEV)	+= keybdev.o input.o
obj-$(CONFIG_INPUT_MOUSEDEV)	+= mousedev.o input.o
obj-$(CONFIG_INPUT_JOYDEV)	+= joydev.o input.o
obj-$(CONFIG_INPUT_EVDEV)	+= evdev.o input.o

obj-$(CONFIG_USB_SCANNER)	+= scanner.o
obj-$(CONFIG_USB_ACM)		+= acm.o
obj-$(CONFIG_USB_PRINTER)	+= printer.o
obj-$(CONFIG_USB_SERIAL)	+= usb-serial.o
obj-$(CONFIG_USB_AUDIO)		+= audio.o
obj-$(CONFIG_USB_CPIA)		+= cpia.o
obj-$(CONFIG_USB_IBMCAM)	+= ibmcam.o
obj-$(CONFIG_USB_DC2XX)		+= dc2xx.o
obj-$(CONFIG_USB_STORAGE)	+= usb-storage.o
obj-$(CONFIG_USB_USS720)	+= uss720.o
obj-$(CONFIG_USB_DABUSB)	+= dabusb.o
obj-$(CONFIG_USB_PLUSB)		+= plusb.o
obj-$(CONFIG_USB_OV511)		+= ov511.o

# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.

multi-y		:= $(filter $(list-multi), $(obj-y))
multi-m		:= $(filter $(list-multi), $(obj-m))
int-y		:= $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m		:= $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Files that are both resident and modular: remove from modular.

obj-m		:= $(filter-out $(obj-y), $(obj-m))
int-m		:= $(filter-out $(int-y), $(int-m))

# Take multi-part drivers out of obj-y and put components in.

obj-y		:= $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.

O_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-y)))
OX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-y)))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))
MI_OBJS		:= $(sort $(filter-out $(export-objs), $(int-m)))
MIX_OBJS	:= $(sort $(filter     $(export-objs), $(int-m)))

# The global Rules.make.

include $(TOPDIR)/Rules.make

# Link rules for multi-part drivers.

usbcore.o: $(usbcore-objs)
	$(LD) -r -o $@ $(usbcore-objs)

