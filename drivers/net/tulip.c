/* tulip.c: A DEC 21040 ethernet driver for linux. */
/*
   NOTICE: this version works with kernels 1.1.82 and later only!
	Written 1994,1995 by Donald Becker.

	This software may be used and distributed according to the terms
	of the GNU Public License, incorporated herein by reference.

	This driver is for the SMC EtherPower PCI ethernet adapter.
	It should work with most other DEC 21*40-based ethercards.

	The author may be reached as becker@CESDIS.gsfc.nasa.gov, or C/O
	Center of Excellence in Space Data and Information Sciences
	   Code 930.5, Goddard Space Flight Center, Greenbelt MD 20771
*/

static char *version =
"tulip.c:v0.10 8/11/95 becker@cesdis.gsfc.nasa.gov\n"
"        +0.72 4/17/96 "
"http://www.dsl.tutics.tut.ac.jp/~linux/tulip\n";

/* A few user-configurable values. */

/* Default to using 10baseT (i.e. AUI/10base2/100baseT port) port. */
#define	TULIP_10TP_PORT		0
#define	TULIP_100TP_PORT	1
#define	TULIP_AUI_PORT		1
#define	TULIP_BNC_PORT		2
#define	TULIP_MAX_PORT		3
#define	TULIP_AUTO_PORT		-1

#ifndef	TULIP_PORT
#define	TULIP_PORT			TULIP_10TP_PORT
#endif

/* Define to force full-duplex operation on all Tulip interfaces. */
/* #define  TULIP_FULL_DUPLEX 1 */

/* Define to fix port. */
/* #define  TULIP_FIX_PORT 1 */

/* Define to probe only first detected device */
/*#define	TULIP_MAX_CARDS 1*/

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <linux/delay.h>
#include <asm/byteorder.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

/* The total size is unusually large: The 21040 aligns each of its 16
   longword-wide registers on a quadword boundary. */
#define TULIP_TOTAL_SIZE 0x80

/*
				Theory of Operation

I. Board Compatibility

This device driver is designed for the DECchip 21040 "Tulip", Digital's
single-chip ethernet controller for PCI, as used on the SMC EtherPower
ethernet adapter.  It also works with boards based the 21041 (new/experimental)
and 21140 (10/100mbps).


II. Board-specific settings

PCI bus devices are configured by the system at boot time, so no jumpers
need to be set on the board.  The system BIOS should be set to assign the
PCI INTA signal to an otherwise unused system IRQ line.  While it's
physically possible to shared PCI interrupt lines, the kernel doesn't
support it. 

III. Driver operation

IIIa. Ring buffers
The Tulip can use either ring buffers or lists of Tx and Rx descriptors.
The current driver uses a statically allocated Rx ring of descriptors and
buffers, and a list of the Tx buffers.

IIIC. Synchronization
The driver runs as two independent, single-threaded flows of control.  One
is the send-packet routine, which enforces single-threaded use by the
dev->tbusy flag.  The other thread is the interrupt handler, which is single
threaded by the hardware and other software.

The send packet thread has partial control over the Tx ring and 'dev->tbusy'
flag.  It sets the tbusy flag whenever it's queuing a Tx packet. If the next
queue slot is empty, it clears the tbusy flag when finished otherwise it sets
the 'tp->tx_full' flag.

The interrupt handler has exclusive control over the Rx ring and records stats
from the Tx ring.  (The Tx-done interrupt can't be selectively turned off, so
we can't avoid the interrupt overhead by having the Tx routine reap the Tx
stats.)	 After reaping the stats, it marks the queue entry as empty by setting
the 'base' to zero.	 Iff the 'tp->tx_full' flag is set, it clears both the
tx_full and tbusy flags.

IV. Notes

Thanks to Duke Kamstra of SMC for providing an EtherPower board.

The DEC databook doesn't document which Rx filter settings accept broadcast
packets.  Nor does it document how to configure the part to configure the
serial subsystem for normal (vs. loopback) operation or how to have it
autoswitch between internal 10baseT, SIA and AUI transceivers.

The databook claims that CSR13, CSR14, and CSR15 should each be the last
register of the set CSR12-15 written.   Hmmm, now how is that possible?
*/

/* A few values that may be tweaked. */
/* Keep the ring sizes a power of two for efficiency. */
#define TX_RING_SIZE	4
#define RX_RING_SIZE	4
#define PKT_BUF_SZ		1536			/* Size of each temporary Rx buffer.*/

/* This is a mysterious value that can be written to CSR11 in the 21040
   to detect a full-duplex frame.  No one knows what it should be, but if
   left at its default value some 10base2(!) packets trigger a
   full-duplex-request interrupt. */
#define FULL_DUPLEX_MAGIC	0x6969

/* The rest of these values should never change. */

#define PCI_DEVICE_ID_NONE	0xFFFF
#define	ETHNAMSIZ	8
#define	ROUND_UP(size, n)	((size + n - 1) & ~(n - 1))

/* Offsets to the Command and Status Registers, "CSRs".  All accesses
   must be longword instructions and quadword aligned. */
enum tulip_offsets {
				/* 21040		21041		21140		*/
	CSR0=0,		/* BUS mode								*/
    CSR1=0x08,	/* TX poll demand						*/
	CSR2=0x10,	/* RX poll demand						*/
	CSR3=0x18,	/* RX ring base addr					*/
	CSR4=0x20,	/* TX ring base addr					*/
	CSR5=0x28,	/* Status								*/
	CSR6=0x30,	/* Command mode							*/
	CSR7=0x38,	/* Interrupt Mask						*/
	CSR8=0x40,	/* Missed frame counter					*/
	CSR9=0x48,	/* Eth.addrROM	SROM mii	SROM mii	*/
	CSR10=0x50,	/* Diagn.		boot ROM	-			*/
	CSR11=0x58,	/* Full duplex	G.P. timer	G.P. timer	*/
	CSR12=0x60,	/* SIA status				G.P.		*/
	CSR13=0x68,	/* SIA connectivity			-			*/
	CSR14=0x70,	/* SIA TX/RX				-			*/
	CSR15=0x78	/* SIA general				watchdog	*/
};

/* description of CSR0 bus mode register */
#define	TBMOD_RESERVED		0xfff80000	/* I don't know */
#define	TBMOD_RESET			0x00000001
#define	TBMOD_BIGENDIAN		0x00000080
/*
	   Cache alignment bits 15:14	     Burst length 13:8
    	0000	No alignment  0x00000000 unlimited		0800 8 longwords
		4000	8  longwords		0100 1 longword		1000 16 longwords
		8000	16 longwords		0200 2 longwords	2000 32 longwords
		C000	32  longwords		0400 4 longwords
*/
#define	TBMOD_ALIGN0		0x00000000	/* no cache alignment */
#define	TBMOD_ALIGN8		0x00004000	/* 8 longwords */
#define	TBMOD_ALIGN16		0x00008000
#define	TBMOD_ALIGN32		(TBMOD_ALIGN8|TBMOD_ALIGN16)
#define	TBMOD_BURST0		0x00000000	/* unlimited=rx buffer size */
#define	TBMOD_BURST1		0x00000100	/* 1 longwords */
#define	TBMOD_BURST2		0x00000200
#define	TBMOD_BURST4		0x00000400
#define	TBMOD_BURST8		0x00000800
#define	TBMOD_BURST16		0x00001000
#define	TBMOD_BURST32		0x00002000

/* description of CSR1 Tx poll demand register */
/* description of CSR2 Rx poll demand register */
#define	TPOLL_START			0x00000001	/* ? */
#define	TPOLL_TRIGGER		0x00000000	/* ? */

/* description of CSR5 status register from de4x5.h */
#define	TSTAT_BUSERROR		0x03800000
#define	TSTAT_SYSERROR		0x00002000
#define	TSTAT_TxSTAT		0x00700000
#define	TSTAT_RxSTAT		0x000e0000
#define	TSTAT_LKFAIL		0x00001000
#define	TSTAT_NORINTR		0x00010000	/* Normal interrupt */
#define	TSTAT_ABNINTR		0x00008000	/* Abnormal interrupt */
#define	TSTAT_RxMISSED		0x00000100	/* Rx frame missed */
#define	TSTAT_RxUNABL		0x00000080
#define	TSTAT_RxINTR		0x00000040
#define	TSTAT_LKPASS		0x00000010
#define	TSTAT_TEXPIRED		0x00000800	/* Timer Expired */
#define	TSTAT_TxTOUT		0x00000008
#define	TSTAT_TxUNABL		0x00000004
#define	TSTAT_TxINTR		0x00000001
#define	TSTAT_CLEARINTR		0x0001ffff	/* clear all interrupt sources */

/* description of CSR6 command mode register */
#define	TCMOD_SCRM			0x01000000	/* scrambler mode */
#define	TCMOD_PCS			0x00800000	/* PCS function */
#define	TCMOD_TxTHMODE		0x00400000	/* Tx threshold mode */
#define	TCMOD_SW100TP		0x00040000  /* 21140: 100MB */
#define	TCMOD_CAPTURE		0x00020000	/* capture effect */
#define	TCMOD_FULLDUPLEX	0x00000200
#define TCMOD_TH128			0x00008000	/* 10 - 128 bytes threshold */
#define	TCMOD_TxSTART		0x00002000
#define	TCMOD_RxSTART		0x00000002
#define	TCMOD_ALLMCAST		0x00000080	/* pass all multicast */
#define	TCMOD_PROMISC		0x00000040	/* promisc */
#define	TCMOD_BOFFCOUNTER	0x00000020	/* backoff counter */
#define	TCMOD_INVFILTER		0x00000010	/* invert filtering */
#define	TCMOD_HONLYFILTER	0x00000004	/* hash only filtering */
#define TCMOD_HPFILTER		0x00000001	/* hash/perfect Rx filtering */
#define	TCMOD_MODEMASK		(TCMOD_ALLMCAST|TCMOD_PROMISC)
#define	TCMOD_FILTERMASK	(TCMOD_HONLYFILTER|TCMOD_HPFILTER|TCMOD_INVFILTER)
#define	TCMOD_TRxSTART		(TCMOD_TxSTART|TCMOD_RxSTART)
#define	TCMOD_BASE			(TCMOD_CAPTURE|TCMOD_BOFFCOUNTER)
#define	TCMOD_10TP			(TCMOD_TxTHMODE|TCMOD_BASE)
#define	TCMOD_100TP			(TCMOD_SCRM|TCMOD_PCS|TCMOD_SW100TP|TCMOD_BASE)
#define	TCMOD_AUTO			(TCMOD_SW100TP|TCMOD_TH128|TCMOD_10TP)

/* description of CSR7 interrupt mask register */
#define	TINTR_ENABLE		0xFFFFFFFF
#define	TINTR_DISABLE		0x00000000

/* description of CSR11 G.P. timer (21041/21140) register */
#define	TGEPT_COUNT			0x0001FFFF

/* description of CSR12 SIA status(2104x)/GP(21140) register */
#define	TSIAS_CONERROR		0x00000002	/* connection error */
#define	TSIAS_LNKERROR		0x00000004	/* link error */
#define TSIAS_ACTERROR		0x00000200  /* port Rx activity */
#define TSIAS_RxACTIVE		0x00000100  /* port Rx activity */

#define	TGEPR_LK10NG		0x00000080	/* 10Mbps N.G. (R) */
#define	TGEPR_LK100NG		0x00000040	/* 100Mbps N.G. (R) */
#define	TGEPR_DETECT		0x00000020	/* detect signal (R) */
#define	TGEPR_HALFDUPLEX	0x00000008	/* half duplex (W) */
#define	TGEPR_PHYLOOPBACK	0x00000004	/* PHY loopback (W) */
#define	TGEPR_FORCEALED		0x00000002	/* force activity LED on (W) */
#define	TGEPR_FORCE100		0x00000001	/* force 100Mbps mode */

/* description of CSR13 SIA connectivity register */
#define	TSIAC_OUTEN			0x0000e000	/* 21041: Output enable */
#define TSIAC_SELED			0x00000f00	/* 21041: AUI or TP with LEDs */
#define	TSIAC_INEN			0x00001000	/* 21041: Input enable */
#define	TSIAC_NO10TP		0x00000008	/* 10baseT(0) or not(1) */
#define TSIAC_CONFIG		0x00000004	/* Configuration */
#define	TSIAC_SWRESET		0x00000001	/* 21041: software reset */
#define	TSIAC_RESET			0x00000000	/* reset */
#define	TSIAC_C21041		(TSIAC_OUTEN|TSIAC_SELED|TSIAC_SWRESET)
#define	TSIAC_C21040		TSIAC_CONFIG

/* description of CSR14 SIA TX/RX register */
#define	TSIAX_NO10TP		0x0000f73d
#define	TSIAX_10TP			0x0000ff3f

/* description of CSR15 SIA general register */
#define	TSIAG_SWBNCAUI		0x00000008 /* BNC(0) or AUI(1) */
#define	TSIAG_BNC			0x00000006
#define	TSIAG_AUI			(TSIAG_BNC|TSIAG_SWBNCAUI)
#define	TSIAG_10TP			0x00000000

/* description of rx_ring.status */
#define	TRING_OWN			0x80000000	/* Owned by chip */
#define	TRING_CLEAR			0x00000000	/* clear */
#define	TRING_ERROR			0x00008000	/* error summary */
#define	TRING_ETxTO			0x00004000	/* Tx time out */
#define	TRING_ELCOLL		0x00000200	/* late collision */
#define	TRING_EFCOLL		0x00000100	/* fatal collision */
#define	TRING_ELCARR		0x00000800	/* carrier lost */
#define	TRING_ENCARR		0x00000400	/* no carrier */
#define	TRING_ENOHB			0x00000080	/* heartbeat fail */
#define	TRING_ELINK			0x00000004	/* link fail */
#define	TRING_EUFLOW		0x00000002	/* underflow */

#define	TRING_ELEN			0x00004000	/* length error */
#define	TRING_FDESC			0x00000200	/* first descriptor */
#define	TRING_LDESC			0x00000100	/* last descriptor */
#define	TRING_ERUNT			0x00000800	/* runt frame */
#define	TRING_ELONG			0x00000080	/* frame too long */
#define	TRING_EWATCHDOG		0x00000010	/* receive watchdog */
#define	TRING_EDRBIT		0x00000004	/* dribble bit */
#define	TRING_ECRC			0x00000002	/* CRC error */
#define	TRING_EOVERFLOW		0x00000001	/* overflow */

#define	TRING_RxDESCMASK	(TRING_FDESC|TRING_LDESC)
#define	TRING_RxLENGTH		(TRING_ERUNT|TRING_ELONG|TRING_EWATCHDOG)
#define	TRING_RxFRAME		(TRING_EDRBIT)
#define	TRING_RxCRC			(TRING_ECRC)
#define	TRING_RxFIFO		(TRING_EOVERFLOW)
#define	TRING_TxABORT		(TRING_ETxTO|TRING_EFCOLL|TRING_ELINK)
#define	TRING_TxCARR		(TRING_ELCARR|TRING_ENCARR)
#define	TRING_TxWINDOW		(TRING_ELCOLL)
#define	TRING_TxFIFO		(TRING_EUFLOW)
#define	TRING_TxHEARTBEAT	(TRING_ENOHB)
/* The Tulip Rx and Tx buffer descriptors. */
struct tulip_rx_desc {
	s32 status;
	s32 length;
	u32 buffer1, buffer2;			/* We use only buffer 1.  */
};

struct tulip_tx_desc {
	s32 status;
	s32 length;
	u32 buffer1, buffer2;			/* We use only buffer 1.  */
};

struct tulip_private {
	struct tulip_rx_desc rx_ring[RX_RING_SIZE];
	struct tulip_tx_desc tx_ring[TX_RING_SIZE];
	/* The saved address of a sent-in-place packet/buffer, for skfree(). */
	struct sk_buff* tx_skbuff[TX_RING_SIZE];
	char rx_buffs[RX_RING_SIZE][PKT_BUF_SZ];
	/* temporary Rx buffers. */
	struct enet_statistics stats;
	int setup_frame[48];	/* Pseudo-Tx frame to init address table. */
	void (*port_select)(struct device *dev);
	int (*port_fail)(struct device *dev);
	char *signature;
	unsigned int cur_rx, cur_tx;		/* The next free ring entry */
	unsigned int dirty_rx, dirty_tx;	/* The ring entries to be free()ed. */
	unsigned int tx_full:1;				/* The Tx queue is full. */
	unsigned int full_duplex:1;			/* Full-duplex operation requested. */
	unsigned int port_fix:1;			/* Fix if_port to specified port. */
};

struct eeprom {
    union {
		struct { /* broken EEPROM structure */
			u_char addr[ETH_ALEN];
		} ng;
		struct { /* DEC EtherWorks
					and other cards which have correct eeprom structure */
			u_char dum1[20];
			u_char addr[ETH_ALEN];
		} ok;
    } hw;
#define	ng_addr	hw.ng.addr
#define	ok_addr	hw.ok.addr
#define	EE_SIGNLEN	14		/* should be 102 ? */
	u_char sign[EE_SIGNLEN];
};

static int read_eeprom(int ioaddr, struct eeprom *eepp);
static int tulip_open(struct device *dev);
static void tulip_init_ring(struct device *dev);
static int tulip_start_xmit(struct sk_buff *skb, struct device *dev);
static int tulip_rx(struct device *dev);
static void tulip_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int tulip_close(struct device *dev);
static struct enet_statistics *tulip_get_stats(struct device *dev);
static struct device *tulip_alloc(struct device *dev);
static void set_multicast_list(struct device *dev);

#define	generic21140_fail	NULL
static void generic21040_select(struct device *dev);
static void generic21140_select(struct device *dev);
static void generic21041_select(struct device *dev);
static void auto21140_select(struct device *dev);
static void cogent21140_select(struct device *dev);
static int generic21040_fail(struct device *dev);
static int generic21041_fail(struct device *dev);

static struct {
	void (*port_select)(struct device *dev);
	int (*port_fail)(struct device *dev);
	unsigned int vendor_id, device_id;
	char *signature;
	unsigned int array:1;
} cardVendor[] = {
	{generic21140_select, generic21140_fail,
		 0x0000c000, PCI_DEVICE_ID_DEC_TULIP_FAST, "smc9332", 0},
	{generic21041_select, generic21041_fail,
		 0x0000c000, PCI_DEVICE_ID_DEC_TULIP_PLUS, "smc8432", 0},
	{generic21040_select, generic21040_fail,
		 0x0000c000, PCI_DEVICE_ID_DEC_TULIP, "old smc8432", 0},
	{auto21140_select, generic21140_fail,
		 0x0000f400, PCI_DEVICE_ID_DEC_TULIP_FAST, "LA100PCI", 0},
	{cogent21140_select, generic21140_fail,
		 0x00009200, PCI_DEVICE_ID_DEC_TULIP_FAST, "cogent_em110", 0},
	{generic21140_select, generic21140_fail,
		 0x0000f800, PCI_DEVICE_ID_DEC_TULIP_FAST, "DE500", 0},
	{generic21041_select, generic21041_fail,
		 0x0000f800, PCI_DEVICE_ID_DEC_TULIP_PLUS, "DE450", 0},
	{generic21040_select, generic21040_fail,
		 0x0000f800, PCI_DEVICE_ID_DEC_TULIP, "DE43x", 0},
	{generic21040_select, generic21040_fail,
		 0x0040c700, PCI_DEVICE_ID_DEC_TULIP, "EN9400", 0},
	{generic21040_select, generic21040_fail,
		 0x00c09500, PCI_DEVICE_ID_DEC_TULIP, "ZNYX312", 1},
	{generic21040_select, generic21040_fail,
		 0x08002b00, PCI_DEVICE_ID_DEC_TULIP, "QSILVER's", 0},
	{generic21040_select, generic21040_fail,
		 0, PCI_DEVICE_ID_DEC_TULIP, "21040", 0},
	{generic21140_select, generic21140_fail,
		 0, PCI_DEVICE_ID_DEC_TULIP_FAST, "21140", 0},
	{generic21041_select, generic21041_fail,
		 0, PCI_DEVICE_ID_DEC_TULIP_PLUS, "21041", 0},
	{NULL, NULL, 0, 0, "Unknown", 0}
};


/* Serial EEPROM section.
   A "bit" grungy, but we work our way through bit-by-bit :->. */
/*  EEPROM_Ctrl bits. */
#define EE_SHIFT_CLK	0x02	/* EEPROM shift clock. */
#define EE_CS			0x01	/* EEPROM chip select. */
#define EE_DATA_WRITE	0x04	/* EEPROM chip data in. */
#define EE_WRITE_0		0x01
#define EE_WRITE_1		0x05
#define EE_DATA_READ	0x08	/* EEPROM chip data out. */
#define EE_ENB			(0x4800 | EE_CS)

/* The EEPROM commands include the alway-set leading bit. */
#define EE_WRITE_CMD	(5 << 6)
#define EE_READ_CMD		(6 << 6)
#define EE_ERASE_CMD	(7 << 6)

#ifdef MODULE
static int if_port=TULIP_AUTO_PORT;
static size_t alloc_size;
#ifdef TULIP_FULL_DUPLEX
static int full_duplex=1;
#else
static int full_duplex=0;
#endif
#endif

#define	tio_write(val, port)	outl(val, ioaddr + port)
#define	tio_read(port)			inl(ioaddr + port)

static void inline
tio_sia_write(u32 ioaddr, u32 val13, u32 val14, u32 val15)
{
	tio_write(0,CSR13);
	tio_write(val15,CSR15);
	tio_write(val14,CSR14);
	tio_write(val13,CSR13);
}

/*
   card_type returns 1 if the card is 'etherarray'
*/

static int
card_type(struct tulip_private *tp, int device_id, int vendor_id)
{
	int n;

	for (n = 0; cardVendor[n].device_id; n ++)
		if (cardVendor[n].device_id == device_id
			&& (cardVendor[n].vendor_id == vendor_id
				|| cardVendor[n].vendor_id == 0)) break;
	tp->port_select = cardVendor[n].port_select;
	tp->port_fail = cardVendor[n].port_fail;
	tp->signature = cardVendor[n].signature;
	return(cardVendor[n].array ? 1: 0);
}

static int
read_eeprom(int ioaddr, struct eeprom *eepp)
{
    int i, n;
    unsigned short val = 0;
    int read_cmd = EE_READ_CMD;
    u_char *p=(u_char *)eepp;

	for (n = 0; n < sizeof(struct eeprom) / 2; n ++, read_cmd ++) {
		tio_write(EE_ENB & ~EE_CS, CSR9);
		tio_write(EE_ENB, CSR9);

		/* Shift the read command bits out. */
		for (i = 10; i >= 0; i--) {
			short dataval = (read_cmd & (1 << i)) ? EE_DATA_WRITE : 0;
			tio_write(EE_ENB | dataval, CSR9);
			udelay(100);
			tio_write(EE_ENB | dataval | EE_SHIFT_CLK, CSR9);
			udelay(150);
			tio_write(EE_ENB | dataval, CSR9);
			udelay(250);
		}
		tio_write(EE_ENB, CSR9);

		for (i = 16; i > 0; i--) {
			tio_write(EE_ENB | EE_SHIFT_CLK, CSR9);
			udelay(100);
			val = (val << 1)
				| ((tio_read(CSR9) & EE_DATA_READ) ? 1 : 0);
			tio_write(EE_ENB, CSR9);
			udelay(100);
		}

		/* Terminate the EEPROM access. */
		tio_write(EE_ENB & ~EE_CS, CSR9);
		*p ++ = val;
		*p ++ = val >> 8;
    }
	/* broken eeprom ? */
	p = (u_char *)eepp;
	for (i = 0; i < 8; i ++)
		if (p[i] != p[15 - i] || p[i] != p[16 + i]) return(0);
	return(-1); /* broken */
}

/* Is this required ? */
static int
generic21040_fail(struct device *dev)
{
	int ioaddr = dev->base_addr;

	return(tio_read(CSR12) & TSIAS_CONERROR);
}

static int
generic21041_fail(struct device *dev)
{
	int ioaddr = dev->base_addr;
	u32 csr12 = tio_read(CSR12);

	return((!(csr12 & TSIAS_CONERROR)
			|| !(csr12 & TSIAS_LNKERROR)) ? 0: 1);
}

static void
generic21040_select(struct device *dev)
{
	int ioaddr = dev->base_addr;
	const char *media;

	dev->if_port &= 3;
	switch (dev->if_port)
	{
	case TULIP_10TP_PORT:
		media = "10baseT";
		break;
	case TULIP_AUI_PORT:
		media = "AUI";
		break;
	case TULIP_BNC_PORT:
		media = "BNC";
		break;
	default:
		media = "unknown type";
		break;
	}
	printk("%s: enabling %s port.\n", dev->name, media);
	/* Set the full duplex match frame. */
	tio_write(FULL_DUPLEX_MAGIC, CSR11);
	tio_write(TSIAC_RESET, CSR13);
	/* Reset the serial interface */
	tio_write((dev->if_port ? TSIAC_NO10TP: 0) | TSIAC_C21040, CSR13);
}

#if 0
static void
generic_timer(struct device *dev, u32 count)
{
	int ioaddr = dev->base_addr;

	tio_write(count, CSR11);
	while (tio_read(CSR11) & TGEPT_COUNT);
}
#endif

static void
generic21041_select(struct device *dev)
{
	int ioaddr = dev->base_addr;
	u32 tsiac = TSIAC_C21041;
	u32 tsiax = TSIAX_10TP;
	u32 tsiag = TSIAG_10TP;

	switch(dev->if_port) {
	case TULIP_AUI_PORT:
		tsiac |= TSIAC_NO10TP;
		tsiax = TSIAX_NO10TP;
		tsiag = TSIAG_AUI;
		break;
	case TULIP_BNC_PORT:
		tsiac |= TSIAC_NO10TP;
		tsiax = TSIAX_NO10TP;
		tsiag = TSIAG_BNC;
		break;
	default:
		dev->if_port = TULIP_10TP_PORT;
		break;
	}
	tio_sia_write(ioaddr, tsiac, tsiax, tsiag);
	if (dev->start)
		printk("%s: enabling %s port.\n", dev->name,
			   (dev->if_port == TULIP_AUI_PORT) ? "AUI":
			   (dev->if_port == TULIP_BNC_PORT) ? "BNC": "10TP");
}

static void
auto21140_select(struct device *dev)
{
	int i, ioaddr = dev->base_addr;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;

	/* kick port */
	tio_write(TPOLL_TRIGGER, CSR1);
	tio_write(TINTR_ENABLE, CSR7);
	tio_write(TCMOD_AUTO|TCMOD_TRxSTART, CSR6);
	dev->if_port = !(tio_read(CSR12) & TGEPR_FORCEALED);
	printk("%s: probed %s port.\n",
		   dev->name, dev->if_port ? "100TX" : "10TP");
	tio_write((dev->if_port ? TGEPR_FORCE100: 0)
			  | (tp->full_duplex ? 0:TGEPR_HALFDUPLEX), CSR12);	
	tio_write(TINTR_DISABLE, CSR7);
	i = tio_read(CSR8) & 0xffff;
	tio_write(TCMOD_AUTO, CSR6);
}

static void
cogent21140_select(struct device *dev)
{
	int ioaddr = dev->base_addr, csr6;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	dev->if_port &= 1;
	csr6 = tio_read(CSR6) &
		~(TCMOD_10TP|TCMOD_100TP|TCMOD_TRxSTART|TCMOD_SCRM);
	/* Stop the transmit process. */
	tio_write(csr6 | TCMOD_RxSTART, CSR6);
	printk("%s: enabling %s port.\n",
		   dev->name, dev->if_port ? "100baseTx" : "10baseT");
	/* Turn on the output drivers */
	tio_write(0x0000013F, CSR12);
	tio_write((dev->if_port ? TGEPR_FORCE100: 0)
			  | (tp->full_duplex ? 0:TGEPR_HALFDUPLEX), CSR12);
	tio_write((dev->if_port ? TCMOD_100TP: TCMOD_10TP)
			  | TCMOD_TRxSTART | TCMOD_TH128 | csr6, CSR6);
}

static void
generic21140_select(struct device *dev)
{
	int ioaddr = dev->base_addr, csr6;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;

	dev->if_port &= 1;
	csr6 = tio_read(CSR6) &
		~(TCMOD_10TP|TCMOD_100TP|TCMOD_TRxSTART|TCMOD_SCRM);

	/* Stop the transmit process. */
	tio_write(csr6 | TCMOD_RxSTART, CSR6);
	if (dev->start)
		printk("%s: enabling %s port.\n",
			   dev->name, dev->if_port ? "100TX" : "10TP");
	tio_write((dev->if_port ? TCMOD_100TP: TCMOD_10TP)
			  | TCMOD_TRxSTART | TCMOD_TH128 | csr6, CSR6);
	tio_write((dev->if_port ? TGEPR_FORCE100: 0)
			  | (tp->full_duplex ? 0:TGEPR_HALFDUPLEX), CSR12);
}

static int
tulip_open(struct device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int ioaddr = dev->base_addr;

	/* Reset the chip, holding bit 0 set at least 10 PCI cycles. */
	tio_write(tio_read(CSR0)|TBMOD_RESET, CSR0);
	udelay(1000);
	/* Deassert reset.  Set 8 longword cache alignment, 8 longword burst.
	   -> Set 32 longword cache alignment, unlimited longword burst ?
	   Wait the specified 50 PCI cycles after a reset by initializing
	   Tx and Rx queues and the address filter list. */
	tio_write(tio_read(CSR0)|TBMOD_ALIGN32|TBMOD_BURST0, CSR0);

	if (request_irq(dev->irq, (void *)&tulip_interrupt, SA_SHIRQ,
					tp->signature, dev))
		return -EAGAIN;

	tulip_init_ring(dev);

	/* Fill the whole address filter table with our physical address. */
	{ 
		unsigned short *eaddrs = (unsigned short *)dev->dev_addr;
		int *setup_frm = tp->setup_frame, i;

		/* You must add the broadcast address when doing perfect filtering! */
		*setup_frm++ = 0xffff;
		*setup_frm++ = 0xffff;
		*setup_frm++ = 0xffff;
		/* Fill the rest of the accept table with our physical address. */
		for (i = 1; i < 16; i++) {
			*setup_frm++ = eaddrs[0];
			*setup_frm++ = eaddrs[1];
			*setup_frm++ = eaddrs[2];
		}
		/* Put the setup frame on the Tx list. */
		tp->tx_ring[0].length = 0x08000000 | 192;
		tp->tx_ring[0].buffer1 = virt_to_bus(tp->setup_frame);
		tp->tx_ring[0].buffer2 = 0;
		tp->tx_ring[0].status = TRING_OWN;

		tp->cur_tx++, tp->dirty_tx++;
	}

	tio_write(virt_to_bus(tp->rx_ring), CSR3);
	tio_write(virt_to_bus(tp->tx_ring), CSR4);

	dev->tbusy = 0;
	dev->interrupt = 0;
	dev->start = 1;

	if (tp->port_select) tp->port_select(dev);

	/* Start the chip's Tx and Rx processes. */
	tio_write(tio_read(CSR6) | TCMOD_TRxSTART
			  | (tp->full_duplex ? TCMOD_FULLDUPLEX:0), CSR6);

	/* Trigger an immediate transmit demand to process the setup frame. */
	tio_write(TPOLL_TRIGGER, CSR1);

	/* Enable interrupts by setting the interrupt mask. */
	tio_write(TINTR_ENABLE, CSR7);

	MOD_INC_USE_COUNT;
	return 0;
}

/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void
tulip_init_ring(struct device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int i;

	tp->tx_full = 0;
	tp->cur_rx = tp->cur_tx = 0;
	tp->dirty_rx = tp->dirty_tx = 0;

	for (i = 0; i < RX_RING_SIZE; i++) {
		tp->rx_ring[i].status = TRING_OWN;
		tp->rx_ring[i].length = PKT_BUF_SZ;
		tp->rx_ring[i].buffer1 = virt_to_bus(tp->rx_buffs[i]);
		tp->rx_ring[i].buffer2 = virt_to_bus(&tp->rx_ring[i+1]);
	}
	/* Mark the last entry as wrapping the ring. */ 
	tp->rx_ring[i-1].length = PKT_BUF_SZ | 0x02000000;
	tp->rx_ring[i-1].buffer2 = virt_to_bus(&tp->rx_ring[0]);

	/* The Tx buffer descriptor is filled in as needed, but we
	   do need to clear the ownership bit. */
	for (i = 0; i < TX_RING_SIZE; i++) {
		tp->tx_ring[i].status = 0x00000000;
	}
}

static int
tulip_start_xmit(struct sk_buff *skb, struct device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int ioaddr = dev->base_addr;
	int entry;

	/* Transmitter timeout, serious problems. */
	if (dev->tbusy || (tp->port_fail && tp->port_fail(dev))) {
		int tickssofar = jiffies - dev->trans_start;
		int i;
		if (tickssofar < 40) return(1);
		if (tp->port_select) {
			if (!tp->port_fix) dev->if_port ++;
			tp->port_select(dev);
			dev->trans_start = jiffies;
			return(0);
		}
		printk("%s: transmit timed out, status %8.8x,"
			   "SIA %8.8x %8.8x %8.8x %8.8x, resetting...\n",
			   dev->name, tio_read(CSR5), tio_read(CSR12),
			   tio_read(CSR13), tio_read(CSR14), tio_read(CSR15));
#ifndef	__alpha__
		printk("  Rx ring %8.8x: ", (int)tp->rx_ring);
#endif
		for (i = 0; i < RX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)tp->rx_ring[i].status);
#ifndef	__alpha__
		printk("\n  Tx ring %8.8x: ", (int)tp->tx_ring);
#endif
		for (i = 0; i < TX_RING_SIZE; i++)
			printk(" %8.8x", (unsigned int)tp->tx_ring[i].status);
		printk("\n");

		tp->stats.tx_errors++;
		/* Perhaps we should reinitialize the hardware here. */
		dev->if_port = 0;
		tio_write(TSIAC_CONFIG, CSR13);
		/* Start the chip's Tx and Rx processes . */
		tio_write(TCMOD_10TP | TCMOD_TRxSTART, CSR6);
		/* Trigger an immediate transmit demand. */
		tio_write(TPOLL_TRIGGER, CSR1);

		dev->tbusy=0;
		dev->trans_start = jiffies;
		return(0);
	}

	if (skb == NULL || skb->len <= 0) {
		printk("%s: Obsolete driver layer request made: skbuff==NULL.\n",
			   dev->name);
		dev_tint(dev);
		return(0);
	}

	/* Block a timer-based transmit from overlapping.  This could better be
	   done with atomic_swap(1, dev->tbusy), but set_bit() works as well.
	   If this ever occurs the queue layer is doing something evil! */
	if (set_bit(0, (void*)&dev->tbusy) != 0) {
		printk("%s: Transmitter access conflict.\n", dev->name);
		return 1;
	}

	/* Caution: the write order is important here, set the base address
	   with the "ownership" bits last. */

	/* Calculate the next Tx descriptor entry. */
	entry = tp->cur_tx % TX_RING_SIZE;

	tp->tx_full = 1;
	tp->tx_skbuff[entry] = skb;
	tp->tx_ring[entry].length = skb->len |
		(entry == TX_RING_SIZE-1 ? 0xe2000000 : 0xe0000000);
	tp->tx_ring[entry].buffer1 = virt_to_bus(skb->data);
	tp->tx_ring[entry].buffer2 = 0;
	tp->tx_ring[entry].status = TRING_OWN;	/* Pass ownership to the chip. */

	tp->cur_tx++;

	/* Trigger an immediate transmit demand. */
	tio_write(TPOLL_TRIGGER, CSR1);

	dev->trans_start = jiffies;

	return(0);
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void tulip_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	struct device *dev = (struct device *)dev_id;
	struct tulip_private *lp;
	int csr5, ioaddr, boguscnt=10;

	if (dev == NULL) {
		printk ("tulip_interrupt(): irq %d for unknown device.\n", irq);
		return;
	}

	ioaddr = dev->base_addr;
	lp = (struct tulip_private *)dev->priv;
	if (dev->interrupt)
		printk("%s: Re-entering the interrupt handler.\n", dev->name);

	dev->interrupt = 1;

	do {
		csr5 = tio_read(CSR5);
		/* Acknowledge all of the current interrupt sources ASAP. */
		tio_write(csr5 & TSTAT_CLEARINTR, CSR5);
		/* check interrupt ? */
		if ((csr5 & (TSTAT_NORINTR|TSTAT_ABNINTR)) == 0) break;

		if (csr5 & TSTAT_RxINTR)			/* Rx interrupt */
			tulip_rx(dev);

		if (csr5 & TSTAT_TxINTR) {		/* Tx-done interrupt */
			int dirty_tx = lp->dirty_tx;

			while (dirty_tx < lp->cur_tx) {
				int entry = dirty_tx % TX_RING_SIZE;
				int status = lp->tx_ring[entry].status;

				if (status < 0)
					break;			/* It still hasn't been Txed */

				if (status & TRING_ERROR) {
					/* There was an major error, log it. */
					lp->stats.tx_errors++;
					if (status & TRING_TxABORT) lp->stats.tx_aborted_errors++;
					if (status & TRING_TxCARR) lp->stats.tx_carrier_errors++;
					if (status & TRING_TxWINDOW) lp->stats.tx_window_errors++;
					if (status & TRING_TxFIFO) lp->stats.tx_fifo_errors++;
					if ((status & TRING_TxHEARTBEAT) && !lp->full_duplex)
						lp->stats.tx_heartbeat_errors++;
#ifdef ETHER_STATS
					if (status & 0x0100) lp->stats.collisions16++;
#endif
				} else {
#ifdef ETHER_STATS
					if (status & 0x0001) lp->stats.tx_deferred++;
#endif
					lp->stats.collisions += (status >> 3) & 15;
					lp->stats.tx_packets++;
				}

				/* Free the original skb. */
				dev_kfree_skb(lp->tx_skbuff[entry], FREE_WRITE);
				dirty_tx++;
			}

#ifndef final_version
			if (lp->cur_tx - dirty_tx >= TX_RING_SIZE) {
				printk("out-of-sync dirty pointer, %d vs. %d, full=%d.\n",
					   dirty_tx, lp->cur_tx, lp->tx_full);
				dirty_tx += TX_RING_SIZE;
			}
#endif

			if (lp->tx_full && dev->tbusy
				&& dirty_tx > lp->cur_tx - TX_RING_SIZE + 2) {
				/* The ring is no longer full, clear tbusy. */
				lp->tx_full = 0;
				dev->tbusy = 0;
				mark_bh(NET_BH);
			}

			lp->dirty_tx = dirty_tx;
		}

		/* Log errors. */
		if (csr5 & TSTAT_ABNINTR) {	/* Abnormal error summary bit. */
			if (csr5 & TSTAT_TxTOUT) lp->stats.tx_errors++; /* Tx babble. */
			if (csr5 & TSTAT_RxMISSED) { 		/* Missed a Rx frame. */
				lp->stats.rx_errors++;
				lp->stats.rx_missed_errors += tio_read(CSR8) & 0xffff;
			}
			if (csr5 & TSTAT_TEXPIRED) {
				printk("%s: Something Wicked happened! %8.8x.\n",
					   dev->name, csr5);
				/* Hmmmmm, it's not clear what to do here. */
			}
		}
		if (--boguscnt < 0) {
			printk("%s: Too much work at interrupt, csr5=0x%8.8x.\n",
				   dev->name, csr5);
			/* Clear all interrupt sources. */
			tio_write(TSTAT_CLEARINTR, CSR5);
			break;
		}
	} while (1);

	/* Special code for testing *only*. */
	{
		static int stopit = 10;
		if (dev->start == 0  &&  --stopit < 0) {
			printk("%s: Emergency stop, looping startup interrupt.\n",
				   dev->name);
			free_irq(irq, dev);
		}
	}

	dev->interrupt = 0;
	return;
}

static int
tulip_rx(struct device *dev)
{
	struct tulip_private *lp = (struct tulip_private *)dev->priv;
	int entry = lp->cur_rx % RX_RING_SIZE;
	int i;

	/* If we own the next entry, it's a new packet. Send it up. */
	while (lp->rx_ring[entry].status >= 0) {
		int status = lp->rx_ring[entry].status;

		if ((status & TRING_RxDESCMASK) != TRING_RxDESCMASK) {
			printk("%s: Ethernet frame spanned multiple buffers,"
				   "status %8.8x!\n", dev->name, status);
		} else if (status & TRING_ERROR) {
			/* There was a fatal error. */
			lp->stats.rx_errors++; /* end of a packet.*/
			if (status & TRING_RxLENGTH) lp->stats.rx_length_errors++;
			if (status & TRING_RxFRAME) lp->stats.rx_frame_errors++;
			if (status & TRING_RxCRC) lp->stats.rx_crc_errors++;
			if (status & TRING_RxFIFO) lp->stats.rx_fifo_errors++;
		} else {
			/* Malloc up new buffer, compatible with net-2e. */
			short pkt_len = lp->rx_ring[entry].status >> 16;
			struct sk_buff *skb;

			skb = dev_alloc_skb(pkt_len + 2);
			if (skb == NULL) {
				printk("%s: Memory squeeze, deferring packet.\n",
					   dev->name);
				/* Check that at least two ring entries are free.
				   If not, free one and mark stats->rx_dropped++. */
				for (i=0; i < RX_RING_SIZE; i++)
					if (lp->rx_ring[(entry+i) % RX_RING_SIZE].status < 0)
						break;

				if (i > RX_RING_SIZE -2) {
					lp->stats.rx_dropped++;
					lp->rx_ring[entry].status = TRING_OWN;
					lp->cur_rx++;
				}
				break;
			}
			skb->dev = dev;
			skb_reserve(skb, 2);
			memcpy(skb_put(skb, pkt_len),
				   bus_to_virt(lp->rx_ring[entry].buffer1), pkt_len);
			/* Needed for 1.3.x */
			skb->protocol = eth_type_trans(skb,dev);
			netif_rx(skb);
			lp->stats.rx_packets++;
		}

		lp->rx_ring[entry].status = TRING_OWN;
		entry = (++lp->cur_rx) % RX_RING_SIZE;
	}
	return(0);
}

static int
tulip_close(struct device *dev)
{
	int ioaddr = dev->base_addr;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;

	dev->start = 0;
	dev->tbusy = 1;

	/* Disable interrupts by clearing the interrupt mask. */
	tio_write(TINTR_DISABLE, CSR7);
	/* Stop the chip's Tx and Rx processes. */
	tio_write(tio_read(CSR6) & ~(TCMOD_TRxSTART), CSR6);
	/* Leave the card in 10baseT state. */
	tio_write(TSIAC_CONFIG, CSR13);

	tp->stats.rx_missed_errors += tio_read(CSR8) & 0xffff;

	tio_write(0, CSR13);
/*	tio_write(0, CSR8);	wake up chip ? */

	free_irq(dev->irq, dev);

	MOD_DEC_USE_COUNT;
	return(0);
}

static int
tulip_config(struct device *dev, struct ifmap *map)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;

	if (map->port == 0xff) return(-EINVAL);
	dev->if_port = map->port;
	tp->port_fix = 1;
	if (tp->port_select) tp->port_select(dev);
	return(0);
}

static struct enet_statistics *
tulip_get_stats(struct device *dev)
{
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	/*	short ioaddr = dev->base_addr;*/

	return(&tp->stats);
}

/*
 *	Set or clear the multicast filter for this adaptor.
 */

static void set_multicast_list(struct device *dev)
{
	short ioaddr = dev->base_addr;
	int csr6 = tio_read(CSR6) & ~(TCMOD_MODEMASK|TCMOD_FILTERMASK);

	if (dev->flags&IFF_PROMISC) 
	{			/* Set promiscuous. why ALLMULTI ? */
		tio_write(csr6 | TCMOD_PROMISC | TCMOD_ALLMCAST, CSR6);
		/* Log any net taps. */
		printk("%s: Promiscuous mode enabled.\n", dev->name);
	}
	else if (dev->mc_count > 15 || (dev->flags&IFF_ALLMULTI)) 
	{
		/* Too many to filter perfectly -- accept all multicasts. */
		tio_write(csr6 | TCMOD_ALLMCAST, CSR6);
	}
	else
	{
		struct tulip_private *tp = (struct tulip_private *)dev->priv;
		struct dev_mc_list *dmi=dev->mc_list;
		int *setup_frm = tp->setup_frame;
		unsigned short *eaddrs;
		int i;

		/* We have <= 15 addresses that we can use the wonderful
		   16 address perfect filtering of the Tulip.  Note that only
		   the low shortword of setup_frame[] is valid. */
		tio_write(csr6 | 0x0000, CSR6);
		for (i = 0; i < dev->mc_count; i ++) {
			eaddrs=(unsigned short *)dmi->dmi_addr;
			dmi=dmi->next;
			*setup_frm++ = *eaddrs++;
			*setup_frm++ = *eaddrs++;
			*setup_frm++ = *eaddrs++;
		}
		/* Fill the rest of the table with our physical address. */
		eaddrs = (unsigned short *)dev->dev_addr;
		do {
			*setup_frm++ = eaddrs[0];
			*setup_frm++ = eaddrs[1];
			*setup_frm++ = eaddrs[2];
		} while (++i < 16);

		/* Now add this frame to the Tx list. */
	}
}

static struct device *tulip_alloc(struct device *dev)
{
	struct tulip_private *tp;
	char *buff;
#ifndef	MODULE
	size_t alloc_size;
#endif
	if (!dev || dev->priv) {
		struct device *olddev = dev;

		alloc_size = sizeof(struct device)
			+ sizeof(struct tulip_private)
			+ ETHNAMSIZ;
		alloc_size = ROUND_UP(alloc_size, 8);

		buff = (char *)kmalloc(alloc_size, GFP_KERNEL);
		dev = (struct device *)buff;
		if (dev == NULL) {
			printk("tulip_alloc: kmalloc failed.\n");
			return(NULL);
		}
		tp = (struct tulip_private *)(buff + sizeof(struct device));
		memset(buff, 0, alloc_size);
		dev->priv = (void *)tp;
		dev->name = (char *)(buff + sizeof(struct device)
							 + sizeof(struct tulip_private));
		if (olddev) {
			dev->next = olddev->next;
			olddev->next = dev;
		}
	} else {
		alloc_size = ROUND_UP(sizeof(struct tulip_private), 8);
		tp = (struct tulip_private *)kmalloc(alloc_size, GFP_KERNEL);
		memset((void *)tp, 0, alloc_size);
		dev->priv = (void *)tp;
	}
	return(dev);
}

int
tulip_hwinit(struct device *dev, int ioaddr,
			 int irq, int device_id)
{
	/* See note below on the Znyx 315 etherarray. */
	static unsigned char last_phys_addr[6] = {0x00, 'L', 'i', 'n', 'u', 'x'};
	char detect_mesg[80], *mesgp=detect_mesg;
	struct tulip_private *tp = (struct tulip_private *)dev->priv;
	int i;
	unsigned short sum, bitsum;

	if (check_region(ioaddr, TULIP_TOTAL_SIZE) != 0) {
		printk("tulip_hwinit: region already allocated at %#3x.\n",
			   ioaddr);
		return(-1);
	}

	mesgp += sprintf(mesgp, "(DEC 21%d4%d Tulip",
					 device_id == PCI_DEVICE_ID_DEC_TULIP_FAST,
					 device_id == PCI_DEVICE_ID_DEC_TULIP_PLUS);

	/* Stop the chip's Tx and Rx processes. */
	tio_write(tio_read(CSR6) & ~TCMOD_TRxSTART, CSR6);
	/* Clear the missed-packet counter. */
	i = tio_read(CSR8) & 0xffff;

	if (device_id == PCI_DEVICE_ID_DEC_TULIP_PLUS
	    && (tio_read(CSR9) & 0x8000)) {
		mesgp += sprintf(mesgp, " treat as 21040");
	    device_id = PCI_DEVICE_ID_DEC_TULIP;
	}
	
	/* The station address ROM is read byte serially.  The register must
	   be polled, waiting for the value to be read bit serially from the
	   EEPROM.
	   */
	sum = 0;
	if (device_id == PCI_DEVICE_ID_DEC_TULIP) {
		tio_write(0, CSR9);
	    /* Reset the pointer with a dummy write. */
	    bitsum = 0xff;
	    for (i = 0; i < 6; i++) {
			int value, boguscnt = 100000;
			do
				value = tio_read(CSR9);
			while (value < 0  && --boguscnt > 0);
			dev->dev_addr[i] = value;
			sum += value & 0xFF;
			bitsum &= value;
	    }
	} else {
	    /* Must be a 21140/21041, with a serial EEPROM interface. */
	    struct eeprom eep;
	    u_char *addr;

	    if (read_eeprom(ioaddr, &eep) < 0) {
			addr = eep.ng_addr;/* broken EEPROM structure */
	    } else {
			addr = eep.ok_addr;/* DEC EtherWorks */
	    }
	    for (i = 0; i < ETH_ALEN; i++) {
			sum += addr[i];
			dev->dev_addr[i] = addr[i];
	    }
	}
	/* Make certain the data structures are quadword aligned. */

	mesgp += sprintf(mesgp, ") at %#3x, ", ioaddr);

	/* On the Zynx 315 etherarray boards only the first Tulip has an EEPROM.
	   The addresses of the subsequent ports are derived from the first. */
	if (sum == 0) {
		for (i = 0; i < ETH_ALEN - 1; i++)
			dev->dev_addr[i] = last_phys_addr[i];
		dev->dev_addr[i] = last_phys_addr[i] + 1;
	}
	for (i = 0; i < ETH_ALEN - 1; i++)
		mesgp += sprintf(mesgp, "%2.2x:", dev->dev_addr[i]);
	mesgp += sprintf(mesgp, "%2.2x, IRQ %d\n",
					 last_phys_addr[i] = dev->dev_addr[i], irq);

	/* copy ethernet address */
	if (card_type(tp, device_id,
				  htonl((*(int*)dev->dev_addr) & 0xFFFFFF)))
		for (i = 0; i < ETH_ALEN - 1; i++)
			last_phys_addr[i] = dev->dev_addr[i];
	/* We do a request_region() only to register /proc/ioports info. */
	request_region(ioaddr, TULIP_TOTAL_SIZE, tp->signature);

	dev->base_addr = ioaddr;
	dev->irq = irq;

	/* The Tulip-specific entries in the device structure. */
	dev->open = &tulip_open;
	dev->hard_start_xmit = &tulip_start_xmit;
	dev->stop = &tulip_close;
	dev->get_stats = &tulip_get_stats;
	dev->set_config = &tulip_config;
	dev->set_multicast_list = &set_multicast_list;

#ifdef	MODULE
    ether_setup(dev);
	if (if_port == TULIP_AUTO_PORT)
		if_port = TULIP_PORT;
	else
		tp->port_fix = 1;
	dev->if_port = if_port;
	tp->full_duplex = full_duplex;
#else
#ifdef TULIP_FULL_DUPLEX
	tp->full_duplex = 1;
#endif
    init_etherdev(dev, 0);
	dev->if_port = TULIP_PORT;
#endif

#ifdef	TULIP_FIX_PORT
	tp->port_fix = 1;
#endif

	printk("%s: %s %s", dev->name, tp->signature, detect_mesg);

	/* Reset the xcvr interface and turn on heartbeat. */
	tio_write(TSIAC_RESET, CSR13);
	tio_write(TSIAC_CONFIG, CSR13);

	return(0);
}

int tulip_probe(struct device *dev)
{
	static struct device *tulip_head=NULL;
	u_char pci_bus, pci_device_fn, pci_latency, pci_irq;
	u_int pci_ioaddr;
	u_short pci_command;
	u_int pci_chips[] = {
		PCI_DEVICE_ID_DEC_TULIP,
		PCI_DEVICE_ID_DEC_TULIP_FAST,
		PCI_DEVICE_ID_DEC_TULIP_PLUS,
		PCI_DEVICE_ID_NONE
	};
	int num=0, cno;
	int pci_index;

    if (!pcibios_present()) return(-ENODEV);

	for (pci_index = 0; pci_index < 8; pci_index++) {
		/* Search for the PCI_DEVICE_ID_DEV_TULIP* chips */
		for (cno = 0; pci_chips[cno] != PCI_DEVICE_ID_NONE; cno ++)
			if (pcibios_find_device(PCI_VENDOR_ID_DEC,
									pci_chips[cno],
									pci_index, &pci_bus,
									&pci_device_fn) == 0) {
				struct device *dp;

				/* get IO address */
				pcibios_read_config_dword(pci_bus, pci_device_fn,
										  PCI_BASE_ADDRESS_0,
										  &pci_ioaddr);
				/* Remove I/O space marker in bit 0. */
				pci_ioaddr &= ~3;
				for (dp = tulip_head; dp != NULL; dp = dp->next)
					if (dp->base_addr == pci_ioaddr) break;
				if (dp) continue;
				/* get IRQ */
				pcibios_read_config_byte(pci_bus, pci_device_fn,
										 PCI_INTERRUPT_LINE, &pci_irq);
#ifdef	MODULE
				/* compare requested IRQ/IO address */
				if (dev && dev->base_addr &&
					dev->base_addr != pci_ioaddr) continue;
#else
				if ((dev = tulip_alloc(dev)) == NULL) break;
#endif
				if (!tulip_head) {
					printk(version);
					tulip_head = dev;
				}

				/* Get and check the bus-master and latency values. */
				pcibios_read_config_word(pci_bus, pci_device_fn,
										 PCI_COMMAND, &pci_command);
				if ( ! (pci_command & PCI_COMMAND_MASTER)) {
					printk("  PCI Master Bit has not been set!"
						   " Setting...\n");
					pci_command |= PCI_COMMAND_MASTER;
					pcibios_write_config_word(pci_bus, pci_device_fn,
											  PCI_COMMAND, pci_command);
				}
				pcibios_read_config_byte(pci_bus, pci_device_fn,
										 PCI_LATENCY_TIMER,
										 &pci_latency);
				if (pci_latency < 10) {
					printk("  PCI latency timer (CFLT) is"
						   " unreasonably low at %d."
						   "  Setting to 100 clocks.\n", pci_latency);
					pcibios_write_config_byte(pci_bus, pci_device_fn,
											  PCI_LATENCY_TIMER, 100);
				}
				if (tulip_hwinit(dev, pci_ioaddr, pci_irq,
								 pci_chips[cno]) < 0) continue;
				num ++;
#ifdef	MODULE
				return(0);
#endif
#ifdef	TULIP_MAX_CARDS
				if (num >= TULIP_MAX_CARDS) return(0);
#endif
		}
	}
	return(num > 0 ? 0: -ENODEV);
}

#ifdef MODULE
#ifdef __alpha__
#if 1
static int io = 0xb000;
#else
static int io = 0x10400;
#endif
#else
static int io = 0xfc80;
#endif

static struct device *mod_dev;

int init_module(void)
{
	if ((mod_dev = tulip_alloc(0)) == NULL) return(-EIO);

	mod_dev->base_addr = io;
	mod_dev->irq = 0;
	mod_dev->init = &tulip_probe;

	if (register_netdev(mod_dev)) {
		printk("tulip: register_netdev() returned non-zero.\n");
		kfree_s(mod_dev, alloc_size);
		return -EIO;
	}
	return(0);
}

void
cleanup_module(void)
{
	release_region(mod_dev->base_addr, TULIP_TOTAL_SIZE);
	unregister_netdev(mod_dev);
	kfree_s(mod_dev, alloc_size);
}

#endif /* MODULE */


/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/usr/src/linux/net/inet -Wall -Wstrict-prototypes -O6 -m486 -c tulip.c"
 *  c-indent-level: 4
 *  tab-width: 4
 * End:
 */
