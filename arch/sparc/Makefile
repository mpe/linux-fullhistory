#
# sparc/Makefile
#
# Makefile for the architecture dependant flags and dependancies on the
# Sparc.
#
# Copyright (C) 1994 David S. Miller (davem@caip.rutgers.edu)
#

#
# How to link, we send the linker the address at which the text section
# is to start. The prom loads us at 0x0-kernel_size. There is also an
# alias of this address space at 0xf8000000-(0xf8000000+kernel_size) but
# I ignore it and eliminate those mappings during vm initialization and
# just leave the low mapping.
#
LINKFLAGS = -N -Ttext 0x00004000
CFLAGS := $(CFLAGS) -pipe

HEAD := arch/sparc/kernel/head.o

SUBDIRS := $(SUBDIRS) arch/sparc/kernel
ARCHIVES := arch/sparc/kernel/kernel.o $(ARCHIVES)

archclean:

archdep:
