# File: drivers/atm/Makefile
#
# Makefile for the Linux network (ATM) device drivers.
#

O_TARGET := atm.o
O_OBJS	 := atmdev_init.o
M_OBJS	 :=
MOD_LIST_NAME := ATM_MODULES

include ../../.config

ifeq ($(CONFIG_ATM_ENI),y)
O_OBJS += eni.o
NEED_SUNI_LX = suni.o
else
  ifeq ($(CONFIG_ATM_ENI),m)
  M_OBJS += eni.o
  NEED_SUNI_MX = suni.o
  endif
endif

ifeq ($(CONFIG_ATM_ZATM),y)
O_OBJS += zatm.o
OX_OBJS += uPD98402.o
else
  ifeq ($(CONFIG_ATM_ZATM),m)
  M_OBJS += zatm.o
  MX_OBJS += uPD98402.o
  endif
endif

ifeq ($(CONFIG_ATM_NICSTAR),y)
O_OBJS += nicstar.o
  ifeq ($(CONFIG_ATM_NICSTAR_USE_SUNI),y)
  NEED_SUNI_LX = suni.o
  endif
  ifeq ($(CONFIG_ATM_NICSTAR_USE_IDT77105),y)
  NEED_IDT77105_LX = idt77105.o
  endif
else
  ifeq ($(CONFIG_ATM_NICSTAR),m)
  M_OBJS += nicstar.o
    ifeq ($(CONFIG_ATM_NICSTAR_USE_SUNI),y)
    NEED_SUNI_MX = suni.o
    endif
    ifeq ($(CONFIG_ATM_NICSTAR_USE_IDT77105),y)
    NEED_SUNI_MX = idt77105.o
    endif
  endif
endif

ifeq ($(CONFIG_ATM_HORIZON),y)
O_OBJS += horizon.o
else
  ifeq ($(CONFIG_ATM_HORIZON),m)
  M_OBJS += horizon.o
  endif
endif

ifeq ($(CONFIG_ATM_AMBASSADOR),y)
O_OBJS += ambassador.o
else
  ifeq ($(CONFIG_ATM_AMBASSADOR),m)
  M_OBJS += ambassador.o
  endif
endif

ifeq ($(CONFIG_ATM_TCP),y)
O_OBJS += atmtcp.o
else
  ifeq ($(CONFIG_ATM_TCP),m)
  M_OBJS += atmtcp.o
  endif
endif

ifeq ($(CONFIG_ATM_IA),y)
O_OBJS += iphase.o
NEED_SUNI_LX = suni.o
else
ifeq ($(CONFIG_ATM_IA),m)
  M_OBJS += iphase.o
  NEED_SUNI_MX = suni.o
  endif
endif

ifeq ($(NEED_SUNI_LX),)
  MX_OBJS += $(NEED_SUNI_MX)
else
  OX_OBJS += $(NEED_SUNI_LX)
endif

ifeq ($(NEED_IDT77105_LX),)
  MX_OBJS += $(NEED_IDT77105_MX)
else
  OX_OBJS += $(NEED_IDT77105_LX)
endif

ifeq ($(CONFIG_ATM_FORE200E_PCA),y)
  FORE200E_FW_OBJS += fore200e_pca_fw.o
  ifeq ($(strip $(CONFIG_ATM_FORE200E_PCA_FW)),"")
    CONFIG_ATM_FORE200E_PCA_DEFAULT_FW := y
  endif
  ifeq ($(CONFIG_ATM_FORE200E_PCA_DEFAULT_FW),y)
#   guess the target endianess to choose the right PCA-200E firmware image
    CONFIG_ATM_FORE200E_PCA_FW := $(shell if test -n "`$(CC) -E -dM ../../include/asm/byteorder.h | grep ' __LITTLE_ENDIAN '`"; then echo pca200e.bin; else echo pca200e_ecd.bin2; fi)
  endif
endif
ifeq ($(CONFIG_ATM_FORE200E_SBA),y)
  FORE200E_FW_OBJS += fore200e_sba_fw.o
  ifeq ($(strip $(CONFIG_ATM_FORE200E_SBA_FW)),"")
    CONFIG_ATM_FORE200E_SBA_DEFAULT_FW := y
  endif
  ifeq ($(CONFIG_ATM_FORE200E_SBA_DEFAULT_FW),y)
    CONFIG_ATM_FORE200E_SBA_FW := sba200e_ecd.bin2
  endif
endif
ifeq ($(CONFIG_ATM_FORE200E),y)
  O_OBJS += fore200e.o $(FORE200E_FW_OBJS)
else
  ifeq ($(CONFIG_ATM_FORE200E),m)
    M_OBJS += fore_200e.o
  endif
endif

EXTRA_CFLAGS=-g

include $(TOPDIR)/Rules.make


# FORE Systems 200E-series firmware magic
fore200e_pca_fw.c: $(patsubst "%", %, $(CONFIG_ATM_FORE200E_PCA_FW)) \
	  fore200e_mkfirm
	./fore200e_mkfirm -k -b _fore200e_pca_fw \
	  -i $(CONFIG_ATM_FORE200E_PCA_FW) -o $@
	@ ( \
	  echo 'ifeq ($(strip $(CONFIG_ATM_FORE200E_PCA_FW)), $$(CONFIG_ATM_FORE200E_PCA_FW))'; \
	  echo 'FORE200E_FW_UP_TO_DATE += $@'; \
	  echo 'endif' \
	) >.$@.fw

fore200e_sba_fw.c: $(patsubst "%", %, $(CONFIG_ATM_FORE200E_SBA_FW)) \
	  fore200e_mkfirm
	./fore200e_mkfirm -k -b _fore200e_sba_fw \
	  -i $(CONFIG_ATM_FORE200E_SBA_FW) -o $@
	@ ( \
	  echo 'ifeq ($(strip $(CONFIG_ATM_FORE200E_SBA_FW)), $$(CONFIG_ATM_FORE200E_SBA_FW))'; \
	  echo 'FORE200E_FW_UP_TO_DATE += $@'; \
	  echo 'endif' \
	) >.$@.fw

fore200e_mkfirm: fore200e_mkfirm.c
	$(HOSTCC) $(HOSTCFLAGS) $< -o $@

# deal with the various suffixes of the binary firmware images
%.bin %.bin1 %.bin2: %.data
	objcopy -Iihex $< -Obinary $@.gz
	gzip -df $@.gz

# module build
fore_200e.o: fore200e.o $(FORE200E_FW_OBJS)
	$(LD) -r -o $@ $< $(FORE200E_FW_OBJS)

# firmware dependency stuff taken from drivers/sound/Makefile
FORE200E_FW_UP_TO_DATE :=

FORE200E_FW_FILES := $(wildcard .fore200e_*.fw)
ifneq ($(FORE200E_FW_FILES),)
include $(FORE200E_FW_FILES)
endif

FORE200E_FW_CHANGED := $(filter-out $(FORE200E_FW_UP_TO_DATE), \
                       fore200e_pca_fw.c fore200e_sba_fw.c)

ifneq ($(FORE200E_FW_CHANGED),)
$(FORE200E_FW_CHANGED): dummy
endif
