#
# Makefile for the agpgart device driver.  This driver adds a user
# space ioctl interface to use agp memory.  It also adds a kernel interface
# that other drivers could use to manipulate agp memory.

M_OBJS		:= agpgart.o

CFLAGS_agp_backend.o	:=

ifdef CONFIG_AGP_I810
CFLAGS_agp_backend.o	+= -DAGP_BUILD_INTEL_I810
endif
ifdef CONFIG_AGP_INTEL
CFLAGS_agp_backend.o	+= -DAGP_BUILD_INTEL_GENERIC
endif
ifdef CONFIG_AGP_VIA
CFLAGS_agp_backend.o	+= -DAGP_BUILD_VIA_GENERIC
endif
ifdef CONFIG_AGP_AMD
CFLAGS_agp_backend.o	+= -DAGP_BUILD_AMD_IRONGATE
endif
ifdef CONFIG_AGP_SIS
CFLAGS_agp_backend.o	+= -DAGP_BUILD_SIS_GENERIC
endif
ifdef CONFIG_AGP_ALI
CFLAGS_agp_backend.o	+= -DAGP_BUILD_ALI_M1541
endif

include $(TOPDIR)/Rules.make

agpgart.o: agp_backend.o agpgart_fe.o
	$(LD) $(LD_RFLAG) -r -o $@ agp_backend.o agpgart_fe.o
