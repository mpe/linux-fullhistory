all:	lowlevel.o

OBJS = init.o

ifdef CONFIG_LOWLEVEL_SOUND
ifdef CONFIG_ACI_MIXER
OBJS := $(OBJS) aci.o
endif
endif

lowlevel.o:	$(OBJS)
	ld -r -o lowlevel.o $(OBJS)

clean:
	rm -f core x y z *~ *.o

ifdef HOSTCC
include $(TOPDIR)/Rules.make
endif
