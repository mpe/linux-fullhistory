# File: drivers/net/Makefile
#
# Makefile for the Linux network (ethercard) device drivers.
#

SUB_DIRS     := 
MOD_SUB_DIRS := $(SUB_DIRS)
ALL_SUB_DIRS := $(SUB_DIRS) fc hamradio irda pcmcia tokenring wan

L_TARGET := net.a
L_OBJS   := auto_irq.o
M_OBJS   :=
MOD_LIST_NAME := NET_MODULES

# Need these to keep track of whether the 7990 (LANCE), 8390, PPP and SLHC 
# modules should really go in the kernel or a module.
CONFIG_8390_BUILTIN :=
CONFIG_8390_MODULE  :=
CONFIG_SLHC_BUILTIN :=
CONFIG_SLHC_MODULE  :=
CONFIG_PPPDEF_BUILTIN :=
CONFIG_PPPDEF_MODULE  :=
CONFIG_7990_BUILTIN :=
CONFIG_7990_MODULE :=
CONFIG_82596_BUILTIN :=
CONFIG_82596_MODULE :=

ifeq ($(CONFIG_PCMCIA),y)
  SUB_DIRS += pcmcia
  MOD_SUB_DIRS += pcmcia
else
  ifeq ($(CONFIG_PCMCIA),m)
    MOD_SUB_DIRS += pcmcia
  endif
endif

ifeq ($(CONFIG_ISDN),y)
  ifeq ($(CONFIG_ISDN_PPP),y)
    CONFIG_SLHC_BUILTIN = y
    CONFIG_PPPDEF_BUILTIN = y
  endif
else
  ifeq ($(CONFIG_ISDN),m)
    ifeq ($(CONFIG_ISDN_PPP),y)
      CONFIG_SLHC_MODULE = y
      CONFIG_PPPDEF_MODULE = y
    endif
  endif
endif

ifeq ($(CONFIG_NET),y)
L_OBJS += Space.o net_init.o loopback.o
endif

ifeq ($(CONFIG_SEEQ8005),y)
L_OBJS += seeq8005.o
endif

ifeq ($(CONFIG_ETHERTAP),y)
L_OBJS += ethertap.o
else
  ifeq ($(CONFIG_ETHERTAP),m)
  M_OBJS += ethertap.o
  endif
endif

ifeq ($(CONFIG_NET_SB1000),y)
L_OBJS += sb1000.o
else
  ifeq ($(CONFIG_NET_SB1000),m)
  M_OBJS += sb1000.o
  endif
endif

ifeq ($(CONFIG_DAYNAPORT), y)
L_OBJS += daynaport.o
CONFIG_8390_BUILTIN = y
endif

ifeq ($(CONFIG_APNE),y)
L_OBJS += apne.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_APNE),m)
  M_OBJS += apne.o
  CONFIG_8390_MODULE = y
  endif
endif

ifeq ($(CONFIG_PCMCIA_PCNET),y)
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_PCMCIA_PCNET),m)
  CONFIG_8390_MODULE = y
  endif
endif

ifeq ($(CONFIG_SHAPER),y)
L_OBJS += shaper.o
else
  ifeq ($(CONFIG_SHAPER),m)
  M_OBJS += shaper.o
  endif
endif

ifeq ($(CONFIG_SK_G16),y)
L_OBJS += sk_g16.o
endif

ifeq ($(CONFIG_HP100),y)
L_OBJS += hp100.o
else
  ifeq ($(CONFIG_HP100),m)
  M_OBJS += hp100.o
  endif
endif

ifeq ($(CONFIG_SMC9194),y)
L_OBJS += smc9194.o
else
  ifeq ($(CONFIG_SMC9194),m)
  M_OBJS += smc9194.o
  endif
endif

ifeq ($(CONFIG_ARM_AM79C961A),y)
L_OBJS += am79c961a.o
else
  ifeq ($(CONFIG_ARM_AM79C961A),m)
  M_OBJS += am79c961a.o
  endif
endif

ifeq ($(CONFIG_ARM_ETHERH),y)
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_ARM_ETHERH),m)
  CONFIG_8390_MODULE = y
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

ifeq ($(CONFIG_NE2K_PCI),y)
L_OBJS += ne2k-pci.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_NE2K_PCI),m)
  CONFIG_8390_MODULE = y
  M_OBJS += ne2k-pci.o
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

ifeq ($(CONFIG_NE2_MCA),y)
L_OBJS += ne2.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_NE2_MCA),m)
  CONFIG_8390_MODULE = y
  M_OBJS += ne2.o
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

ifeq ($(CONFIG_ULTRAMCA),y)
L_OBJS += smc-mca.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_ULTRAMCA),m)
  CONFIG_8390_MODULE = y
  M_OBJS += smc-mca.o
  endif
endif

ifeq ($(CONFIG_ULTRA32),y)
L_OBJS += smc-ultra32.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_ULTRA32),m)
  CONFIG_8390_MODULE = y
  M_OBJS += smc-ultra32.o
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

ifeq ($(CONFIG_ES3210),y)
L_OBJS += es3210.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_ES3210),m)
  CONFIG_8390_MODULE = y
  M_OBJS += es3210.o
  endif
endif

ifeq ($(CONFIG_LNE390),y)
L_OBJS += lne390.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_LNE390),m)
  CONFIG_8390_MODULE = y
  M_OBJS += lne390.o
  endif
endif

ifeq ($(CONFIG_NE3210),y)
L_OBJS += ne3210.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_NE3210),m)
  CONFIG_8390_MODULE = y
  M_OBJS += ne3210.o
  endif
endif

ifeq ($(CONFIG_PLIP),y)
L_OBJS += plip.o
else
  ifeq ($(CONFIG_PLIP),m)
  M_OBJS += plip.o
  endif
endif

# bsd_comp.o is *always* a module, for some documented reason
# (licensing).
ifeq ($(CONFIG_PPP),y)
LX_OBJS += ppp_generic.o
CONFIG_SLHC_BUILTIN = y
  ifeq ($(CONFIG_PPP_ASYNC),y)
  LX_OBJS += ppp_async.o
  else
    ifeq ($(CONFIG_PPP_ASYNC),m)
    MX_OBJS += ppp_async.o
    endif
  endif
  ifeq ($(CONFIG_PPP_DEFLATE),y)
  CONFIG_PPPDEF_BUILTIN = y
  else
    ifeq ($(CONFIG_PPP_DEFLATE),m)
    CONFIG_PPPDEF_MODULE = y
    endif
  endif
  ifeq ($(CONFIG_PPP_BSDCOMP),m)
  M_OBJS += bsd_comp.o
  endif
else
  ifeq ($(CONFIG_PPP),m)
  MX_OBJS += ppp_generic.o
  CONFIG_SLHC_MODULE = y
    ifeq ($(CONFIG_PPP_ASYNC),m)
    MX_OBJS += ppp_async.o
    endif
    ifeq ($(CONFIG_PPP_DEFLATE),m)
    CONFIG_PPPDEF_MODULE = y
    endif
    ifeq ($(CONFIG_PPP_BSDCOMP),m)
    M_OBJS += bsd_comp.o
    endif
  endif
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
else
  ifeq ($(CONFIG_LANCE),m)
  M_OBJS += lance.o
  endif
endif

ifeq ($(CONFIG_SUN3LANCE),y)
L_OBJS += sun3lance.o
endif

ifeq ($(CONFIG_PCNET32),y)
L_OBJS += pcnet32.o
else
  ifeq ($(CONFIG_PCNET32),m)
  M_OBJS += pcnet32.o
  endif
endif

ifeq ($(CONFIG_DEFXX),y)
L_OBJS += defxx.o
endif

ifeq ($(CONFIG_SUNLANCE),y)
L_OBJS += sunlance.o
else
  ifeq ($(CONFIG_SUNLANCE),m)
  M_OBJS += sunlance.o
  endif
endif

ifeq ($(CONFIG_SGISEEQ), y)
L_OBJS += sgiseeq.o
endif

ifeq ($(CONFIG_HAPPYMEAL),y)
L_OBJS += sunhme.o
else
  ifeq ($(CONFIG_HAPPYMEAL),m)
  M_OBJS += sunhme.o
  endif
endif

ifeq ($(CONFIG_SUNQE),y)
L_OBJS += sunqe.o
else
  ifeq ($(CONFIG_SUNQE),m)
  M_OBJS += sunqe.o
  endif
endif

ifeq ($(CONFIG_SUNBMAC),y)
L_OBJS += sunbmac.o
else
  ifeq ($(CONFIG_SUNBMAC),m)
  M_OBJS += sunbmac.o
  endif
endif

ifeq ($(CONFIG_SUNBMAC),y)
L_OBJS += sunbmac.o
else
  ifeq ($(CONFIG_SUNBMAC),m)
  M_OBJS += sunbmac.o
  endif
endif

ifeq ($(CONFIG_MYRI_SBUS),y)
L_OBJS += myri_sbus.o
else
  ifeq ($(CONFIG_MYRI_SBUS),m)
  M_OBJS += myri_sbus.o
  endif
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

ifeq ($(CONFIG_ELMC),y)
L_OBJS += 3c523.o
else
  ifeq ($(CONFIG_ELMC),m)
  M_OBJS += 3c523.o
  endif
endif

ifeq ($(CONFIG_SKMC),y)
L_OBJS += sk_mca.o
else
  ifeq ($(CONFIG_SKMC),m)
  M_OBJS += sk_mca.o
  endif
endif

ifeq ($(CONFIG_ELMC_II),y)
L_OBJS += 3c527.o
else
  ifeq ($(CONFIG_ELMC_II),m)
  M_OBJS += 3c527.o
  endif
endif

ifeq ($(CONFIG_EL3),y)
L_OBJS += 3c509.o
else
  ifeq ($(CONFIG_EL3),m)
  M_OBJS += 3c509.o
  endif
endif

ifeq ($(CONFIG_3C515),y)
L_OBJS += 3c515.o
else
  ifeq ($(CONFIG_3C515),m)
  M_OBJS += 3c515.o
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

ifeq ($(CONFIG_EEXPRESS_PRO100),y)
L_OBJS += eepro100.o
else
  ifeq ($(CONFIG_EEXPRESS_PRO100),m)
  M_OBJS += eepro100.o
  endif
endif

ifeq ($(CONFIG_RTL8139),y)
L_OBJS += rtl8139.o
else
  ifeq ($(CONFIG_RTL8139),m)
  M_OBJS += rtl8139.o
  endif
endif

ifeq ($(CONFIG_SIS900),y)
L_OBJS += sis900.o
else
  ifeq ($(CONFIG_SIS900),m)
  M_OBJS += sis900.o
  endif
endif

ifeq ($(CONFIG_DM9102),y) 
L_OBJS += dmfe.o
else
  ifeq ($(CONFIG_DM9102),m)
  M_OBJS += dmfe.o
  endif
endif


ifeq ($(CONFIG_YELLOWFIN),y)
L_OBJS += yellowfin.o
else
  ifeq ($(CONFIG_YELLOWFIN),m)
  M_OBJS += yellowfin.o
  endif
endif

ifeq ($(CONFIG_ACENIC),y)
L_OBJS += acenic.o
else
  ifeq ($(CONFIG_ACENIC),m)
  M_OBJS += acenic.o
  endif
endif

ifeq ($(CONFIG_WAVELAN),y)
L_OBJS += wavelan.o
else
  ifeq ($(CONFIG_WAVELAN),m)
  M_OBJS += wavelan.o
  endif
endif

ifeq ($(CONFIG_ARLAN),y)
LX_OBJS += arlan.o arlan-proc.o
else
  ifeq ($(CONFIG_ARLAN),m)
  MX_OBJS += arlan.o arlan-proc.o
  endif
endif

ifeq ($(CONFIG_TLAN),y)
L_OBJS += tlan.o
else
  ifeq ($(CONFIG_TLAN),m)
  M_OBJS += tlan.o
  endif
endif

ifeq ($(CONFIG_VIA_RHINE),y)
L_OBJS += via-rhine.o
else
  ifeq ($(CONFIG_VIA_RHINE),m)
  M_OBJS += via-rhine.o
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

ifeq ($(CONFIG_NI5010),y)
L_OBJS += ni5010.o
else
  ifeq ($(CONFIG_NI5010),m)
  M_OBJS += ni5010.o
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
else
  ifeq ($(CONFIG_NI65),m)
  M_OBJS += ni65.o
  endif
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
CONFIG_82596_BUILTIN = y
else
  ifeq ($(CONFIG_APRICOT),m)
  CONFIG_82596_MODULE = y
  endif
endif

ifeq ($(CONFIG_MVME16x_NET),y)
CONFIG_82596_BUILTIN = y
else
  ifeq ($(CONFIG_MVME16x_NET),m)
  CONFIG_82596_MODULE = y
  endif
endif

ifeq ($(CONFIG_BVME6000_NET),y)
CONFIG_82596_BUILTIN = y
else
  ifeq ($(CONFIG_BVME6000_NET),m)
  CONFIG_82596_MODULE = y
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
LX_OBJS += arcnet.o
else
  ifeq ($(CONFIG_ARCNET),m)
  MX_OBJS += arcnet.o
  endif
endif

ifeq ($(CONFIG_ARCNET_COM90xx),y)
L_OBJS += com90xx.o
else
  ifeq ($(CONFIG_ARCNET_COM90xx),m)
  M_OBJS += com90xx.o
  endif
endif

ifeq ($(CONFIG_ARCNET_COM90xxIO),y)
L_OBJS += com90io.o
else
  ifeq ($(CONFIG_ARCNET_COM90xxIO),m)
  M_OBJS += com90io.o
  endif
endif

ifeq ($(CONFIG_ARCNET_RIM_I),y)
L_OBJS += arc-rimi.o
else
  ifeq ($(CONFIG_ARCNET_RIM_I),m)
  M_OBJS += arc-rimi.o
  endif
endif

ifeq ($(CONFIG_ARCNET_COM20020),y)
L_OBJS += com20020.o
else
  ifeq ($(CONFIG_ARCNET_COM20020),m)
  M_OBJS += com20020.o
  endif
endif

ifeq ($(CONFIG_ETH16I),y)
L_OBJS += eth16i.o
else
  ifeq ($(CONFIG_ETH16I),m)
  M_OBJS += eth16i.o
  endif
endif

ifeq ($(CONFIG_EPIC100),y)
L_OBJS += epic100.o
else
  ifeq ($(CONFIG_EPIC100),m)
  M_OBJS += epic100.o
  endif
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

# if anything built-in uses ppp_deflate, then build it into the kernel also.
# If not, but a module uses it, build as a module.
ifdef CONFIG_PPPDEF_BUILTIN
L_OBJS += ppp_deflate.o
else
  ifdef CONFIG_PPPDEF_MODULE
  M_OBJS += ppp_deflate.o
  endif
endif

ifeq ($(CONFIG_ARIADNE2),y)
L_OBJS += ariadne2.o
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_ARIADNE2),m)
  M_OBJS += ariadne2.o
  CONFIG_8390_MODULE = y
  endif
endif

ifeq ($(CONFIG_PCMCIA_PCNET),y)
CONFIG_8390_BUILTIN = y
else
  ifeq ($(CONFIG_PCMCIA_PCNET),m)
  CONFIG_8390_MODULE = y
  endif
endif

# If anything built-in uses the 8390, then build it into the kernel also.
# If not, but a module uses it, build as a module.
ifdef CONFIG_8390_BUILTIN
L_OBJS += 8390.o
else
  ifdef CONFIG_8390_MODULE
  MX_OBJS += 8390.o
  endif
endif

ifeq ($(CONFIG_HPLANCE),y)
L_OBJS += hplance.o
CONFIG_7990_BUILTIN = y
else
  ifeq ($(CONFIG_HPLANCE),m)
  CONFIG_7990_MODULE = y
  M_OBJS += hplance.o
  endif
endif

ifeq ($(CONFIG_MVME147_NET),y)
L_OBJS += mvme147.o
CONFIG_7990_BUILTIN = y
else
  ifeq ($(CONFIG_MVME147_NET),m)
  CONFIG_7990_MODULE = y
  M_OBJS += mvme147.o
  endif
endif

# If we need generic LANCE support, either in the kernel or as a module,
# build it in the appropriate way.
ifdef CONFIG_7990_BUILTIN
L_OBJS += 7990.o
else
  ifdef CONFIG_7990_MODULE
  M_OBJS += 7990.o
  endif
endif

# If anything built-in uses the 82596, then build it into the kernel also.
# If not, but a module uses it, build as a module.
ifdef CONFIG_82596_BUILTIN
L_OBJS += 82596.o
else
  ifdef CONFIG_82596_MODULE
  M_OBJS += 82596.o
  endif
endif

ifeq ($(CONFIG_EQUALIZER),y)
L_OBJS += eql.o
else
  ifeq ($(CONFIG_EQUALIZER),m)
  M_OBJS += eql.o
  endif
endif

ifeq ($(CONFIG_MIPS_JAZZ_SONIC),y)
L_OBJS += jazzsonic.o
else
  ifeq ($(CONFIG_MIPS_JAZZ_SONIC),m)
  M_OBJS += jazzsonic.o
  endif
endif

ifeq ($(CONFIG_BAGETLANCE),y)
L_OBJS += bagetlance.o
else
  ifeq ($(CONFIG_BAGETLANCE),m)
  M_OBJS += bagetlance.o
  endif
endif

ifeq ($(CONFIG_DECLANCE),y)
L_OBJS += declance.o
else
  ifeq ($(CONFIG_DECLANCE),m)
  M_OBJS += declance.o
  endif
endif

ifeq ($(CONFIG_ATARILANCE),y)
L_OBJS += atarilance.o
else
  ifeq ($(CONFIG_ATARILANCE),m)
  M_OBJS += atarilance.o
  endif
endif

ifeq ($(CONFIG_ATARI_BIONET),y)
L_OBJS += atari_bionet.o
else
  ifeq ($(CONFIG_ATARI_BIONET),m)
  M_OBJS += atari_bionet.o
  endif
endif

ifeq ($(CONFIG_ATARI_PAMSNET),y)
L_OBJS += atari_pamsnet.o
else
  ifeq ($(CONFIG_ATARI_PAMSNET),m)
  M_OBJS += atari_pamsnet.o
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

ifeq ($(CONFIG_CS89x0),y)
L_OBJS += cs89x0.o
else
  ifeq ($(CONFIG_CS89x0),m)
  M_OBJS += cs89x0.o
  endif
endif

ifeq ($(CONFIG_LTPC),y)
L_OBJS += ltpc.o
else
  ifeq ($(CONFIG_LTPC),m)
  M_OBJS += ltpc.o
  endif
endif

ifeq ($(CONFIG_COPS),y)
L_OBJS += cops.o
else
  ifeq ($(CONFIG_COPS),m)
  M_OBJS += cops.o
  endif
endif

ifeq ($(CONFIG_IPDDP),y)
L_OBJS += ipddp.o
else
  ifeq ($(CONFIG_IPDDP),m)
  M_OBJS += ipddp.o
  endif
endif

ifeq ($(CONFIG_RCPCI),y)
L_OBJS += rcpci.o
else
  ifeq ($(CONFIG_RCPCI),m)
  M_OBJS += rcpci.o
  endif
endif

ifeq ($(CONFIG_MACE),y)
L_OBJS += mace.o
else
  ifeq ($(CONFIG_MACE),m)
  M_OBJS += mace.o
  endif
endif

ifeq ($(CONFIG_MACSONIC),y)
L_OBJS += macsonic.o
endif

ifeq ($(CONFIG_BMAC),y)
L_OBJS += bmac.o
else
  ifeq ($(CONFIG_BMAC),m)
  M_OBJS += bmac.o
  endif
endif

ifeq ($(CONFIG_ADAPTEC_STARFIRE),y)
L_OBJS += starfire.o
else
  ifeq ($(CONFIG_ADAPTEC_STARFIRE),m)
  M_OBJS += starfire.o
  endif
endif

#
# HIPPI adapters
#

ifeq ($(CONFIG_ROADRUNNER),y)
L_OBJS += rrunner.o
else
  ifeq ($(CONFIG_ROADRUNNER),m)
  M_OBJS += rrunner.o
  endif
endif

ifeq ($(CONFIG_IRDA),y)
SUB_DIRS += irda
MOD_IN_SUB_DIRS += irda
else
  ifeq ($(CONFIG_IRDA),m)
  MOD_IN_SUB_DIRS += irda
  endif
endif

ifeq ($(CONFIG_TR),y)
SUB_DIRS += tokenring
MOD_IN_SUB_DIRS += tokenring
else
  ifeq ($(CONFIG_TR),m)
  MOD_IN_SUB_DIRS += tokenring
  endif
endif

ifeq ($(CONFIG_WAN),y)
SUB_DIRS += wan
MOD_IN_SUB_DIRS += wan
else
  ifeq ($(CONFIG_WAN),m)
  MOD_IN_SUB_DIRS += wan
  endif
endif

ifeq ($(CONFIG_NET_FC),y)
SUB_DIRS += fc
MOD_IN_SUB_DIRS += fc
else
  ifeq ($(CONFIG_NET_FC),m)
  MOD_IN_SUB_DIRS += fc
  endif
endif

include $(TOPDIR)/Rules.make

clean:
	rm -f core *.o *.a *.s

rcpci.o: rcpci45.o rclanmtl.o
	$(LD) -r -o rcpci.o rcpci45.o rclanmtl.o
