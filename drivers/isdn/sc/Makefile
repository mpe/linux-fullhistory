#
#  $Id: Makefile,v 1.1 1997/03/22 02:01:22 fritz Exp $
#  Copyright (C) 1996  SpellCaster Telecommunications Inc.
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
#
#  For more information, please contact gpl-info@spellcast.com or write:
#
#     SpellCaster Telecommunications Inc.
#     5621 Finch Avenue East, Unit #3
#     Scarborough, Ontario  Canada
#     M1B 2T9
#     +1 (416) 297-8565
#     +1 (416) 297-6433 Facsimile
#

L_OBJS :=
M_OBJS :=
O_OBJS := shmem.o init.o debug.o packet.o command.o event.o \
	ioctl.o interrupt.o message.o timer.o

O_TARGET :=
ifeq ($(CONFIG_ISDN_DRV_SC),y)
	O_TARGET += sc.o
else
  ifeq ($(CONFIG_ISDN_DRV_SC),m)
    O_TARGET += sc.o
    M_OBJS += sc.o
  endif
endif

include $(TOPDIR)/Rules.make
