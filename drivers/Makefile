#
# Makefile for the Linux kernel device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile.

SUB_DIRS     := block char net parport sound misc media
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS) pci sgi ide scsi sbus cdrom isdn pnp i2o \
				ieee1394 macintosh video dio zorro fc4 \
				usb nubus tc atm pcmcia i2c telephony \
				acpi mtd input md

ifdef CONFIG_DIO
SUB_DIRS += dio
MOD_SUB_DIRS += dio
endif

ifdef CONFIG_PCI
SUB_DIRS += pci
endif

ifeq ($(CONFIG_PCMCIA),y)
SUB_DIRS += pcmcia
else
  ifeq ($(CONFIG_PCMCIA),m)
  MOD_SUB_DIRS += pcmcia
  endif
endif

ifdef CONFIG_MTD
SUB_DIRS += mtd
MOD_SUB_DIRS += mtd
endif

ifdef CONFIG_SBUS
SUB_DIRS += sbus
MOD_SUB_DIRS += sbus
endif

ifdef CONFIG_ZORRO
SUB_DIRS += zorro
endif

ifdef CONFIG_NUBUS
SUB_DIRS += nubus
endif

ifdef CONFIG_TC
SUB_DIRS += tc
endif

ifdef CONFIG_VT
SUB_DIRS += video
MOD_SUB_DIRS += video
endif

ifdef CONFIG_MAC
SUB_DIRS += macintosh
MOD_SUB_DIRS += macintosh
endif

ifdef CONFIG_ALL_PPC
SUB_DIRS += macintosh
MOD_SUB_DIRS += macintosh
endif

ifeq ($(CONFIG_USB),y)
SUB_DIRS += usb
MOD_SUB_DIRS += usb
else
  ifeq ($(CONFIG_USB),m)
  MOD_SUB_DIRS += usb
  endif
endif 

ifeq ($(CONFIG_INPUT),y)
SUB_DIRS += input
MOD_SUB_DIRS += input
else
  ifeq ($(CONFIG_INPUT),m)
  MOD_SUB_DIRS += input
  endif
endif 

ifeq ($(CONFIG_PHONE),y)
SUB_DIRS += telephony
MOD_SUB_DIRS += telephony
else
  ifeq ($(CONFIG_PHONE),m)
  MOD_SUB_DIRS += telephony
  endif
endif 

ifdef CONFIG_SGI
SUB_DIRS += sgi
MOD_SUB_DIRS += sgi
endif

ifeq ($(CONFIG_I2O),y)
SUB_DIRS += i2o
MOD_SUB_DIRS += i2o
else
  ifeq ($(CONFIG_I2O),m)
  MOD_SUB_DIRS += i2o
  endif
endif 

# If CONFIG_IDE is set, the core of ATA support will be added to the kernel,
# but some of the low-level things may also be modules.
ifeq ($(CONFIG_IDE),y)
SUB_DIRS += ide
MOD_SUB_DIRS += ide
else
  ifeq ($(CONFIG_IDE),m)
  MOD_SUB_DIRS += ide
  endif
endif

# If CONFIG_SCSI is set, the core of SCSI support will be added to the kernel,
# but some of the low-level things may also be modules.
ifeq ($(CONFIG_SCSI),y)
SUB_DIRS += scsi
MOD_SUB_DIRS += scsi
else
  ifeq ($(CONFIG_SCSI),m)
  MOD_SUB_DIRS += scsi
  endif
endif

ifeq ($(CONFIG_BLK_DEV_MD),y)
SUB_DIRS += md
MOD_SUB_DIRS += md
else
  ifeq ($(CONFIG_BLK_DEV_MD),m)
  MOD_SUB_DIRS += md
  endif
endif

ifeq ($(CONFIG_IEEE1394),y)
SUB_DIRS += ieee1394
MOD_SUB_DIRS += ieee1394
else
  ifeq ($(CONFIG_IEEE1394),m)
  MOD_SUB_DIRS += ieee1394
  endif
endif

ifeq ($(CONFIG_PNP),y)
SUB_DIRS += pnp
MOD_SUB_DIRS += pnp
else
  ifeq ($(CONFIG_PNP),m)
  MOD_SUB_DIRS += pnp
  endif
endif

ifneq ($(CONFIG_CD_NO_IDESCSI)$(CONFIG_BLK_DEV_IDECD)$(CONFIG_BLK_DEV_SR)$(CONFIG_PARIDE_PCD),)
SUB_DIRS += cdrom
MOD_SUB_DIRS += cdrom
endif

ifeq ($(CONFIG_ISDN),y)
SUB_DIRS += isdn
MOD_SUB_DIRS += isdn
else
  ifeq ($(CONFIG_ISDN),m)
  MOD_SUB_DIRS += isdn
  endif
endif

ifdef CONFIG_ATM
SUB_DIRS += atm
MOD_SUB_DIRS += atm
endif

ifeq ($(CONFIG_FC4),y)
SUB_DIRS += fc4
MOD_SUB_DIRS += fc4
else
  ifeq ($(CONFIG_FC4),m)
  MOD_SUB_DIRS += fc4
  endif
endif

# When MOD_LIST_NAME is set, make will try to add $(MOD_SUB_DIRS).o to
# modules/MOD_LIST_NAME.  We don't have hamradio.o and Linus
# sort of insisted on making hamradio/ a subdirectory of drivers/net/.
# #FIXME# MOD_LIST_NAME doesn't exist any more -- does this comment
# #FIXME# still mean anything?

ifeq ($(CONFIG_HAMRADIO),y)
  SUB_DIRS += net/hamradio
  MOD_SUB_DIRS += net/hamradio
endif

ifeq ($(CONFIG_I2C),y)
SUB_DIRS += i2c
MOD_SUB_DIRS += i2c
else
  ifeq ($(CONFIG_I2C),m)
  MOD_SUB_DIRS += i2c
  endif
endif

ifeq ($(CONFIG_ACPI),y)
SUB_DIRS += acpi
MOD_SUB_DIRS += acpi
endif

include $(TOPDIR)/Rules.make
