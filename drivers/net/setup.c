/*
 *	New style setup code for the network devices
 */
 
#include <linux/config.h>
#include <linux/netdevice.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/netlink.h>

extern int mkiss_init_ctrl_dev(void);
extern int ppp_init(void);
extern int slip_init_ctrl_dev(void);
extern int strip_init_ctrl_dev(void);
extern int x25_asy_init_ctrl_dev(void);
extern int slhc_install(void);
  
extern int bpq_init(void);
extern int dmascc_init(void);
extern int scc_init(void);
extern int yam_init(void);

extern int awc4500_pci_probe(void);
extern int awc4500_isa_probe(void);
extern int awc4500_pnp_probe(void);
extern int awc4500_365_probe(void);
extern int arcnet_init(void); 
extern int cpm_enet_init(void); 
extern int dlci_setup(void); 
extern int lapbeth_init(void);
extern int sdla_setup(void); 
extern int sdla_c_setup(void); 

extern int abyss_probe(void);
extern int madgemc_probe(void);
extern int tms_pci_probe(void);

/* Pad device name to IFNAMSIZ=16. F.e. __PAD6 is tring of 9 zeros. */
#define __PAD6 "\0\0\0\0\0\0\0\0\0"
#define __PAD5 __PAD6 "\0"
#define __PAD4 __PAD5 "\0"
#define __PAD3 __PAD4 "\0"
#define __PAD2 __PAD3 "\0"


/*
 *	Devices in this list must do new style probing. That is they must
 *	allocate their own device objects and do their own bus scans.
 */

struct net_probe
{
	int (*probe)(void);
	int status;	/* non-zero if autoprobe has failed */
};
 
struct net_probe pci_probes[] __initdata = {
	/*
	 *	Early setup devices
	 */

#if defined(CONFIG_SCC)
	{scc_init, 0},
#endif
#if defined(CONFIG_DMASCC)
	{dmascc_init, 0},
#endif	
#if defined(CONFIG_BPQETHER)
	{bpq_init, 0},
#endif
#if defined(CONFIG_DLCI)
	{dlci_setup, 0},
#endif
#if defined(CONFIG_SDLA)
	{sdla_c_setup, 0},
#endif
#if defined(CONFIG_LAPBETHER)
	{lapbeth_init, 0},
#endif
#if defined(CONFIG_ARCNET)
	{arcnet_init, 0},
#endif
#if defined(CONFIG_8xx)
        {cpm_enet_init, 0},
#endif
	/*
	 *	SLHC if present needs attaching so other people see it
	 *	even if not opened.
	 */
	 
#ifdef CONFIG_INET	 
#if (defined(CONFIG_SLIP) && defined(CONFIG_SLIP_COMPRESSED)) \
	 || defined(CONFIG_PPP) \
    || (defined(CONFIG_ISDN) && defined(CONFIG_ISDN_PPP))
	{slhc_install, 0},
#endif	
#endif

/*
*
*	Wireless non-HAM
*
*/
#ifdef CONFIG_AIRONET4500_NONCS

#ifdef CONFIG_AIRONET4500_PCI
	{awc4500_pci_probe,0},
#endif

#ifdef CONFIG_AIRONET4500_PNP
	{awc4500_pnp_probe,0},
#endif

#endif
/*
 *	Amateur Radio Drivers
 */	

#ifdef CONFIG_YAM
	{yam_init, 0},
#endif	/* CONFIG_YAM */

/*
 *	Token Ring Drivers
 */  
#ifdef CONFIG_ABYSS
	{abyss_probe, 0},
#endif
#ifdef CONFIG_MADGEMC
	{madgemc_probe, 0},
#endif
#ifdef CONFIG_TMSPCI
	{tms_pci_probe, 0},
#endif
 
	{NULL, 0},
};


/*
 *	Run the updated device probes. These do not need a device passed
 *	into them.
 */
 
static void __init network_probe(void)
{
	struct net_probe *p = pci_probes;

	while (p->probe != NULL)
	{
		p->status = p->probe();
		p++;
	}
}


/*
 *	Initialise the line discipline drivers
 */
 
static void __init network_ldisc_init(void)
{
#if defined(CONFIG_SLIP)
	slip_init_ctrl_dev();
#endif
#if defined(CONFIG_X25_ASY)
	x25_asy_init_ctrl_dev();
#endif
#if defined(CONFIG_MKISS)
	mkiss_init_ctrl_dev();
#endif
#if defined(CONFIG_STRIP)
	strip_init_ctrl_dev();
#endif
#if defined(CONFIG_PPP)
	ppp_init();
#endif
}


static void __init appletalk_device_init(void)
{
#if defined(CONFIG_IPDDP)
	extern int ipddp_init(struct net_device *dev);
	static struct net_device dev_ipddp = {
				"ipddp0"  __PAD6,
				0, 0, 0, 0,
				0x0, 0,
				0, 0, 0, NULL, ipddp_init 
	};
	
	dev_ipddp.init(&dev_ipddp);
#endif /* CONFIG_IPDDP */
}


/*
 *	The loopback device is global so it can be directly referenced
 *	by the network code.
 */
 
extern int loopback_init(struct net_device *dev);
struct net_device loopback_dev = 
{
	"lo" __PAD2,		/* Software Loopback interface		*/
	0x0,			/* recv memory end			*/
	0x0,			/* recv memory start			*/
	0x0,			/* memory end				*/
	0x0,			/* memory start				*/
	0,			/* base I/O address			*/
	0,			/* IRQ					*/
	0, 0, 0,		/* flags				*/
	NULL,			/* next device				*/
	loopback_init		/* loopback_init should set up the rest	*/
};

static void special_device_init(void)
{
#ifdef CONFIG_DUMMY
	{
		extern int dummy_init(struct net_device *dev);
		static struct net_device dummy_dev = {
			"dummy" __PAD5, 0x0, 0x0, 0x0, 0x0, 0, 0, 0, 0, 0, NULL, dummy_init, 
		};
		register_netdev(&dummy_dev);
	}
#endif	
#ifdef CONFIG_EQUALIZER
	{
		extern int eql_init(struct net_device *dev);
		static struct net_device eql_dev = 
		{
			"eql" __PAD3,			/* Master device for IP traffic load balancing */
			0x0, 0x0, 0x0, 0x0,		/* recv end/start; mem end/start */
			0,				/* base I/O address */
			0,				/* IRQ */
			0, 0, 0,			/* flags */
			NULL,				/* next device */
			eql_init			/* set up the rest */
		};
		register_netdev(&eql_dev);
	}
#endif	
#ifdef CONFIG_APBIF
	{
		extern int bif_init(struct net_device *dev);
		static struct net_device bif_dev = 
		{
        		"bif" __PAD3, 0x0, 0x0, 0x0, 0x0, 0, 0, 0, 0, 0, NULL, bif_init 
        	};
		register_netdev(&bif_dev);
        }
#endif
#ifdef CONFIG_NET_SB1000
	{
		extern int sb1000_probe(struct net_device *dev);
		static struct net_device sb1000_dev = 
		{
			"cm0", 0x0, 0x0, 0x0, 0x0, 0, 0, 0, 0, 0, NULL, sb1000_probe 
		};
		register_netdev(&sb1000_dev);
	}
#endif
	register_netdev(&loopback_dev);
}

/*
 *	Initialise network devices
 */
 
void __init net_device_init(void)
{
	/* Devices supporting the new probing API */
	network_probe();
	/* Line disciplines */
	network_ldisc_init();
	/* Appletalk */
	appletalk_device_init();
	/* Special devices */
	special_device_init();
	/* That kicks off the legacy init functions */
}
