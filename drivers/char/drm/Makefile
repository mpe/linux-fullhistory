#
# Makefile for the drm device driver.  This driver provides support for
# the Direct Rendering Infrastructure (DRI) in XFree86 4.x.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definitions are now inherited from the
# parent makes..
#

O_TARGET	:= drm.o

L_OBJS		:= init.o memory.o proc.o auth.o context.o drawable.o bufs.o \
			lists.o lock.o ioctl.o fops.o vm.o dma.o

M_OBJS		:= 

ifeq ($(CONFIG_DRM_GAMMA),y)
 OX_OBJS += gamma_drv.o
 O_OBJS += gamma_dma.o
else
  ifeq ($(CONFIG_DRM_GAMMA),m)
    MIX_OBJS += gamma_drv.o
    MI_OBJS += gamma_dma.o
    M_OBJS += gamma.o
  endif
endif

ifeq ($(CONFIG_DRM_TDFX),y)
 OX_OBJS += tdfx_drv.o
 O_OBJS += tdfx_context.o
else
  ifeq ($(CONFIG_DRM_TDFX),m)
    MIX_OBJS += tdfx_drv.o
    MI_OBJS += tdfx_context.o
    M_OBJS += tdfx.o
  endif
endif

O_OBJS += $(L_OBJS)

include $(TOPDIR)/Rules.make

gamma.o : gamma_drv.o gamma_dma.o $(L_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ gamma_drv.o gamma_dma.o $(L_OBJS)

tdfx.o: tdfx_drv.o tdfx_context.o $(L_OBJS)
	$(LD) $(LD_RFLAG) -r -o $@ tdfx_drv.o tdfx_context.o $(L_OBJS)
