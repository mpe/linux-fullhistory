#
# Makefile for the drm device driver.  This driver provides support for
# the Direct Rendering Infrastructure (DRI) in XFree86 4.x.
#

O_TARGET =	drm.o

export-objs =	drm_syms.o	
list-multi =	drm.o gamma.o tdfx.o r128.o ffb.o mga.o i810.o
drm-objs =	init.o memory.o proc.o auth.o context.o \
		drawable.o bufs.o lists.o lock.o ioctl.o \
		fops.o vm.o dma.o ctxbitmap.o drm_syms.o
gamma-objs =	gamma_drv.o			gamma_dma.o
tdfx-objs =	tdfx_drv.o	tdfx_context.o
r128-objs =	r128_drv.o	r128_context.o	r128_dma.o	r128_bufs.o
ffb-objs =	ffb_drv.o	ffb_context.o
mga-objs =	mga_drv.o	mga_context.o	mga_dma.o mga_bufs.o mga_state.o
i810-objs =	i810_drv.o	i810_context.o	i810_dma.o	i810_bufs.o

ifeq ($(CONFIG_AGP), y)
   drm-objs += agpsupport.o
else
 ifeq ($(CONFIG_AGP), m)
   drm-objs += agpsupport.o
 endif
endif

obj-$(CONFIG_DRM) 	+= drm.o
obj-$(CONFIG_DRM_GAMMA) += gamma.o 
obj-$(CONFIG_DRM_TDFX)  += tdfx.o
obj-$(CONFIG_DRM_R128)  += r128.o
obj-$(CONFIG_DRM_FFB)   += ffb.o
obj-$(CONFIG_DRM_MGA)   += mga.o
obj-$(CONFIG_DRM_I810)  += i810.o


# Extract lists of the multi-part drivers.
# The 'int-*' lists are the intermediate files used to build the multi's.
multi-y		:= $(filter $(list-multi), $(obj-y))
multi-m		:= $(filter $(list-multi), $(obj-m))
int-y		:= $(sort $(foreach m, $(multi-y), $($(basename $(m))-objs)))
int-m		:= $(sort $(foreach m, $(multi-m), $($(basename $(m))-objs)))

# Files that are both resident and modular: remove from modular.
obj-m		:= $(filter-out $(obj-y), $(obj-m))
int-m		:= $(filter-out $(int-y), $(int-m))

# Take multi-part drivers out of obj-y and put components in.
obj-y		:= $(filter-out $(list-multi), $(obj-y)) $(int-y)

# Translate to Rules.make lists.
O_OBJS		:= $(filter-out $(export-objs), $(obj-y))
OX_OBJS		:= $(filter     $(export-objs), $(obj-y))
M_OBJS		:= $(sort $(filter-out $(export-objs), $(obj-m)))
MX_OBJS		:= $(sort $(filter     $(export-objs), $(obj-m)))
MI_OBJS		:= $(sort $(filter-out $(export-objs), $(int-m)))
MIX_OBJS	:= $(sort $(filter     $(export-objs), $(int-m)))

include $(TOPDIR)/Rules.make

gamma.o: $(gamma-objs)
	$(LD) -r -o $@ $(gamma-objs)

tdfx.o: $(tdfx-objs)
	$(LD) -r -o $@ $(tdfx-objs)

mga.o: $(mga-objs)
	$(LD) -r -o $@ $(mga-objs)

i810.o: $(i810-objs)
	$(LD) -r -o $@ $(i810-objs)

r128.o: $(r128-objs)
	$(LD) -r -o $@ $(r128-objs)

ffb.o: $(ffb-objs)
	$(LD) -r -o $@ $(ffb-objs)
