#
# Makefile for the kernel video drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

GSPA = gspa
GSPH2C = gspahextoc

L_TARGET := video.a
L_OBJS   :=
M_OBJS   :=
LX_OBJS  :=
MX_OBJS  :=
MOD_LIST_NAME := VIDEO_MODULES

# Frame Buffer Console

ifeq ($(CONFIG_FB),y)
  L_OBJS += fonts.o font_8x8.o font_8x16.o pearl_8x8.o
  LX_OBJS += fbcon.o fbcmap.o
endif

# Frame buffer devices

ifeq ($(CONFIG_APOLLO),y)
L_OBJS += dn_fb.o
endif

ifeq ($(CONFIG_FB_AMIGA),y)
L_OBJS += amifb.o
else
  ifeq ($(CONFIG_FB_AMIGA),m)
  M_OBJS += amifb.o
  endif
endif

ifeq ($(CONFIG_FB_ATARI),y)
L_OBJS += atafb.o
else
  ifeq ($(CONFIG_FB_ATARI),m)
  M_OBJS += atafb.o
  endif
endif

ifeq ($(CONFIG_FB_CYBER),y)
LX_OBJS += cyberfb.o
else
  ifeq ($(CONFIG_FB_CYBER),m)
  MX_OBJS += cyberfb.o
  endif
endif

ifeq ($(CONFIG_FB_RETINAZ3),y)
LX_OBJS += retz3fb.o
else
  ifeq ($(CONFIG_FB_RETINAZ3),m)
  MX_OBJS += retz3fb.o
  endif
endif

ifeq ($(CONFIG_FB_VIRTUAL),y)
L_OBJS += vfb.o
else
  ifeq ($(CONFIG_FB_VIRTUAL),m)
  M_OBJS += vfb.o
  endif
endif

ifeq ($(CONFIG_FB_OPEN_FIRMWARE),y)
L_OBJS += offb.o
endif

ifeq ($(CONFIG_FB_MACH64),y)
L_OBJS += mach64fb.o
else
  ifeq ($(CONFIG_FB_MACH64),m)
  M_OBJS += mach64fb.o
  endif
endif

ifeq ($(CONFIG_FB_TGA),y)
L_OBJS += tgafb.o
endif

# Low level drivers

ifeq ($(CONFIG_FBCON_AFB),y)
L_OBJS += fbcon-afb.o
else
  ifeq ($(CONFIG_FBCON_AFB),m)
  M_OBJS += fbcon-afb.o
  endif
endif

ifeq ($(CONFIG_FBCON_CFB8),y)
L_OBJS += fbcon-cfb8.o
else
  ifeq ($(CONFIG_FBCON_CFB8),m)
  M_OBJS += fbcon-cfb8.o
  endif
endif

ifeq ($(CONFIG_FBCON_CFB16),y)
LX_OBJS += fbcon-cfb16.o
else
  ifeq ($(CONFIG_FBCON_CFB16),m)
  MX_OBJS += fbcon-cfb16.o
  endif
endif

ifeq ($(CONFIG_FBCON_ILBM),y)
L_OBJS += fbcon-ilbm.o
else
  ifeq ($(CONFIG_FBCON_ILBM),m)
  M_OBJS += fbcon-ilbm.o
  endif
endif

ifeq ($(CONFIG_FBCON_IPLAN2P2),y)
L_OBJS += fbcon-iplan2p2.o
else
  ifeq ($(CONFIG_FBCON_IPLAN2P2),m)
  M_OBJS += fbcon-iplan2p2.o
  endif
endif

ifeq ($(CONFIG_FBCON_IPLAN2P4),y)
L_OBJS += fbcon-iplan2p4.o
else
  ifeq ($(CONFIG_FBCON_IPLAN2P4),m)
  M_OBJS += fbcon-iplan2p4.o
  endif
endif

ifeq ($(CONFIG_FBCON_IPLAN2P8),y)
L_OBJS += fbcon-iplan2p8.o
else
  ifeq ($(CONFIG_FBCON_IPLAN2P8),m)
  M_OBJS += fbcon-iplan2p8.o
  endif
endif

ifeq ($(CONFIG_FBCON_MFB),y)
L_OBJS += fbcon-mfb.o
else
  ifeq ($(CONFIG_FBCON_MFB),m)
  M_OBJS += fbcon-mfb.o
  endif
endif

ifeq ($(CONFIG_FBCON_CYBER),y)
L_OBJS += fbcon-cyber.o
else
  ifeq ($(CONFIG_FBCON_CYBER),m)
  M_OBJS += fbcon-cyber.o
  endif
endif

ifeq ($(CONFIG_FBCON_RETINAZ3),y)
L_OBJS += fbcon-retz3.o
else
  ifeq ($(CONFIG_FBCON_RETINAZ3),m)
  M_OBJS += fbcon-retz3.o
  endif
endif

ifeq ($(CONFIG_FBCON_MACH64),y)
L_OBJS += fbcon-mach64.o
else
  ifeq ($(CONFIG_FBCON_MACH64),m)
  M_OBJS += fbcon-mach64.o
  endif
endif

# GSP Console

ifdef CONFIG_AMIGA_GSP
L_OBJS := $(L_OBJS) gspcon.o gspcore.o
endif

# VGA Console

ifdef CONFIG_ABSTRACT_CONSOLE
ifdef CONFIG_VGA_CONSOLE
L_OBJS := $(L_OBJS) vgacon.o
endif
endif

include $(TOPDIR)/Rules.make

gspcore.c: gspcore.gsp
	$(GSPA) $< > $*.hex
	$(GSPH2C) $*.hex > gspcore.c
