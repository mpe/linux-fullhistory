#
# Makefile for the kernel miscellaneous drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

L_TARGET := misc.a
MX_OBJS  :=
LX_OBJS  := 
MI_OBJS  :=
MIX_OBJS :=

ifeq ($(CONFIG_PARPORT),y)
  L_OBJS += parport_share.o parport_ieee1284.o parport_procfs.o
  ifeq ($(CONFIG_PARPORT_PC),y)
    LX_OBJS += parport_pc.o
  else
    ifeq ($(CONFIG_PARPORT_PC),m)
      M_OBJS += parport_pc.o
    endif
  endif
  ifeq ($(CONFIG_PARPORT_AX),y)
    LX_OBJS += parport_ax.o
  else
    ifeq ($(CONFIG_PARPORT_AX),m)
      M_OBJS += parport_ax.o
    endif
  endif
  LX_OBJS += parport_init.o
else
  ifeq ($(CONFIG_PARPORT),m)
    MI_OBJS += parport_share.o parport_ieee1284.o
    ifneq ($(CONFIG_PROC_FS),n) 
      MI_OBJS += parport_procfs.o
    endif
    MIX_OBJS += parport_init.o
    M_OBJS += parport.o
  endif
  ifeq ($(CONFIG_PARPORT_PC),m)
    M_OBJS += parport_pc.o
  endif
  ifeq ($(CONFIG_PARPORT_AX),m)
    M_OBJS += parport_ax.o
  endif
endif

include $(TOPDIR)/Rules.make

# Special rule to build the composite parport.o module
parport.o: $(MI_OBJS) $(MIX_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ $(MI_OBJS) $(MIX_OBJS)
