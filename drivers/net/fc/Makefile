
# Makefile for linux/drivers/net/fc
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#

L_TARGET := fc.a
L_OBJS   := 
M_OBJS   := 
MX_OBJS  := 
MOD_LIST_NAME := FC_MODULES
FC_SRCS = $(wildcard $(L_OBJS:%.o=%.c))

ifeq ($(CONFIG_IPHASE5526),y)
L_OBJS += iph5526.o
else
  ifeq ($(CONFIG_IPHASE5526),m)
    M_OBJS += iph5526.o 
  endif
endif

include $(TOPDIR)/Rules.make

clean:
	rm *.o 

