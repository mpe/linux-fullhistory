#
# Makefile for the kernel Plug-and-Play device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..

O_TARGET := pnp.o

export-objs := isapnp.o
multi-objs := isa-pnp.o

ifdef CONFIG_PROC_FS
PROC_OBJS   += isapnp_proc.o
endif

isa-pnp-objs := isapnp.o quirks.o $(PROC_OBJS)

obj-$(CONFIG_ISAPNP) += isa-pnp.o

include $(TOPDIR)/Rules.make

isa-pnp.o: $(isa-pnp-objs)
	$(LD) $(LD_RFLAG) -r -o $@ $(isa-pnp-objs)
