# $Id: Makefile,v 1.8 1998/09/16 12:24:51 jj Exp $
# Makefile for the Sparc boot stuff.
#
# Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
# Copyright (C) 1997,1998 Jakub Jelinek (jj@ultra.linux.cz)

ROOT_IMG	=/usr/src/root.img
ELFTOAOUT	=elftoaout

all: btfix.o

tftpboot.img: piggyback
	$(ELFTOAOUT) $(TOPDIR)/vmlinux -o tftpboot.img
	./piggyback tftpboot.img $(TOPDIR)/System.map $(ROOT_IMG)

piggyback: piggyback.c
	$(HOSTCC) $(HOSTCFLAGS) -o piggyback piggyback.c

btfixupprep: btfixupprep.c
	$(HOSTCC) $(HOSTCFLAGS) -o btfixupprep btfixupprep.c

clean:
	rm -f btfixupprep piggyback tftpboot.img btfix.o btfix.s

BTOBJS := $(HEAD) init/main.o init/version.o \
	$(CORE_FILES_NO_BTFIX) $(FILESYSTEMS) \
	$(NETWORKS) $(DRIVERS)

vmlinux.o: dummy
	$(LD) -r $(patsubst %,$(TOPDIR)/%,$(BTOBJS)) $(LIBS) -o vmlinux.o

btfix.s: btfixupprep vmlinux.o
	$(OBJDUMP) -x vmlinux.o | ./btfixupprep > btfix.s

btfix.o: btfix.s
	$(CC) -c -o btfix.o btfix.s

include $(TOPDIR)/Rules.make
