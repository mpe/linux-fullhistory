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
L_OBJS   := via-cuda.o adb.o nvram.o macio-adb.o via-pmu.o mediabay.o
endif

ifeq ($(CONFIG_SERIAL)$(CONFIG_PMAC),yy)
  LX_OBJS += macserial.o
else
  ifeq ($(CONFIG_SERIAL)$(CONFIG_PMAC),my)
    MX_OBJS += macserial.o
  endif
endif

ifdef CONFIG_MAC_KEYBOARD
L_OBJS += mac_keyb.o
endif

ifdef CONFIG_VT
  ifdef CONFIG_PMAC_CONSOLE
    L_OBJS += pmac-cons.o
    ifdef CONFIG_CONTROL_VIDEO
      L_OBJS += control.o
    endif
    ifdef CONFIG_PLATINUM_VIDEO
      L_OBJS += platinum.o
    endif
    ifdef CONFIG_VALKYRIE_VIDEO
      L_OBJS += valkyrie.o
    endif
    ifdef CONFIG_CHIPS_VIDEO
      L_OBJS += chips.o
    endif
    ifdef CONFIG_ATY_VIDEO
      L_OBJS += aty.o
    endif
    ifdef CONFIG_IMSTT_VIDEO
      L_OBJS += imstt.o
    endif
  endif
endif

include $(TOPDIR)/Rules.make

# Integrated in mac_keyb.c
# mackeymap.map is retained for historical reasons
#mackeymap.c: mackeymap.map
#	loadkeys --mktable mackeymap.map > mackeymap.c
