# $Id: Makefile,v 1.3 1997/08/29 11:08:34 davem Exp $
# Makefile for the Sparc64 boot stuff.
#
# Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
# Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)

ROOT_IMG	=/usr/src/root.img
ELFTOAOUT	=elftoaout

all: boot

boot:
	@echo "Nothing special to be done for 'boot' on Linux/UltraSPARC."

tftpboot.img: piggyback
	$(ELFTOAOUT) $(TOPDIR)/vmlinux -o tftpboot.img
	./piggyback tftpboot.img $(TOPDIR)/System.map $(ROOT_IMG)

piggyback: piggyback.c
	$(HOSTCC) $(HOSTCFLAGS) -o piggyback piggyback.c

dep:

