#
# drivers/net/pcmcia/Makefile
#
# Makefile for the Linux PCMCIA network device drivers.
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

O_TARGET := pcmcia_net.o
MOD_LIST_NAME := PCMCIA_NET_MODULES

obj-y		:=
obj-m		:=
obj-n		:=
obj-		:=

# Things that need to export symbols
export-objs	:= ray_cs.o

# 16-bit client drivers
obj-$(CONFIG_PCMCIA_3C589)	+= 3c589_cs.o
obj-$(CONFIG_PCMCIA_3C574)	+= 3c574_cs.o
obj-$(CONFIG_PCMCIA_FMVJ18X)	+= fmvj18x_cs.o
obj-$(CONFIG_PCMCIA_NMCLAN)	+= nmclan_cs.o
obj-$(CONFIG_PCMCIA_PCNET)	+= pcnet_cs.o
obj-$(CONFIG_PCMCIA_SMC91C92)	+= smc91c92_cs.o
obj-$(CONFIG_PCMCIA_XIRC2PS)	+= xirc2ps_cs.o
obj-$(CONFIG_ARCNET_COM20020_CS)+= com20020_cs.o

# 16-bit wireless client drivers
obj-$(CONFIG_PCMCIA_RAYCS)	+= ray_cs.o
obj-$(CONFIG_PCMCIA_NETWAVE)	+= netwave_cs.o
obj-$(CONFIG_PCMCIA_WAVELAN)	+= wavelan_cs.o
obj-$(CONFIG_AIRONET4500_CS)	+= aironet4500_cs.o

# Cardbus client drivers
obj-$(CONFIG_PCMCIA_3C575)	+= 3c575_cb.o
obj-$(CONFIG_PCMCIA_XIRTULIP)	+= xircom_tulip_cb.o

obj-$(CONFIG_PCMCIA_IBMTR)	+= ibmtr_cs.o

O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS		:= $(filter     $(export-objs), $(obj-y))
M_OBJS		:= $(filter-out $(export-objs), $(obj-m))
MX_OBJS		:= $(filter     $(export-objs), $(obj-m))

include $(TOPDIR)/Rules.make

.ibmtr.o: ../tokenring/ibmtr.c
	$(CC) $(CFLAGS) -D__NO_VERSION__ -DPCMCIA -c -o $@ ../tokenring/ibmtr.c

ibmtr_cs.o: .ibmtr.o ibmtr_cs.c
	$(CC) $(CFLAGS) -DPCMCIA -c -o .$@ ibmtr_cs.c
	$(LD) -r -o $@ .$@ .ibmtr.o
