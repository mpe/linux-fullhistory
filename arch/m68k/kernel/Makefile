#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o

all: head.o kernel.o
O_TARGET := kernel.o
O_OBJS := entry.o process.o traps.o ints.o signal.o ptrace.o \
	  setup.o sys_m68k.o time.o
OX_OBJS := m68k_ksyms.o

ifdef CONFIG_KGDB
O_OBJS += kgdb.o
endif

ifdef CONFIG_PCI
O_OBJS += bios32.o
endif

head.o: head.S m68k_defs.h

m68k_defs.h: m68k_defs.c m68k_defs.head
	rm -f m68k_defs.d
	SUNPRO_DEPENDENCIES="m68k_defs.d m68k_defs.h" \
	$(CC) $(filter-out -MD,$(CFLAGS)) -S m68k_defs.c
	cp m68k_defs.head m68k_defs.h
	grep '^#define' m68k_defs.s >> m68k_defs.h
	rm m68k_defs.s
-include m68k_defs.d

include $(TOPDIR)/Rules.make
