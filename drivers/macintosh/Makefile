#
# Makefile for the Macintosh-specific device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)

O_TARGET := macintosh.o
O_OBJS   :=
M_OBJS   :=

ifeq ($(CONFIG_PMAC_PBOOK),y)
  O_OBJS += mediabay.o
else
  ifeq ($(CONFIG_MAC_FLOPPY),y)
    O_OBJS += mediabay.o
  endif
endif

ifeq ($(CONFIG_MAC_SERIAL),y)
  O_OBJS += macserial.o
else
  ifeq ($(CONFIG_MAC_SERIAL),m)
    M_OBJS += macserial.o
  endif
endif

ifeq ($(CONFIG_NVRAM),y)
  O_OBJS += nvram.o
else
  ifeq ($(CONFIG_NVRAM),m)
    M_OBJS += nvram.o
  endif
endif

ifdef CONFIG_ADB
  OX_OBJS  := adb.o
endif

ifdef CONFIG_ADB_KEYBOARD
  O_OBJS += mac_keyb.o
endif

ifdef CONFIG_ADB_MACII
  O_OBJS += via-macii.o
endif

ifdef CONFIG_ADB_MACIISI
  O_OBJS += via-maciisi.o
endif

ifdef CONFIG_ADB_CUDA
  O_OBJS += via-cuda.o
endif

ifdef CONFIG_ADB_IOP
  O_OBJS += adb-iop.o
endif

ifdef CONFIG_ADB_PMU
  O_OBJS += via-pmu.o
endif

ifdef CONFIG_ADB_PMU68K
  O_OBJS += via-pmu68k.o
endif

ifdef CONFIG_ADB_MACIO
  O_OBJS += macio-adb.o
endif

include $(TOPDIR)/Rules.make

# Integrated in mac_keyb.c
# mackeymap.map is retained for historical reasons
#mackeymap.c: mackeymap.map
#	loadkeys --mktable mackeymap.map > mackeymap.c
