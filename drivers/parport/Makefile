#
# Makefile for the kernel Parallel port device drivers.
#
# Note! Parport is the Borg.  We have assimilated some other
# drivers in the `char', `net' and `scsi' directories,
# but left them there to allay suspicion.
#
# 7 October 2000, Bartlomiej Zolnierkiewicz <bkz@linux-ide.org>
# Rewritten to use lists instead of if-statements.
#

L_TARGET := parport.a

export-objs	:= init.o parport_pc.o

list-multi	:= parport.o
parport-objs	:= share.o ieee1284.o ieee1284_ops.o init.o procfs.o

ifeq ($(CONFIG_PARPORT_1284),y)
	parport-objs	+= daisy.o probe.o
endif

obj-$(CONFIG_PARPORT)		+= parport.o
obj-$(CONFIG_PARPORT_PC)	+= parport_pc.o
obj-$(CONFIG_PARPORT_AMIGA)	+= parport_amiga.o
obj-$(CONFIG_PARPORT_MFC3)	+= parport_mfc3.o
obj-$(CONFIG_PARPORT_ATARI)	+= parport_atari.o
obj-$(CONFIG_PARPORT_SUNBPP)	+= parport_sunbpp.o

# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.
multi-y		:= $(filter $(list-multi), $(obj-y))
multi-m		:= $(filter $(list-multi), $(obj-m))
int-y		:= $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m		:= $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Take multi-part drivers out of obj-y and put components in.
obj-y		:= $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.
L_OBJS		:= $(filter-out $(export-objs), $(obj-y))
LX_OBJS		:= $(filter	$(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out	$(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter	$(export-objs), $(obj-m)))
MI_OBJS		:= $(sort $(filter-out	$(export-objs), $(int-m)))
MIX_OBJS	:= $(sort $(filter	$(export-objs), $(int-m)))

include $(TOPDIR)/Rules.make

parport.o: $(parport-objs)
	$(LD) -r -o $@ $(parport-objs)
