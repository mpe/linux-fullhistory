#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) $(ASFLAGS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<
.S.s:
	$(CPP) $(CFLAGS) -D__ASSEMBLY__ $< -o $*.s
.S.o:
	$(CPP) $(CFLAGS) -D__ASSEMBLY__ $< -o $*.s
	$(AS) $(ASFLAGS) -o $*.o $*.s
	rm $*.s

HOST_CC = gcc

OBJS  = misc.o setup.o port_io.o irq.o pci.o traps.o stubs.o process.o \
	signal.o ksyms.o time.o syscalls.o usercpy.o\
	support.o ptrace.o 

all: head.o kernel.o

head.o: head.s
head.s: head.S $(TOPDIR)/include/linux/tasks.h ppc_defs.h

ppc_defs.h: mk_defs
	mk_defs $@

mk_defs: mk_defs.c
	$(HOSTCC) $(CFLAGSINC) -Wl,-static ${CFLAGS} -o mk_defs mk_defs.c


kernel.o: $(OBJS)
	$(LD) -r -o kernel.o $(OBJS)
	sync

dep:
	$(CPP) -M *.c > .depend

fastdep:

modules:

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
