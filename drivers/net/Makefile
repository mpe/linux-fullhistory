# File: drivers/net/Makefile
#
# Makefile for the Linux network (ethercard) device drivers.
#

# This will go away in some future future: hidden configuration files
# are difficult for users to deal with.
include CONFIG

L_TARGET := net.a
L_OBJS   := Space.o auto_irq.o net_init.o loopback.o
M_OBJS   :=
MOD_LIST_NAME := NET_MODULES

ifdef CONFIG_SEEQ8005
L_OBJS += seeq8005.o
endif

ifdef CONFIG_IBMTR
L_OBJS += ibmtr.o
else
M_OBJS += ibmtr.o
endif

ifdef CONFIG_SK_G16
L_OBJS += sk_g16.o
endif

ifdef CONFIG_NET_IPIP
L_OBJS += tunnel.o
else
M_OBJS += tunnel.o
endif

ifdef CONFIG_HP100
L_OBJS += hp100.o
else
M_OBJS += hp100.o
endif

ifdef CONFIG_WD80x3
L_OBJS += wd.o
CONFIG_8390 = CONFIG_8390
else
M_OBJS += wd.o
endif

ifdef CONFIG_EL2
L_OBJS += 3c503.o
CONFIG_8390 = CONFIG_8390
else
M_OBJS += 3c503.o
endif

ifdef CONFIG_NE2000
L_OBJS += ne.o
CONFIG_8390 = CONFIG_8390
else
M_OBJS += ne.o
endif

ifdef CONFIG_HPLAN
L_OBJS += hp.o
CONFIG_8390 = CONFIG_8390
else
M_OBJS += hp.o
endif

ifdef CONFIG_HPLAN_PLUS
L_OBJS += hp-plus.o
CONFIG_8390 = CONFIG_8390
else
M_OBJS += hp-plus.o
endif

ifdef CONFIG_ULTRA
L_OBJS += smc-ultra.o
CONFIG_8390 = CONFIG_8390
else
M_OBJS += smc-ultra.o
endif

ifdef CONFIG_E2100
L_OBJS += e2100.o
CONFIG_8390 = CONFIG_8390
else
M_OBJS += e2100.o
endif

ifdef CONFIG_PLIP
L_OBJS += plip.o
else
M_OBJS += plip.o
endif

ifdef CONFIG_PPP
L_OBJS += ppp.o
CONFIG_SLHC = CONFIG_SLHC
else
M_OBJS += ppp.o
endif

ifdef CONFIG_SLIP
L_OBJS += slip.o
CONFIG_SLHC = CONFIG_SLHC
else
M_OBJS += slip.o
endif

ifdef CONFIG_DE650
ETDRV_OBJS := $(L_OBJS) de650.o
CONFIG_8390 = CONFIG_8390
endif

ifdef CONFIG_3C589
L_OBJS += 3c589.o
endif

ifdef CONFIG_DUMMY
L_OBJS += dummy.o
else
M_OBJS += dummy.o
endif

ifdef CONFIG_DE600
L_OBJS += de600.o
else
M_OBJS += de600.o
endif
	
ifdef CONFIG_DE620
L_OBJS += de620.o
else
M_OBJS += de620.o
endif
	
ifdef CONFIG_AT1500
L_OBJS += lance.o
endif

ifdef CONFIG_LANCE
L_OBJS += lance.o
endif

ifdef CONFIG_AT1700
L_OBJS += at1700.o
else
M_OBJS += at1700.o
endif

ifdef CONFIG_EL1
L_OBJS += 3c501.o
else
M_OBJS += 3c501.o
endif

ifdef CONFIG_EL16
L_OBJS += 3c507.o
else
M_OBJS += 3c507.o
endif

ifdef CONFIG_EL3
L_OBJS += 3c509.o
else
M_OBJS += 3c509.o
endif

ifdef CONFIG_EEXPRESS
L_OBJS += eexpress.o
else
M_OBJS += eexpress.o
endif

ifdef CONFIG_EEXPRESS_PRO
L_OBJS += eepro.o
else
M_OBJS += eepro.o
endif

ifdef CONFIG_WAVELAN
L_OBJS += wavelan.o
else
M_OBJS += wavelan.o
endif

ifdef CONFIG_ZNET
L_OBJS += znet.o
endif

ifdef CONFIG_DEPCA
L_OBJS += depca.o
else
M_OBJS += depca.o
endif

ifdef CONFIG_EWRK3
L_OBJS += ewrk3.o
else
M_OBJS += ewrk3.o
endif

ifdef CONFIG_ATP
L_OBJS += atp.o
endif

ifdef CONFIG_DE4X5
L_OBJS += de4x5.o
else
M_OBJS += de4x5.o
endif

ifdef CONFIG_NI52
L_OBJS += ni52.o
endif

ifdef CONFIG_NI65
L_OBJS += ni65.o
endif

ifdef CONFIG_ELPLUS
L_OBJS += 3c505.o
else
M_OBJS += 3c505.o
endif

ifdef CONFIG_AC3200
L_OBJS += ac3200.o
CONFIG_8390 = CONFIG_8390
else
M_OBJS += ac3200.o
endif

ifdef CONFIG_APRICOT
L_OBJS += apricot.o
else
M_OBJS += apricot.o
endif

ifdef CONFIG_DEC_ELCP
L_OBJS += tulip.o
else
M_OBJS += tulip.o
endif

ifdef CONFIG_ARCNET
L_OBJS += arcnet.o
else
M_OBJS += arcnet.o
endif

ifdef CONFIG_PI
L_OBJS += pi2.o
CONFIG_PI = CONFIG_PI
endif

ifdef CONFIG_SLHC
L_OBJS += slhc.o
else
M_OBJS += slhc.o
endif

ifdef CONFIG_8390
L_OBJS += 8390.o
else
M_OBJS += 8390.o
endif

ifdef CONFIG_EQUALIZER
L_OBJS += eql.o
else
M_OBJS += eql.o
endif

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s

wd.o:	wd.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(WD_OPTS) -c $<

3c503.o:	3c503.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EL2_OPTS) -c $<

pi2.o:  pi2.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PI_OPTS) -c $<

3c505.o: 3c505.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(ELP_OPTS) -c $<

de4x5.o: de4x5.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DE4X5_OPTS) -c $<

ewrk3.o: ewrk3.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(EWRK3_OPTS) -c $<

depca.o: depca.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DEPCA_OPTS) -c $<

Space.o: ../../include/linux/autoconf.h

net_init.o: ../../include/linux/autoconf.h

ne.o:	ne.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NE_OPTS) -c $<

hp.o:	hp.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HP_OPTS) -c $<

plip.o:	plip.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PLIP_OPTS) -c $<

slip.o:	slip.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

dummy.o: dummy.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

de600.o: de600.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DE600_OPTS) -c $<

de620.o: de620.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DE620_OPTS) -c $<

8390.o: 8390.c 8390.h CONFIG
