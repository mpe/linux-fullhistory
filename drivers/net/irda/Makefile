#
# Makefile for the Linux IrDA infrared port device drivers.
#
# 9 Aug 2000, Christoph Hellwig <hch@caldera.de>
# Rewritten to use lists instead of if-statements.
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

O_TARGET := irda.o

export-objs	= irport.o


obj-$(CONFIG_IRTTY_SIR)		+= irtty.o
obj-$(CONFIG_IRPORT_SIR)	+= 		irport.o
obj-$(CONFIG_NSC_FIR)		+= nsc-ircc.o
obj-$(CONFIG_WINBOND_FIR)	+= w83977af_ir.o
obj-$(CONFIG_TOSHIBA_FIR)	+= toshoboe.o
obj-$(CONFIG_SMC_IRCC_FIR)	+= smc-ircc.o	irport.o
obj-$(CONFIG_ESI_DONGLE)	+= esi.o
obj-$(CONFIG_TEKRAM_DONGLE)	+= tekram.o
obj-$(CONFIG_ACTISYS_DONGLE)	+= actisys.o
obj-$(CONFIG_GIRBIL_DONGLE)	+= girbil.o
obj-$(CONFIG_LITELINK_DONGLE)	+= litelink.o
obj-$(CONFIG_OLD_BELKIN_DONGLE)	+= old_belkin.o


# Files that are both resident and modular: remove from modular.
obj-m		:= $(filter-out $(obj-y), $(obj-m))

# Translate to Rules.make lists.
O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS		:= $(filter     $(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))

include $(TOPDIR)/Rules.make
