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

L_TARGET := macintosh.a
M_OBJS   :=

ifndef CONFIG_MBX
L_OBJS   := via-cuda.o nvram.o macio-adb.o via-pmu.o mediabay.o
LX_OBJS  := adb.o
endif

ifeq ($(CONFIG_MAC_SERIAL),y)
  L_OBJS += macserial.o
else
  ifeq ($(CONFIG_MAC_SERIAL),m)
    M_OBJS += macserial.o
  endif
endif

ifdef CONFIG_MAC_KEYBOARD
L_OBJS += mac_keyb.o
endif

include $(TOPDIR)/Rules.make

# Integrated in mac_keyb.c
# mackeymap.map is retained for historical reasons
#mackeymap.c: mackeymap.map
#	loadkeys --mktable mackeymap.map > mackeymap.c
