#
# Makefile for the kernel character device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

#
# This file contains the font map for the default (hardware) font
#
FONTMAPFILE = cp437.uni

L_TARGET := char.a
M_OBJS   :=
L_OBJS   := tty_io.o n_tty.o console.o keyboard.o serial.o \
	tty_ioctl.o pty.o vt.o mem.o vc_screen.o \
	defkeymap.o consolemap.o vesa_blank.o selection.o

ifdef CONFIG_CYCLADES
L_OBJS += cyclades.o
endif

ifeq ($(CONFIG_STALLION),y)
L_OBJS += stallion.o
else
  ifeq ($(CONFIG_STALLION),m)
  M_OBJS += stallion.o
  endif
endif

ifeq ($(CONFIG_ISTALLION),y)
L_OBJS += istallion.o
else
  ifeq ($(CONFIG_ISTALLION),m)
  M_OBJS += istallion.o
  endif
endif

ifeq ($(CONFIG_ATIXL_BUSMOUSE),y)
M = y
L_OBJS += atixlmouse.o
else
  ifeq ($(CONFIG_ATIXL_BUSMOUSE),m)
  M_OBJS += atixlmouse.o
  MM = m
  endif
endif

ifeq ($(CONFIG_BUSMOUSE),y)
M = y
L_OBJS += busmouse.o
else
  ifeq ($(CONFIG_BUSMOUSE),m)
  M_OBJS += busmouse.o
  MM = m
  endif
endif

ifeq ($(CONFIG_PRINTER),y)
L_OBJS += lp.o
else
  ifeq ($(CONFIG_PRINTER),m)
  M_OBJS += lp.o
  endif
endif

ifeq ($(CONFIG_MS_BUSMOUSE),y)
M = y
L_OBJS += msbusmouse.o
else
  ifeq ($(CONFIG_MS_BUSMOUSE),m)
  M_OBJS += msbusmouse.o
  MM = m
  endif
endif

ifeq ($(CONFIG_PSMOUSE),y)
M = y
L_OBJS += psaux.o
else
  ifeq ($(CONFIG_PSMOUSE),m)
  M_OBJS += psaux.o
  MM = m
  endif
endif

ifdef CONFIG_QIC02_TAPE
L_OBJS += tpqic02.o 
endif

ifdef M
L_OBJS += mouse.o
else
  ifdef MM
  M_OBJS += mouse.o
  endif
endif
  
ifdef CONFIG_SCC
L_OBJS += scc.o
endif

include $(TOPDIR)/Rules.make

fastdep: uni_hash.tbl

consolemap.o:

conmakehash: conmakehash.c
	$(HOSTCC) -o conmakehash conmakehash.c

uni_hash.tbl: $(FONTMAPFILE) conmakehash
	./conmakehash $(FONTMAPFILE) > uni_hash.tbl
