#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.s:
	$(CPP) -D__ASSEMBLY__ -traditional $< -o $*.s
.S.o:
	$(CC) -D__ASSEMBLY__ -traditional -c $< -o $*.o

all: kernel.o head.o

O_TARGET := kernel.o
O_OBJS   := entry.o traps.o process.o osf_sys.o irq.o signal.o setup.o \
	    bios32.o ptrace.o time.o fpreg.o
OX_OBJS  := alpha_ksyms.o


ifdef CONFIG_ALPHA_APECS
O_OBJS   += apecs.o
endif
ifdef CONFIG_ALPHA_CIA
O_OBJS   += cia.o
endif
ifdef CONFIG_ALPHA_LCA
O_OBJS   += lca.o
endif
ifdef CONFIG_ALPHA_PYXIS
O_OBJS   += pyxis.o
endif
ifdef CONFIG_ALPHA_T2
O_OBJS   += t2.o
endif
ifneq ($(CONFIG_ALPHA_PC164)$(CONFIG_ALPHA_LX164),nn)
O_OBJS   += smc37c93x.o
endif
ifdef CONFIG_ALPHA_SX164
O_OBJS   += smc37c669.o
endif


all: kernel.o head.o

head.o: head.s

head.s: head.S $(TOPDIR)/include/asm-alpha/system.h
	$(CPP) -traditional -o $*.s $<

include $(TOPDIR)/Rules.make
