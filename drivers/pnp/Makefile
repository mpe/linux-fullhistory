#
# Makefile for the kernel Plug-and-Play device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

PROC_OBJS :=

ifdef CONFIG_PROC_FS
PROC_OBJS   += isapnp_proc.o
endif

ifeq ($(CONFIG_ISAPNP),m)
	M_OBJS   := isa-pnp.o
	MIX_OBJS := isapnp.o
	MI_OBJS  := quirks.o $(PROC_OBJS)
endif

ifeq ($(CONFIG_ISAPNP),y)
	O_TARGET := isa-pnp.o
	OX_OBJS  := isapnp.o
	O_OBJS   := quirks.o $(PROC_OBJS)
endif


include $(TOPDIR)/Rules.make

isa-pnp.o: isapnp.o quirks.o $(PROC_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ isapnp.o quirks.o $(PROC_OBJS)
