#
# Makefile for the kernel i2c bus driver.
#

SUB_DIRS     :=
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)
MOD_LIST_NAME := I2C_MODULES

L_TARGET := i2c.a
MX_OBJS :=  
M_OBJS  := 
LX_OBJS :=
L_OBJS  := 

# -----
# i2c core components
# -----

ifeq ($(CONFIG_I2C),y)
  LX_OBJS += i2c-core.o
else
  ifeq ($(CONFIG_I2C),m)
    MX_OBJS += i2c-core.o
  endif
endif

ifeq ($(CONFIG_I2C_CHARDEV),y)
  L_OBJS += i2c-dev.o
else
  ifeq ($(CONFIG_I2C_CHARDEV),m)
    M_OBJS += i2c-dev.o
  endif
endif

# -----
# Bit banging adapters...
# -----

ifeq ($(CONFIG_I2C_ALGOBIT),y)
  LX_OBJS += i2c-algo-bit.o
else
  ifeq ($(CONFIG_I2C_ALGOBIT),m)
    MX_OBJS += i2c-algo-bit.o
  endif
endif

ifeq ($(CONFIG_I2C_PHILIPSPAR),y)
  L_OBJS += i2c-philips-par.o
else
  ifeq ($(CONFIG_I2C_PHILIPSPAR),m)
    M_OBJS += i2c-philips-par.o
  endif
endif

ifeq ($(CONFIG_I2C_ELV),y)
  L_OBJS += i2c-elv.o
else
  ifeq ($(CONFIG_I2C_ELV),m)
    M_OBJS += i2c-elv.o
  endif
endif

ifeq ($(CONFIG_I2C_VELLEMAN),y)
  L_OBJS += i2c-velleman.o
else
  ifeq ($(CONFIG_I2C_VELLEMAN),m)
    M_OBJS += i2c-velleman.o
  endif
endif



# -----
# PCF components
# -----

ifeq ($(CONFIG_I2C_ALGOPCF),y)
  LX_OBJS += i2c-algo-pcf.o
else
  ifeq ($(CONFIG_I2C_ALGOPCF),m)
    MX_OBJS += i2c-algo-pcf.o
  endif
endif

ifeq ($(CONFIG_I2C_ELEKTOR),y)
  L_OBJS += i2c-elektor.o
else
  ifeq ($(CONFIG_I2C_ELEKTOR),m)
    M_OBJS += i2c-elektor.o
  endif
endif

# This is needed for automatic patch generation: sensors code starts here
# This is needed for automatic patch generation: sensors code ends here

include $(TOPDIR)/Rules.make

