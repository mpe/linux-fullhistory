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

ifdef CONFIG_ATIXL_BUSMOUSE
M = y
L_OBJS += atixlmouse.o
endif

ifdef CONFIG_BUSMOUSE
M = y
L_OBJS += busmouse.o
endif

ifdef CONFIG_PRINTER
L_OBJS += lp.o
else
M_OBJS += lp.o
endif

ifdef CONFIG_MS_BUSMOUSE
M = y
L_OBJS += msbusmouse.o
endif

ifdef CONFIG_82C710_MOUSE
CONFIG_PSMOUSE = CONFIG_PSMOUSE
endif

ifdef CONFIG_PSMOUSE
M = y
L_OBJS += psaux.o
endif

ifdef CONFIG_QIC02_TAPE
L_OBJS += tpqic02.o 
endif

ifdef M
L_OBJS += mouse.o
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
	./conmakehash $(FONTMAPFILE) 641 283 6 > uni_hash.tbl
