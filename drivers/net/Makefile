# File: net/drv/Makefile
#
# Makefile for the Linux network (ethercard) device drivers.
#
# Note! Dependencies are done automagically by 'make dep', which also
# removes any old dependencies. DON'T put your own dependencies here
# unless it's something special (ie not a .c file).
#
# Note 2! The CFLAGS definition is now in the main makefile...

include CONFIG

.c.s:
	$(CC) $(CFLAGS) -S -o $*.s $<
.s.o:
	$(AS) -o $*.o $<
.c.o:
	$(CC) $(CFLAGS) -c -o $*.o $<

NETDRV_OBJS := Space.o auto_irq.o
CFLAGS := $(CFLAGS) -I../../net/inet
CPP := $(CPP) -I../../net/inet

# The point of the makefile...
all: net.a

Space.o: Space.c /usr/include/linux/autoconf.h
	$(CC) $(CFLAGS) $(OPTS) $(DL_OPTS) -c $< -o $@

ifdef CONFIG_WD80x3
NETDRV_OBJS := $(NETDRV_OBJS) wd.o
CONFIG_8390 = CONFIG_8390
wd.o:	wd.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WD_OPTS) -c wd.c
endif

ifdef CONFIG_EL2
NETDRV_OBJS := $(NETDRV_OBJS) 3c503.o
CONFIG_8390 = CONFIG_8390
3c503.o:	3c503.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EL2_OPTS) -c 3c503.c
endif

ifdef CONFIG_NE2000
NETDRV_OBJS := $(NETDRV_OBJS) ne.o
CONFIG_8390 = CONFIG_8390
ne.o:	ne.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NE_OPTS) -c ne.c
endif

ifdef CONFIG_HPLAN
NETDRV_OBJS := $(NETDRV_OBJS) hp.o
CONFIG_8390 = CONFIG_8390
hp.o:	hp.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HP_OPTS) -c hp.c
endif

ifdef CONFIG_ULTRA
NETDRV_OBJS := $(NETDRV_OBJS) smc-ultra.o
CONFIG_8390 = CONFIG_8390
endif

ifdef CONFIG_8390
NETDRV_OBJS := $(NETDRV_OBJS) 8390.o
endif

ifdef CONFIG_PLIP
NETDRV_OBJS := $(NETDRV_OBJS) plip.o
plip.o:	plip.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PLIP_OPTS) -c plip.c
endif

ifdef CONFIG_SLIP
NETDRV_OBJS := $(NETDRV_OBJS) slip.o slhc.o
slip.o:	slip.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(SLIP_OPTS) -c slip.c
endif

ifdef CONFIG_DE600
NETDRV_OBJS := $(NETDRV_OBJS) d_link.o
d_link.o: d_link.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DL_OPTS) -c d_link.c
endif

ifdef CONFIG_AT1500
NETDRV_OBJS := $(NETDRV_OBJS) lance.o
endif

ifdef CONFIG_EL3
NETDRV_OBJS := $(NETDRV_OBJS) 3c509.o
endif

ifdef CONFIG_ZNET
NETDRV_OBJS := $(NETDRV_OBJS) znet.o
endif
ifdef CONFIG_EEXPRESS
NETDRV_OBJS := $(NETDRV_OBJS) eexpress.o
endif
ifdef CONFIG_EL1
NETDRV_OBJS := $(NETDRV_OBJS) 3c501.o
endif
ifdef CONFIG_EL16
NETDRV_OBJS := $(NETDRV_OBJS) 3c507.o
endif
ifdef CONFIG_DEPCA
NETDRV_OBJS := $(NETDRV_OBJS) depca.o
endif
ifdef CONFIG_ATP
NETDRV_OBJS := $(NETDRV_OBJS) atlantec.o
endif
ifdef CONFIG_NI52
NETDRV_OBJS := $(NETDRV_OBJS) ni52.o
endif
ifdef CONFIG_NI65
NETDRV_OBJS := $(NETDRV_OBJS) ni65.o
endif

ifdef CONFIG_IP_DEFRAG
NETDRV_OBJS := $(NETDRV_OBJS) ip-frag.o
endif

net.a: $(NETDRV_OBJS)
	rm -f $@
	$(AR) rcs $@ $^

clean:
	rm -f core *.o *.a *.s

dep:
	$(CPP) -M *.c > .depend

tar:
	tar -cvf /dev/f1 .

#
# include a dependency file if one exists
#
ifeq (.depend,$(wildcard .depend))
include .depend
endif
