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
L_OBJS   := via-cuda.o adb.o nvram.o

ifeq ($(CONFIG_SERIAL),y)
  LX_OBJS += macserial.o
else
  ifeq ($(CONFIG_SERIAL),m)
    MX_OBJS += macserial.o
  endif
endif

ifdef CONFIG_MAC_KEYBOARD
L_OBJS += mac_keyb.o mackeymap.o
endif

ifdef CONFIG_VT
  ifdef CONFIG_PMAC_CONSOLE
    L_OBJS += pmac-cons.o control.o platinum.o valkyrie.o
    ifdef CONFIG_ATY_VIDEO
      L_OBJS += aty.o
    endif
    ifdef CONFIG_IMSTT_VIDEO
      L_OBJS += imstt.o
    endif
  endif
endif

include $(TOPDIR)/Rules.make

mackeymap.c: mackeymap.map
	loadkeys --mktable mackeymap.map > mackeymap.c
