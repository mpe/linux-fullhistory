/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		Holds initial configuration information for devices.
 *
 * Version:	@(#)Space.c	1.0.7	08/12/93
 *
 * Authors:	Ross Biro, <bir7@leland.Stanford.Edu>
 *		Fred N. van Kempen, <waltje@uWalt.NL.Mugnet.ORG>
 *		Donald J. Becker, <becker@super.org>
 *
 * Changelog:
 *		Paul Gortmaker (06/98): 
 *		 - sort probes in a sane way, make sure all (safe) probes
 *		   get run once & failed autoprobes don't autoprobe again.
 *
 *	FIXME:
 *		Phase out placeholder dev entries put in the linked list
 *		here in favour of drivers using init_etherdev(NULL, ...)
 *		combined with a single find_all_devs() function (for 2.3)
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 */
#include <linux/config.h>
#include <linux/netdevice.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netlink.h>

#define	NEXT_DEV	NULL


/* A unified ethernet device probe.  This is the easiest way to have every
   ethernet adaptor have the name "eth[0123...]".
   */

extern int ne2_probe(struct device *dev);
extern int tulip_probe(struct device *dev);
extern int hp100_probe(struct device *dev);
extern int ultra_probe(struct device *dev);
extern int ultra32_probe(struct device *dev);
extern int ultramca_probe(struct device *dev);
extern int wd_probe(struct device *dev);
extern int el2_probe(struct device *dev);
extern int ne2k_pci_probe(struct device *dev);
extern int ne_probe(struct device *dev);
extern int hp_probe(struct device *dev);
extern int hp_plus_probe(struct device *dev);
extern int znet_probe(struct device *);
extern int express_probe(struct device *);
extern int eepro_probe(struct device *);
extern int eepro100_probe(struct device *);
extern int el3_probe(struct device *);
extern int at1500_probe(struct device *);
extern int pcnet32_probe(struct device *);
extern int at1700_probe(struct device *);
extern int fmv18x_probe(struct device *);
extern int eth16i_probe(struct device *);
extern int depca_probe(struct device *);
extern int i82596_probe(struct device *);
extern int ewrk3_probe(struct device *);
extern int de4x5_probe(struct device *);
extern int el1_probe(struct device *);
extern int wavelan_probe(struct device *);
extern int el16_probe(struct device *);
extern int elmc_probe(struct device *);
extern int elplus_probe(struct device *);
extern int ac3200_probe(struct device *);
extern int es_probe(struct device *);
extern int lne390_probe(struct device *);
extern int ne3210_probe(struct device *);
extern int e2100_probe(struct device *);
extern int ni5010_probe(struct device *);
extern int ni52_probe(struct device *);
extern int ni65_probe(struct device *);
extern int sonic_probe(struct device *);
extern int SK_init(struct device *);
extern int seeq8005_probe(struct device *);
extern int tc59x_probe(struct device *);
extern int dgrs_probe(struct device *);
extern int smc_init( struct device * );
extern int sparc_lance_probe(struct device *);
extern int happy_meal_probe(struct device *);
extern int qec_probe(struct device *);
extern int myri_sbus_probe(struct device *);
extern int sgiseeq_probe(struct device *);
extern int atarilance_probe(struct device *);
extern int a2065_probe(struct device *);
extern int ariadne_probe(struct device *);
extern int ariadne2_probe(struct device *);
extern int hydra_probe(struct device *);
extern int apne_probe(struct device *);
extern int bionet_probe(struct device *);
extern int pamsnet_probe(struct device *);
extern int tlan_probe(struct device *);
extern int mace_probe(struct device *);
extern int bmac_probe(struct device *);
extern int cs89x0_probe(struct device *dev);
extern int ethertap_probe(struct device *dev);
extern int ether1_probe (struct device *dev);
extern int ether3_probe (struct device *dev);
extern int etherh_probe (struct device *dev);
extern int am79c961_probe(struct device *dev);
extern int epic100_probe(struct device *dev);
extern int rtl8139_probe(struct device *dev);
extern int hplance_probe(struct device *dev);
extern int via_rhine_probe(struct device *dev);
extern int tc515_probe(struct device *dev);
extern int lance_probe(struct device *dev);
extern int rcpci_probe(struct device *);

/* Gigabit Ethernet adapters */
extern int yellowfin_probe(struct device *dev);
extern int acenic_probe(struct device *dev);

/* Detachable devices ("pocket adaptors") */
extern int atp_init(struct device *);
extern int de600_probe(struct device *);
extern int de620_probe(struct device *);

/* FDDI adapters */
extern int dfx_probe(struct device *dev);
extern int apfddi_init(struct device *dev);

/* HIPPI boards */
extern int rr_hippi_probe(struct device *);

struct devprobe
{
	int (*probe)(struct device *dev);
	int status;	/* non-zero if autoprobe has failed */
};

/*
 * probe_list walks a list of probe functions and calls each so long
 * as a non-zero ioaddr is given, or as long as it hasn't already failed 
 * to find a card in the past (as recorded by "status") when asked to
 * autoprobe (i.e. a probe that fails to find a card when autoprobing
 * will not be asked to autoprobe again).  It exits when a card is found.
 */
__initfunc(static int probe_list(struct device *dev, struct devprobe *plist))
{
	struct devprobe *p = plist;
	unsigned long base_addr = dev->base_addr;

	while (p->probe != NULL) {
		if (base_addr && p->probe(dev) == 0)	/* probe given addr */
			return 0;
		else if (p->status == 0) {		/* has autoprobe failed yet? */
			p->status = p->probe(dev);	/* no, try autoprobe */
			if (p->status == 0)
				return 0;
		}
		p++;
	}
	return -ENODEV;
}

/*
 * If your probe touches ISA ports (<0x400) in addition to
 * looking for PCI cards, then put it in the isa_probes
 * list instead.
 */
struct devprobe pci_probes[] __initdata = {
#ifdef CONFIG_DGRS
	{dgrs_probe, 0},
#endif
#ifdef CONFIG_RCPCI
	{rcpci_probe, 0},
#endif
#ifdef CONFIG_VORTEX
	{tc59x_probe, 0},
#endif
#ifdef CONFIG_NE2K_PCI
	{ne2k_pci_probe, 0},
#endif
#ifdef CONFIG_PCNET32
	{pcnet32_probe, 0},
#endif	
#ifdef CONFIG_EEXPRESS_PRO100	/* Intel EtherExpress Pro/100 */
	{eepro100_probe, 0},
#endif
#ifdef CONFIG_DEC_ELCP 
	{tulip_probe, 0},
#endif
#ifdef CONFIG_DE4X5             /* DEC DE425, DE434, DE435 adapters */
	{de4x5_probe, 0},
#endif
#ifdef CONFIG_TLAN
	{tlan_probe, 0},
#endif
#ifdef CONFIG_EPIC100
	{epic100_probe, 0},
#endif
#ifdef CONFIG_RTL8139
	{rtl8139_probe, 0},
#endif
#ifdef CONFIG_YELLOWFIN
	{yellowfin_probe, 0},
#endif
#ifdef CONFIG_ACENIC
	{acenic_probe, 0},
#endif
#ifdef CONFIG_VIA_RHINE
	{via_rhine_probe, 0},
#endif
	{NULL, 0},
};

/*
 * This is a bit of an artificial separation as there are PCI drivers
 * that also probe for EISA cards (in the PCI group) and there are ISA
 * drivers that probe for EISA cards (in the ISA group).  These are the
 * EISA only driver probes.
 */
struct devprobe eisa_probes[] __initdata = {
#ifdef CONFIG_ULTRA32 
	{ultra32_probe, 0},	
#endif
#ifdef CONFIG_AC3200	
	{ac3200_probe, 0},
#endif
#ifdef CONFIG_ES3210
	{es_probe, 0},
#endif
#ifdef CONFIG_LNE390
	{lne390_probe, 0},
#endif
#ifdef CONFIG_NE3210
	{ne3210_probe, 0},
#endif
	{NULL, 0},
};

struct devprobe sparc_probes[] __initdata = {
#ifdef CONFIG_HAPPYMEAL
	{happy_meal_probe, 0},
#endif
#ifdef CONFIG_SUNLANCE
	{sparc_lance_probe, 0},
#endif
#ifdef CONFIG_SUNQE
	{qec_probe, 0},
#endif
#ifdef CONFIG_MYRI_SBUS
	{myri_sbus_probe, 0},
#endif
	{NULL, 0},
};

struct devprobe mca_probes[] __initdata = {
#ifdef CONFIG_ULTRAMCA 
	{ultramca_probe, 0},
#endif
#ifdef CONFIG_NE2_MCA
	{ne2_probe, 0},
#endif
#ifdef CONFIG_ELMC		/* 3c523 */
	{elmc_probe, 0},
#endif
	{NULL, 0},
};

/*
 * ISA probes that touch addresses < 0x400 (including those that also
 * look for EISA/PCI cards in addition to ISA cards).
 */
struct devprobe isa_probes[] __initdata = {
#ifdef CONFIG_EL3		/* ISA, EISA (MCA someday) 3c5x9 */
	{el3_probe, 0},
#endif
#ifdef CONFIG_HP100 		/* ISA, EISA & PCI */
	{hp100_probe, 0},
#endif	
#ifdef CONFIG_3C515
	{tc515_probe, 0},
#endif
#ifdef CONFIG_ULTRA 
	{ultra_probe, 0},
#endif
#ifdef CONFIG_WD80x3 
	{wd_probe, 0},
#endif
#ifdef CONFIG_EL2 		/* 3c503 */
	{el2_probe, 0},
#endif
#ifdef CONFIG_HPLAN
	{hp_probe, 0},
#endif
#ifdef CONFIG_HPLAN_PLUS
	{hp_plus_probe, 0},
#endif
#ifdef CONFIG_E2100		/* Cabletron E21xx series. */
	{e2100_probe, 0},
#endif
#ifdef CONFIG_NE2000		/* ISA (use ne2k-pci for PCI cards) */
	{ne_probe, 0},
#endif
#ifdef CONFIG_LANCE		/* ISA/VLB (use pcnet32 for PCI cards) */
	{lance_probe, 0},
#endif
#ifdef CONFIG_SMC9194
	{smc_init, 0},
#endif
#ifdef CONFIG_SEEQ8005 
	{seeq8005_probe, 0},
#endif
#ifdef CONFIG_AT1500
	{at1500_probe, 0},
#endif
#ifdef CONFIG_CS89x0
 	{cs89x0_probe, 0},
#endif
#ifdef CONFIG_AT1700
	{at1700_probe, 0},
#endif
#ifdef CONFIG_FMV18X		/* Fujitsu FMV-181/182 */
	{fmv18x_probe, 0},
#endif
#ifdef CONFIG_ETH16I
	{eth16i_probe, 0},	/* ICL EtherTeam 16i/32 */
#endif
#ifdef CONFIG_ZNET		/* Zenith Z-Note and some IBM Thinkpads. */
	{znet_probe, 0},
#endif
#ifdef CONFIG_EEXPRESS		/* Intel EtherExpress */
	{express_probe, 0},
#endif
#ifdef CONFIG_EEXPRESS_PRO	/* Intel EtherExpress Pro/10 */
	{eepro_probe, 0},
#endif
#ifdef CONFIG_DEPCA		/* DEC DEPCA */
	{depca_probe, 0},
#endif
#ifdef CONFIG_EWRK3             /* DEC EtherWORKS 3 */
    	{ewrk3_probe, 0},
#endif
#if defined(CONFIG_APRICOT) || defined(CONFIG_MVME16x_NET) || defined(CONFIG_BVME6000_NET)	/* Intel I82596 */
	{i82596_probe, 0},
#endif
#ifdef CONFIG_EL1		/* 3c501 */
	{el1_probe, 0},
#endif
#ifdef CONFIG_WAVELAN		/* WaveLAN */
	{wavelan_probe, 0},
#endif
#ifdef CONFIG_EL16		/* 3c507 */
	{el16_probe, 0},
#endif
#ifdef CONFIG_ELPLUS		/* 3c505 */
	{elplus_probe, 0},
#endif
#ifdef CONFIG_SK_G16
	{SK_init, 0},
#endif
#ifdef CONFIG_NI5010
	{ni5010_probe, 0},
#endif
#ifdef CONFIG_NI52
	{ni52_probe, 0},
#endif
#ifdef CONFIG_NI65
	{ni65_probe, 0},
#endif
	{NULL, 0},
};

struct devprobe parport_probes[] __initdata = {
#ifdef CONFIG_DE600		/* D-Link DE-600 adapter */
	{de600_probe, 0},
#endif
#ifdef CONFIG_DE620		/* D-Link DE-620 adapter */
	{de620_probe, 0},
#endif
#ifdef CONFIG_ATP		/* AT-LAN-TEC (RealTek) pocket adaptor. */
	{atp_init, 0},
#endif
	{NULL, 0},
};

struct devprobe m68k_probes[] __initdata = {
#ifdef CONFIG_ATARILANCE	/* Lance-based Atari ethernet boards */
	{atarilance_probe, 0},
#endif
#ifdef CONFIG_A2065		/* Commodore/Ameristar A2065 Ethernet Board */
	{a2065_probe, 0},
#endif
#ifdef CONFIG_ARIADNE		/* Village Tronic Ariadne Ethernet Board */
	{ariadne_probe, 0},
#endif
#ifdef CONFIG_ARIADNE2		/* Village Tronic Ariadne II Ethernet Board */
	{ariadne2_probe, 0},
#endif
#ifdef CONFIG_HYDRA		/* Hydra Systems Amiganet Ethernet board */
	{hydra_probe, 0},
#endif
#ifdef CONFIG_APNE		/* A1200 PCMCIA NE2000 */
	{apne_probe, 0},
#endif
#ifdef CONFIG_ATARI_BIONET	/* Atari Bionet Ethernet board */
	{bionet_probe, 0},
#endif
#ifdef CONFIG_ATARI_PAMSNET	/* Atari PAMsNet Ethernet board */
	{pamsnet_probe, 0},
#endif
#ifdef CONFIG_HPLANCE		/* HP300 internal Ethernet */
	{hplance_probe, 0},
#endif
	{NULL, 0},
};

struct devprobe ppc_probes[] __initdata = {
#ifdef CONFIG_MACE
	{mace_probe, 0},
#endif
#ifdef CONFIG_BMAC
	{bmac_probe, 0},
#endif
	{NULL, 0},
};

struct devprobe sgi_probes[] __initdata = {
#ifdef CONFIG_SGISEEQ
	{sgiseeq_probe, 0},
#endif
	{NULL, 0},
};

struct devprobe mips_probes[] __initdata = {
#ifdef CONFIG_MIPS_JAZZ_SONIC
	{sonic_probe, 0},
#endif
	{NULL, 0},
};

struct devprobe arm_probes[] __initdata = {
#ifdef CONFIG_ARM_ETHERH
	{etherh_probe , 0},
#endif
#ifdef CONFIG_ARM_ETHER3
	{ether3_probe , 0},
#endif
#ifdef CONFIG_ARM_ETHER1
	{ether1_probe , 0},
#endif
#ifdef CONFIG_ARM_AM79C961A
	{am79c961_probe, 0},
#endif
	{NULL, 0},
};

/*
 * Unified ethernet device probe, segmented per architecture and
 * per bus interface.
 */
__initfunc(static int ethif_probe(struct device *dev))
{
	unsigned long base_addr = dev->base_addr;

	/* 
	 * Backwards compatibility - historically an I/O base of 1 was 
	 * used to indicate not to probe for this ethN interface 
	 */
	if (base_addr == 1)
		return 1;		/* ENXIO */

	/* 
	 * The arch specific probes are 1st so that any on-board ethernet
	 * will be probed before other ISA/EISA/MCA/PCI bus cards.
	 */
	if (probe_list(dev, arm_probes) == 0)
		return 0;
	if (probe_list(dev, m68k_probes) == 0)
		return 0;
	if (probe_list(dev, mips_probes) == 0)
		return 0;
	if (probe_list(dev, ppc_probes) == 0)
		return 0;
	if (probe_list(dev, sgi_probes) == 0)
		return 0;
	if (probe_list(dev, sparc_probes) == 0)
		return 0;
	if (probe_list(dev, pci_probes) == 0)
		return 0;
	if (probe_list(dev, eisa_probes) == 0)
		return 0;
	if (probe_list(dev, mca_probes) == 0)
		return 0;
        /*
         * Backwards compatibility - an I/O of 0xffe0 was used to indicate
         * that we shouldn't do a bunch of potentially risky ISA probes
         * for ethN (N>1).  Since the widespread use of modules, *nobody*
         * compiles a kernel with all the ISA drivers built in anymore,
         * and so we should delete this check in linux 2.3 - Paul G.
         */
	if (base_addr != 0xffe0 && probe_list(dev, isa_probes) == 0) 
		return 0;
	if (probe_list(dev, parport_probes) == 0)
		return 0;
	return -ENODEV;
}

#ifdef CONFIG_FDDI
__initfunc(static int fddiif_probe(struct device *dev))
{
    unsigned long base_addr = dev->base_addr;

    if (base_addr == 1)
	    return 1;		/* ENXIO */

    if (1
#ifdef CONFIG_DEFXX
	&& dfx_probe(dev)
#endif
#ifdef CONFIG_APFDDI
	&& apfddi_init(dev);
#endif
	&& 1 ) {
	    return 1;	/* -ENODEV or -EAGAIN would be more accurate. */
    }
    return 0;
}
#endif

#ifdef CONFIG_HIPPI
static int hippi_probe(struct device *dev)
{
	/*
	 * Damn this is ugly.
	 *
	 * Why the heck would we want to determine this from the base
	 * address? Stupid PC'ism .... grrrrr.
	 */
	if (dev->base_addr == -1)
		return 1;

	if (1
#ifdef CONFIG_ROADRUNNER
	    && rr_hippi_probe(dev)
#endif
	    && 1 ) {
		return 1; /* -ENODEV or -EAGAIN would be more accurate. */
	}
	return 0;
}
#endif

#ifdef CONFIG_ETHERTAP
    static struct device tap0_dev = { "tap0", 0, 0, 0, 0, NETLINK_TAPBASE, 0, 0, 0, 0, NEXT_DEV, ethertap_probe, };
#   undef NEXT_DEV
#   define NEXT_DEV	(&tap0_dev)
#endif

#ifdef CONFIG_SDLA
    extern int sdla_init(struct device *);
    static struct device sdla0_dev = { "sdla0", 0, 0, 0, 0, 0, 0, 0, 0, 0, NEXT_DEV, sdla_init, };

#   undef NEXT_DEV
#   define NEXT_DEV	(&sdla0_dev)
#endif

#if defined(CONFIG_LTPC)
    extern int ltpc_probe(struct device *);
    static struct device dev_ltpc = {
        "lt0\0   ",
                0, 0, 0, 0,
                0x0, 0,
                0, 0, 0, NEXT_DEV, ltpc_probe };
#   undef NEXT_DEV
#   define NEXT_DEV	(&dev_ltpc)
#endif  /* LTPC */

#if defined(CONFIG_COPS)
    extern int cops_probe(struct device *);
    static struct device cops2_dev = { "lt2", 0, 0, 0, 0, 0x0, 0, 0, 0, 0, NEXT_DEV, cops_probe };
    static struct device cops1_dev = { "lt1", 0, 0, 0, 0, 0x0, 0, 0, 0, 0, &cops2_dev, cops_probe };
    static struct device cops0_dev = { "lt0", 0, 0, 0, 0, 0x0, 0, 0, 0, 0, &cops1_dev, cops_probe };
#   undef NEXT_DEV
#   define NEXT_DEV     (&cops0_dev)
#endif  /* COPS */

#if defined(CONFIG_IPDDP)
    extern int ipddp_init(struct device *dev);
    static struct device dev_ipddp = {
        "ipddp0\0   ",
                0, 0, 0, 0,
                0x0, 0,
                0, 0, 0, NEXT_DEV, ipddp_init };
#   undef NEXT_DEV
#   define NEXT_DEV     (&dev_ipddp)
#endif /* CONFIG_IPDDP */

/* The first device defaults to I/O base '0', which means autoprobe. */
#ifndef ETH0_ADDR
# define ETH0_ADDR 0
#endif
#ifndef ETH0_IRQ
# define ETH0_IRQ 0
#endif

/* "eth0" defaults to autoprobe (== 0), other use a base of 0xffe0 (== -0x20),
   which means "don't do ISA probes".  Distributions don't ship kernels with
   all ISA drivers compiled in anymore, so its probably no longer an issue. */

#define ETH_NOPROBE_ADDR 0xffe0

static struct device eth7_dev = {
    "eth7", 0,0,0,0,ETH_NOPROBE_ADDR /* I/O base*/, 0,0,0,0, NEXT_DEV, ethif_probe };
static struct device eth6_dev = {
    "eth6", 0,0,0,0,ETH_NOPROBE_ADDR /* I/O base*/, 0,0,0,0, &eth7_dev, ethif_probe };
static struct device eth5_dev = {
    "eth5", 0,0,0,0,ETH_NOPROBE_ADDR /* I/O base*/, 0,0,0,0, &eth6_dev, ethif_probe };
static struct device eth4_dev = {
    "eth4", 0,0,0,0,ETH_NOPROBE_ADDR /* I/O base*/, 0,0,0,0, &eth5_dev, ethif_probe };
static struct device eth3_dev = {
    "eth3", 0,0,0,0,ETH_NOPROBE_ADDR /* I/O base*/, 0,0,0,0, &eth4_dev, ethif_probe };
static struct device eth2_dev = {
    "eth2", 0,0,0,0,ETH_NOPROBE_ADDR /* I/O base*/, 0,0,0,0, &eth3_dev, ethif_probe };
static struct device eth1_dev = {
    "eth1", 0,0,0,0,ETH_NOPROBE_ADDR /* I/O base*/, 0,0,0,0, &eth2_dev, ethif_probe };

static struct device eth0_dev = {
    "eth0", 0, 0, 0, 0, ETH0_ADDR, ETH0_IRQ, 0, 0, 0, &eth1_dev, ethif_probe };

#   undef NEXT_DEV
#   define NEXT_DEV	(&eth0_dev)

#if defined(SLIP) || defined(CONFIG_SLIP)
	/* To be exact, this node just hooks the initialization
	   routines to the device structures.			*/
extern int slip_init_ctrl_dev(struct device *);
static struct device slip_bootstrap = {
  "slip_proto", 0x0, 0x0, 0x0, 0x0, 0, 0, 0, 0, 0, NEXT_DEV, slip_init_ctrl_dev, };
#undef NEXT_DEV
#define NEXT_DEV (&slip_bootstrap)
#endif	/* SLIP */

#if defined(X25_ASY) || defined(CONFIG_X25_ASY)
	/* To be exact, this node just hooks the initialization
	   routines to the device structures.			*/
extern int x25_asy_init_ctrl_dev(struct device *);
static struct device x25_asy_bootstrap = {
  "x25_proto", 0x0, 0x0, 0x0, 0x0, 0, 0, 0, 0, 0, NEXT_DEV, x25_asy_init_ctrl_dev, };
#undef NEXT_DEV
#define NEXT_DEV (&x25_asy_bootstrap)
#endif	/* X25_ASY */
  
#if defined(CONFIG_MKISS)
	/* To be exact, this node just hooks the initialization
	   routines to the device structures.			*/
extern int mkiss_init_ctrl_dev(struct device *);
static struct device mkiss_bootstrap = {
  "mkiss_proto", 0x0, 0x0, 0x0, 0x0, 0, 0, 0, 0, 0, NEXT_DEV, mkiss_init_ctrl_dev, };
#undef NEXT_DEV
#define NEXT_DEV (&mkiss_bootstrap)
#endif	/* MKISS */
  
#if defined(CONFIG_STRIP)
extern int strip_init_ctrl_dev(struct device *);
static struct device strip_bootstrap = {
    "strip_proto", 0x0, 0x0, 0x0, 0x0, 0, 0, 0, 0, 0, NEXT_DEV, strip_init_ctrl_dev, };
#undef NEXT_DEV
#define NEXT_DEV (&strip_bootstrap)
#endif   /* STRIP */

#if defined(CONFIG_PPP)
extern int ppp_init(struct device *);
static struct device ppp_bootstrap = {
    "ppp_proto", 0x0, 0x0, 0x0, 0x0, 0, 0, 0, 0, 0, NEXT_DEV, ppp_init, };
#undef NEXT_DEV
#define NEXT_DEV (&ppp_bootstrap)
#endif   /* PPP */

#ifdef CONFIG_DUMMY
    extern int dummy_init(struct device *dev);
    static struct device dummy_dev = {
	"dummy", 0x0, 0x0, 0x0, 0x0, 0, 0, 0, 0, 0, NEXT_DEV, dummy_init, };
#   undef	NEXT_DEV
#   define	NEXT_DEV	(&dummy_dev)
#endif

#ifdef CONFIG_EQUALIZER
extern int eql_init(struct device *dev);
struct device eql_dev = {
  "eql",			/* Master device for IP traffic load 
				   balancing */
  0x0, 0x0, 0x0, 0x0,		/* recv end/start; mem end/start */
  0,				/* base I/O address */
  0,				/* IRQ */
  0, 0, 0,			/* flags */
  NEXT_DEV,			/* next device */
  eql_init			/* set up the rest */
};
#   undef       NEXT_DEV
#   define      NEXT_DEV        (&eql_dev)
#endif

#ifdef CONFIG_TR
/* Token-ring device probe */
extern int ibmtr_probe(struct device *);

static int
trif_probe(struct device *dev)
{
    if (1
#ifdef CONFIG_IBMTR
	&& ibmtr_probe(dev)
#endif
#ifdef CONFIG_SKTR
	&& sktr_probe(dev)
#endif
#ifdef CONFIG_SMCTR
	&& smctr_probe(dev)
#endif
	&& 1 ) {
	return 1;	/* -ENODEV or -EAGAIN would be more accurate. */
    }
    return 0;
}
static struct device tr7_dev = {
    "tr7",0,0,0,0,0,0,0,0,0, NEXT_DEV, trif_probe };
static struct device tr6_dev = {
    "tr6",0,0,0,0,0,0,0,0,0, &tr7_dev, trif_probe };
static struct device tr5_dev = {
    "tr5",0,0,0,0,0,0,0,0,0, &tr6_dev, trif_probe };
static struct device tr4_dev = {
    "tr4",0,0,0,0,0,0,0,0,0, &tr5_dev, trif_probe };
static struct device tr3_dev = {
    "tr3",0,0,0,0,0,0,0,0,0, &tr4_dev, trif_probe };
static struct device tr2_dev = {
    "tr2",0,0,0,0,0,0,0,0,0, &tr3_dev, trif_probe };
static struct device tr1_dev = {
    "tr1",0,0,0,0,0,0,0,0,0, &tr2_dev, trif_probe };
static struct device tr0_dev = {
    "tr0",0,0,0,0,0,0,0,0,0, &tr1_dev, trif_probe };
#   undef       NEXT_DEV
#   define      NEXT_DEV        (&tr0_dev)

#endif 

#ifdef CONFIG_FDDI
	static struct device fddi7_dev =
		{"fddi7", 0, 0, 0, 0, 0, 0, 0, 0, 0, NEXT_DEV, fddiif_probe};
	static struct device fddi6_dev =
		{"fddi6", 0, 0, 0, 0, 0, 0, 0, 0, 0, &fddi7_dev, fddiif_probe};
	static struct device fddi5_dev =
		{"fddi5", 0, 0, 0, 0, 0, 0, 0, 0, 0, &fddi6_dev, fddiif_probe};
	static struct device fddi4_dev =
		{"fddi4", 0, 0, 0, 0, 0, 0, 0, 0, 0, &fddi5_dev, fddiif_probe};
	static struct device fddi3_dev =
		{"fddi3", 0, 0, 0, 0, 0, 0, 0, 0, 0, &fddi4_dev, fddiif_probe};
	static struct device fddi2_dev =
		{"fddi2", 0, 0, 0, 0, 0, 0, 0, 0, 0, &fddi3_dev, fddiif_probe};
	static struct device fddi1_dev =
		{"fddi1", 0, 0, 0, 0, 0, 0, 0, 0, 0, &fddi2_dev, fddiif_probe};
	static struct device fddi0_dev =
		{"fddi0", 0, 0, 0, 0, 0, 0, 0, 0, 0, &fddi1_dev, fddiif_probe};
#undef	NEXT_DEV
#define	NEXT_DEV	(&fddi0_dev)
#endif 

#ifdef CONFIG_HIPPI
	static struct device hip3_dev =
		{"hip3", 0, 0, 0, 0, 0, 0, 0, 0, 0, NEXT_DEV, hippi_probe};
	static struct device hip2_dev =
		{"hip2", 0, 0, 0, 0, 0, 0, 0, 0, 0, &hip3_dev, hippi_probe};
	static struct device hip1_dev =
		{"hip1", 0, 0, 0, 0, 0, 0, 0, 0, 0, &hip2_dev, hippi_probe};
	static struct device hip0_dev =
		{"hip0", 0, 0, 0, 0, 0, 0, 0, 0, 0, &hip1_dev, hippi_probe};

#undef	NEXT_DEV
#define	NEXT_DEV	(&hip0_dev)
#endif 

#ifdef CONFIG_APBIF
    extern int bif_init(struct device *dev);
    static struct device bif_dev = {
        "bif", 0x0, 0x0, 0x0, 0x0, 0, 0, 0, 0, 0, NEXT_DEV, bif_init };
#   undef       NEXT_DEV
#   define      NEXT_DEV        (&bif_dev)
#endif
	
extern int loopback_init(struct device *dev);
struct device loopback_dev = {
	"lo",			/* Software Loopback interface		*/
	0x0,			/* recv memory end			*/
	0x0,			/* recv memory start			*/
	0x0,			/* memory end				*/
	0x0,			/* memory start				*/
	0,			/* base I/O address			*/
	0,			/* IRQ					*/
	0, 0, 0,		/* flags				*/
	NEXT_DEV,		/* next device				*/
	loopback_init		/* loopback_init should set up the rest	*/
};

struct device *dev_base = &loopback_dev;
