#
# Makefile for the memory technology device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#
# $Id: Makefile,v 1.22 2000/07/14 08:10:52 dwmw2 Exp $

ifndef CONFIG_MTD

# We're being invoked outside a normal kernel build. Fake it
EXTRA_CFLAGS= -I$(shell pwd)/../include

MIX_OBJS = mtdcore.o doc2000.o doc2001.o cfi_probe.o cfi_cmdset_0001.o \
	map_ram.o map_rom.o cfi_cmdset_0002.o
MI_OBJS = doc1000.o docprobe.o slram.o pmc551.o mtdram.o physmap.o \
	nora.o octagon-5066.o pnc2000.o rpxlite.o vmax301.o mtdchar.o \
	mtdblock.o ftl.o nftl.o

CFLAGS_nftl.o := -DCONFIG_NFTL_RW
CFLAGS_mtdram.o := -DCONFIG_MTDRAM_TOTAL_SIZE=4096 -DCONFIG_MTDRAM_ERASE_SIZE=128
CFLAGS_physmap.o := -DCONFIG_MTD_PHYSMAP_START=0x8000000 -DCONFIG_MTD_PHYSMAP_LEN=0x4000000

else 

O_OBJS          :=
OX_OBJS         :=
M_OBJS          :=
MX_OBJS         :=

# Object file lists.

obj-y           :=
obj-m           :=
obj-n           :=
obj-            :=

O_TARGET	:= mtdlink.o
SUB_DIRS	:=
ALL_SUB_DIRS 	:=
MOD_SUB_DIRS	:=

export-objs	:=	mtdcore.o doc2000.o doc2001.o cfi_probe.o cfi_cmdset_0001.o cfi_cmdset_0002.o
list-multi	:=

# MTD devices
obj-$(CONFIG_MTD)		+= mtdcore.o
obj-$(CONFIG_MTD_DOC1000)	+= doc1000.o
obj-$(CONFIG_MTD_DOC2000)	+= doc2000.o
obj-$(CONFIG_MTD_DOC2001)	+= doc2001.o
obj-$(CONFIG_MTD_DOCPROBE)	+= docprobe.o
obj-$(CONFIG_MTD_SLRAM)		+= slram.o
obj-$(CONFIG_MTD_PMC551)	+= pmc551.o
obj-$(CONFIG_MTD_MTDRAM)	+= mtdram.o

# Chip drivers
obj-$(CONFIG_MTD_JEDEC)		+= jedec.o
obj-$(CONFIG_MTD_RAM)		+= map_ram.o
obj-$(CONFIG_MTD_ROM)		+= map_rom.o
obj-$(CONFIG_MTD_CFI)		+= cfi_probe.o
obj-$(CONFIG_MTD_CFI_INTELEXT)	+= cfi_cmdset_0001.o
obj-$(CONFIG_MTD_CFI_AMDSTD)	+= cfi_cmdset_0002.o

# Chip mappings
obj-$(CONFIG_MTD_PHYSMAP)	+= physmap.o
obj-$(CONFIG_MTD_MIXMEM)	+= mixmem.o
obj-$(CONFIG_MTD_NORA)		+= nora.o
obj-$(CONFIG_MTD_OCTAGON)	+= octagon-5066.o
obj-$(CONFIG_MTD_PNC2000)	+= pnc2000.o
obj-$(CONFIG_MTD_RPXLITE)	+= rpxlite.o
obj-$(CONFIG_MTD_VMAX)		+= vmax301.o

# Users
obj-$(CONFIG_MTD_CHAR)		+= mtdchar.o
obj-$(CONFIG_MTD_BLOCK)		+= mtdblock.o
obj-$(CONFIG_FTL)		+= ftl.o
obj-$(CONFIG_NFTL)		+= nftl.o

# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.

multi-y         := $(filter $(list-multi), $(obj-y))
multi-m         := $(filter $(list-multi), $(obj-m))
int-y           := $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m           := $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Files that are both resident and modular: remove from modular.

obj-m           := $(filter-out $(obj-y), $(obj-m))
int-m           := $(filter-out $(int-y), $(int-m))

# Take multi-part drivers out of obj-y and put components in.

obj-y           := $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.

O_OBJS          := $(filter-out $(export-objs), $(obj-y))
OX_OBJS         := $(filter     $(export-objs), $(obj-y))
M_OBJS          := $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS         := $(sort $(filter     $(export-objs), $(obj-m)))


endif

include $(TOPDIR)/Rules.make
