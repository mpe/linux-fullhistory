#
# Makefile for the Linux/MIPS kernel.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

.S.s:
	$(CPP) $(CFLAGS) $< -o $*.s
.S.o:
	$(CC) $(CFLAGS) -c $< -o $*.o

all:	kernel.o head.o
EXTRA_ASFLAGS = -mips3 -mcpu=r4000
O_TARGET := kernel.o
O_OBJS	:= process.o signal.o entry.o traps.o irq.o ptrace.o vm86.o ioport.o \
	setup.o syscall.o sysmips.o time.o bios32.o ipc.o

#
# Kernel debugging
#
ifdef CONFIG_REMOTE_DEBUG
OBJS += gdb-low.o gdb-stub.o 
endif

#
# Board specific code
#
ifdef CONFIG_MIPS_JAZZ
O_OBJS += jazzdma.o jazz-c.o
endif

ifdef CONFIG_ACER_PICA_61
O_OBJS += pica.o
endif

ifdef CONFIG_DESKSTATION_TYNE
O_OBJS += tyne.o tyne-c.o
endif

ifdef CONFIG_MIPS_MAGNUM_4000
O_OBJS += magnum4000.o
endif

#
# CPU model specific code
#
ifdef CONFIG_CPU_R2000
O_OBJS += r3000.o
endif

ifdef CONFIG_CPU_R3000
O_OBJS += r3000.o
endif

ifdef CONFIG_CPU_R4X00
O_OBJS += r4xx0.o
endif

ifdef CONFIG_CPU_R4600
O_OBJS += r4xx0.o
endif

ifdef CONFIG_CPU_R6000
	exit 1			# no detailed informations about CPU yet.
endif

ifdef CONFIG_CPU_R8000
	exit 1			# no detailed informations about CPU yet.
endif

ifdef CONFIG_CPU_R10000
O_OBJS += r4xx0.o
endif

#
# Since we add the same object files to O_OBJS for different configurations.
# O_OBJS might contain duplicate files.  We correct this by filtering out
# duplicate files.  Just to avoid users having to know about all the
# compatibility stuff between various boards and boards.
#
O_OBJS := $(sort $(O_OBJS))

all: kernel.o head.o

entry.o: entry.S

head.o: head.S

magnum4000.o: magnum4000.S

pica.o: pica.S

r4xx0.o: r4xx0.S

tyne.o: tyne.S

clean:

include $(TOPDIR)/Rules.make
