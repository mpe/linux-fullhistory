# $Id: Makefile,v 1.3 2000/01/17 23:32:46 ralf Exp $
#
# Makefile for the ARC prom monitor library routines under Linux.
#

L_TARGET = arclib.a
L_OBJS  = console.o init.o printf.o tree.o env.o cmdline.o misc.o time.o \
	  file.o identify.o

ifdef CONFIG_ARC_MEMORY
L_OBJS += memory.o
endif

include $(TOPDIR)/Rules.make
