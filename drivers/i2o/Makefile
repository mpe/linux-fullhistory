#
# Makefile for the kernel I2O OSM.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now inherited from the
# parent makefile.
#

#
# Note : at this point, these files are compiled on all systems.
# In the future, some of these should be built conditionally.
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)


L_TARGET := i2o.a
L_OBJS   := 
M_OBJS   :=

ifeq ($(CONFIG_I2O_PCI),y)
L_OBJS += i2o_pci.o
else
  ifeq ($(CONFIG_I2O_PCI),m)
  MX_OBJS += i2o_pci.o
  endif
endif

ifeq ($(CONFIG_I2O),y)
LX_OBJS += i2o_core.o i2o_config.o
else
  ifeq ($(CONFIG_I2O),m)
  MX_OBJS += i2o_core.o i2o_config.o
  endif
endif

ifeq ($(CONFIG_I2O_BLOCK),y)
LX_OBJS += i2o_block.o
else
  ifeq ($(CONFIG_I2O_BLOCK),m)
  MX_OBJS += i2o_block.o
  endif
endif

ifeq ($(CONFIG_I2O_LAN),y)
LX_OBJS += i2o_lan.o
else
  ifeq ($(CONFIG_I2O_LAN),m)
  MX_OBJS += i2o_lan.o
  endif
endif

ifeq ($(CONFIG_I2O_SCSI),y)
LX_OBJS += i2o_scsi.o
else
  ifeq ($(CONFIG_I2O_SCSI),m)
  MX_OBJS += i2o_scsi.o
  endif
endif

ifeq ($(CONFIG_I2O_PROC),y)
LX_OBJS += i2o_proc.o
else
  ifeq ($(CONFIG_I2O_PROC),m)
  MX_OBJS += i2o_proc.o
  endif
endif

include $(TOPDIR)/Rules.make

