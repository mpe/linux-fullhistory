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

L_TARGET := video.a
L_OBJS   :=
M_OBJS   :=
LX_OBJS  :=
MX_OBJS  :=
MOD_LIST_NAME := VIDEO_MODULES

CONFIG_FBGEN_BUILTIN :=
CONFIG_FBGEN_MODULE :=

# Frame Buffer Console

ifeq ($(CONFIG_FB),y)
  # Nasty trick to make sure all wanted stuff is linked in
  O_TARGET = fbdev.o
  L_OBJS += fbdev.o
endif

ifeq ($(CONFIG_DUMMY_CONSOLE),y)
  L_OBJS += dummycon.o
endif

ifeq ($(CONFIG_PROM_CONSOLE),y)
  L_OBJS += promcon.o promcon_tbl.o
endif

ifeq ($(CONFIG_FB),y)
  L_OBJS += fonts.o
  OX_OBJS += fbcon.o fbcmap.o fbmem.o
  ifeq ($(CONFIG_FONT_8x8),y)
    L_OBJS += font_8x8.o
  endif
  ifeq ($(CONFIG_FONT_8x16),y)
    L_OBJS += font_8x16.o
  endif
  ifeq ($(CONFIG_FONT_SUN8x16),y)
    L_OBJS += font_sun8x16.o
  endif
  ifeq ($(CONFIG_FONT_SUN12x22),y)
    L_OBJS += font_sun12x22.o
  endif
  ifeq ($(CONFIG_FONT_6x11),y)
    L_OBJS += font_6x11.o
  endif
  ifeq ($(CONFIG_FONT_ACORN_8x8),y)
    L_OBJS += font_acorn_8x8.o
  endif
  ifeq ($(CONFIG_FONT_PEARL_8x8),y)
    L_OBJS += font_pearl_8x8.o
  endif
endif

# Frame Buffer Devices

ifeq ($(CONFIG_FB_ACORN),y)
L_OBJS += acornfb.o
else
  ifeq ($(CONFIG_FB_ACORN),m)
  M_OBJS += acornfb.o
  endif
endif

ifeq ($(CONFIG_FB_AMIGA),y)
L_OBJS += amifb.o
else
  ifeq ($(CONFIG_FB_AMIGA),m)
  M_OBJS += amifb.o
  endif
endif

ifeq ($(CONFIG_FB_PM2),y)
L_OBJS += pm2fb.o
CONFIG_FBGEN_BUILTIN = y
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

ifeq ($(CONFIG_FB_IGA),y)
L_OBJS += igafb.o
endif

ifeq ($(CONFIG_FB_CONTROL),y)
L_OBJS += controlfb.o
endif

ifeq ($(CONFIG_FB_PLATINUM),y)
L_OBJS += platinumfb.o
endif

ifeq ($(CONFIG_FB_VALKYRIE),y)
L_OBJS += valkyriefb.o
endif

ifeq ($(CONFIG_FB_CT65550),y)
L_OBJS += chipsfb.o
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
L_OBJS += offb.o macmodes.o
endif

ifeq ($(CONFIG_FB_IMSTT),y)
L_OBJS += imsttfb.o
endif

ifeq ($(CONFIG_FB_RETINAZ3),y)
L_OBJS += retz3fb.o
else
  ifeq ($(CONFIG_FB_RETINAZ3),m)
  M_OBJS += retz3fb.o
  endif
endif

ifeq ($(CONFIG_FB_CLGEN),y)
L_OBJS += clgenfb.o
CONFIG_FBGEN_BUILTIN = y
else
  ifeq ($(CONFIG_FB_CLGEN),m)
  M_OBJS += clgenfb.o
  CONFIG_FBGEN_MODULE = y
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
else
  ifeq ($(CONFIG_FB_TGA),m)
  M_OBJS += tgafb.o
  endif
endif

ifeq ($(CONFIG_FB_VESA),y)
L_OBJS += vesafb.o
endif

ifeq ($(CONFIG_FB_VIRGE),y)
L_OBJS += virgefb.o
else
  ifeq ($(CONFIG_FB_VIRGE),m)
  M_OBJS += virgefb.o
  endif
endif

ifdef CONFIG_FB_G364
L_OBJS := $(L_OBJS) g364fb.o
endif

ifeq ($(CONFIG_FB_SBUS),y)
L_OBJS += sbusfb.o
  ifeq ($(CONFIG_FB_CREATOR),y)
    L_OBJS += creatorfb.o
  else
    ifeq ($(CONFIG_FB_CREATOR),m)
    M_OBJS += creatorfb.o
    endif
  endif
  ifeq ($(CONFIG_FB_CGSIX),y)
    L_OBJS += cgsixfb.o
  else
    ifeq ($(CONFIG_FB_CGSIX),m)
    M_OBJS += cgsixfb.o
    endif
  endif
  ifeq ($(CONFIG_FB_BWTWO),y)
    L_OBJS += bwtwofb.o
  else
    ifeq ($(CONFIG_FB_BWTWO),m)
    M_OBJS += bwtwofb.o
    endif
  endif
  ifeq ($(CONFIG_FB_CGTHREE),y)
    L_OBJS += cgthreefb.o
  else
    ifeq ($(CONFIG_FB_CGTHREE),m)
    M_OBJS += cgthreefb.o
    endif
  endif
  ifeq ($(CONFIG_FB_TCX),y)
    L_OBJS += tcxfb.o
  else
    ifeq ($(CONFIG_FB_TCX),m)
    M_OBJS += tcxfb.o
    endif
  endif
  ifeq ($(CONFIG_FB_CGFOURTEEN),y)
    L_OBJS += cgfourteenfb.o
  else
    ifeq ($(CONFIG_FB_CGFOURTEEN),m)
    M_OBJS += cgfourteenfb.o
    endif
  endif
  ifeq ($(CONFIG_FB_LEO),y)
    L_OBJS += leofb.o
  else
    ifeq ($(CONFIG_FB_LEO),m)
    M_OBJS += leofb.o
    endif
  endif
else
  ifeq ($(CONFIG_FB_SBUS),m)
  M_OBJS += sbusfb.o
    ifeq ($(CONFIG_FB_CREATOR),y)
      M_OBJS += creatorfb.o
    else
      ifeq ($(CONFIG_FB_CREATOR),m)
      M_OBJS += creatorfb.o
      endif
    endif
    ifeq ($(CONFIG_FB_CGSIX),y)
      M_OBJS += cgsixfb.o
    else
      ifeq ($(CONFIG_FB_CGSIX),m)
      M_OBJS += cgsixfb.o
      endif
    endif
    ifeq ($(CONFIG_FB_BWTWO),y)
      M_OBJS += bwtwofb.o
    else
      ifeq ($(CONFIG_FB_BWTWO),m)
      M_OBJS += bwtwofb.o
      endif
    endif
    ifeq ($(CONFIG_FB_CGTHREE),y)
      M_OBJS += cgthreefb.o
    else
      ifeq ($(CONFIG_FB_CGTHREE),m)
      M_OBJS += cgthreefb.o
      endif
    endif
    ifeq ($(CONFIG_FB_TCX),y)
      M_OBJS += tcxfb.o
    else
      ifeq ($(CONFIG_FB_TCX),m)
      M_OBJS += tcxfb.o
      endif
    endif
    ifeq ($(CONFIG_FB_CGFOURTEEN),y)
      M_OBJS += cgfourteenfb.o
    else
      ifeq ($(CONFIG_FB_CGFOURTEEN),m)
      M_OBJS += cgfourteenfb.o
      endif
    endif
    ifeq ($(CONFIG_FB_LEO),y)
      M_OBJS += leofb.o
    else
      ifeq ($(CONFIG_FB_LEO),m)
      M_OBJS += leofb.o
      endif
    endif
  endif
endif

ifeq ($(CONFIG_FB_VIRTUAL),y)
L_OBJS += vfb.o
else
  ifeq ($(CONFIG_FB_VIRTUAL),m)
  M_OBJS += vfb.o
  endif
endif

ifdef CONFIG_FBGEN_BUILTIN
OX_OBJS += fbgen.o
else
  ifdef CONFIG_FBGEN_MODULE
  MX_OBJS += fbgen.o
  endif
endif

ifeq ($(CONFIG_FB_MATROX),y)
L_OBJS += matroxfb.o
else
  ifeq ($(CONFIG_FB_MATROX),m)
  M_OBJS += matroxfb.o
  endif
endif

# Generic Low Level Drivers

ifeq ($(CONFIG_FBCON_AFB),y)
OX_OBJS += fbcon-afb.o
else
  ifeq ($(CONFIG_FBCON_AFB),m)
  MX_OBJS += fbcon-afb.o
  endif
endif

ifeq ($(CONFIG_FBCON_CFB2),y)
OX_OBJS += fbcon-cfb2.o
else
  ifeq ($(CONFIG_FBCON_CFB2),m)
  MX_OBJS += fbcon-cfb2.o
  endif
endif

ifeq ($(CONFIG_FBCON_CFB4),y)
OX_OBJS += fbcon-cfb4.o
else
  ifeq ($(CONFIG_FBCON_CFB4),m)
  MX_OBJS += fbcon-cfb4.o
  endif
endif

ifeq ($(CONFIG_FBCON_CFB8),y)
OX_OBJS += fbcon-cfb8.o
else
  ifeq ($(CONFIG_FBCON_CFB8),m)
  MX_OBJS += fbcon-cfb8.o
  endif
endif

ifeq ($(CONFIG_FBCON_CFB16),y)
OX_OBJS += fbcon-cfb16.o
else
  ifeq ($(CONFIG_FBCON_CFB16),m)
  MX_OBJS += fbcon-cfb16.o
  endif
endif

ifeq ($(CONFIG_FBCON_CFB24),y)
OX_OBJS += fbcon-cfb24.o
else
  ifeq ($(CONFIG_FBCON_CFB24),m)
  MX_OBJS += fbcon-cfb24.o
  endif
endif

ifeq ($(CONFIG_FBCON_CFB32),y)
OX_OBJS += fbcon-cfb32.o
else
  ifeq ($(CONFIG_FBCON_CFB32),m)
  MX_OBJS += fbcon-cfb32.o
  endif
endif

ifeq ($(CONFIG_FBCON_ILBM),y)
OX_OBJS += fbcon-ilbm.o
else
  ifeq ($(CONFIG_FBCON_ILBM),m)
  MX_OBJS += fbcon-ilbm.o
  endif
endif

ifeq ($(CONFIG_FBCON_IPLAN2P2),y)
OX_OBJS += fbcon-iplan2p2.o
else
  ifeq ($(CONFIG_FBCON_IPLAN2P2),m)
  MX_OBJS += fbcon-iplan2p2.o
  endif
endif

ifeq ($(CONFIG_FBCON_IPLAN2P4),y)
OX_OBJS += fbcon-iplan2p4.o
else
  ifeq ($(CONFIG_FBCON_IPLAN2P4),m)
  MX_OBJS += fbcon-iplan2p4.o
  endif
endif

ifeq ($(CONFIG_FBCON_IPLAN2P8),y)
OX_OBJS += fbcon-iplan2p8.o
else
  ifeq ($(CONFIG_FBCON_IPLAN2P8),m)
  MX_OBJS += fbcon-iplan2p8.o
  endif
endif

ifeq ($(CONFIG_FBCON_IPLAN2P16),y)
OX_OBJS += fbcon-iplan2p16.o
else
  ifeq ($(CONFIG_FBCON_IPLAN2P16),m)
  MX_OBJS += fbcon-iplan2p16.o
  endif
endif

ifeq ($(CONFIG_FBCON_MAC),y)
OX_OBJS += fbcon-mac.o
else
  ifeq ($(CONFIG_FBCON_MAC),m)
  MX_OBJS += fbcon-mac.o
  endif
endif

ifeq ($(CONFIG_FBCON_MFB),y)
OX_OBJS += fbcon-mfb.o
else
  ifeq ($(CONFIG_FBCON_MFB),m)
  MX_OBJS += fbcon-mfb.o
  endif
endif

ifeq ($(CONFIG_FBCON_VGA),y)
OX_OBJS += fbcon-vga.o
else
  ifeq ($(CONFIG_FBCON_VGA),m)
  MX_OBJS += fbcon-vga.o
  endif
endif

# VGA Text Console

ifdef CONFIG_VGA_CONSOLE
L_OBJS += vgacon.o
endif

# MDA Text Console

ifeq ($(CONFIG_MDA_CONSOLE),y)
L_OBJS += mdacon.o
else
  ifeq ($(CONFIG_MDA_CONSOLE),m)
  M_OBJS += mdacon.o
  endif
endif

include $(TOPDIR)/Rules.make

promcon_tbl.c: prom.uni ../char/conmakehash
	../char/conmakehash prom.uni | \
	sed -e '/#include <[^>]*>/p' -e 's/types/init/' \
	    -e 's/dfont\(_uni.*\]\)/promfont\1 __initdata/' > promcon_tbl.c

promcon_tbl.o: promcon_tbl.c $(TOPDIR)/include/linux/types.h
