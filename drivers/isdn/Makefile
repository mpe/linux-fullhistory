# Makefile for the kernel ISDN subsystem and device drivers.

# The target object and module list name.

O_TARGET	:= isdn.a

# Objects that export symbols.

export-objs	:= isdn_common.o

# Multipart objects.

list-multi	:= isdn.o
isdn-objs	:= isdn_net.o isdn_tty.o isdn_cards.o isdn_v110.o \
		   isdn_common.o

# Optional parts of multipart objects.

isdn-objs-$(CONFIG_ISDN_PPP)		+= isdn_ppp.o
isdn-objs-$(CONFIG_ISDN_X25)		+= isdn_concap.o isdn_x25iface.o
isdn-objs-$(CONFIG_ISDN_AUDIO)		+= isdn_audio.o
isdn-objs-$(CONFIG_ISDN_TTY_FAX)	+= isdn_ttyfax.o
isdn-objs-$(CONFIG_ISDN_WITH_ABC)	+= isdn_dwabc.o

isdn-objs				+= $(isdn-objs-y)

# Object file lists.

obj-y	:=
obj-m	:=
obj-n	:=
obj-	:=

# Object files in subdirectories

subdir-$(CONFIG_ISDN_DIVERSION)		+= divert
subdir-$(CONFIG_ISDN_DRV_HISAX)		+= hisax
subobj-$(CONFIG_ISDN_DRV_HISAX)		+= hisax/hisax_drv.o
subdir-$(CONFIG_ISDN_DRV_ICN)		+= icn
subobj-$(CONFIG_ISDN_DRV_ICN)		+= icn/icn_drv.o
subdir-$(CONFIG_ISDN_DRV_PCBIT)		+= pcbit
subobj-$(CONFIG_ISDN_DRV_PCBIT)		+= pcbit/pcbit_drv.o
subdir-$(CONFIG_ISDN_DRV_SC)		+= sc
subobj-$(CONFIG_ISDN_DRV_SC)		+= sc/sc_drv.o
subdir-$(CONFIG_ISDN_CAPI)		+= avmb1
subobj-$(CONFIG_ISDN_CAPI)		+= avmb1/avmb1.o
subdir-$(CONFIG_ISDN_DRV_LOOP)		+= isdnloop
subobj-$(CONFIG_ISDN_DRV_LOOP)		+= isdnloop/isdnloop_drv.o
subdir-$(CONFIG_ISDN_DRV_ACT2000)	+= act2000
subobj-$(CONFIG_ISDN_DRV_ACT2000)	+= act2000/act2000_drv.o
subdir-$(CONFIG_ISDN_DRV_EICON)		+= eicon
subobj-$(CONFIG_ISDN_DRV_EICON)		+= eicon/eicon_drv.o
subdir-$(CONFIG_HYSDN)			+= hysdn
subobj-$(CONFIG_HYSDN)			+= hysdn/hysdn_drv.o

obj-y					+= $(subobj-y)

# Each configuration option enables a list of files.

obj-$(CONFIG_ISDN)			+= isdn.o
obj-$(CONFIG_ISDN_PPP_BSDCOMP)		+= isdn_bsdcomp.o

# The global Rules.make.

include $(TOPDIR)/Rules.make

# Link rules for multi-part drivers.

isdn.o: $(isdn-objs)
	$(LD) -r -o $@ $(isdn-objs)
