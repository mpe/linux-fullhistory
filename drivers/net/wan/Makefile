#
# Makefile for the Linux network (wan) device drivers.
#
# 3 Aug 2000, Christoph Hellwig <hch@caldera.de>
# Rewritten to use lists instead of if-statements.
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS) lmc

O_TARGET := wan.o

export-objs =	z85230.o syncppp.o comx.o sdladrv.o cycx_drv.o
list-multi =	wanpipe.o cyclomx.o

wanpipe-objs = sdlamain.o $(wanpipe-y)
wanpipe-$(CONFIG_WANPIPE_X25)	+= sdla_x25.o
wanpipe-$(CONFIG_WANPIPE_FR)	+= sdla_fr.o
wanpipe-$(CONFIG_WANPIPE_CHDLC)	+= sdla_chdlc.o
wanpipe-$(CONFIG_WANPIPE_PPP)	+= sdla_ppp.o

cyclomx-objs = cycx_main.o $(cyclomx-y)  
cyclomx-$(CONFIG_CYCLOMX_X25)	+= cycx_x25.o


obj-$(CONFIG_HOSTESS_SV11)	+= z85230.o	syncppp.o	hostess_sv11.o
obj-$(CONFIG_SEALEVEL_4021)	+= z85230.o	syncppp.o	sealevel.o
obj-$(CONFIG_COMX)		+= 				comx.o
obj-$(CONFIG_COMX_HW_COMX)	+= 				comx-hw-comx.o
obj-$(CONFIG_COMX_HW_LOCOMX)	+= z85230.o	syncppp.o	comx-hw-locomx.o
obj-$(CONFIG_COMX_HW_MIXCOM)	+=				comx-hw-mixcom.o
obj-$(CONFIG_COMX_PROTO_PPP)	+=		syncppp.o	comx-proto-ppp.o
obj-$(CONFIG_COMX_PROTO_LAPB)	+=				comx-proto-lapb.o
obj-$(CONFIG_COMX_PROTO_FR)	+=				comx-proto-fr.o
obj-$(CONFIG_COSA)		+=		syncppp.o	cosa.o
obj-$(CONFIG_LANMEDIA)		+=		syncppp.o
obj-$(CONFIG_X25_ASY)		+= x25_asy.o

ifeq ($(CONFIG_LANMEDIA),y)
  SUB_DIRS += lmc
  MOD_IN_SUB_DIRS += lmc
  obj-y += lmc/lmc.o
else
  ifeq ($(CONFIG_LANMEDIA),m)
  MOD_IN_SUB_DIRS += lmc
  endif
endif
 
obj-$(CONFIG_DLCI)		+= dlci.o 
obj-$(CONFIG_SDLA)		+= sdla.o
obj-$(CONFIG_VENDOR_SANGOMA)	+= sdladrv.o wanpipe.o
obj-$(CONFIG_CYCLADES_SYNC)	+= cycx_drv.o cyclomx.o
obj-$(CONFIG_LAPBETHER)		+= lapbether.o
obj-$(CONFIG_SBNI)		+= sbni.o


# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.
multi-y		:= $(filter $(list-multi), $(obj-y))
multi-m		:= $(filter $(list-multi), $(obj-m))
int-y		:= $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m		:= $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Files that are both resident and modular: remove from modular.
obj-m		:= $(filter-out $(obj-y), $(obj-m))
int-m		:= $(filter-out $(int-y), $(int-m))

# Take multi-part drivers out of obj-y and put components in.
obj-y		:= $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.
O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS		:= $(filter     $(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))
MI_OBJS		:= $(sort $(filter-out $(export-objs), $(int-m)))
MIX_OBJS	:= $(sort $(filter     $(export-objs), $(int-m)))

include $(TOPDIR)/Rules.make

wanpipe.o: $(wanpipe-objs)
	$(LD) -r -o $@ $(wanpipe-objs)

cyclomx.o: $(cyclomx-objs)
	$(LD) -r -o $@ $(cyclomx-objs)

