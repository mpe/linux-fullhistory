#
# Makefile for the Zorro bus specific drivers.
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

L_TARGET := zorro.a

# Nasty trick as nobody references zorrosyms.o, but we still want it linked.
ifeq ($(CONFIG_MODULES),y)
O_TARGET = zorro_syms.o
OX_OBJS  = zorrosyms.o
O_OBJS   = zorro.o
L_OBJS   := zorro_syms.o
else
L_OBJS   := zorro.o
endif

ifdef CONFIG_PROC_FS
L_OBJS   += proc.o
endif

include $(TOPDIR)/Rules.make
