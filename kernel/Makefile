#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.s:
	$(CPP) -traditional $< -o $*.s
.c.s:
	$(CC) $(CFLAGS) -S $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c $<

OBJS  = sched.o dma.o fork.o exec_domain.o panic.o printk.o sys.o \
	module.o exit.o signal.o itimer.o info.o time.o softirq.o \
	resource.o

SYMTAB_OBJS = ksyms.o

all: kernel.o

include ../versions.mk

kernel.o: $(SYMTAB_OBJS) $(OBJS)
	$(LD) -r -o kernel.o $(SYMTAB_OBJS) $(OBJS)
	sync

sched.o: sched.c
	$(CC) $(CFLAGS) $(PROFILING) -fno-omit-frame-pointer -c $<

dep:
	$(CPP) -M *.c > .depend

dummy:
modules:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif

