#
# Makefile for Linux arch/m68k/console source directory
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

GSPA = gspa
GSPH2C = gspahextoc

L_TARGET = console.a
L_OBJS = fbcon.o fonts.o font_8x16.o font_8x8.o pearl_8x8.o
M_OBJS =

ifdef CONFIG_AMIGA_GSP
L_OBJS := $(L_OBJS) gspcon.o gspcore.o
endif

include $(TOPDIR)/Rules.make

gspcore.c: gspcore.gsp
	$(GSPA) $< > $*.hex
	$(GSPH2C) $*.hex > gspcore.c
