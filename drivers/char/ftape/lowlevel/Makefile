#
#       Copyright (C) 1996, 1997 Clau-Justus Heine.
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
# 
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING.  If not, write to
# the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
#
# $Source: /homes/cvs/ftape-stacked/ftape/lowlevel/Makefile,v $
# $Revision: 1.4 $
# $Date: 1997/10/07 09:26:02 $
#
#      Makefile for the lowlevel part QIC-40/80/3010/3020 floppy-tape
#      driver for Linux.
#

#
#  This isn't used inside the kernel, only for my private development
#  version
#
ifndef TOPDIR
TOPDIR=../..
include $(TOPDIR)/MCONFIG
endif

O_TARGET := ftape.o
O_OBJS    = ftape-init.o fdc-io.o fdc-isr.o \
	    ftape-bsm.o ftape-ctl.o ftape-read.o ftape-rw.o \
	    ftape-write.o ftape-io.o ftape-calibr.o ftape-ecc.o fc-10.o \
	    ftape-buffer.o ftape-format.o

ifeq ($(CONFIG_FTAPE),y)
O_OBJS   += ftape-setup.o
endif

ifndef CONFIG_FT_NO_TRACE_AT_ALL
O_OBJS   += ftape-tracing.o
endif

ifeq ($(CONFIG_PROC_FS),y)
ifeq ($(CONFIG_FT_PROC_FS),y)
O_OBJS += ftape-proc.o
endif
endif

OX_OBJS = ftape_syms.o

M_OBJS  = $(O_TARGET)

include $(TOPDIR)/Rules.make

