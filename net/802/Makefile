#
# Makefile for the Linux 802.x protocol layers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

O_TARGET := 802.o
O_OBJS	  = p8023.o

ifdef CONFIG_TR
O_OBJS += tr.o
endif

ifdef CONFIG_IPX
O_OBJS += p8022.o psnap.o
endif

ifdef CONFIG_ATALK
ifndef CONFIG_IPX
O_OBJS += p8022.o psnap.o
endif
endif

include $(TOPDIR)/Rules.make

tar:
		tar -cvf /dev/f1 .
