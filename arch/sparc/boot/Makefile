# $Id: Makefile,v 1.4 1997/07/11 11:05:18 jj Exp $
# Makefile for the Sparc boot stuff.
#
# Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
# Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)

ROOT_IMG	=/usr/src/root.img
ELFTOAOUT	=elftoaout

all: boot

boot:
	@echo "Nothing special to be done for 'boot' on Linux/SPARC."

tftpboot.img: piggyback
	$(ELFTOAOUT) $(TOPDIR)/vmlinux -o tftpboot.img
	./piggyback tftpboot.img $(TOPDIR)/System.map $(ROOT_IMG)

piggyback: piggyback.c
	$(HOSTCC) $(HOSTCFLAGS) -o piggyback piggyback.c

dep:

