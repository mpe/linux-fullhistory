# $Id: Makefile,v 1.2 1999/12/04 03:59:01 ralf Exp $
#
# Makefile for MIPS kernel build tools.
#
# Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
# Copyright (C) 1997, 1999 Ralf Baechle (ralf@gnu.org)
# Copyright (C) 1999 Silicon Graphics, Inc.
#
TARGET	:= $(TOPDIR)/include/asm-$(ARCH)/offset.h

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

all: $(TARGET)

$(TARGET): offset.h
	cmp -s $^ $@ || (cp $^ $(TARGET).new && mv $(TARGET).new $(TARGET))

offset.h: offset.s
	sed -n '/^@@@/s///p' $^ >$@

offset.s: offset.c

clean:
	rm -f offset.[hs] $(TARGET).new

include $(TOPDIR)/Rules.make
