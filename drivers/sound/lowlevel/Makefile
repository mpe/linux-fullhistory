all:	lowlevel.o

ALLOBJS = init.o aci.o awe_wave.o aedsp16.o
OBJS = init.o

ifdef CONFIG_LOWLEVEL_SOUND
ifdef CONFIG_ACI_MIXER
OBJS := $(OBJS) aci.o
endif
ifdef CONFIG_AWE32_SYNTH
OBJS := $(OBJS) awe_wave.o
endif
ifdef CONFIG_AEDSP16
OBJS := $(OBJS) aedsp16.o
endif
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

ifdef HOSTCC
include $(TOPDIR)/Rules.make
else
USE_DEPEND=y
endif

ifdef USE_DEPEND
# include a dependency file if one exists
ifeq (.depend,$(wildcard .depend))
include .depend
endif
endif
