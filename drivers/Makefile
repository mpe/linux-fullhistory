#
# Makefile for the linux kernel device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

SUB_DIRS     := block char net misc #streams
MOD_SUB_DIRS := $(SUB_DIRS) sbus
ALL_SUB_DIRS := $(SUB_DIRS) pci scsi sbus sound cdrom isdn pnp macintosh

ifdef CONFIG_PCI
SUB_DIRS += pci
endif

ifdef CONFIG_SBUS
SUB_DIRS += sbus
endif

ifdef CONFIG_PPC
SUB_DIRS += macintosh
MOD_SUB_DIRS += macintosh
endif

# If CONFIG_SCSI is set, the core of scsi support will be added to the kernel,
# but some of the low-level things may also be modules.
ifeq ($(CONFIG_SCSI),y)
SUB_DIRS += scsi
MOD_SUB_DIRS += scsi
else
  ifeq ($(CONFIG_SCSI),m)
  MOD_SUB_DIRS += scsi
  endif
endif

ifeq ($(CONFIG_SOUND),y)
SUB_DIRS += sound
else
  ifeq ($(CONFIG_SOUND),m)
  MOD_SUB_DIRS += sound
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

ifneq ($(CONFIG_CD_NO_IDESCSI)$(CONFIG_BLK_DEV_IDECD)$(CONFIG_BLK_DEV_SR),)
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

include $(TOPDIR)/Rules.make
