# Makefile for the Linux video drivers.
# 5 Aug 1999, James Simmons, <mailto:jsimmons@edgeglobal.com>
# Rewritten to use lists instead of if-statements.

SUB_DIRS        :=
MOD_SUB_DIRS    := $(SUB_DIRS)
MOD_IN_SUB_DIRS :=
ALL_SUB_DIRS    := $(SUB_DIRS) matrox

O_TARGET := video.o
O_OBJS   :=
M_OBJS   :=
# This is a nice idea but needs depmod altering
# MOD_LIST_NAME := VIDEO_MODULES

# All of the (potential) objects that export symbols.
# This list comes from 'grep -l EXPORT_SYMBOL *.[hc]'.

export-objs    :=  fbmem.o fbcmap.o fbcon.o fbcon-afb.o fbcon-ilbm.o fbcon-vga.o \
		   fbcon-iplan2p2.o fbcon-iplan2p4.o fbcon-iplan2p8.o fbcon-vga-planes.o \
		   fbcon-cfb16.o fbcon-cfb2.o fbcon-cfb24.o fbcon-cfb32.o fbcon-cfb4.o fbcon-cfb8.o \
		   fbcon-mac.o fbcon-mfb.o fbcon-vga8-planes.o \
		   matrox/matroxfb.o

# Object file lists.
obj-y          :=
obj-m          :=
obj-n          :=
obj-           :=

# Each configuration option enables a list of files.

obj-$(CONFIG_DUMMY_CONSOLE)       += dummycon.o
obj-$(CONFIG_SGI_NEWPORT_CONSOLE) += newport_con.o vga_font.o
obj-$(CONFIG_PROM_CONSOLE)        += promcon.o promcon_tbl.o
obj-$(CONFIG_VGA_CONSOLE)         += vgacon.o
obj-$(CONFIG_MDA_CONSOLE)         += mdacon.o

obj-$(CONFIG_FONT_SUN8x16)        += font_sun8x16.o
obj-$(CONFIG_FONT_SUN12x22)       += font_sun12x22.o
obj-$(CONFIG_FONT_8x8)            += font_8x8.o
obj-$(CONFIG_FONT_8x16)           += font_8x16.o
obj-$(CONFIG_FONT_6x11)           += font_6x11.o
obj-$(CONFIG_FONT_PEARL_8x8)      += font_pearl_8x8.o
obj-$(CONFIG_FONT_ACORN_8x8)      += font_acorn_8x8.o

obj-$(CONFIG_FB)                  += fbmem.o fbcmap.o modedb.o fbcon.o fonts.o fbmon.o 

obj-$(CONFIG_FB_ACORN)            += acornfb.o
obj-$(CONFIG_FB_AMIGA)            += amifb.o
obj-$(CONFIG_FB_PM2)              += pm2fb.o fbgen.o
obj-$(CONFIG_FB_APOLLO)           += dnfb.o
obj-$(CONFIG_FB_Q40)              += q40fb.o
obj-$(CONFIG_FB_ATARI)            += atafb.o
obj-$(CONFIG_FB_ATY)              += atyfb.o
obj-$(CONFIG_FB_ATY128)           += aty128fb.o
obj-$(CONFIG_FB_IGA)              += igafb.o
obj-$(CONFIG_FB_CONTROL)          += controlfb.o
obj-$(CONFIG_FB_PLATINUM)         += platinumfb.o
obj-$(CONFIG_FB_VALKYRIE)         += valkyriefb.o
obj-$(CONFIG_FB_CT65550)          += chipsfb.o
obj-$(CONFIG_FB_CYBER)            += cyberfb.o
obj-$(CONFIG_FB_CYBER2000)        += cyber2000fb.o
obj-$(CONFIG_FB_SGIVW)            += sgivwfb.o
obj-$(CONFIG_FB_RIVA)             += rivafb.o riva_hw.o
obj-$(CONFIG_FB_3DFX)             += tdfxfb.o
obj-$(CONFIG_FB_MAC)              += macfb.o macmodes.o
obj-$(CONFIG_FB_HP300)            += hpfb.o
obj-$(CONFIG_FB_OF)               += offb.o macmodes.o
obj-$(CONFIG_FB_IMSTT)            += imsttfb.o
obj-$(CONFIG_FB_RETINAZ3)         += retz3fb.o
obj-$(CONFIG_FB_CLGEN)            += clgenfb.o fbgen.o
obj-$(CONFIG_FB_S3TRIO)           += S3triofb.o
obj-$(CONFIG_FB_TGA)              += tgafb.o fbgen.o
obj-$(CONFIG_FB_VESA)             += vesafb.o 
obj-$(CONFIG_FB_VGA16)            += vga16fb.o fbcon-vga-planes.o
obj-$(CONFIG_FB_VIRGE)            += virgefb.o
obj-$(CONFIG_FB_G364)             += g364fb.o
obj-$(CONFIG_FB_FM2)              += fm2fb.o
obj-$(CONFIG_FB_CREATOR)          += creatorfb.o sbusfb.o
obj-$(CONFIG_FB_CGSIX)            += cgsixfb.o sbusfb.o
obj-$(CONFIG_FB_BWTWO)            += bwtwofb.o sbusfb.o
obj-$(CONFIG_FB_CGTHREE)          += cgthreefb.o sbusfb.o
obj-$(CONFIG_FB_TCX)              += tcxfb.o sbusfb.o
obj-$(CONFIG_FB_CGFOURTEEN)       += cgfourteenfb.o sbusfb.o
obj-$(CONFIG_FB_P9100)            += p9100fb.o sbusfb.o
obj-$(CONFIG_FB_LEO)              += leofb.o sbusfb.o
obj-$(CONFIG_FB_SUN3)             += sun3fb.o
obj-$(CONFIG_FB_BWTWO)            += bwtwofb.o
obj-$(CONFIG_FB_VIRTUAL)          += vfb.o  

ifeq ($(CONFIG_FB_MATROX),y)
SUB_DIRS			  += matrox
obj-y				  += matrox/matrox.o
MOD_SUB_DIRS			  += matrox
else
  ifeq ($(CONFIG_FB_MATROX),m)
  MOD_SUB_DIRS			  += matrox
  endif
endif

# Generic Low Level Drivers

obj-$(CONFIG_FBCON_AFB)           += fbcon-afb.o
obj-$(CONFIG_FBCON_CFB2)          += fbcon-cfb2.o
obj-$(CONFIG_FBCON_CFB4)          += fbcon-cfb4.o
obj-$(CONFIG_FBCON_CFB8)          += fbcon-cfb8.o
obj-$(CONFIG_FBCON_CFB16)         += fbcon-cfb16.o
obj-$(CONFIG_FBCON_CFB24)         += fbcon-cfb24.o
obj-$(CONFIG_FBCON_CFB32)         += fbcon-cfb32.o
obj-$(CONFIG_FBCON_ILBM)          += fbcon-ilbm.o
obj-$(CONFIG_FBCON_IPLAN2P2)      += fbcon-iplan2p2.o
obj-$(CONFIG_FBCON_IPLAN2P4)      += fbcon-iplan2p4.o
obj-$(CONFIG_FBCON_IPLAN2P8)      += fbcon-iplan2p8.o
obj-$(CONFIG_FBCON_IPLAN2P16)     += fbcon-iplan2p16.o
obj-$(CONFIG_FBCON_MAC)           += fbcon-mac.o
obj-$(CONFIG_FBCON_MFB)           += fbcon-mfb.o
obj-$(CONFIG_FBCON_VGA)           += fbcon-vga.o

# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.

multi-y         := $(filter $(list-multi), $(obj-y))
multi-m         := $(filter $(list-multi), $(obj-m))
int-y           := $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m           := $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Files that are both resident and modular: remove from modular.

obj-m          := $(filter-out $(obj-y), $(obj-m))
int-m          := $(filter-out $(int-y), $(int-m))

# Take multi-part drivers out of obj-y and put components in.

obj-y          := $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.

O_OBJS         := $(filter-out $(export-objs), $(obj-y))
OX_OBJS        := $(filter     $(export-objs), $(obj-y))
M_OBJS         := $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS        := $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s

../conmakehash: ../conmakehash.c
	$(HOSTCC) $(HOSTCFLAGS) -o ../conmakehash ../conmakehash.c

promcon_tbl.c: prom.uni ../char/conmakehash
	../char/conmakehash prom.uni | \
	sed -e '/#include <[^>]*>/p' -e 's/types/init/' \
	    -e 's/dfont\(_uni.*\]\)/promfont\1 __initdata/' > promcon_tbl.c

promcon_tbl.o: promcon_tbl.c $(TOPDIR)/include/linux/types.h
