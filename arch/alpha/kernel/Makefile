#
# Makefile for the linux kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now in the main makefile...

.S.s:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -E -o $*.s $<
.S.o:
	$(CC) -D__ASSEMBLY__ $(AFLAGS) -c -o $*.o $<

all: kernel.o head.o

O_TARGET := kernel.o
O_OBJS   := entry.o traps.o process.o osf_sys.o irq.o signal.o setup.o \
	    bios32.o ptrace.o time.o fpreg.o
OX_OBJS  := alpha_ksyms.o


ifdef CONFIG_ALPHA_GENERIC

O_OBJS	 += core_apecs.o core_cia.o core_lca.o core_mcpcia.o core_pyxis.o \
	    core_t2.o core_tsunami.o core_polaris.o \
	    sys_alcor.o sys_cabriolet.o sys_dp264.o sys_eb64p.o \
	    sys_jensen.o sys_miata.o sys_mikasa.o sys_noritake.o \
	    sys_rawhide.o sys_ruffian.o sys_sable.o sys_sio.o \
	    sys_sx164.o sys_takara.o sys_rx164.o \
	    es1888.o smc37c669.o smc37c93x.o
else

# Core logic support
ifdef CONFIG_ALPHA_APECS
O_OBJS   += core_apecs.o
endif
ifdef CONFIG_ALPHA_CIA
O_OBJS   += core_cia.o
endif
ifdef CONFIG_ALPHA_LCA
O_OBJS   += core_lca.o
endif
ifdef CONFIG_ALPHA_MCPCIA
O_OBJS   += core_mcpcia.o
endif
ifdef CONFIG_ALPHA_PYXIS
O_OBJS   += core_pyxis.o
endif
ifdef CONFIG_ALPHA_T2
O_OBJS   += core_t2.o
endif
ifdef CONFIG_ALPHA_TSUNAMI
O_OBJS   += core_tsunami.o
endif
ifdef CONFIG_ALPHA_POLARIS
O_OBJS   += core_polaris.o
endif

# Board support
ifneq ($(CONFIG_ALPHA_ALCOR)$(CONFIG_ALPHA_XLT),)
O_OBJS   += sys_alcor.o
endif
ifneq ($(CONFIG_ALPHA_CABRIOLET)$(CONFIG_ALPHA_EB164)$(CONFIG_ALPHA_EB66P)$(CONFIG_ALPHA_LX164)$(CONFIG_ALPHA_PC164),)
O_OBJS   += sys_cabriolet.o
endif
ifdef CONFIG_ALPHA_DP264
O_OBJS   += sys_dp264.o
endif
ifneq ($(CONFIG_ALPHA_EB64P)$(CONFIG_ALPHA_EB66),)
O_OBJS   += sys_eb64p.o
endif
ifdef CONFIG_ALPHA_JENSEN
O_OBJS   += sys_jensen.o
endif
ifdef CONFIG_ALPHA_MIATA
O_OBJS   += sys_miata.o
endif
ifdef CONFIG_ALPHA_MIKASA
O_OBJS   += sys_mikasa.o
endif
ifdef CONFIG_ALPHA_NORITAKE
O_OBJS   += sys_noritake.o
endif
ifdef CONFIG_ALPHA_RAWHIDE
O_OBJS   += sys_rawhide.o
endif
ifdef CONFIG_ALPHA_RUFFIAN
O_OBJS   += sys_ruffian.o
endif
ifdef CONFIG_ALPHA_RX164
O_OBJS   += sys_rx164.o
endif
ifdef CONFIG_ALPHA_SABLE
O_OBJS   += sys_sable.o
endif
ifneq ($(CONFIG_ALPHA_BOOK1)$(CONFIG_ALPHA_AVANTI)$(CONFIG_ALPHA_NONAME)$(CONFIG_ALPHA_P2K)$(CONFIG_ALPHA_XL),)
O_OBJS   += sys_sio.o
endif
ifdef CONFIG_ALPHA_SX164
O_OBJS   += sys_sx164.o
endif
ifdef CONFIG_ALPHA_TAKARA
O_OBJS   += sys_takara.o
endif

# Device support
ifdef CONFIG_ALPHA_MIATA
O_OBJS   += es1888.o
endif
ifneq ($(CONFIG_ALPHA_SX164)$(CONFIG_ALPHA_MIATA)$(CONFIG_ALPHA_DP264),)
O_OBJS   += smc37c669.o
endif
ifneq ($(CONFIG_ALPHA_PC164)$(CONFIG_ALPHA_LX164),)
O_OBJS   += smc37c93x.o
endif

endif # GENERIC

ifdef CONFIG_SMP
O_OBJS   += smp.o
endif

all: kernel.o head.o

asm_offsets: check_asm
	./check_asm > $(TOPDIR)/include/asm-alpha/asm_offsets.h

check_asm: check_asm.c
	gcc -o $@ $< -I$(TOPDIR)/include -D__KERNEL__ -ffixed-8

clean::
	rm -f check_asm

include $(TOPDIR)/Rules.make
