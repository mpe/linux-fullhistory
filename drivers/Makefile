#
# Makefile for the linux kernel device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

SUB_DIRS     := block char net #streams
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS) pci scsi sound cdrom

ifdef CONFIG_PCI
SUB_DIRS += pci
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

ifdef CONFIG_CD_NO_IDESCSI
SUB_DIRS += cdrom
MOD_SUB_DIRS += cdrom
endif

include $(TOPDIR)/Rules.make
