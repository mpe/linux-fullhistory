#
# Makefile for drivers/net/tokenring
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


L_TARGET := tr.a
L_OBJS   := 
M_OBJS   :=

ifeq ($(CONFIG_IBMTR),y)
  L_OBJS += ibmtr.o
else
  ifeq ($(CONFIG_IBMTR),m)
    M_OBJS += ibmtr.o
  endif
endif

ifeq ($(CONFIG_IBMOL),y)
  L_OBJS += olympic.o
else
  ifeq ($(CONFIG_IBMOL),m)
    M_OBJS += olympic.o
  endif
endif

ifeq ($(CONFIG_TMS380TR),y)
  L_OBJS += tms380tr.o
else
  ifeq ($(CONFIG_TMS380TR),m)
    M_OBJS += tms380tr.o
  endif
endif

include $(TOPDIR)/Rules.make

