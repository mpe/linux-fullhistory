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

O_TARGET := kernel.o
O_OBJS    = sched.o dma.o fork.o exec_domain.o panic.o printk.o sys.o \
	module.o exit.o signal.o itimer.o info.o time.o softirq.o \
	resource.o

SYMTAB_OBJS = ksyms.o
O_OBJS     += $(SYMTAB_OBJS)

ifdef CONFIG_MODVERSIONS
$(O_TARGET): $(SYMTAB_OBJS:.o=.ver)
dep: $(SYMTAB_OBJS:.o=.ver)
fastdep: $(SYMTAB_OBJS:.o=.ver)
endif

include $(TOPDIR)/Rules.make

include ../versions.mk

sched.o: sched.c
	$(CC) $(CFLAGS) $(PROFILING) -fno-omit-frame-pointer -c $<
