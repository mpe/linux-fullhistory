# File: drivers/net/wan/Makefile
#
# Makefile for the Linux network (wan) device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now inherited from the
# parent makefile.
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

L_TARGET := wan.a
L_OBJS   :=
M_OBJS   :=

# Need these to keep track of whether the 82530 or SYNCPPP
# modules should really go in the kernel or a module.
CONFIG_85230_BUILTIN :=
CONFIG_85230_MODULE  :=
CONFIG_SYNCPPP_BUILTIN :=
CONFIG_SYNCPPP_MODULE  :=

ifeq ($(CONFIG_HOSTESS_SV11),y)
L_OBJS += hostess_sv11.o
CONFIG_85230_BUILTIN = y
CONFIG_SYNCPPP_BUILTIN = y
else
  ifeq ($(CONFIG_HOSTESS_SV11),m)
  CONFIG_85230_MODULE = y
  CONFIG_SYNCPPP_MODULE = y
  M_OBJS += hostess_sv11.o
  endif
endif

ifeq ($(CONFIG_SEALEVEL_4021),y)
L_OBJS += sealevel.o
CONFIG_85230_BUILTIN = y
CONFIG_SYNCPPP_BUILTIN = y
else
  ifeq ($(CONFIG_SEALEVEL_4021),m)
  CONFIG_85230_MODULE = y
  CONFIG_SYNCPPP_MODULE = y
  M_OBJS += sealevel.o
  endif
endif

ifeq ($(CONFIG_COSA),y)
L_OBJS += cosa.o
CONFIG_SYNCPPP_BUILTIN = y
else
  ifeq ($(CONFIG_COSA),m)
  CONFIG_SYNCPPP_MODULE = y
  M_OBJS += cosa.o
  endif
endif

# If anything built-in uses syncppp, then build it into the kernel also.
# If not, but a module uses it, build as a module.

ifdef CONFIG_SYNCPPP_BUILTIN
LX_OBJS += syncppp.o
else
  ifdef CONFIG_SYNCPPP_MODULE
  MX_OBJS += syncppp.o
  endif
endif

# If anything built-in uses Z85230, then build it into the kernel also.
# If not, but a module uses it, build as a module.

ifdef CONFIG_85230_BUILTIN
LX_OBJS += z85230.o
else
  ifdef CONFIG_85230_MODULE
  MX_OBJS += z85230.o
  endif
endif

ifeq ($(CONFIG_DLCI),y)
L_OBJS += dlci.o 
else
  ifeq ($(CONFIG_DLCI),m)
  M_OBJS += dlci.o
  endif
endif

ifeq ($(CONFIG_SDLA),y)
  L_OBJS += sdla.o
else
  ifeq ($(CONFIG_SDLA),m)
  M_OBJS += sdla.o
endif

ifeq ($(CONFIG_VENDOR_SANGOMA),y)
  LX_OBJS += sdladrv.o
  L_OBJS += sdlamain.o
  ifeq ($(CONFIG_WANPIPE_X25),y)
    L_OBJS += sdla_x25.o
  endif
  ifeq ($(CONFIG_WANPIPE_FR),y)
    L_OBJS += sdla_fr.o
  endif
  ifeq ($(CONFIG_WANPIPE_PPP),y)
    L_OBJS += sdla_ppp.o
  endif
endif

endif

ifeq ($(CONFIG_VENDOR_SANGOMA),m)
  MX_OBJS += sdladrv.o
  M_OBJS += wanpipe.o
  WANPIPE_OBJS = sdlamain.o
  ifeq ($(CONFIG_WANPIPE_X25),y)
    WANPIPE_OBJS += sdla_x25.o
  endif
  ifeq ($(CONFIG_WANPIPE_FR),y)
    WANPIPE_OBJS += sdla_fr.o
  endif
  ifeq ($(CONFIG_WANPIPE_PPP),y)
    WANPIPE_OBJS += sdla_ppp.o
  endif
endif

ifeq ($(CONFIG_CYCLADES_SYNC),y)
  LX_OBJS += cycx_drv.o
  L_OBJS += cycx_main.o
  ifeq ($(CONFIG_CYCLOMX_X25),y)
    L_OBJS += cycx_x25.o
  endif
endif

ifeq ($(CONFIG_CYCLADES_SYNC),m)
  MX_OBJS += cycx_drv.o
  M_OBJS += cyclomx.o
  CYCLOMX_OBJS = cycx_main.o
  ifeq ($(CONFIG_CYCLOMX_X25),y)
    CYCLOMX_OBJS += cycx_x25.o
  endif
endif

ifeq ($(CONFIG_X25_ASY),y)
L_OBJS += x25_asy.o
else
  ifeq ($(CONFIG_X25_ASY),m)
  M_OBJS += x25_asy.o
  endif
endif

ifeq ($(CONFIG_LAPBETHER),y)
L_OBJS += lapbether.o
else
  ifeq ($(CONFIG_LAPBETHER),m)
  M_OBJS += lapbether.o
  endif
endif

ifeq ($(CONFIG_SBNI),y)
L_OBJS += sbni.o
else
  ifeq ($(CONFIG_SBNI),m)
  M_OBJS += sbni.o
  endif
endif

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s

wanpipe.o: $(WANPIPE_OBJS)
	ld -r -o $@ $(WANPIPE_OBJS)

cyclomx.o: $(CYCLOMX_OBJS)
	ld -r -o $@ $(CYCLOMX_OBJS)

