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
	$(CPP) $(CFLAGS) -D__ASSEMBLY__ -traditional $< -o $*.s
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o

OBJS  = process.o signal.o entry.o traps.o irq.o ptrace.o cache.o resume.o \
	ioport.o setup.o bios32.o tynedma.o

all: kernel.o head.o

cache.o: cache.s

cache.s: cache.S $(TOPDIR)/include/asm/mipsconfig.h \
	$(TOPDIR)/include/asm/regdef.h $(TOPDIR)/include/asm/segment.h

entry.o: entry.s

entry.s: entry.S $(TOPDIR)/include/linux/sys.h \
	$(TOPDIR)/include/linux/autoconf.h $(TOPDIR)/include/asm/segment.h \
	$(TOPDIR)/include/asm/mipsregs.h $(TOPDIR)/include/asm/mipsconfig.h \
	$(TOPDIR)/include/asm/page.h $(TOPDIR)/include/asm/stackframe.h \
	$(TOPDIR)/include/asm/regdef.h $(TOPDIR)/include/asm/processor.h

head.o: head.s

head.s: head.S $(TOPDIR)/include/linux/tasks.h

resume.o: resume.s

resume.s: resume.S $(TOPDIR)/include/asm/regdef.h \
	$(TOPDIR)/include/asm/processor.h $(TOPDIR)/include/asm/mipsregs.h \
	$(TOPDIR)/include/asm/mipsconfig.h

tlb.o: tlb.s

tlb.s:	tlb.S $(TOPDIR)/include/asm/regdef.h $(TOPDIR)/include/asm/mipsregs.h \
	$(TOPDIR)/include/asm/bootinfo.h

kernel.o: $(OBJS)
	$(LD) -r -o kernel.o $(OBJS)
	sync

dep:
	$(CPP) -M *.c > .depend

modules:

dummy:

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
