/*
 *	Wavelan ISA driver
 *
 *		Jean II - HPLB '96
 *
 * Reorganisation and extension of the driver.
 *
 * This file contain all definition and declarations necessary for the
 * wavelan isa driver. This file is a private header, so it should
 * be included only on wavelan.c !!!
 */

#ifndef WAVELAN_P_H
#define WAVELAN_P_H

/************************** DOCUMENTATION **************************/
/*
 * This driver provide a Linux interface to the Wavelan ISA hardware
 * The Wavelan is a product of Lucent ("http://wavelan.netland.nl/").
 * This division was formerly part of NCR and then AT&T.
 * Wavelan are also distributed by DEC (RoamAbout), Digital Ocean and
 * Aironet (Arlan). If you have one of those product, you will need to
 * make some changes below...
 *
 * This driver is still a beta software. A lot of bugs have been corrected,
 * a lot of functionalities are implemented, the whole appear pretty stable,
 * but there is still some area of improvement (encryption, performance...).
 *
 * To know how to use this driver, read the NET3 HOWTO.
 * If you want to exploit the many other fonctionalities, look comments
 * in the code...
 *
 * This driver is the result of the effort of many peoples (see below).
 */

/* ------------------------ SPECIFIC NOTES ------------------------ */
/*
 * MAC address and hardware detection :
 * ----------------------------------
 *	The detection code of the wavelan chech that the first 3
 *	octets of the MAC address fit the company code. This type of
 *	detection work well for AT&T cards (because the AT&T code is
 *	hardcoded in wavelan.h), but of course will fail for other
 *	manufacturer.
 *
 *	If you are sure that your card is derived from the wavelan,
 *	here is the way to configure it :
 *	1) Get your MAC address
 *		a) With your card utilities (wfreqsel, instconf, ...)
 *		b) With the driver :
 *			o compile the kernel with DEBUG_CONFIG_INFO enabled
 *			o Boot and look the card messages
 *	2) Set your MAC code (3 octets) in MAC_ADDRESSES[][3] (wavelan.h)
 *	3) Compile & verify
 *	4) Send me the MAC code - I will include it in the next version...
 *
 * "CU Inactive" message at boot up :
 * -----------------------------------
 *	It seem that there is some weird timings problems with the
 *	Intel microcontroler. In fact, this message is triggered by a
 *	bad reading of the on board ram the first time we read the
 *	control block. If you ignore this message, all is ok (but in
 *	fact, currently, it reset the wavelan hardware).
 *
 *	To get rid of that problem, there is two solution. The first
 *	is to add a dummy read of the scb at the end of
 *	wv_82586_config. The second is to add the timers
 *	wv_synchronous_cmd and wv_ack (the udelay just after the
 *	waiting loops - seem that the controler is not totally ready
 *	when it say it is !).
 *
 *	In the current code, I use the second solution (to be
 *	consistent with the original solution of Bruce Janson).
 */

/* --------------------- WIRELESS EXTENSIONS --------------------- */
/*
 * This driver is the first one to support "wireless extensions".
 * This set of extensions provide you some way to control the wireless
 * caracteristics of the hardware in a standard way and support for
 * applications for taking advantage of it (like Mobile IP).
 *
 * By default, these wireless extensions are disabled, because they
 * need a patch to the Linux Kernel. This simple patch may be found
 * with the driver + some utilities to access those wireless
 * extensions (iwconfig...). Hopefully, those wireless extensions will
 * make their way in the kernel someday.
 *
 * You also will need to enable the CONFIG_NET_RADIO in the kernel
 * configuration to enable the wireless extensions.
 */

/* ---------------------------- FILES ---------------------------- */
/*
 * wavelan.c :		The actual code for the driver - C functions
 *
 * wavelan.p.h :	Private header : local types / vars for the driver
 *
 * wavelan.h :		Description of the hardware interface & structs
 *
 * i82586.h :		Description if the Ethernet controler
 */

/* --------------------------- HISTORY --------------------------- */
/*
 * (Made with information in drivers headers. It may not be accurate,
 * and I garantee nothing except my best effort...)
 *
 * The history of the Wavelan drivers is as complicated as history of
 * the Wavelan itself (NCR -> AT&T -> Lucent).
 *
 * All started with Anders Klemets <klemets@paul.rutgers.edu>,
 * writting a Wavelan ISA driver for the MACH microkernel. Girish
 * Welling <welling@paul.rutgers.edu> had also worked on it.
 * Keith Moore modify this for the Pcmcia hardware.
 * 
 * Robert Morris <rtm@das.harvard.edu> port these two drivers to BSDI
 * and add specific Pcmcia support (there is currently no equivalent
 * of the PCMCIA package under BSD...).
 *
 * Jim Binkley <jrb@cs.pdx.edu> port both BSDI drivers to freeBSD.
 *
 * Bruce Janson <bruce@cs.usyd.edu.au> port the BSDI ISA driver to Linux.
 *
 * Anthony D. Joseph <adj@lcs.mit.edu> started modify Bruce driver
 * (with help of the BSDI PCMCIA driver) for PCMCIA.
 * Yunzhou Li <yunzhou@strat.iol.unh.edu> finished is work.
 * Joe Finney <joe@comp.lancs.ac.uk> patched the driver to start
 * correctly 2.00 cards (2.4 GHz with frequency selection).
 * David Hinds <dhinds@hyper.stanford.edu> integrated the whole in his
 * Pcmcia package (+ bug corrections).
 *
 * I (Jean Tourrilhes - jt@hplb.hpl.hp.com) then started to make some
 * patchs to the Pcmcia driver. After, I added code in the ISA driver
 * for Wireless Extensions and full support of frequency selection
 * cards. Then, I've done the same to the Pcmcia driver + some
 * reorganisation. Finally, I came back to the ISA driver to
 * upgrade it at the same level as the Pcmcia one and reorganise
 * the code
 * Loeke Brederveld <lbrederv@wavelan.com> from Lucent has given me
 * much needed informations on the Wavelan hardware.
 */

/* The original copyrights and litteratures mention others names and
 * credits. I don't know what there part in this development was...
 */

/* By the way : for the copyright & legal stuff :
 * Almost everybody wrote code under GNU or BSD license (or alike),
 * and want that their original copyright remain somewhere in the
 * code (for myself, I go with the GPL).
 * Nobody want to take responsibility for anything, except the fame...
 */

/* --------------------------- CREDITS --------------------------- */
/*
 * This software was developed as a component of the
 * Linux operating system.
 * It is based on other device drivers and information
 * either written or supplied by:
 *	Ajay Bakre (bakre@paul.rutgers.edu),
 *	Donald Becker (becker@cesdis.gsfc.nasa.gov),
 *	Loeke Brederveld (Loeke.Brederveld@Utrecht.NCR.com),
 *	Anders Klemets (klemets@it.kth.se),
 *	Vladimir V. Kolpakov (w@stier.koenig.ru),
 *	Marc Meertens (Marc.Meertens@Utrecht.NCR.com),
 *	Pauline Middelink (middelin@polyware.iaf.nl),
 *	Robert Morris (rtm@das.harvard.edu),
 *	Jean Tourrilhes (jt@hplb.hpl.hp.com),
 *	Girish Welling (welling@paul.rutgers.edu),
 *	Clark Woodworth <clark@hiway1.exit109.com>
 *	Yongguang Zhang <ygz@isl.hrl.hac.com>...
 *
 * Thanks go also to:
 *	James Ashton (jaa101@syseng.anu.edu.au),
 *	Alan Cox (iialan@iiit.swan.ac.uk),
 *	Allan Creighton (allanc@cs.usyd.edu.au),
 *	Matthew Geier (matthew@cs.usyd.edu.au),
 *	Remo di Giovanni (remo@cs.usyd.edu.au),
 *	Eckhard Grah (grah@wrcs1.urz.uni-wuppertal.de),
 *	Vipul Gupta (vgupta@cs.binghamton.edu),
 *	Mark Hagan (mhagan@wtcpost.daytonoh.NCR.COM),
 *	Tim Nicholson (tim@cs.usyd.edu.au),
 *	Ian Parkin (ian@cs.usyd.edu.au),
 *	John Rosenberg (johnr@cs.usyd.edu.au),
 *	George Rossi (george@phm.gov.au),
 *	Arthur Scott (arthur@cs.usyd.edu.au),
 *	Peter Storey,
 * for their assistance and advice.
 *
 * Additional Credits:
 *
 * My developpement has been done under Linux 2.0.x (Debian 1.1) with
 *	an HP Vectra XP/60.
 *
 */

/* ------------------------- IMPROVEMENTS ------------------------- */
/*
 * I proudly present :
 *
 * Changes mades in first pre-release :
 * ----------------------------------
 *	- Reorganisation of the code, function name change
 *	- Creation of private header (wavelan.p.h)
 *	- Reorganised debug messages
 *	- More comments, history, ...
 *	- mmc_init : configure the PSA if not done
 *	- mmc_init : correct default value of level threshold for pcmcia
 *	- mmc_init : 2.00 detection better code for 2.00 init
 *	- better info at startup
 *	- irq setting (note : this setting is permanent...)
 *	- Watchdog : change strategy (+ solve module removal problems)
 *	- add wireless extensions (ioctl & get_wireless_stats)
 *	  get/set nwid/frequency on fly, info for /proc/net/wireless
 *	- More wireless extension : SETSPY and GETSPY
 *	- Make wireless extensions optional
 *	- Private ioctl to set/get quality & level threshold, histogram
 *	- Remove /proc/net/wavelan
 *	- Supress useless stuff from lp (net_local)
 *	- kernel 2.1 support (copy_to/from_user instead of memcpy_to/fromfs)
 *	- Add message level (debug stuff in /var/adm/debug & errors not
 *	  displayed at console and still in /var/adm/messages)
 *	- multi device support
 *	- Start fixing the probe (init code)
 *	- More inlines
 *	- man page
 *	- Lot of others minor details & cleanups
 *
 * Changes made in second pre-release :
 * ----------------------------------
 *	- Cleanup init code (probe & module init)
 *	- Better multi device support (module)
 *	- name assignement (module)
 *
 * Changes made in third pre-release :
 * ---------------------------------
 *	- Be more conservative on timers
 *	- Preliminary support for multicast (I still lack some details...)
 *
 * Changes made in fourth pre-release :
 * ----------------------------------
 *	- multicast (revisited and finished)
 *	- Avoid reset in set_multicast_list (a really big hack)
 *	  if somebody could apply this code for other i82586 based driver...
 *	- Share on board memory 75% RU / 25% CU (instead of 50/50)
 *
 * Changes made for release in 2.1.15 :
 * ----------------------------------
 *	- Change the detection code for multi manufacturer code support
 *
 * Changes made for release in 2.1.17 :
 * ----------------------------------
 *	- Update to wireless extensions changes
 *	- Silly bug in card initial configuration (psa_conf_status)
 *
 * Changes made for release in 2.1.27 :
 * ----------------------------------
 *	- Small bug in debug code (probably not the last one...)
 *	- Remove extern kerword for wavelan_probe()
 *	- Level threshold is now a standard wireless extension (version 4 !)
 *	- modules parameters types (new module interface)
 *
 * Wishes & dreams :
 * ---------------
 *	- Encryption stuff
 *	- Roaming
 */

/***************************** INCLUDES *****************************/

#include	<linux/module.h>

#include	<linux/kernel.h>
#include	<linux/sched.h>
#include	<linux/types.h>
#include	<linux/fcntl.h>
#include	<linux/interrupt.h>
#include	<linux/stat.h>
#include	<linux/ptrace.h>
#include	<linux/ioport.h>
#include	<linux/in.h>
#include	<linux/string.h>
#include	<linux/delay.h>
#include	<asm/system.h>
#include	<asm/bitops.h>
#include	<asm/io.h>
#include	<asm/dma.h>
#include	<asm/uaccess.h>
#include	<linux/errno.h>
#include	<linux/netdevice.h>
#include	<linux/etherdevice.h>
#include	<linux/skbuff.h>
#include	<linux/malloc.h>
#include	<linux/timer.h>

#include <linux/wireless.h>		/* Wireless extensions */

/* Wavelan declarations */
#include	"i82586.h"
#include	"wavelan.h"

/****************************** DEBUG ******************************/

#undef DEBUG_MODULE_TRACE	/* Module insertion/removal */
#undef DEBUG_CALLBACK_TRACE	/* Calls made by Linux */
#undef DEBUG_INTERRUPT_TRACE	/* Calls to handler */
#undef DEBUG_INTERRUPT_INFO	/* type of interrupt & so on */
#define DEBUG_INTERRUPT_ERROR	/* problems */
#undef DEBUG_CONFIG_TRACE	/* Trace the config functions */
#undef DEBUG_CONFIG_INFO	/* What's going on... */
#define DEBUG_CONFIG_ERRORS	/* Errors on configuration */
#undef DEBUG_TX_TRACE		/* Transmission calls */
#undef DEBUG_TX_INFO		/* Header of the transmited packet */
#define DEBUG_TX_ERROR		/* unexpected conditions */
#undef DEBUG_RX_TRACE		/* Transmission calls */
#undef DEBUG_RX_INFO		/* Header of the transmited packet */
#define DEBUG_RX_ERROR		/* unexpected conditions */
#undef DEBUG_PACKET_DUMP	16	/* Dump packet on the screen */
#undef DEBUG_IOCTL_TRACE	/* Misc call by Linux */
#undef DEBUG_IOCTL_INFO		/* Various debug info */
#define DEBUG_IOCTL_ERROR	/* What's going wrong */
#define DEBUG_BASIC_SHOW	/* Show basic startup info */
#undef DEBUG_VERSION_SHOW	/* Print version info */
#undef DEBUG_PSA_SHOW		/* Dump psa to screen */
#undef DEBUG_MMC_SHOW		/* Dump mmc to screen */
#undef DEBUG_SHOW_UNUSED	/* Show also unused fields */
#undef DEBUG_I82586_SHOW	/* Show i82586 status */
#undef DEBUG_DEVICE_SHOW	/* Show device parameters */

/* Options : */
#define USE_PSA_CONFIG		/* Use info from the PSA */
#define IGNORE_NORMAL_XMIT_ERRS	/* Don't bother with normal conditions */
#undef STRUCT_CHECK		/* Verify padding of structures */
#undef PSA_CRC			/* Check CRC in PSA */
#undef OLDIES			/* Old code (to redo) */
#undef RECORD_SNR		/* To redo */
#undef EEPROM_IS_PROTECTED	/* Doesn't seem to be necessary */
#define MULTICAST_AVOID		/* Avoid extra multicast (I'm sceptical) */

#ifdef WIRELESS_EXT	/* If wireless extension exist in the kernel */
/* Warning : these stuff will slow down the driver... */
#define WIRELESS_SPY		/* Enable spying addresses */
#undef HISTOGRAM		/* Enable histogram of sig level... */
#endif

/************************ CONSTANTS & MACROS ************************/

#ifdef DEBUG_VERSION_SHOW
static const char	*version	= "wavelan.c : v15 (wireless extensions) 12/2/97\n";
#endif

/* Watchdog temporisation */
#define	WATCHDOG_JIFFIES	32	/* TODO: express in HZ. */

/* Macro to get the number of elements in an array */
#define	NELS(a)				(sizeof(a) / sizeof(a[0]))

/* ------------------------ PRIVATE IOCTL ------------------------ */

#define SIOCSIPQTHR	SIOCDEVPRIVATE		/* Set quality threshold */
#define SIOCGIPQTHR	SIOCDEVPRIVATE + 1	/* Get quality threshold */
#define SIOCSIPLTHR	SIOCDEVPRIVATE + 2	/* Set level threshold */
#define SIOCGIPLTHR	SIOCDEVPRIVATE + 3	/* Get level threshold */

#define SIOCSIPHISTO	SIOCDEVPRIVATE + 6	/* Set histogram ranges */
#define SIOCGIPHISTO	SIOCDEVPRIVATE + 7	/* Get histogram values */

/****************************** TYPES ******************************/

/* Shortcuts */
typedef struct device		device;
typedef struct net_device_stats	en_stats;
typedef struct iw_statistics	iw_stats;
typedef struct iw_quality	iw_qual;
typedef struct iw_freq		iw_freq;
typedef struct net_local	net_local;
typedef struct timer_list	timer_list;

/* Basic types */
typedef u_char		mac_addr[WAVELAN_ADDR_SIZE];	/* Hardware address */

/*
 * Static specific data for the interface.
 *
 * For each network interface, Linux keep data in two structure. "device"
 * keep the generic data (same format for everybody) and "net_local" keep
 * the additional specific data.
 * Note that some of this specific data is in fact generic (en_stats, for
 * example).
 */
struct net_local
{
  net_local *	next;		/* Linked list of the devices */
  device *	dev;		/* Reverse link... */
  en_stats	stats;		/* Ethernet interface statistics */
  int		nresets;	/* Number of hw resets */
  u_char	reconfig_82586;	/* Need to reconfigure the controler */
  u_char	promiscuous;	/* Promiscuous mode */
  int		mc_count;	/* Number of multicast addresses */
  timer_list	watchdog;	/* To avoid blocking state */
  u_short	hacr;		/* Current host interface state */

  int		tx_n_in_use;
  u_short	rx_head;
  u_short	rx_last;
  u_short	tx_first_free;
  u_short	tx_first_in_use;

#ifdef WIRELESS_EXT
  iw_stats	wstats;		/* Wireless specific stats */
#endif

#ifdef WIRELESS_SPY
  int		spy_number;		/* Number of addresses to spy */
  mac_addr	spy_address[IW_MAX_SPY];	/* The addresses to spy */
  iw_qual	spy_stat[IW_MAX_SPY];		/* Statistics gathered */
#endif	/* WIRELESS_SPY */
#ifdef HISTOGRAM
  int		his_number;		/* Number of intervals */
  u_char	his_range[16];		/* Boundaries of interval ]n-1; n] */
  u_long	his_sum[16];		/* Sum in interval */
#endif	/* HISTOGRAM */
};

/**************************** PROTOTYPES ****************************/

/* ----------------------- MISC SUBROUTINES ------------------------ */
static inline unsigned long	/* flags */
	wv_splhi(void);		/* Disable interrupts */
static inline void
	wv_splx(unsigned long);	/* ReEnable interrupts : flags */
static u_char
	wv_irq_to_psa(int);
static int
	wv_psa_to_irq(u_char);
/* ------------------- HOST ADAPTER SUBROUTINES ------------------- */
static inline u_short		/* data */
	hasr_read(u_short);	/* Read the host interface : base address */
static inline void
	hacr_write(u_short,	/* Write to host interface : base address */
		   u_short),	/* data */
	hacr_write_slow(u_short,
		   u_short),
	set_chan_attn(u_short,	/* ioaddr */
		      u_short),	/* hacr */
	wv_hacr_reset(u_short),	/* ioaddr */
	wv_16_off(u_short,	/* ioaddr */
		  u_short),	/* hacr */
	wv_16_on(u_short,	/* ioaddr */
		 u_short),	/* hacr */
	wv_ints_off(device *),
	wv_ints_on(device *);
/* ----------------- MODEM MANAGEMENT SUBROUTINES ----------------- */
static void
	psa_read(u_short,	/* Read the Parameter Storage Area */
		 u_short,	/* hacr */
		 int,		/* offset in PSA */
		 u_char *,	/* buffer to fill */
		 int),		/* size to read */
	psa_write(u_short, 	/* Write to the PSA */
		  u_short,	/* hacr */
		  int,		/* Offset in psa */
		  u_char *,	/* Buffer in memory */
		  int);		/* Length of buffer */
static inline void
	mmc_out(u_short,	/* Write 1 byte to the Modem Manag Control */
		u_short,
		u_char),
	mmc_write(u_short,	/* Write n bytes to the MMC */
		  u_char,
		  u_char *,
		  int);
static inline u_char		/* Read 1 byte from the MMC */
	mmc_in(u_short,
	       u_short);
static inline void
	mmc_read(u_short,	/* Read n bytes from the MMC */
		 u_char,
		 u_char *,
		 int),
	fee_wait(u_short,	/* Wait for frequency EEprom : base address */
		 int,		/* Base delay to wait for */
		 int);		/* Number of time to wait */
static void
	fee_read(u_short,	/* Read the frequency EEprom : base address */
		 u_short,	/* destination offset */
		 u_short *,	/* data buffer */
		 int),		/* number of registers */
	fee_write(u_short,	/* Write to frequency EEprom : base address */
		  u_short,	/* destination offset */
		  u_short *,	/* data buffer */
		  int);		/* number of registers */
/* ---------------------- I82586 SUBROUTINES ----------------------- */
static /*inline*/ void
	obram_read(u_short,	/* ioaddr */
		   u_short,	/* o */
		   u_char *,	/* b */
		   int);	/* n */
static inline void
	obram_write(u_short,	/* ioaddr */
		    u_short,	/* o */
		    u_char *,	/* b */
		    int);	/* n */
static void
	wv_ack(device *);
static inline int
	wv_synchronous_cmd(device *,
			   const char *),
	wv_config_complete(device *,
			   u_short,
			   net_local *);
static int
	wv_complete(device *,
		    u_short,
		    net_local *);
static inline void
	wv_82586_reconfig(device *);
/* ------------------- DEBUG & INFO SUBROUTINES ------------------- */
#ifdef DEBUG_I82586_SHOW
static void
	wv_scb_show(unsigned short);
#endif
static inline void
	wv_init_info(device *);	/* display startup info */
/* ------------------- IOCTL, STATS & RECONFIG ------------------- */
static en_stats	*
	wavelan_get_stats(device *);	/* Give stats /proc/net/dev */
static void
	wavelan_set_multicast_list(device *);
/* ----------------------- PACKET RECEPTION ----------------------- */
static inline void
	wv_packet_read(device *,	/* Read a packet from a frame */
		       u_short,
		       int),
	wv_receive(device *);	/* Read all packets waiting */
/* --------------------- PACKET TRANSMISSION --------------------- */
static inline void
	wv_packet_write(device *,	/* Write a packet to the Tx buffer */
			void *,
			short);
static int
	wavelan_packet_xmit(struct sk_buff *,	/* Send a packet */
			    device *);
/* -------------------- HARDWARE CONFIGURATION -------------------- */
static inline int
	wv_mmc_init(device *),		/* Initialize the modem */
	wv_ru_start(device *),		/* Start the i82586 receiver unit */
	wv_cu_start(device *),		/* Start the i82586 command unit */
	wv_82586_start(device *);	/* Start the i82586 */
static void
	wv_82586_config(device *);	/* Configure the i82586 */
static inline void
	wv_82586_stop(device *);
static int
	wv_hw_reset(device *),		/* Reset the wavelan hardware */
	wv_check_ioaddr(u_short,	/* ioaddr */
			u_char *);	/* mac address (read) */
/* ---------------------- INTERRUPT HANDLING ---------------------- */
static void
	wavelan_interrupt(int,		/* Interrupt handler */
			  void *,
			  struct pt_regs *);
static void
	wavelan_watchdog(u_long);	/* Transmission watchdog */
/* ------------------- CONFIGURATION CALLBACKS ------------------- */
static int
	wavelan_open(device *),		/* Open the device */
	wavelan_close(device *),	/* Close the device */
	wavelan_config(device *);	/* Configure one device */
extern int
	wavelan_probe(device *);	/* See Space.c */

/**************************** VARIABLES ****************************/

/*
 * This is the root of the linked list of wavelan drivers
 * It is use to verify that we don't reuse the same base address
 * for two differents drivers and to make the cleanup when
 * removing the module.
 */
static net_local *	wavelan_list	= (net_local *) NULL;

/*
 * This table is used to translate the psa value to irq number
 * and vice versa...
 */
static u_char	irqvals[]	=
{
	   0,    0,    0, 0x01,
	0x02, 0x04,    0, 0x08,
	   0,    0, 0x10, 0x20,
	0x40,    0,    0, 0x80,
};

/*
 * Table of the available i/o address (base address) for wavelan
 */
static unsigned short	iobase[]	=
{
#if	0
  /* Leave out 0x3C0 for now -- seems to clash with some video
   * controllers.
   * Leave out the others too -- we will always use 0x390 and leave
   * 0x300 for the Ethernet device.
   * Jean II : 0x3E0 is really fine as well...
   */
  0x300, 0x390, 0x3E0, 0x3C0
#endif	/* 0 */
  0x390, 0x3E0
};

#ifdef	MODULE
/* Parameters set by insmod */
static int	io[4]	= { 0, 0, 0, 0 };
static int	irq[4]	= { 0, 0, 0, 0 };
static char	name[4][IFNAMSIZ] = { "", "", "", "" };
MODULE_PARM(io, "1-4i");
MODULE_PARM(irq, "1-4i");
MODULE_PARM(name, "1-4c" __MODULE_STRING(IFNAMSIZ));
#endif	/* MODULE */

#endif	/* WAVELAN_P_H */
