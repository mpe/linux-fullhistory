# File: drivers/net/Makefile
#
# Makefile for the Linux network (ethercard) device drivers.
#

# This will go away in some future future: hidden configuration files
# are difficult for users to deal with.
include CONFIG

L_TARGET := net.a
L_OBJS   := auto_irq.o
M_OBJS   :=
MOD_LIST_NAME := NET_MODULES

# Need these to keep track of whether the 8390 and SLHC modules should
# really go in the kernel or a module.
CONFIG_8390_BUILTIN :=
CONFIG_8390_MODULE  :=
CONFIG_SLHC_BUILTIN :=
CONFIG_SLHC_MODULE  :=

ifeq ($(CONFIG_ISDN),y)
  ifeq ($(CONFIG_ISDN_PPP),y)
    CONFIG_SLHC_BUILTIN = y
  endif
else
  ifeq ($(CONFIG_ISDN),m)
    ifeq ($(CONFIG_ISDN_PPP),y)
      CONFIG_SLHC_MODULE = y
    endif
  endif
endif

ifeq ($(CONFIG_NET),y)
L_OBJS += Space.o net_init.o loopback.o
endif

ifeq ($(CONFIG_SEEQ8005),y)
L_OBJS += seeq8005.o
endif

ifeq ($(CONFIG_IBMTR),y)
L_OBJS += ibmtr.o
else
  ifeq ($(CONFIG_IBMTR),m)
  M_OBJS += ibmtr.o
  endif
endif

ifeq ($(CONFIG_SK_G16),y)
L_OBJS += sk_g16.o
endif

ifeq ($(CONFIG_NET_IPIP),y)
L_OBJS += new_tunnel.o
else
  ifeq ($(CONFIG_NET_IPIP),m)
  M_OBJS += new_tunnel.o
  endif
endif

ifeq ($(CONFIG_HP100),y)
L_OBJS += hp100.o
else
  ifeq ($(CONFIG_HP100),m)
  M_OBJS += hp100.o
  endif
endif

ifeq ($(CONFIG_WIC),y)
L_OBJS += wic.o
else
  ifeq ($(CONFIG_WIC),m)
  M_OBJS += wic.o
  endif
endif

ifeq ($(CONFIG_SMC9194),y)
L_OBJS += smc9194.o
else
  ifeq ($(CONFIG_SMC9194),m)
  M_OBJS += smc9194.o
  endif
endif


ifeq ($(CONFIG_WD80x3),y)
L_OBJS += wd.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_WD80x3),m)
  CONFIG_8390_MODULE = y
  M_OBJS += wd.o
  endif
endif

ifeq ($(CONFIG_EL2),y)
L_OBJS += 3c503.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_EL2),m)
  CONFIG_8390_MODULE = y
  M_OBJS += 3c503.o
  endif
endif

ifeq ($(CONFIG_NE2000),y)
L_OBJS += ne.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_NE2000),m)
  CONFIG_8390_MODULE = y
  M_OBJS += ne.o
  endif
endif

ifeq ($(CONFIG_HPLAN),y)
L_OBJS += hp.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_HPLAN),m)
  CONFIG_8390_MODULE = y
  M_OBJS += hp.o
  endif
endif

ifeq ($(CONFIG_HPLAN_PLUS),y)
L_OBJS += hp-plus.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_HPLAN_PLUS),m)
  CONFIG_8390_MODULE = y
  M_OBJS += hp-plus.o
  endif
endif

ifeq ($(CONFIG_ULTRA),y)
L_OBJS += smc-ultra.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_ULTRA),m)
  CONFIG_8390_MODULE = y
  M_OBJS += smc-ultra.o
  endif
endif

ifeq ($(CONFIG_E2100),y)
L_OBJS += e2100.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_E2100),m)
  CONFIG_8390_MODULE = y
  M_OBJS += e2100.o
  endif
endif

ifeq ($(CONFIG_PLIP),y)
L_OBJS += plip.o
else
  ifeq ($(CONFIG_PLIP),m)
  M_OBJS += plip.o
  endif
endif

ifeq ($(CONFIG_PPP),y)
LX_OBJS += ppp.o
CONFIG_SLHC_BUILTIN = y
else
  ifeq ($(CONFIG_PPP),m)
  CONFIG_SLHC_MODULE = y
  MX_OBJS += ppp.o
  endif
endif

ifdef CONFIG_PPP
  M_OBJS += bsd_comp.o
endif

ifeq ($(CONFIG_SLIP),y)
L_OBJS += slip.o
  ifeq ($(CONFIG_SLIP_COMPRESSED),y)
  CONFIG_SLHC_BUILTIN = y
  endif
else
  ifeq ($(CONFIG_SLIP),m)
    ifeq ($(CONFIG_SLIP_COMPRESSED),y)
    CONFIG_SLHC_MODULE = y
  endif
  M_OBJS += slip.o
  endif
endif

ifeq ($(CONFIG_STRIP),y)
L_OBJS += strip.o
else
  ifeq ($(CONFIG_STRIP),m)
  M_OBJS += strip.o
  endif
endif

ifeq ($(CONFIG_DE650),y)
ETDRV_OBJS := $(L_OBJS) de650.o
CONFIG_8390_BUILTIN = y
endif

ifeq ($(CONFIG_3C589),y)
L_OBJS += 3c589.o
endif

ifeq ($(CONFIG_DUMMY),y)
L_OBJS += dummy.o
else
  ifeq ($(CONFIG_DUMMY),m)
  M_OBJS += dummy.o
  endif
endif

ifeq ($(CONFIG_DE600),y)
L_OBJS += de600.o
else
  ifeq ($(CONFIG_DE600),m)
  M_OBJS += de600.o
  endif
endif

ifeq ($(CONFIG_DE620),y)
L_OBJS += de620.o
else
  ifeq ($(CONFIG_DE620),m)
  M_OBJS += de620.o
  endif
endif

ifeq ($(CONFIG_AT1500),y)
L_OBJS += lance.o
endif

ifeq ($(CONFIG_LANCE),y)
L_OBJS += lance.o
  ifeq ($(CONFIG_LANCE32),y)
  L_OBJS += lance32.o
  endif
endif

ifeq ($(CONFIG_SUNLANCE),y)
L_OBJS += sunlance.o
endif

ifeq ($(CONFIG_AT1700),y)
L_OBJS += at1700.o
else
  ifeq ($(CONFIG_AT1700),m)
  M_OBJS += at1700.o
  endif
endif

ifeq ($(CONFIG_FMV18X),y)
L_OBJS += fmv18x.o
else
  ifeq ($(CONFIG_FMV18X),m)
  M_OBJS += fmv18x.o
  endif
endif

ifeq ($(CONFIG_EL1),y)
L_OBJS += 3c501.o
else
  ifeq ($(CONFIG_EL1),m)
  M_OBJS += 3c501.o
  endif
endif

ifeq ($(CONFIG_EL16),y)
L_OBJS += 3c507.o
else
  ifeq ($(CONFIG_EL16),m)
  M_OBJS += 3c507.o
  endif
endif

ifeq ($(CONFIG_EL3),y)
L_OBJS += 3c509.o
else
  ifeq ($(CONFIG_EL3),m)
  M_OBJS += 3c509.o
  endif
endif

ifeq ($(CONFIG_VORTEX),y)
L_OBJS += 3c59x.o
else
  ifeq ($(CONFIG_VORTEX),m)
  M_OBJS += 3c59x.o
  endif
endif

ifeq ($(CONFIG_EEXPRESS),y)
L_OBJS += eexpress.o
else
  ifeq ($(CONFIG_EEXPRESS),m)
  M_OBJS += eexpress.o
  endif
endif

ifeq ($(CONFIG_EEXPRESS_PRO),y)
L_OBJS += eepro.o
else
  ifeq ($(CONFIG_EEXPRESS_PRO),m)
  M_OBJS += eepro.o
  endif
endif

ifeq ($(CONFIG_WAVELAN),y)
L_OBJS += wavelan.o
else
  ifeq ($(CONFIG_WAVELAN),m)
  M_OBJS += wavelan.o
  endif
endif

ifeq ($(CONFIG_ZNET),y)
L_OBJS += znet.o
endif

ifeq ($(CONFIG_DEPCA),y)
L_OBJS += depca.o
else
  ifeq ($(CONFIG_DEPCA),m)
  M_OBJS += depca.o
  endif
endif

ifeq ($(CONFIG_EWRK3),y)
L_OBJS += ewrk3.o
else
  ifeq ($(CONFIG_EWRK3),m)
  M_OBJS += ewrk3.o
  endif
endif

ifeq ($(CONFIG_ATP),y)
L_OBJS += atp.o
endif

ifeq ($(CONFIG_DE4X5),y)
L_OBJS += de4x5.o
else
  ifeq ($(CONFIG_DE4X5),m)
  M_OBJS += de4x5.o
  endif
endif

ifeq ($(CONFIG_NI52),y)
L_OBJS += ni52.o
else
  ifeq ($(CONFIG_NI52),m)
  M_OBJS += ni52.o
  endif
endif

ifeq ($(CONFIG_NI65),y)
L_OBJS += ni65.o
endif

ifeq ($(CONFIG_ELPLUS),y)
L_OBJS += 3c505.o
else
  ifeq ($(CONFIG_ELPLUS),m)
  M_OBJS += 3c505.o
  endif
endif

ifeq ($(CONFIG_AC3200),y)
L_OBJS += ac3200.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_AC3200),m)
  CONFIG_8390_MODULE = y
  M_OBJS += ac3200.o
  endif
endif

ifeq ($(CONFIG_APRICOT),y)
L_OBJS += apricot.o
else
  ifeq ($(CONFIG_APRICOT),m)
  M_OBJS += apricot.o
  endif
endif

ifeq ($(CONFIG_DEC_ELCP),y)
L_OBJS += tulip.o
else
  ifeq ($(CONFIG_DEC_ELCP),m)
  M_OBJS += tulip.o
  endif
endif

ifeq ($(CONFIG_ARCNET),y)
L_OBJS += arcnet.o
else
  ifeq ($(CONFIG_ARCNET),m)
  M_OBJS += arcnet.o
  endif
endif

ifeq ($(CONFIG_ETH16I),y)
L_OBJS += eth16i.o
else
  ifeq ($(CONFIG_ETH16I),m)
  M_OBJS += eth16i.o
  endif
endif

ifeq ($(CONFIG_PI),y)
L_OBJS += pi2.o
endif

ifeq ($(CONFIG_PT),y)
L_OBJS += pt.o
endif

# If anything built-in uses slhc, then build it into the kernel also.
# If not, but a module uses it, build as a module.
ifdef CONFIG_SLHC_BUILTIN
LX_OBJS += slhc.o
else
  ifdef CONFIG_SLHC_MODULE
  MX_OBJS += slhc.o
  endif
endif


# If anything built-in uses the 8390, then build it into the kernel also.
# If not, but a module uses it, build as a module.
ifdef CONFIG_8390_BUILTIN
L_OBJS += 8390.o
else
  ifdef CONFIG_8390_MODULE
  M_OBJS += 8390.o
  endif
endif


ifeq ($(CONFIG_EQUALIZER),y)
L_OBJS += eql.o
else
  ifeq ($(CONFIG_EQUALIZER),m)
  M_OBJS += eql.o
  endif
endif

ifeq ($(CONFIG_ATARILANCE),y)
L_OBJS += atarilance.o
else
  ifeq ($(CONFIG_ATARILANCE),m)
  M_OBJS += atarilance.o
  endif
endif

ifeq ($(CONFIG_A2065),y)
L_OBJS += a2065.o
else
  ifeq ($(CONFIG_A2065),m)
  M_OBJS += a2065.o
  endif
endif

ifeq ($(CONFIG_HYDRA),y)
L_OBJS += hydra.o
else
  ifeq ($(CONFIG_HYDRA),m)
  M_OBJS += hydra.o
  endif
endif

ifeq ($(CONFIG_SDLA),y)
L_OBJS += sdla.o
else
  ifeq ($(CONFIG_SDLA),m)
  M_OBJS += sdla.o
  endif
endif

ifeq ($(CONFIG_DLCI),y)
L_OBJS += dlci.o 
else
  ifeq ($(CONFIG_DLCI),m)
  M_OBJS += dlci.o
  endif
endif

ifeq ($(CONFIG_ARIADNE),y)
L_OBJS += ariadne.o
else
  ifeq ($(CONFIG_ARIADNE),m)
  M_OBJS += ariadne.o
  endif
endif

ifeq ($(CONFIG_DGRS),y)
L_OBJS += dgrs.o
else
  ifeq ($(CONFIG_DGRS),m)
  M_OBJS += dgrs.o
  endif
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

Space.o: Space.c ../../include/linux/autoconf.h CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(OPTS) -c $<

net_init.o: ../../include/linux/autoconf.h

ne.o:	ne.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(NE_OPTS) -c $<

hp.o:	hp.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(HP_OPTS) -c $<

plip.o:	plip.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(PLIP_OPTS) -c $<

slip.o:	slip.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

strip.o: strip.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

dummy.o: dummy.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<

de600.o: de600.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DE600_OPTS) -c $<

de620.o: de620.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(DE620_OPTS) -c $<

lance.o: lance.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LANCE_OPTS) -c $<

8390.o: 8390.c 8390.h CONFIG

sdla.o: sdla.c CONFIG 

dlci.o: dlci.c CONFIG

dgrs.o: dgrs.c dgrs.h CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $<
tulip.o: tulip.c CONFIG
	$(CC) $(CPPFLAGS) $(CFLAGS) $(TULIP_OPTS) -c $<
