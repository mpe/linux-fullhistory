all:	lowlevel.o

ALLOBJS = init.o aci.o awe_wave.o aedsp16.o
OBJS = init.o

ifeq ($(CONFIG_LOWLEVEL_SOUND),y)
ifeq ($(CONFIG_ACI_MIXER),y)
   OBJS := $(OBJS) aci.o
endif
ifeq ($(CONFIG_AWE32_SYNTH),y)
OBJS := $(OBJS) awe_wave.o
else 
  ifeq ($(CONFIG_AWE32_SYNTH),m)
     MX_OBJS := $(MX_OBJS) awe_wave.o
  endif
endif
ifeq ($(CONFIG_AEDSP16),y)
   OBJS := $(OBJS) aedsp16.o
else 
  ifeq ($(CONFIG_AEDSP16),m)
     MX_OBJS := $(MX_OBJS) aedsp16.o
  endif
endif
endif

ifndef TOPDIR
TOPDIR=/usr/src/linux
endif

lowlevel.o:	$(OBJS)
	$(LD) -r -o lowlevel.o $(OBJS)

module: manual_config.h
	rm -f lowlevel.o
	make CFLAGS="$(CFLAGS) -DLOWLEVEL_MODULE" $(ALLOBJS)
	$(LD) -r -o lowlevel.o $(ALLOBJS)
	touch module

manual_config.h:
	@echo You should create `pwd`/manual_config.h.
	@echo See `pwd`/README for more info.
	@exit 1

clean:
	rm -f core x y z *~ *.o module .depend

dep:
	$(CPP) -M $(CFLAGS) -I. *.c > .depend

ifndef HOSTCC
#
#	Running outside the kernel build.
#
CC	= gcc
HOSTCC	= gcc
CFLAGS	= -O2 -D__KERNEL__ -DMODULE -I/usr/src/linux/include -Wall -Wstrict-prototypes -fomit-frame-pointer -pipe -m486
USE_DEPEND=y
else
include $(TOPDIR)/Rules.make
endif

ifdef USE_DEPEND
# include a dependency file if one exists
ifeq (.depend,$(wildcard .depend))
include .depend
endif
endif
