#
# Makefile for the kernel multimedia device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

MEDIAS = $(join $(SUB_DIRS),$(SUB_DIRS:%=/%.o))

SUB_DIRS     := video radio
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS)

O_TARGET     := media.o
O_OBJS       := $(MEDIAS)

include $(TOPDIR)/Rules.make
