#
# drivers/scsi/pcmcia/Makefile
#
# Makefile for the Linux PCMCIA SCSI drivers.
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

MOD_LIST_NAME := PCMCIA_SCSI_MODULES

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

vpath %c ..

CFLAGS_aha152x.o = -DPCMCIA -D__NO_VERSION__ -DAHA152X_STAT
CFLAGS_aic7xxx.o = -DPCMCIA -D__NO_VERSION__
CFLAGS_fdomain.o = -DPCMCIA -D__NO_VERSION__
CFLAGS_qlogicfas.o = -DPCMCIA -D__NO_VERSION__

# 16-bit client drivers
obj-$(CONFIG_PCMCIA_QLOGIC)	+= qlogic_cs.o
obj-$(CONFIG_PCMCIA_FDOMAIN)	+= fdomain_cs.o
obj-$(CONFIG_PCMCIA_AHA152X)	+= aha152x_cs.o

# Cardbus client drivers
obj-$(CONFIG_PCMCIA_APA1480)	+= apa1480_cb.o

list-multi	:= qlogic_cs.o fdomain_cs.o aha152x_cs.o apa1480_cb.o
aha152x-objs	:= aha152x_stub.o aha152x.o
apa1480-objs	:= apa1480_stub.o aic7xxx.o
fdomain-objs	:= fdomain_stub.o fdomain.o
qlogic-objs	:= qlogic_stub.o qlogicfas.o

# Extract lists of the multi-part drivers.

multi-y		:= $(filter $(list-multi), $(obj-y))
multi-m		:= $(filter $(list-multi), $(obj-m))
int-y		:= $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m		:= $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Translate to Rules.make lists.

O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS		:= $(filter     $(export-objs), $(obj-y))
M_OBJS		:= $(filter-out $(export-objs), $(obj-m))
MX_OBJS		:= $(filter     $(export-objs), $(obj-m))
MI_OBJS		:= $(filter-out $(export-objs), $(int-m))
MIX_OBJS	:= $(filter     $(export-objs), $(int-m))

include $(TOPDIR)/Rules.make

aha152x_cs.o: $(aha152x-objs)
	$(LD) -r -o $@ $(aha152x-objs)

apa1480_cb.o: $(apa1480-objs)
	$(LD) -r -o $@ $(apa1480-objs)

fdomain_cs.o: $(fdomain-objs)
	$(LD) -r -o $@ $(fdomain-objs)

qlogic_cs.o: $(qlogic-objs)
	$(LD) -r -o $@ $(qlogic-objs)
