#
# Makefile for the Linux kernel device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile.

SUB_DIRS     := block char net misc sound
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS) pci scsi sbus cdrom isdn pnp \
				 macintosh video dio zorro fc4

ifdef CONFIG_DIO
SUB_DIRS += dio
MOD_SUB_DIRS += dio
endif

ifdef CONFIG_PCI
SUB_DIRS += pci
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

ifdef CONFIG_VT
SUB_DIRS += video
MOD_SUB_DIRS += video
endif

ifdef CONFIG_PPC
SUB_DIRS += macintosh
MOD_SUB_DIRS += macintosh
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

ifeq ($(CONFIG_AP1000),y)
SUB_DIRS += ap1000
ALL_SUB_DIRS += ap1000
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

ifeq ($(CONFIG_HAMRADIO),y)
  SUB_DIRS += net/hamradio
  MOD_SUB_DIRS += net/hamradio
endif

include $(TOPDIR)/Rules.make
