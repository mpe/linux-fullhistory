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
  L_OBJS += fonts.o font_8x8.o font_8x16.o pearl_8x8.o font_6x11.o
  LX_OBJS += fbcon.o fbcmap.o fbgen.o
endif

# Frame Buffer Devices

ifeq ($(CONFIG_FB_AMIGA),y)
L_OBJS += amifb.o
else
  ifeq ($(CONFIG_FB_AMIGA),m)
  M_OBJS += amifb.o
  endif
endif

ifeq ($(CONFIG_FB_APOLLO),y)
L_OBJS += dnfb.o
endif

ifeq ($(CONFIG_FB_ATARI),y)
L_OBJS += atafb.o
else
  ifeq ($(CONFIG_FB_ATARI),m)
  M_OBJS += atafb.o
  endif
endif

ifeq ($(CONFIG_FB_ATY),y)
L_OBJS += atyfb.o
endif

ifeq ($(CONFIG_FB_CYBER),y)
L_OBJS += cyberfb.o
else
  ifeq ($(CONFIG_FB_CYBER),m)
  M_OBJS += cyberfb.o
  endif
endif

ifeq ($(CONFIG_FB_MAC),y)
L_OBJS += macfb.o
endif

ifeq ($(CONFIG_FB_HP300),y)
L_OBJS += hpfb.o
endif

ifeq ($(CONFIG_FB_OF),y)
L_OBJS += offb.o
endif

ifeq ($(CONFIG_FB_RETINAZ3),y)
L_OBJS += retz3fb.o
else
  ifeq ($(CONFIG_FB_RETINAZ3),m)
  M_OBJS += retz3fb.o
  endif
endif

ifeq ($(CONFIG_FB_S3TRIO),y)
L_OBJS += S3triofb.o
else
  ifeq ($(CONFIG_FB_S3TRIO),m)
  M_OBJS += S3triofb.o
  endif
endif

ifeq ($(CONFIG_FB_TGA),y)
L_OBJS += tgafb.o
endif

ifeq ($(CONFIG_FB_VGA),y)
L_OBJS += vgafb.o
endif

ifeq ($(CONFIG_FB_VESA),y)
L_OBJS += vesafb.o
endif

ifeq ($(CONFIG_FB_MDA),y)
L_OBJS += mdafb.o
else
  ifeq ($(CONFIG_FB_MDA),m)
  M_OBJS += mdafb.o
  endif
endif

ifeq ($(CONFIG_FB_VIRGE),y)
L_OBJS += virgefb.o
else
  ifeq ($(CONFIG_FB_VIRGE),m)
  M_OBJS += virgefb.o
  endif
endif

ifeq ($(CONFIG_FB_VIRTUAL),y)
L_OBJS += vfb.o
else
  ifeq ($(CONFIG_FB_VIRTUAL),m)
  M_OBJS += vfb.o
  endif
endif

# Generic Low Level Drivers

ifdef CONFIG_FBCON_AFB
LX_OBJS += fbcon-afb.o
endif

ifdef CONFIG_FBCON_CFB2
LX_OBJS += fbcon-cfb2.o
endif

ifdef CONFIG_FBCON_CFB4
LX_OBJS += fbcon-cfb4.o
endif

ifdef CONFIG_FBCON_CFB8
LX_OBJS += fbcon-cfb8.o
endif

ifdef CONFIG_FBCON_CFB16
LX_OBJS += fbcon-cfb16.o
endif

ifdef CONFIG_FBCON_CFB24
LX_OBJS += fbcon-cfb24.o
endif

ifdef CONFIG_FBCON_CFB32
LX_OBJS += fbcon-cfb32.o
endif

ifdef CONFIG_FBCON_ILBM
LX_OBJS += fbcon-ilbm.o
endif

ifdef CONFIG_FBCON_IPLAN2P2
LX_OBJS += fbcon-iplan2p2.o
endif

ifdef CONFIG_FBCON_IPLAN2P4
LX_OBJS += fbcon-iplan2p4.o
endif

ifdef CONFIG_FBCON_IPLAN2P8
LX_OBJS += fbcon-iplan2p8.o
endif

ifdef CONFIG_FBCON_MAC
LX_OBJS += fbcon-mac.o
endif

ifdef CONFIG_FBCON_MFB
LX_OBJS += fbcon-mfb.o
endif

ifdef CONFIG_FBCON_VGA
LX_OBJS += fbcon-vga.o
endif

# GSP Console

ifdef CONFIG_AMIGA_GSP
L_OBJS := $(L_OBJS) gspcon.o gspcore.o
endif

# VGA Text Console

ifdef CONFIG_VGA_CONSOLE
ifndef CONFIG_FB_VGA
L_OBJS := $(L_OBJS) vgacon.o
endif
endif

# Console Wrapper

ifdef CONFIG_ABSCON_COMPAT
L_OBJS := $(L_OBJS) compatcon.o
endif

include $(TOPDIR)/Rules.make

gspcore.c: gspcore.gsp
	$(GSPA) $< > $*.hex
	$(GSPH2C) $*.hex > gspcore.c
