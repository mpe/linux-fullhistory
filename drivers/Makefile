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
ALL_SUB_DIRS := $(SUB_DIRS) pci scsi sound

ifdef CONFIG_PCI
SUB_DIRS += pci
endif

ifdef CONFIG_SCSI
SUB_DIRS += scsi
MOD_SUB_DIRS += scsi
endif

# I think this should have an else clause for modules, but the original
# makefile did not, so I am not adding it.
# -jln 12 Aug 1995
ifdef CONFIG_SOUND
SUB_DIRS += sound
endif

include $(TOPDIR)/Rules.make
