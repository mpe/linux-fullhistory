# $Id: Makefile,v 1.20 2000/07/04 08:58:10 dwmw2 Exp $

# Uncomment this to enable the DBG macro (see mtd.h)
#CFLAGS+= -DZDBG

ifndef CONFIG_MTD
# We're being invoked outside a normal kernel build. Fake it
EXTRA_CFLAGS= -I$(shell pwd)/../include

HWDRIVERS = slram.o docprobe.o doc1000.o nora.o physmap.o rpxlite.o vmax301.o octagon-5066.o pmc551.o mtdram.o
USERDRIVERS = ftl.o nftl.o mtdblock.o mtdchar.o
MIX_OBJS = mtdcore.o mapped.o doc2000.o doc2001.o cfi_probe.o cfi_cmdset_0001.o
MI_OBJS = $(HWDRIVERS) $(USERDRIVERS)
CFLAGS_nftl.o := -DCONFIG_NFTL_RW
else

O_TARGET	:= mtdlink.o
SUB_DIRS	:=
ALL_SUB_DIRS 	:=
MOD_LIST_NAME	:= MTD_MODULES


ifeq ($(CONFIG_MTD),y)
  OX_OBJS += mtdcore.o mapped.o
else
  ifeq ($(CONFIG_MTD),m)
    MX_OBJS += mtdcore.o mapped.o
  endif
endif

ifeq ($(CONFIG_MTD_NORA),y)
  O_OBJS += nora.o
else
  ifeq ($(CONFIG_MTD_NORA),m)
    M_OBJS += nora.o
  endif
endif

ifeq ($(CONFIG_MTD_RPXLITE),y)
  O_OBJS += rpxlite.o
else
  ifeq ($(CONFIG_MTD_RPXLITE),m)
    M_OBJS += rpxlite.o
  endif
endif

ifeq ($(CONFIG_MTD_PHYSMAP),y)
  O_OBJS += physmap.o
else
  ifeq ($(CONFIG_MTD_PHYSMAP),m)
    M_OBJS += physmap.o
  endif
endif

ifeq ($(CONFIG_MTD_CFI),y)
  OX_OBJS += cfi_probe.o
else
  ifeq ($(CONFIG_MTD_CFI),m)
    MX_OBJS += cfi_probe.o
  endif
endif

ifeq ($(CONFIG_MTD_CFI_INTELEXT),y)
  OX_OBJS += cfi_cmdset_0001.o
else
  ifeq ($(CONFIG_MTD_CFI_INTELEXT),m)
    MX_OBJS += cfi_cmdset_0001.o
  endif
endif

ifeq ($(CONFIG_MTD_DOC1000),y)
  O_OBJS += doc1000.o
else
  ifeq ($(CONFIG_MTD_DOC1000),m)
    M_OBJS += doc1000.o
  endif
endif

ifeq ($(CONFIG_MTD_DOC2000),y)
  OX_OBJS += doc2000.o
else
  ifeq ($(CONFIG_MTD_DOC2000),m)
    MX_OBJS += doc2000.o
  endif
endif

ifeq ($(CONFIG_MTD_DOC2001),y)
  OX_OBJS += doc2001.o
else
  ifeq ($(CONFIG_MTD_DOC2001),m)
    MX_OBJS += doc2001.o
  endif
endif

ifeq ($(CONFIG_MTD_DOCPROBE),y)
  O_OBJS += docprobe.o
else
  ifeq ($(CONFIG_MTD_DOCPROBE),m)
    M_OBJS += docprobe.o
  endif
endif

ifeq ($(CONFIG_MTD_SLRAM),y)
  O_OBJS += slram.o
else
  ifeq ($(CONFIG_MTD_SLRAM),m)
    M_OBJS += slram.o
  endif
endif

ifeq ($(CONFIG_MTD_OCTAGON),y)
  O_OBJS += octagon-5066.o
else
  ifeq ($(CONFIG_MTD_OCTAGON),m)
    M_OBJS += octagon-5066.o
  endif
endif

ifeq ($(CONFIG_MTD_PMC551),y)
  O_OBJS += pmc551.o
else
  ifeq ($(CONFIG_MTD_PMC551),m)
    M_OBJS += pmc551.o
  endif
endif

ifeq ($(CONFIG_MTD_PMC551_BUGFIX),y)
  CFLAGS_pmc551.o += -DPMC551_DRAM_BUG
endif

ifeq ($(CONFIG_MTD_VMAX),y)
  O_OBJS += vmax301.o
else
  ifeq ($(CONFIG_MTD_VMAX),m)
    M_OBJS += vmax301.o
  endif
endif

ifeq ($(CONFIG_MTD_MIXMEM),y)
  O_OBJS += mixmem.o
else
  ifeq ($(CONFIG_MTD_MIXMEM),m)
    M_OBJS += mixmem.o
  endif
endif

ifeq ($(CONFIG_MTD_MTDRAM),y)
  O_OBJS += mtdram.o
else
  ifeq ($(CONFIG_MTD_MTDRAM),m)
    M_OBJS += mtdram.o
  endif
endif

ifeq ($(CONFIG_FTL),y)
  O_OBJS += ftl.o
else
  ifeq ($(CONFIG_FTL),m)
    M_OBJS += ftl.o
  endif
endif

ifeq ($(CONFIG_NFTL),y)
  O_OBJS += nftl.o
else
  ifeq ($(CONFIG_NFTL),m)
    M_OBJS += nftl.o
  endif
endif

ifeq ($(CONFIG_MTD_BLOCK),y)
  O_OBJS += mtdblock.o
else
  ifeq ($(CONFIG_MTD_BLOCK),m)
    M_OBJS += mtdblock.o
  endif
endif


ifeq ($(CONFIG_MTD_CHAR),y)
  O_OBJS += mtdchar.o
else
  ifeq ($(CONFIG_MTD_CHAR),m)
    M_OBJS += mtdchar.o
  endif
endif

endif

include $(TOPDIR)/Rules.make
