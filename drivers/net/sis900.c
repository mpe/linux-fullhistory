/*****************************************************************************/
/*      sis900.c: A SiS 900 PCI Fast Ethernet driver for Linux.              */
/*                                                                           */
/*                Silicon Integrated System Corporation                      */ 
/*                Revision:	1.05	Aug 7 1999                           */
/*                                                                           */
/*****************************************************************************/

/*                                                                            
      Modified from the driver which is originally written by Donald Becker. 

      This software may be used and distributed according to the terms
      of the GNU Public License (GPL), incorporated herein by reference.
      Drivers based on this skeleton fall under the GPL and must retain
      the authorship (implicit copyright) notice.

      The author may be reached as becker@tidalwave.net, or
      Donald Becker
      312 Severn Ave. #W302
      Annapolis MD 21403

      Support and updates [to the original skeleton] available at
      http://www.tidalwave.net/~becker/pci-skeleton.html
*/

static const char *version =
"sis900.c:v1.05  8/07/99\n";

static int max_interrupt_work = 20;
#define sis900_debug debug
static int sis900_debug = 0;

static int multicast_filter_limit = 128;

#define MAX_UNITS 8             /* More are supported, limit only on options */
static int speeds[MAX_UNITS] = {100, 100, 100, 100, 100, 100, 100, 100};
static int full_duplex[MAX_UNITS] = {1, 1, 1, 1, 1, 1, 1, 1};

#define TX_BUF_SIZE     1536
#define RX_BUF_SIZE     1536

#define TX_DMA_BURST    0
#define RX_DMA_BURST    0
#define TX_FIFO_THRESH  16
#define TxDRNT_100      (1536>>5)
#define TxDRNT_10       16 
#define RxDRNT_100      8
#define RxDRNT_10       8 
#define TRUE            1
#define FALSE           0

/* Operational parameters that usually are not changed. */
/* Time in jiffies before concluding the transmitter is hung. */
#define TX_TIMEOUT  (4*HZ)

#include <linux/module.h>
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/timer.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <asm/processor.h>      /* Processor type for cache alignment. */
#include <asm/bitops.h>
#include <asm/io.h>

#define RUN_AT(x) (jiffies + (x))

#include <linux/delay.h>

#if LINUX_VERSION_CODE < 0x20123
#define test_and_set_bit(val, addr) set_bit(val, addr)
#endif
#if LINUX_VERSION_CODE <= 0x20139
#define net_device_stats enet_statistics
#else
#define NETSTATS_VER2
#endif
#if LINUX_VERSION_CODE < 0x20155  ||  defined(CARDBUS)
/* Grrrr, the PCI code changed, but did not consider CardBus... */
#include <linux/bios32.h>
#define PCI_SUPPORT_VER1
#else
#define PCI_SUPPORT_VER2
#endif
#if LINUX_VERSION_CODE < 0x20159
#define dev_free_skb(skb) dev_kfree_skb(skb, FREE_WRITE);
#else
#define dev_free_skb(skb) dev_kfree_skb(skb);
#endif

/* The I/O extent. */
#define SIS900_TOTAL_SIZE 0x100

/* This table drives the PCI probe routines.  It's mostly boilerplate in all
   of the drivers, and will likely be provided by some future kernel.
   Note the matching code -- the first table entry matchs all 56** cards but
   second only the 1234 card.
*/

enum pci_flags_bit {
        PCI_USES_IO=1, PCI_USES_MEM=2, PCI_USES_MASTER=4,
};

struct pci_id_info {
        const char *name;
        u16     vendor_id, device_id, device_id_mask, flags;
        int io_size;
        struct net_device *(*probe1)(int pci_bus, int pci_devfn, struct net_device *dev,
                         long ioaddr, int irq, int chip_idx, int fnd_cnt);
};

static struct net_device * sis900_probe1(int pci_bus, int pci_devfn,
                                  struct net_device *dev, long ioaddr,
                                  int irq, int chp_idx, int fnd_cnt);

static struct pci_id_info pci_tbl[] =
{{ "SiS 900 PCI Fast Ethernet",
   0x1039, 0x0900, 0xffff, PCI_USES_IO|PCI_USES_MASTER, 0x100, sis900_probe1},
  { "SiS 7016 PCI Fast Ethernet",
   0x1039, 0x7016, 0xffff, PCI_USES_IO|PCI_USES_MASTER, 0x100, sis900_probe1},
 {0,},                                          /* 0 terminated list. */
};

/* The capability table matches the chip table above. */
enum {HAS_MII_XCVR=0x01, HAS_CHIP_XCVR=0x02, HAS_LNK_CHNG=0x04};
static int sis_cap_tbl[] = {
        HAS_MII_XCVR|HAS_CHIP_XCVR|HAS_LNK_CHNG,
        HAS_MII_XCVR|HAS_CHIP_XCVR|HAS_LNK_CHNG,
};

/* The rest of these values should never change. */
#define NUM_TX_DESC     16      /* Number of Tx descriptor registers. */
#define NUM_RX_DESC     8       /* Number of Rx descriptor registers. */

/* Symbolic offsets to registers. */
enum SIS900_registers {
        cr=0x0,                 //Command Register
        cfg=0x4,                //Configuration Register
        mear=0x8,               //EEPROM Access Register
        ptscr=0xc,              //PCI Test Control Register
        isr=0x10,               //Interrupt Status Register
        imr=0x14,               //Interrupt Mask Register
        ier=0x18,               //Interrupt Enable Register
        epar=0x18,              //Enhanced PHY Access Register
        txdp=0x20,              //Transmit Descriptor Pointer Register
        txcfg=0x24,             //Transmit Configuration Register
        rxdp=0x30,              //Receive Descriptor Pointer Register
        rxcfg=0x34,             //Receive Configuration Register
        flctrl=0x38,            //Flow Control Register
        rxlen=0x3c,             //Receive Packet Length Register
        rfcr=0x48,              //Receive Filter Control Register
        rfdr=0x4C,              //Receive Filter Data Register
        pmctrl=0xB0,            //Power Management Control Register
        pmer=0xB4               //Power Management Wake-up Event Register
};

#define RESET           0x00000100
#define SWI             0x00000080
#define RxRESET         0x00000020
#define TxRESET         0x00000010
#define RxDIS           0x00000008
#define RxENA           0x00000004
#define TxDIS           0x00000002
#define TxENA           0x00000001

#define BISE            0x80000000
#define EUPHCOM         0x00000100
#define REQALG          0x00000080
#define SB              0x00000040
#define POW             0x00000020
#define EXD             0x00000010
#define PESEL           0x00000008
#define LPM             0x00000004
#define BEM             0x00000001

/* Interrupt register bits, using my own meaningful names. */
#define WKEVT           0x10000000
#define TxPAUSEEND      0x08000000
#define TxPAUSE         0x04000000
#define TxRCMP          0x02000000
#define RxRCMP          0x01000000
#define DPERR           0x00800000
#define SSERR           0x00400000
#define RMABT           0x00200000
#define RTABT           0x00100000
#define RxSOVR          0x00010000
#define HIBERR          0x00008000
#define SWINT           0x00001000
#define MIBINT          0x00000800
#define TxURN           0x00000400
#define TxIDLE          0x00000200
#define TxERR           0x00000100
#define TxDESC          0x00000080
#define TxOK            0x00000040
#define RxORN           0x00000020
#define RxIDLE          0x00000010
#define RxEARLY         0x00000008
#define RxERR           0x00000004
#define RxDESC          0x00000002
#define RxOK            0x00000001

#define IE              0x00000001

#define TxCSI           0x80000000
#define TxHBI           0x40000000
#define TxMLB           0x20000000
#define TxATP           0x10000000
#define TxIFG           0x0C000000
#define TxMXF           0x03800000
#define TxMXF_shift     0x23
#define TxMXDMA         0x00700000
#define TxMXDMA_shift   20
#define TxRTCNT         0x000F0000
#define TxRTCNT_shift   16
#define TxFILLT         0x00007F00
#define TxFILLT_shift   8
#define TxDRNT          0x0000007F

#define RxAEP           0x80000000
#define RxARP           0x40000000
#define RxATP           0x10000000
#define RxAJAB          0x08000000
#define RxMXF           0x03800000
#define RxMXF_shift     23
#define RxMXDMA         0x00700000
#define RxMXDMA_shift   20
#define RxDRNT          0x0000007F

#define RFEN            0x80000000
#define RFAAB           0x40000000
#define RFAAM           0x20000000
#define RFAAP           0x10000000
#define RFPromiscuous   (RFAAB|RFAAM|RFAAP)
#define RFAA_shift      28
#define RFEP            0x00070000
#define RFEP_shift      16

#define RFDAT           0x0000FFFF

#define OWN             0x80000000
#define MORE            0x40000000
#define INTR            0x20000000
#define OK              0x08000000
#define DSIZE           0x00000FFF

#define SUPCRC          0x10000000
#define ABORT           0x04000000
#define UNDERRUN        0x02000000
#define NOCARRIER       0x01000000
#define DEFERD          0x00800000
#define EXCDEFER        0x00400000
#define OWCOLL          0x00200000
#define EXCCOLL         0x00100000
#define COLCNT          0x000F0000

#define INCCRC          0x10000000
//      ABORT           0x04000000
#define OVERRUN         0x02000000
#define DEST            0x01800000
#define BCAST           0x01800000
#define MCAST           0x01000000
#define UNIMATCH        0x00800000
#define TOOLONG         0x00400000
#define RUNT            0x00200000
#define RXISERR         0x00100000
#define CRCERR          0x00080000
#define FAERR           0x00040000
#define LOOPBK          0x00020000
#define RXCOL           0x00010000

#define EuphLiteEEMACAddr               0x08
#define EuphLiteEEVendorID              0x02
#define EuphLiteEEDeviceID              0x03
#define EuphLiteEECardTypeRev           0x0b
#define EuphLiteEEPlexusRev             0x0c
#define EuphLiteEEChecksum              0x0f

#define RXSTS_shift     18
#define OWN             0x80000000
#define MORE            0x40000000
#define INTR            0x20000000
#define OK              0x08000000
#define DSIZE           0x00000FFF
/* MII register offsets */
#define MII_CONTROL             0x0000
#define MII_STATUS              0x0001
#define MII_PHY_ID0             0x0002
#define MII_PHY_ID1             0x0003
#define MII_ANAR                0x0004
#define MII_ANLPAR              0x0005
#define MII_ANER                0x0006
/* MII Control register bit definitions. */
#define MIICNTL_FDX             0x0100
#define MIICNTL_RST_AUTO        0x0200
#define MIICNTL_ISOLATE         0x0400
#define MIICNTL_PWRDWN          0x0800
#define MIICNTL_AUTO            0x1000
#define MIICNTL_SPEED           0x2000
#define MIICNTL_LPBK            0x4000
#define MIICNTL_RESET           0x8000
/* MII Status register bit significance. */
#define MIISTAT_EXT             0x0001
#define MIISTAT_JAB             0x0002
#define MIISTAT_LINK            0x0004
#define MIISTAT_CAN_AUTO        0x0008
#define MIISTAT_FAULT           0x0010
#define MIISTAT_AUTO_DONE       0x0020
#define MIISTAT_CAN_T           0x0800
#define MIISTAT_CAN_T_FDX       0x1000
#define MIISTAT_CAN_TX          0x2000
#define MIISTAT_CAN_TX_FDX      0x4000
#define MIISTAT_CAN_T4          0x8000
/* MII NWAY Register Bits ...
** valid for the ANAR (Auto-Negotiation Advertisement) and
** ANLPAR (Auto-Negotiation Link Partner) registers */
#define MII_NWAY_NODE_SEL       0x001f
#define MII_NWAY_CSMA_CD        0x0001
#define MII_NWAY_T              0x0020
#define MII_NWAY_T_FDX          0x0040
#define MII_NWAY_TX             0x0080
#define MII_NWAY_TX_FDX         0x0100
#define MII_NWAY_T4             0x0200
#define MII_NWAY_RF             0x2000
#define MII_NWAY_ACK            0x4000
#define MII_NWAY_NP             0x8000

/* MII Auto-Negotiation Expansion Register Bits */
#define MII_ANER_PDF            0x0010
#define MII_ANER_LP_NP_ABLE     0x0008
#define MII_ANER_NP_ABLE        0x0004
#define MII_ANER_RX_PAGE        0x0002
#define MII_ANER_LP_AN_ABLE     0x0001
#define HALF_DUPLEX                     1
#define FDX_CAPABLE_DUPLEX_UNKNOWN      2
#define FDX_CAPABLE_HALF_SELECTED       3
#define FDX_CAPABLE_FULL_SELECTED       4
#define HW_SPEED_UNCONFIG       0
#define HW_SPEED_10_MBPS        10
#define HW_SPEED_100_MBPS       100
#define HW_SPEED_DEFAULT        (HW_SPEED_10_MBPS)

#define ACCEPT_ALL_PHYS         0x01
#define ACCEPT_ALL_MCASTS       0x02
#define ACCEPT_ALL_BCASTS       0x04
#define ACCEPT_ALL_ERRORS       0x08
#define ACCEPT_CAM_QUALIFIED    0x10
#define MAC_LOOPBACK            0x20
//#define FDX_CAPABLE_FULL_SELECTED     4
#define CRC_SIZE                4
#define MAC_HEADER_SIZE         14

typedef struct _EuphLiteDesc {
        u32     llink;
        unsigned char*  buf;
        u32     physAddr;
        /* Hardware sees the physical address of descriptor */
        u32     plink;
        u32     cmdsts;
        u32     bufPhys;
} EuphLiteDesc;

struct sis900_private {
        char devname[8];                /* Used only for kernel debugging. */
        const char *product_name;
        struct net_device *next_module;
        int chip_id;
        int chip_revision;
        unsigned char pci_bus, pci_devfn;
#if LINUX_VERSION_CODE > 0x20139
        struct net_device_stats stats;
#else
        struct enet_statistics stats;
#endif
        struct timer_list timer;        /* Media selection timer. */
        unsigned int cur_rx;    /* Index into the Rx buffer of next Rx pkt. */
        unsigned int cur_tx, dirty_tx, tx_flag;

        /* The saved address of a sent-in-place packet/buffer, for skfree(). */
        struct sk_buff* tx_skbuff[NUM_TX_DESC];
        EuphLiteDesc tx_buf[NUM_TX_DESC];       /* Tx bounce buffers */
        EuphLiteDesc rx_buf[NUM_RX_DESC];
        unsigned char *rx_bufs;
        unsigned char *tx_bufs;                 /* Tx bounce buffer region. */
        char phys[4];                           /* MII device addresses.    */
        int phy_idx;                            /* Support Max 4 PHY        */
        u16 pmd_status;
        unsigned int tx_full;                   /* The Tx queue is full.    */
	int MediaSpeed;                         /* user force speed         */
	int MediaDuplex;                        /* user force duplex        */
        int full_duplex;                        /* Full/Half-duplex.        */
        int speeds;                             /* 100/10 Mbps.             */
        u16 LinkOn;
        u16 LinkChange;
};

#ifdef MODULE
#if LINUX_VERSION_CODE > 0x20115
MODULE_AUTHOR("Jim Huang <cmhuang@sis.com.tw>");
MODULE_DESCRIPTION("SiS 900 PCI Fast Ethernet driver");
MODULE_PARM(speeds, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(full_duplex, "1-" __MODULE_STRING(MAX_UNITS) "i");
MODULE_PARM(multicast_filter_limit, "i");
MODULE_PARM(max_interrupt_work, "i");
MODULE_PARM(debug, "i");
#endif
#endif

static int sis900_open(struct net_device *dev);
static u16 read_eeprom(long ioaddr, int location);
static int mdio_read(struct net_device *dev, int phy_id, int location);
static void mdio_write(struct net_device *dev, int phy_id, int location, int val);
static void sis900_timer(unsigned long data);
static void sis900_tx_timeout(struct net_device *dev);
static void sis900_init_ring(struct net_device *dev);
static int sis900_start_xmit(struct sk_buff *skb, struct net_device *dev);
static int sis900_rx(struct net_device *dev);
static void sis900_interrupt(int irq, void *dev_instance, struct pt_regs *regs);
static int sis900_close(struct net_device *dev);
static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd);
static struct enet_statistics *sis900_get_stats(struct net_device *dev);
static void set_rx_mode(struct net_device *dev);
static void sis900_reset(struct net_device *dev);
static u16 elAutoNegotiate(struct net_device *dev, int phy_id, int *duplex, int *speed);
static void elSetCapability(struct net_device *dev, int phy_id, int duplex, int speed);
static u16 elPMDreadMode(struct net_device *dev, int phy_id, int *speed, int *duplex);
static u16 elMIIpollBit(struct net_device *dev, int phy_id, int location, u16 mask, u16 polarity, u16 *value);
static void elSetMediaType(struct net_device *dev, int speed, int duplex);

/* A list of all installed SiS900 devices, for removing the driver module. */
static struct net_device *root_sis900_dev = NULL;

/* Ideally we would detect all network cards in slot order.  That would
   be best done a central PCI probe dispatch, which wouldn't work
   well when dynamically adding drivers.  So instead we detect just the
   SiS 900 cards in slot order. */

int sis900_probe(struct net_device *dev)
{
        int cards_found = 0;
        int pci_index = 0;
        unsigned char pci_bus, pci_device_fn;

        if ( ! pcibios_present())
                return -ENODEV;

        for (;pci_index < 0xff; pci_index++) {
                u16 vendor, device, pci_command, new_command;
                int chip_idx, irq;
                long ioaddr;

                if (pcibios_find_class (PCI_CLASS_NETWORK_ETHERNET << 8,
                                                pci_index,
                                                &pci_bus, &pci_device_fn)
                                != PCIBIOS_SUCCESSFUL) {
                        break;
                }
                pcibios_read_config_word(pci_bus, pci_device_fn, PCI_VENDOR_ID,
                                                        &vendor);
                pcibios_read_config_word(pci_bus, pci_device_fn, PCI_DEVICE_ID,
                                                        &device);

                for (chip_idx = 0; pci_tbl[chip_idx].vendor_id; chip_idx++)
                        if (vendor == pci_tbl[chip_idx].vendor_id &&
                                (device & pci_tbl[chip_idx].device_id_mask) ==
                                pci_tbl[chip_idx].device_id)
                                break;
                if (pci_tbl[chip_idx].vendor_id == 0)   /* Compiled out! */
                        continue;

	{
                struct pci_dev *pdev = pci_find_slot(pci_bus, pci_device_fn);
                ioaddr = pdev->resource[0].start;
                irq = pdev->irq;
        }

                if ((pci_tbl[chip_idx].flags & PCI_USES_IO) &&
                        check_region(ioaddr, pci_tbl[chip_idx].io_size))
                        continue;

                /* Activate the card: fix for brain-damaged Win98 BIOSes. */
                pcibios_read_config_word(pci_bus, pci_device_fn,
                                         PCI_COMMAND, &pci_command);
                new_command = pci_command | (pci_tbl[chip_idx].flags & 7);
                if (pci_command != new_command) {
                        printk(KERN_INFO "  The PCI BIOS has not enabled the"
                                   " device at %d/%d!"
                                   "Updating PCI command %4.4x->%4.4x.\n",
                                        pci_bus, pci_device_fn,
                                        pci_command, new_command);

                        pcibios_write_config_word(pci_bus, pci_device_fn,
                                                  PCI_COMMAND, new_command);
                }

                dev = pci_tbl[chip_idx].probe1(pci_bus,
                                                pci_device_fn,
                                                dev,
                                                ioaddr,
                                                irq,
                                                chip_idx,
                                                cards_found);

                if (dev  && (pci_tbl[chip_idx].flags & PCI_COMMAND_MASTER)) {
                        u8 pci_latency;

                        pcibios_read_config_byte(pci_bus, pci_device_fn,
                                        PCI_LATENCY_TIMER, &pci_latency);

                        if (pci_latency < 32) {
                           printk(KERN_NOTICE "  PCI latency timer (CFLT) is "
                           "unreasonably low at %d.  Setting to 64 clocks.\n",
                                           pci_latency);
                           pcibios_write_config_byte(pci_bus, pci_device_fn,
                                          PCI_LATENCY_TIMER, 64);
                        }
                }
                dev = 0;
                cards_found++;
        }
        return cards_found ? 0 : -ENODEV;
}

static struct net_device * sis900_probe1(   int pci_bus,
                                        int pci_devfn,
                                        struct net_device *dev,
                                        long ioaddr,
                                        int irq,
                                        int chip_idx,
                                        int found_cnt)
{
        static int did_version = 0;     /* Already printed version info. */
        struct sis900_private *tp;
	u16    status;
        int    duplex = found_cnt < MAX_UNITS ? full_duplex[found_cnt] : 0 ;
        int    speed  = found_cnt < MAX_UNITS ? speeds[found_cnt] : 0 ;
        int    phy=0, phy_idx=0, i;

        if (did_version++ == 0)
                printk(KERN_INFO "%s", version);

        dev = init_etherdev(dev, 0);
        
        if(dev==NULL)
        	return NULL;

        printk(KERN_INFO "%s: %s at %#lx, IRQ %d, ",
                   dev->name, pci_tbl[chip_idx].name, ioaddr, irq);

        if ((u16)read_eeprom(ioaddr, EuphLiteEEVendorID) != 0xffff) {
                for (i = 0; i < 3; i++)
                        ((u16 *)(dev->dev_addr))[i] =
                                        read_eeprom(ioaddr,i+EuphLiteEEMACAddr);
                for (i = 0; i < 5; i++)
                        printk("%2.2x:", (u8)dev->dev_addr[i]);
                printk("%2.2x.\n", dev->dev_addr[i]);
        } else
                printk(KERN_INFO "Error EEPROM read\n");

        /* We do a request_region() to register /proc/ioports info. */
        request_region(ioaddr, pci_tbl[chip_idx].io_size, dev->name);

        dev->base_addr = ioaddr;
        dev->irq = irq;

        /* Some data structures must be quadword aligned. */
        tp = kmalloc(sizeof(*tp), GFP_KERNEL | GFP_DMA);
        if(tp==NULL)
        {
        	release_region(ioaddr, pci_tbl[chip_idx].io_size);
        	return NULL;
        }
        memset(tp, 0, sizeof(*tp));
        dev->priv = tp;

        tp->next_module = root_sis900_dev;
        root_sis900_dev = dev;

        tp->chip_id = chip_idx;
        tp->pci_bus = pci_bus;
        tp->pci_devfn = pci_devfn;

        /* Find the connected MII xcvrs.
           Doing this in open() would allow detecting external xcvrs later, but
           takes too much time. */
        if (sis_cap_tbl[chip_idx] & HAS_MII_XCVR) {
                for (phy = 0, phy_idx = 0;
                        phy < 32 && phy_idx < sizeof(tp->phys); phy++)
                {
                        int mii_status ;
			mii_status = mdio_read(dev, phy, MII_STATUS);

                        if (mii_status != 0xffff && mii_status != 0x0000) {
                                tp->phy_idx = phy_idx;
                                tp->phys[phy_idx++] = phy;
                                tp->pmd_status=mdio_read(dev, phy, MII_STATUS);
                                printk(KERN_INFO "%s: MII transceiver found "
                                                 "at address %d.\n",
                                                 dev->name, phy);
                                break;
                        }
                }

                if (phy_idx == 0) {
                        printk(KERN_INFO "%s: No MII transceivers found!\n",
                                        dev->name);
                        tp->phys[0] = -1;
			tp->pmd_status = 0;
                }
        } else {
                        tp->phys[0] = -1;
			tp->pmd_status = 0;
        }

        if ((tp->pmd_status > 0) && (phy_idx > 0)) {
		if (sis900_debug > 1) {
			printk(KERN_INFO "duplex=%d, speed=%d\n",
						duplex, speed);
		}
		if (!duplex && !speed) {  
			// auto-config media type
			// Set full capability
			if (sis900_debug > 1) {
				printk(KERN_INFO "Auto Config ...\n");
			}
			elSetCapability(dev, tp->phys[tp->phy_idx], 1, 100);
            		tp->pmd_status=elAutoNegotiate(dev,
               	    				       tp->phys[tp->phy_idx],
			                  	       &tp->full_duplex,
						       &tp->speeds);
		} else {
			tp->MediaSpeed = speed;
			tp->MediaDuplex = duplex;
			elSetCapability(dev, tp->phys[tp->phy_idx],
					duplex, speed);
            		elAutoNegotiate(dev, tp->phys[tp->phy_idx],
			                &tp->full_duplex,
					&tp->speeds);
			status = mdio_read(dev, phy, MII_ANLPAR);
			if ( !(status & (MII_NWAY_T  | MII_NWAY_T_FDX |
					 MII_NWAY_TX | MII_NWAY_TX_FDX )))
			{
				u16 cmd=0;
				cmd |= ( speed == 100 ?
					 MIICNTL_SPEED : 0 );
				cmd |= ( duplex ? MIICNTL_FDX : 0 );
        			mdio_write(dev, phy, MII_CONTROL, cmd);
				elSetMediaType(dev, speed==100 ? 
						    HW_SPEED_100_MBPS :
						    HW_SPEED_10_MBPS,
						    duplex ?
						    FDX_CAPABLE_FULL_SELECTED:
						    FDX_CAPABLE_HALF_SELECTED);
        			elMIIpollBit(dev, phy, MII_STATUS,
						MIISTAT_LINK, TRUE, &status);
			} else {
				status = mdio_read(dev, phy, MII_STATUS);
			}
		}

                if (tp->pmd_status & MIISTAT_LINK) 
               	        tp->LinkOn = TRUE;
	        else
                        tp->LinkOn = FALSE;

		tp->LinkChange = FALSE;
	
        }

	if (sis900_debug > 1) {
        	if (tp->full_duplex == FDX_CAPABLE_FULL_SELECTED) {
                	printk(KERN_INFO "%s: Media type is Full Duplex.\n",
						dev->name);
        	} else {
                	printk(KERN_INFO "%s: Media type is Half Duplex.\n",
						dev->name);
        	}
        	if (tp->speeds == HW_SPEED_100_MBPS) {
                	printk(KERN_INFO "%s: Speed is 100mbps.\n", dev->name);
        	} else {
                	printk(KERN_INFO "%s: Speed is 10mbps.\n", dev->name);
        	}
	}

        /* The SiS900-specific entries in the device structure. */
        dev->open = &sis900_open;
        dev->hard_start_xmit = &sis900_start_xmit;
        dev->stop = &sis900_close;
        dev->get_stats = &sis900_get_stats;
        dev->set_multicast_list = &set_rx_mode;
        dev->do_ioctl = &mii_ioctl;

        return dev;
}

/* Serial EEPROM section. */

/*  EEPROM_Ctrl bits. */
#define EECLK           0x00000004      /* EEPROM shift clock. */
#define EECS            0x00000008      /* EEPROM chip select. */
#define EEDO            0x00000002      /* EEPROM chip data out. */
#define EEDI            0x00000001      /* EEPROM chip data in. */

/* Delay between EEPROM clock transitions.
   No extra delay is needed with 33Mhz PCI, but 66Mhz may change this.
 */

#define eeprom_delay()  inl(ee_addr)

/* The EEPROM commands include the alway-set leading bit. */
#define EEread          0x0180
#define EEwrite         0x0140
#define EEerase         0x01C0
#define EEwriteEnable   0x0130
#define EEwriteDisable  0x0100
#define EEeraseAll      0x0120
#define EEwriteAll      0x0110
#define EEaddrMask      0x013F
#define EEcmdShift      16

static u16 read_eeprom(long ioaddr, int location)
{
        int i;
        u16 retval = 0;
        long ee_addr = ioaddr + mear;
        u32 read_cmd = location | EEread;

        outl(0, ee_addr);
        eeprom_delay();
        outl(EECLK, ee_addr);
        eeprom_delay();

        /* Shift the read command bits out. */
        for (i = 8; i >= 0; i--) {
                u32 dataval = (read_cmd & (1 << i)) ? EEDI | EECS : EECS;
                outl(dataval, ee_addr);
                eeprom_delay();
                outl(dataval | EECLK, ee_addr);
                eeprom_delay();
        }
        outb(EECS, ee_addr);
        eeprom_delay();

        for (i = 16; i > 0; i--) {
                outl(EECS, ee_addr);
                eeprom_delay();
                outl(EECS | EECLK, ee_addr);
                eeprom_delay();
                retval = (retval << 1) | ((inl(ee_addr) & EEDO) ? 1 : 0);
                eeprom_delay();
        }

        /* Terminate the EEPROM access. */
        outl(0, ee_addr);
        eeprom_delay();
        outl(EECLK, ee_addr);
        return (retval);
}

/* MII serial management: mostly bogus for now. */
/* Read and write the MII management registers using software-generated
   serial MDIO protocol.
   The maximum data clock rate is 2.5 Mhz.  The minimum timing is usually
   met by back-to-back PCI I/O cycles, but we insert a delay to avoid
   "overclocking" issues. */

#define mdio_delay()    inl(mdio_addr)

#define MIIread         0x6000
#define MIIwrite        0x6002
#define MIIpmdMask      0x0F80
#define MIIpmdShift     7
#define MIIregMask      0x007C
#define MIIregShift     2
#define MIIturnaroundBits       2
#define MIIcmdLen       16
#define MIIcmdShift     16
#define MIIreset        0xFFFFFFFF
#define MIIwrLen        32

#define MDC             0x00000040
#define MDDIR           0x00000020
#define MDIO            0x00000010

static void mdio_idle(long mdio_addr)
{
        outl(MDIO | MDDIR, mdio_addr);
        mdio_delay();
        outl(MDIO | MDDIR | MDC, mdio_addr);
}

/* Syncronize the MII management interface by shifting 32 one bits out. */
static void mdio_reset(long mdio_addr)
{
        int i;

        for (i = 31; i >= 0; i--) {
                outl(MDDIR | MDIO, mdio_addr);
                mdio_delay();
                outl(MDDIR | MDIO | MDC, mdio_addr);
                mdio_delay();
        }
        return;
}

static int mdio_read(struct net_device *dev, int phy_id, int location)
{
        long mdio_addr = dev->base_addr + mear;
        int mii_cmd = MIIread|(phy_id<<MIIpmdShift)|(location<<MIIregShift);
        int retval = 0;
        int i;

        mdio_reset(mdio_addr);
        mdio_idle(mdio_addr);

        for (i = 15; i >= 0; i--) {
                int dataval = (mii_cmd & (1 << i)) ? MDDIR | MDIO : MDDIR;
                outl(dataval, mdio_addr);
                outl(dataval | MDC, mdio_addr);
        }

        /* Read the two transition, 16 data, and wire-idle bits. */
        for (i = 16; i > 0; i--) {
                outl(0, mdio_addr);
                //mdio_delay();
                retval = (retval << 1) | ((inl(mdio_addr) & MDIO) ? 1 : 0);
                outl(MDC, mdio_addr);
                mdio_delay();
        }
        return retval;
}

static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
        long mdio_addr = dev->base_addr + mear;
        int mii_cmd = MIIwrite|(phy_id<<MIIpmdShift)|(location<<MIIregShift);
        int i;

        mdio_reset(mdio_addr);
        mdio_idle(mdio_addr);

        /* Shift the command bits out. */
        for (i = 31; i >= 0; i--) {
                int dataval = (mii_cmd & (1 << i)) ? MDDIR | MDIO : MDDIR;
                outb(dataval, mdio_addr);
                mdio_delay();
                outb(dataval | MDC, mdio_addr);
                mdio_delay();
        }
        mdio_delay();
        /* Clear out extra bits. */
        for (i = 2; i > 0; i--) {
                outb(0, mdio_addr);
                mdio_delay();
                outb(MDC, mdio_addr);
                mdio_delay();
        }
        return;
}

static int
sis900_open(struct net_device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        long ioaddr = dev->base_addr;

        if (sis900_debug > 0)
                printk(KERN_INFO "%s sis900_open, IO Addr=%x, Irq=%x\n",
                                dev->name, (unsigned int)ioaddr, dev->irq);

        /* Soft reset the chip. */
        outl(0, ioaddr + imr);
        outl(0, ioaddr + ier);
        outl(0, ioaddr + rfcr);
        outl(RESET | RxRESET | TxRESET, ioaddr + cr);

        if (request_irq(dev->irq, &sis900_interrupt, SA_SHIRQ, dev->name, dev))
        {
                return -EAGAIN;
        }

        MOD_INC_USE_COUNT;      

        tp->tx_bufs = kmalloc(TX_BUF_SIZE * NUM_TX_DESC, GFP_KERNEL);
        tp->rx_bufs = kmalloc(RX_BUF_SIZE * NUM_RX_DESC, GFP_KERNEL);
        if (tp->tx_bufs == NULL || tp->rx_bufs == NULL) {
                if (tp->tx_bufs)
                        kfree(tp->tx_bufs);
                if (tp->rx_bufs)
                        kfree(tp->rx_bufs);
		if (!tp->tx_bufs) {
	              printk(KERN_ERR "%s: Can't allocate a %d byte TX Bufs.\n",
                                   dev->name, TX_BUF_SIZE * NUM_TX_DESC);
		}
		if (!tp->rx_bufs) {
	              printk(KERN_ERR "%s: Can't allocate a %d byte RX Bufs.\n",
                                   dev->name, RX_BUF_SIZE * NUM_RX_DESC);
		}
                return -ENOMEM;
        }

        {
                u32 rfcrSave;
                u32 w;
                u32 i;

                rfcrSave = inl(rfcr);
                outl(rfcrSave & ~RFEN, rfcr);
                for (i=0 ; i<3 ; i++) {
                        w = (u16)*((u16*)(dev->dev_addr)+i);
                        outl((((u32) i) << RFEP_shift), ioaddr + rfcr);
                        outl((u32)w, ioaddr + rfdr);
                        if (sis900_debug > 4) {
                                printk(KERN_INFO "Filter Addr[%d]=%x\n",
                                        i, inl(ioaddr + rfdr));
                        }
                }
                outl(rfcrSave, rfcr);
        }

        sis900_init_ring(dev);
        outl((u32)tp->tx_buf[0].physAddr, ioaddr + txdp);
        outl((u32)tp->rx_buf[0].physAddr, ioaddr + rxdp);

        if (sis900_debug > 4)
                printk(KERN_INFO "txdp:%8.8x\n", inl(ioaddr + txdp));

        /* Check that the chip has finished the reset. */
        {
                u32 status;
                int j=0;
                status = TxRCMP | RxRCMP;
                while (status && (j++ < 30000)) {
                        status ^= (inl(isr) & status);
                }
        }

        outl(PESEL, ioaddr + cfg);

        /* Must enable Tx/Rx before setting transfer thresholds! */
        /*
         *      #define TX_DMA_BURST    0
         *      #define RX_DMA_BURST    0
         *      #define TX_FIFO_THRESH  16
         *      #define TxDRNT_100      (1536>>5)
         *      #define TxDRNT_10       (1536>>5)
         *      #define RxDRNT_100      (1536>>5)
         *      #define RxDRNT_10       (1536>>5)
         */
        outl((RX_DMA_BURST<<20) | (RxDRNT_10 << 1), ioaddr+rxcfg);
        outl(TxATP | (TX_DMA_BURST << 20) | (TX_FIFO_THRESH<<8) | TxDRNT_10,
                                                ioaddr + txcfg);
        if (sis900_debug > 1)
        {
        	if (tp->LinkOn) {
        		printk(KERN_INFO"%s: Media Type %s%s-duplex.\n",
                                dev->name,
                                tp->speeds==HW_SPEED_100_MBPS ?
					"100mbps " : "10mbps ",
                                tp->full_duplex== FDX_CAPABLE_FULL_SELECTED ?
					"full" : "half");
		}
		else printk(KERN_INFO"%s: Media Link Off\n", dev->name);
	}
        set_rx_mode(dev);

        dev->tbusy = 0;
        dev->interrupt = 0;
        dev->start = 1;

        /* Enable all known interrupts by setting the interrupt mask. */
        outl((RxOK|RxERR|RxORN|RxSOVR|TxOK|TxERR|TxURN), ioaddr + imr);
        outl(RxENA, ioaddr + cr);
        outl(IE, ioaddr + ier);

        if (sis900_debug > 3)
                printk(KERN_INFO "%s: sis900_open() ioaddr %#lx IRQ %d \n",
                           dev->name, ioaddr, dev->irq);

        /* Set the timer to switch to check for link beat and perhaps switch
           to an alternate media type. */
        init_timer(&tp->timer);
        tp->timer.expires = RUN_AT((24*HZ)/10);         /* 2.4 sec. */
        tp->timer.data = (unsigned long)dev;
        tp->timer.function = &sis900_timer;             /* timer handler */
        add_timer(&tp->timer);

        return 0;
}

static void sis900_timer(unsigned long data)
{
        struct net_device *dev = (struct net_device *)data;
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        int next_tick = 0;
        u16 status;

        if (!tp->LinkOn) {
                status = mdio_read(dev, tp->phys[tp->phy_idx], MII_STATUS);
		if (status & MIISTAT_LINK) {
                	elPMDreadMode(dev, tp->phys[tp->phy_idx],
                                        &tp->speeds, &tp->full_duplex);
			tp->LinkOn = TRUE;
                        printk(KERN_INFO "%s: Media Link On %s%s-duplex ",
                                   dev->name,
                                   tp->speeds == HW_SPEED_100_MBPS ?
						"100mbps " : "10mbps ",
                                   tp->full_duplex==FDX_CAPABLE_FULL_SELECTED ?
						"full" : "half");
		}
        } else { // previous link on
                status = mdio_read(dev, tp->phys[tp->phy_idx], MII_STATUS);
		if (!(status & MIISTAT_LINK)) {
			tp->LinkOn = FALSE;
                        printk(KERN_INFO "%s: Media Link Off\n", dev->name);
		}
        }
        next_tick = 2*HZ;

        if (next_tick) {
                tp->timer.expires = RUN_AT(next_tick);
                add_timer(&tp->timer);
        }
}

static void sis900_tx_timeout(struct net_device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        long ioaddr = dev->base_addr;
        int i;

        if (sis900_debug > 0)
                printk(KERN_INFO "%s: Transmit timeout, status %2.2x %4.4x \n",
                           dev->name, inl(ioaddr + cr), inl(ioaddr + isr));

        /* Disable interrupts by clearing the interrupt mask. */
        outl(0x0000, ioaddr + imr);

        /* Emit info to figure out what went wrong. */
	if (sis900_debug > 1) {
	        printk(KERN_INFO "%s:Tx queue start entry %d dirty entry %d.\n",
                		   dev->name, tp->cur_tx, tp->dirty_tx);
        	for (i = 0; i < NUM_TX_DESC; i++) 
                	printk(KERN_INFO "%s:  Tx descriptor %d is %8.8x.%s\n",
                        	dev->name, i, (unsigned int)&tp->tx_buf[i],
                     		i == tp->dirty_tx % NUM_TX_DESC ?
                      		" (queue head)" : "");
	}

        /* Soft reset the chip. */
        //outb(RESET, ioaddr + cr);
        /* Check that the chip has finished the reset. */
        /*
        for (i = 1000; i > 0; i--)
                if ((inb(ioaddr + cr) & RESET) == 0)
                        break;
        */

        tp->cur_rx = 0; 
        /* Must enable Tx/Rx before setting transfer thresholds! */
        /*
        set_rx_mode(dev);
        */
        {       /* Save the unsent Tx packets. */
                struct sk_buff *saved_skb[NUM_TX_DESC], *skb;
                int j;
                for (j = 0; tp->cur_tx - tp->dirty_tx > 0 ; j++, tp->dirty_tx++)
                        saved_skb[j]=tp->tx_skbuff[tp->dirty_tx % NUM_TX_DESC];
                tp->dirty_tx = tp->cur_tx = 0;

                for (i = 0; i < j; i++) {
                        skb = tp->tx_skbuff[i] = saved_skb[i];
                        /* Always alignment */
                        memcpy((unsigned char*)(tp->tx_buf[i].buf),
                                                skb->data, skb->len);
                        tp->tx_buf[i].cmdsts = OWN | skb->len;
                        /* Note: the chip doesn't have auto-pad! */
                        /*
                        outl(tp->tx_flag|(skb->len>=ETH_ZLEN?skb->len:ETH_ZLEN),
                                 ioaddr + TxStatus0 + i*4);
                        */
                }
                outl(TxENA, ioaddr + cr);
                tp->cur_tx = i;
                while (i < NUM_TX_DESC)
                        tp->tx_skbuff[i++] = 0;
                if (tp->cur_tx - tp->dirty_tx < NUM_TX_DESC) {/* Typical path */
                        dev->tbusy = 0;
                        tp->tx_full = 0;
                } else {
                        tp->tx_full = 1;
                }
        }

        dev->trans_start = jiffies;
        tp->stats.tx_errors++;
        /* Enable all known interrupts by setting the interrupt mask. */
        outl((RxOK|RxERR|RxORN|RxSOVR|TxOK|TxERR|TxURN), ioaddr + imr);
        return;
}


/* Initialize the Rx and Tx rings, along with various 'dev' bits. */
static void
sis900_init_ring(struct net_device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        int i;

        tp->tx_full = 0;
        tp->cur_rx = 0;
        tp->dirty_tx = tp->cur_tx = 0;

        /* Tx Buffer */
        for (i = 0; i < NUM_TX_DESC; i++) {
                tp->tx_skbuff[i] = 0;
                tp->tx_buf[i].buf = &tp->tx_bufs[i*TX_BUF_SIZE];
                tp->tx_buf[i].bufPhys =
                                virt_to_bus(&tp->tx_bufs[i*TX_BUF_SIZE]);
        }

        /* Tx Descriptor */
        for (i = 0; i< NUM_TX_DESC; i++) {
                tp->tx_buf[i].llink = (u32)
                        &(tp->tx_buf[((i+1) < NUM_TX_DESC) ? (i+1) : 0]);
                tp->tx_buf[i].plink = (u32)
                        virt_to_bus(&(tp->tx_buf[((i+1) < NUM_TX_DESC) ?
                                (i+1) : 0].plink));
                tp->tx_buf[i].physAddr=
                                virt_to_bus(&(tp->tx_buf[i].plink));
                tp->tx_buf[i].cmdsts=0;
        }

        /* Rx Buffer */
        for (i = 0; i < NUM_RX_DESC; i++) {
                tp->rx_buf[i].buf = &tp->rx_bufs[i*RX_BUF_SIZE];
                tp->rx_buf[i].bufPhys =
                                virt_to_bus(&tp->rx_bufs[i*RX_BUF_SIZE]);
        }

        /* Rx Descriptor */
        for (i = 0; i< NUM_RX_DESC; i++) {
                tp->rx_buf[i].llink = (u32)
                        &(tp->rx_buf[((i+1) < NUM_RX_DESC) ? (i+1) : 0]);
                tp->rx_buf[i].plink = (u32)
                        virt_to_bus(&(tp->rx_buf[((i+1) < NUM_RX_DESC) ?
                                (i+1) : 0].plink));
                tp->rx_buf[i].physAddr=
                                virt_to_bus(&(tp->rx_buf[i].plink));
                tp->rx_buf[i].cmdsts=RX_BUF_SIZE;
        }
}

static int
sis900_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        long ioaddr = dev->base_addr;
        int entry;

        /* Block a timer-based transmit from overlapping.  This could better be
           done with atomic_swap(1, dev->tbusy), but set_bit() works as well. */
        if (test_and_set_bit(0, (void*)&dev->tbusy) != 0) {
                if (jiffies - dev->trans_start < TX_TIMEOUT)
                        return 1;
                sis900_tx_timeout(dev);
                return 1;
        }

        /* Calculate the next Tx descriptor entry. ????? */
        entry = tp->cur_tx % NUM_TX_DESC;

        tp->tx_skbuff[entry] = skb;

        if (sis900_debug > 5) {
                int i;
                printk(KERN_INFO "%s: SKB Tx Frame contents:(len=%d)",
                                                dev->name,skb->len);

                for (i = 0; i < skb->len; i++) {
                        printk("%2.2x ",
                        (u8)skb->data[i]);
                }
                printk(".\n");
        }

        memcpy(tp->tx_buf[entry].buf,
                                skb->data, skb->len);

        tp->tx_buf[entry].cmdsts=(OWN | skb->len);

        //tp->tx_buf[entry].plink = 0;
        outl(TxENA, ioaddr + cr);
        if (++tp->cur_tx - tp->dirty_tx < NUM_TX_DESC) {/* Typical path */
                clear_bit(0, (void*)&dev->tbusy);
        } else {
                tp->tx_full = 1;
        }

        /* Note: the chip doesn't have auto-pad! */

        dev->trans_start = jiffies;
        if (sis900_debug > 4)
                printk(KERN_INFO "%s: Queued Tx packet at "
                                "%p size %d to slot %d.\n",
                           dev->name, skb->data, (int)skb->len, entry);

        return 0;
}

/* The interrupt handler does all of the Rx thread work and cleans up
   after the Tx thread. */
static void sis900_interrupt(int irq, void *dev_instance, struct pt_regs *regs)
{
        struct net_device *dev = (struct net_device *)dev_instance;
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        int boguscnt = max_interrupt_work;
        int status;
        long ioaddr = dev->base_addr;

#if defined(__i386__)
        /* A lock to prevent simultaneous entry bug on Intel SMP machines. */
        if (test_and_set_bit(0, (void*)&dev->interrupt)) {
                printk(KERN_INFO "%s: SMP simultaneous entry of "
                                "an interrupt handler.\n", dev->name);
                dev->interrupt = 0;     /* Avoid halting machine. */
                return;
        }
#else
        if (dev->interrupt) {
                printk(KERN_INFO "%s: Re-entering the "
                                "interrupt handler.\n", dev->name);
                return;
        }
        dev->interrupt = 1;
#endif

        do {
                status = inl(ioaddr + isr);
                /* Acknowledge all of the current interrupt sources ASAP. */
                outl(status, ioaddr + isr); // ?????

                if (sis900_debug > 4)
                        printk(KERN_INFO "%s: interrupt  status=%#4.4x "
                                "new intstat=%#4.4x.\n",
                                dev->name, status, inl(ioaddr + isr));

                if ((status & (TxURN|TxERR|TxOK | RxORN|RxERR|RxOK)) == 0) {
                        break;
                }

                if (status & (RxOK|RxORN|RxERR)) /* Rx interrupt */
                        sis900_rx(dev);

                if (status & (TxOK | TxERR)) {
                        unsigned int dirty_tx;

                        if (sis900_debug > 5) {
                                printk(KERN_INFO "TxOK:tp->cur_tx:%d,"
                                                "tp->dirty_tx:%x\n",
                                        tp->cur_tx, tp->dirty_tx);
                        }
                        for (dirty_tx = tp->dirty_tx; dirty_tx < tp->cur_tx;
                                dirty_tx++)
                        {
                                int i;
                                int entry = dirty_tx % NUM_TX_DESC;
                                int txstatus = tp->tx_buf[entry].cmdsts;

                                if (sis900_debug > 4) {
                                        printk(KERN_INFO "%s:     Tx Frame contents:"
                                                "(len=%d)",
                                                dev->name, (txstatus & DSIZE));

                                        for (i = 0; i < (txstatus & DSIZE) ;
                                                                        i++) {
                                                printk("%2.2x ",
                                                (u8)(tp->tx_buf[entry].buf[i]));
                                        }
                                        printk(".\n");
                                }
                                if ( ! (txstatus & (OK | UNDERRUN)))
                                {
                                        if (sis900_debug > 1)
                                                printk(KERN_INFO "Tx NOT (OK,"
                                                        "UnderRun)\n");
                                        break;  /* It still hasn't been Txed */
                                }

                                /* Note: TxCarrierLost is always asserted
                                                at 100mbps.                 */
                                if (txstatus & (OWCOLL | ABORT)) {
                                        /* There was an major error, log it. */
                                        if (sis900_debug > 1)
                                                printk(KERN_INFO "Tx Out of "
                                                        " Window,Abort\n");
#ifndef final_version
                                        if (sis900_debug > 1)
                                                printk(KERN_INFO "%s: Transmit "
                                                    "error, Tx status %8.8x.\n",
                                                           dev->name, txstatus);
#endif
                                        tp->stats.tx_errors++;
                                        if (txstatus & ABORT) {
                                                tp->stats.tx_aborted_errors++;
                                        }
                                        if (txstatus & NOCARRIER)
                                                tp->stats.tx_carrier_errors++;
                                        if (txstatus & OWCOLL)
                                                tp->stats.tx_window_errors++;
#ifdef ETHER_STATS
                                        if ((txstatus & COLCNT)==COLCNT)
                                                tp->stats.collisions16++;
#endif
                                } else {
#ifdef ETHER_STATS
                                        /* No count for tp->stats.tx_deferred */
#endif
                                        if (txstatus & UNDERRUN) {
                                           if (sis900_debug > 2)
                                             printk(KERN_INFO "Tx UnderRun\n");
                                        }
                                        tp->stats.collisions +=
                                                        (txstatus >> 16) & 0xF;
#if LINUX_VERSION_CODE > 0x20119
                                        tp->stats.tx_bytes += txstatus & DSIZE;
#endif
                                        if (sis900_debug > 2)
                                           printk(KERN_INFO "Tx Transmit OK\n");
                                        tp->stats.tx_packets++;
                                }

                                /* Free the original skb. */
                                if (sis900_debug > 2)
                                        printk(KERN_INFO "Free original skb\n");
                                dev_free_skb(tp->tx_skbuff[entry]);
                                tp->tx_skbuff[entry] = 0;
                        } // for dirty

#ifndef final_version
                        if (tp->cur_tx - dirty_tx > NUM_TX_DESC) {
                                printk(KERN_INFO"%s: Out-of-sync dirty pointer,"
                                                " %d vs. %d, full=%d.\n",
                                                dev->name, dirty_tx,
                                                tp->cur_tx, tp->tx_full);
                                dirty_tx += NUM_TX_DESC;
                        }
#endif

                        if (tp->tx_full && dirty_tx > tp->cur_tx-NUM_TX_DESC) {
                                /* The ring is no longer full, clear tbusy. */
				if (sis900_debug > 3)
                                   printk(KERN_INFO "Tx Ring NO LONGER Full\n");
                                tp->tx_full = 0;
                                dev->tbusy = 0;
                                mark_bh(NET_BH);
                        }

                        tp->dirty_tx = dirty_tx;
                        if (sis900_debug > 2)
                           printk(KERN_INFO "TxOK,tp->cur_tx:%d,tp->dirty:%d\n",
                                                tp->cur_tx, tp->dirty_tx);
                } // if (TxOK | TxERR)

                /* Check uncommon events with one test. */
                if (status & (RxORN | TxERR | RxERR)) {
                        if (sis900_debug > 2)
                                printk(KERN_INFO "%s: Abnormal interrupt,"
                                        "status %8.8x.\n", dev->name, status);

                        if (status == 0xffffffff)
                                break;
                        if (status & (RxORN | RxERR))
                                tp->stats.rx_errors++;


                        if (status & RxORN) {
                                tp->stats.rx_over_errors++;
                        }
                }
                if (--boguscnt < 0) {
                        printk(KERN_INFO "%s: Too much work at interrupt, "
                                   "IntrStatus=0x%4.4x.\n",
                                   dev->name, status);
                        break;
                }
        } while (1);

        if (sis900_debug > 3)
                printk(KERN_INFO "%s: exiting interrupt, intr_status=%#4.4x.\n",
                           dev->name, inl(ioaddr + isr));

#if defined(__i386__)
        clear_bit(0, (void*)&dev->interrupt);
#else
        dev->interrupt = 0;
#endif
        return;
}

/* The data sheet doesn't describe the Rx ring at all, so I'm guessing at the
   field alignments and semantics. */
static int sis900_rx(struct net_device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        long ioaddr = dev->base_addr;
        u16 cur_rx = tp->cur_rx % NUM_RX_DESC;
        int rx_status=tp->rx_buf[cur_rx].cmdsts;

        if (sis900_debug > 4)
                printk(KERN_INFO "%s: sis900_rx, current %4.4x,"
                                " rx status=%8.8x\n",
                                dev->name, cur_rx,
                                rx_status);

        while (rx_status & OWN) {
                int rx_size = rx_status & DSIZE;
                rx_size -= CRC_SIZE;

                if (sis900_debug > 4) {
                        int i;
                        printk(KERN_INFO "%s:  sis900_rx, rx status %8.8x,"
                                        " size %4.4x, cur %4.4x.\n",
                                   dev->name, rx_status, rx_size, cur_rx);
                        printk(KERN_INFO "%s: Rx Frame contents:", dev->name);

                        for (i = 0; i < rx_size; i++) {
                                printk("%2.2x ",
                                (u8)(tp->rx_buf[cur_rx].buf[i]));
                        }

                        printk(".\n");
                }
                if (rx_status & TOOLONG) {
                        if (sis900_debug > 1)
                                printk(KERN_INFO "%s: Oversized Ethernet frame,"
                                                " status %4.4x!\n",
                                           dev->name, rx_status);
                        tp->stats.rx_length_errors++;
                } else if (rx_status & (RXISERR | RUNT | CRCERR | FAERR)) {
                        if (sis900_debug > 1)
                                printk(KERN_INFO"%s: Ethernet frame had errors,"
                                        " status %4.4x.\n",
                                        dev->name, rx_status);
                        tp->stats.rx_errors++;
                        if (rx_status & (RXISERR | FAERR))
                                tp->stats.rx_frame_errors++;
                        if (rx_status & (RUNT | TOOLONG))
                                tp->stats.rx_length_errors++;
                        if (rx_status & CRCERR) tp->stats.rx_crc_errors++;
                } else {
                        /* Malloc up new buffer, compatible with net-2e. */
                        /* Omit the four octet CRC from the length. */
                        struct sk_buff *skb;

                        skb = dev_alloc_skb(rx_size + 2);
                        if (skb == NULL) {
                                printk(KERN_INFO "%s: Memory squeeze,"
                                                "deferring packet.\n",
                                                dev->name);
                                /* We should check that some rx space is free.
                                   If not,
                                   free one and mark stats->rx_dropped++. */
                                tp->stats.rx_dropped++;
                                tp->rx_buf[cur_rx].cmdsts = RX_BUF_SIZE;
                                break;
                        }
                        skb->dev = dev;
                        skb_reserve(skb, 2); /* 16 byte align the IP fields. */
                        if (rx_size+CRC_SIZE > RX_BUF_SIZE) {
                                /*
                                int semi_count = RX_BUF_LEN - ring_offset - 4;
                                memcpy(skb_put(skb, semi_count),
                                        &rx_bufs[ring_offset + 4], semi_count);
                                memcpy(skb_put(skb, rx_size-semi_count),
                                        rx_bufs, rx_size - semi_count);
                                if (sis900_debug > 4) {
                                        int i;
                                        printk(KERN_DEBUG"%s:  Frame wrap @%d",
                                                   dev->name, semi_count);
                                        for (i = 0; i < 16; i++)
                                                printk(" %2.2x", rx_bufs[i]);
                                        printk(".\n");
                                        memset(rx_bufs, 0xcc, 16);
                                }
                                */
                        } else {
#if 0  /* USE_IP_COPYSUM */
                                eth_copy_and_sum(skb,
                                   tp->rx_buf[cur_rx].buf, rx_size, 0);
                                skb_put(skb, rx_size);
#else
                                memcpy(skb_put(skb, rx_size),
                                        tp->rx_buf[cur_rx].buf, rx_size);
#endif
                        }
                        skb->protocol = eth_type_trans(skb, dev);
                        netif_rx(skb);
#if LINUX_VERSION_CODE > 0x20119
                        tp->stats.rx_bytes += rx_size;
#endif
                        tp->stats.rx_packets++;
                }
                tp->rx_buf[cur_rx].cmdsts = RX_BUF_SIZE;

                cur_rx = ((cur_rx+1) % NUM_RX_DESC);
                rx_status = tp->rx_buf[cur_rx].cmdsts;
        } // while
        if (sis900_debug > 4)
                printk(KERN_INFO "%s: Done sis900_rx(), current %4.4x "
                                "Cmd %2.2x.\n",
                           dev->name, cur_rx,
                           inb(ioaddr + cr));
        tp->cur_rx = cur_rx;
        return 0;
}

static int
sis900_close(struct net_device *dev)
{
        long ioaddr = dev->base_addr;
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        int i;

        dev->start = 0;
        dev->tbusy = 1;

        if (sis900_debug > 1)
                printk(KERN_DEBUG"%s: Shutting down ethercard, status was 0x%4.4x.\n",
                           dev->name, inl(ioaddr + isr));

        /* Disable interrupts by clearing the interrupt mask. */
        outl(0x0000, ioaddr + imr);

        /* Stop the chip's Tx and Rx DMA processes. */
        outl(0x00, ioaddr + cr);

        del_timer(&tp->timer);

        free_irq(dev->irq, dev);

        for (i = 0; i < NUM_TX_DESC; i++) {
                if (tp->tx_skbuff[i])
                        dev_free_skb(tp->tx_skbuff[i]);
                tp->tx_skbuff[i] = 0;
        }
        kfree(tp->rx_bufs);
        kfree(tp->tx_bufs);

        /* Green! Put the chip in low-power mode. */

        MOD_DEC_USE_COUNT;

        return 0;
}

static int mii_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;
        u16 *data = (u16 *)&rq->ifr_data;

        switch(cmd) {
        case SIOCDEVPRIVATE:            /* Get the address of the PHY in use. */
                data[0] = tp->phys[tp->phy_idx];
                /* Fall Through */
        case SIOCDEVPRIVATE+1:          /* Read the specified MII register. */
                data[3] = mdio_read(dev, data[0] & 0x1f, data[1] & 0x1f);
                return 0;
        case SIOCDEVPRIVATE+2:          /* Write the specified MII register */
                if (!suser())
                        return -EPERM;
                mdio_write(dev, data[0] & 0x1f, data[1] & 0x1f, data[2]);
                return 0;
        default:
                return -EOPNOTSUPP;
        }
}

static struct enet_statistics *
sis900_get_stats(struct net_device *dev)
{
        struct sis900_private *tp = (struct sis900_private *)dev->priv;

        return &tp->stats;
}

/* Set or clear the multicast filter for this adaptor.
   This routine is not state sensitive and need not be SMP locked. */

static u16 elComputeHashTableIndex(u8 *addr)
{
#define POLYNOMIAL 0x04C11DB6L
    u32      crc = 0xffffffff, msb;
    int      i, j;
    u8       byte;

    for( i=0; i<6; i++ ) {
        byte = *addr++;
        for( j=0; j<8; j++ ) {
            msb = crc >> 31;
            crc <<= 1;
            if( msb ^ ( byte & 1 )) {
                crc ^= POLYNOMIAL;
                crc |= 1;
            }
            byte >>= 1;
        }
    }
    // 7 bit crc for 128 bit hash table
    return( (int)(crc >> 25) );
}

static u16 elMIIpollBit(struct net_device *dev,
                         int phy_id,
                         int location,
                         u16 mask,
                         u16 polarity,
                         u16 *value)
{
        u32 i;
        i=0;
        while (1) {
                *value = mdio_read(dev, phy_id, location);
                if (polarity) {
                        if (mask & *value) return(TRUE);
                } else {
                        if (mask & ~(*value)) return(TRUE);
                }
                if (++i == 1200) break;
        }
        return(FALSE);
}

static u16 elPMDreadMode(struct net_device *dev,
                         int phy_id,
                         int *speed,
                         int *duplex)
{
        u16 status, OurCap;

        *speed = HW_SPEED_10_MBPS;
        *duplex = FDX_CAPABLE_HALF_SELECTED;

        status = mdio_read(dev, phy_id, MII_ANLPAR);
        OurCap = mdio_read(dev, phy_id, MII_ANAR);
	if (sis900_debug > 1) {
		printk(KERN_INFO "Link Part Status %4X\n", status);
		printk(KERN_INFO "Our Status %4X\n", OurCap);
		printk(KERN_INFO "Status Reg %4X\n",
					mdio_read(dev, phy_id, MII_STATUS));
	}
	status &= OurCap;

        if ( !( status &
                (MII_NWAY_T|MII_NWAY_T_FDX | MII_NWAY_TX | MII_NWAY_TX_FDX ))) {
		if (sis900_debug > 1) {
			printk(KERN_INFO "The other end NOT support NWAY...\n");
		}
                while (( status = mdio_read(dev, phy_id, 18)) & 0x4000) ;
                while (( status = mdio_read(dev, phy_id, 18)) & 0x0020) ;
                if (status & 0x80)
                        *speed = HW_SPEED_100_MBPS;
                if (status & 0x40)
                        *duplex = FDX_CAPABLE_FULL_SELECTED;
                if (sis900_debug > 3) {
                        printk(KERN_INFO"%s: Setting %s%s-duplex.\n",
                                dev->name,
                                *speed == HW_SPEED_100_MBPS ?
                                        "100mbps " : "10mbps ",
                                *duplex == FDX_CAPABLE_FULL_SELECTED ?
                                        "full" : "half");
                }
        } else {
		if (sis900_debug > 1) {
			printk(KERN_INFO "The other end support NWAY...\n");
		}

                if (status & (MII_NWAY_TX_FDX | MII_NWAY_T_FDX)) {
                        *duplex = FDX_CAPABLE_FULL_SELECTED;
                }
                if (status & (MII_NWAY_TX_FDX | MII_NWAY_TX)) {
                        *speed = HW_SPEED_100_MBPS;
                }
                if (sis900_debug > 3) {
                        printk(KERN_INFO"%s: Setting %s%s-duplex based on"
                                " auto-negotiated partner ability.\n",
                                dev->name,
                                *speed == HW_SPEED_100_MBPS ?
                                        "100mbps " : "10mbps ",
                                *duplex == FDX_CAPABLE_FULL_SELECTED ?
                                        "full" : "half");
                }
        }
        return (status);
}

static u16 elAutoNegotiate(struct net_device *dev, int phy_id, int *duplex, int *speed)
{
        u16 status, retnVal;

	if (sis900_debug > 1) {
		printk(KERN_INFO "AutoNegotiate...\n");
	}
        mdio_write(dev, phy_id, MII_CONTROL, 0);
        mdio_write(dev, phy_id, MII_CONTROL, MIICNTL_AUTO | MIICNTL_RST_AUTO);
        retnVal = elMIIpollBit(dev, phy_id, MII_CONTROL, MIICNTL_RST_AUTO,
				FALSE,&status);
	if (!retnVal) {
		printk(KERN_INFO "Not wait for Reset Complete\n");
	}
        retnVal = elMIIpollBit(dev, phy_id, MII_STATUS, MIISTAT_AUTO_DONE,
				TRUE, &status);
	if (!retnVal) {
		printk(KERN_INFO "Not wait for AutoNego Complete\n");
	}
        retnVal = elMIIpollBit(dev, phy_id, MII_STATUS, MIISTAT_LINK,
				TRUE, &status);
	if (!retnVal) {
		printk(KERN_INFO "Not wait for Link Complete\n");
	}
        if (status & MIISTAT_LINK) {
                elPMDreadMode(dev, phy_id, speed, duplex);
                elSetMediaType(dev, *speed, *duplex);
        }
        return(status);
}

static void elSetCapability(struct net_device *dev, int phy_id,
			    int duplex, int speed)
{
        u16 cap = ( MII_NWAY_T  | MII_NWAY_T_FDX  |
		    MII_NWAY_TX | MII_NWAY_TX_FDX | MII_NWAY_CSMA_CD );

	if (speed != 100) {
		cap &= ~( MII_NWAY_TX | MII_NWAY_TX_FDX );
		if (sis900_debug > 1) {
			printk(KERN_INFO "UNSET 100Mbps\n");
		}
	}

	if (!duplex) {
		cap &= ~( MII_NWAY_T_FDX | MII_NWAY_TX_FDX );
		if (sis900_debug > 1) {
			printk(KERN_INFO "UNSET full-duplex\n");
		}
	}

        mdio_write(dev, phy_id, MII_ANAR, cap);
}

static void elSetMediaType(struct net_device *dev, int speed, int duplex)
{
        long ioaddr = dev->base_addr;
        u32     txCfgOn = 0, txCfgOff = TxDRNT;
        u32     rxCfgOn = 0, rxCfgOff = 0;

        if (speed == HW_SPEED_100_MBPS) {
                txCfgOn |= (TxDRNT_100 | TxHBI);
        } else {
                txCfgOn |= TxDRNT_10;
        }

        if (duplex == FDX_CAPABLE_FULL_SELECTED) {
                txCfgOn |= (TxCSI | TxHBI);
                rxCfgOn |= RxATP;
        } else {
                txCfgOff |= (TxCSI | TxHBI);
                rxCfgOff |= RxATP;
        }
        outl( (inl(ioaddr + txcfg) & ~txCfgOff) | txCfgOn, ioaddr + txcfg);
        outl( (inl(ioaddr + rxcfg) & ~rxCfgOff) | rxCfgOn, ioaddr + rxcfg);
}

static void set_rx_mode(struct net_device *dev)
{
        long ioaddr = dev->base_addr;
        u16 mc_filter[8];
        int i;
        int rx_mode;
        u32 rxCfgOn = 0, rxCfgOff = 0;
        u32 txCfgOn = 0, txCfgOff = 0;

        if (sis900_debug > 3)
                printk(KERN_INFO "%s: set_rx_mode (%4.4x) done--"
                                "RxCfg %8.8x.\n",
                                dev->name, dev->flags, inl(ioaddr + rxcfg));

        /* Note: do not reorder, GCC is clever about common statements. */
        if (dev->flags & IFF_PROMISC) {
                printk(KERN_NOTICE"%s: Promiscuous mode enabled.\n", dev->name);
                rx_mode = ACCEPT_ALL_BCASTS | ACCEPT_ALL_MCASTS |
                                ACCEPT_CAM_QUALIFIED | ACCEPT_ALL_PHYS;
                for (i=0 ; i<8 ; i++)
                        mc_filter[i]=0xffff;
        } else if ((dev->mc_count > multicast_filter_limit)
                           ||  (dev->flags & IFF_ALLMULTI)) {
                rx_mode = ACCEPT_ALL_BCASTS | ACCEPT_ALL_MCASTS |
                                ACCEPT_CAM_QUALIFIED;
                for (i=0 ; i<8 ; i++)
                        mc_filter[i]=0xffff;
        } else {
                struct dev_mc_list *mclist;
                rx_mode = ACCEPT_ALL_BCASTS | ACCEPT_ALL_MCASTS |
                                ACCEPT_CAM_QUALIFIED;
                for (i=0 ; i<8 ; i++)
                        mc_filter[i]=0;
                for (i = 0, mclist = dev->mc_list; mclist && i < dev->mc_count;
                         i++, mclist = mclist->next)
                        set_bit(elComputeHashTableIndex(mclist->dmi_addr),
                                                mc_filter);
        }

        for (i=0 ; i<8 ; i++) {
                outl((u32)(0x00000004+i) << 16, ioaddr + rfcr);
                outl(mc_filter[i], ioaddr + rfdr);
        }
        /* We can safely update without stopping the chip. */
        //rx_mode = ACCEPT_CAM_QUALIFIED | ACCEPT_ALL_BCASTS | ACCEPT_ALL_PHYS;
        //rx_mode = ACCEPT_CAM_QUALIFIED | ACCEPT_ALL_BCASTS;
        outl(RFEN | ((rx_mode & (ACCEPT_ALL_MCASTS | ACCEPT_ALL_BCASTS |
                          ACCEPT_ALL_PHYS)) << RFAA_shift), ioaddr + rfcr);

        if (rx_mode & ACCEPT_ALL_ERRORS) {
                rxCfgOn = RxAEP | RxARP | RxAJAB;
        } else {
                rxCfgOff = RxAEP | RxARP | RxAJAB;
        }
        if (rx_mode & MAC_LOOPBACK) {
                rxCfgOn |= RxATP;
                txCfgOn |= TxMLB;
        } else {
                if (!(( (struct sis900_private *)(dev->priv) )->full_duplex))
                        rxCfgOff |= RxATP;
                txCfgOff |= TxMLB;
        }

        if (sis900_debug > 2) {
                printk(KERN_INFO "Before Set TxCfg=%8.8x\n",inl(ioaddr+txcfg));
                printk(KERN_INFO "Before Set RxCfg=%8.8x\n",inl(ioaddr+rxcfg));
        }

        outl((inl(ioaddr + rxcfg) | rxCfgOn) & ~rxCfgOff, ioaddr + rxcfg);
        outl((inl(ioaddr + txcfg) | txCfgOn) & ~txCfgOff, ioaddr + txcfg);

        if (sis900_debug > 2) {
                printk(KERN_INFO "After Set TxCfg=%8.8x\n",inl(ioaddr+txcfg));
                printk(KERN_INFO "After Set RxCfg=%8.8x\n",inl(ioaddr+rxcfg));
                printk(KERN_INFO "Receive Filter Register:%8.8x\n",
                                                        inl(ioaddr + rfcr));
        }
        return;
}

static void sis900_reset(struct net_device *dev)
{
        long ioaddr = dev->base_addr;

        outl(0, ioaddr + ier);
        outl(0, ioaddr + imr);
        outl(0, ioaddr + rfcr);

        outl(RxRESET | TxRESET | RESET, ioaddr + cr);
        outl(PESEL, ioaddr + cfg);

        set_rx_mode(dev);
}

#ifdef MODULE
int init_module(void)
{
        return sis900_probe(0);
}

void
cleanup_module(void)
{
        struct net_device *next_dev;

        /* No need to check MOD_IN_USE, as sys_delete_module() checks. */
        while (root_sis900_dev) {
                struct sis900_private *tp =
                        (struct sis900_private *)root_sis900_dev->priv;
                next_dev = tp->next_module;
                unregister_netdev(root_sis900_dev);
                release_region(root_sis900_dev->base_addr,
                                           pci_tbl[tp->chip_id].io_size);
                kfree(tp);
                kfree(root_sis900_dev);
                root_sis900_dev = next_dev;
        }
}

#endif  /* MODULE */
/*
 * Local variables:
 *  compile-command: "gcc -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -c sis900.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  SMP-compile-command: "gcc -D__SMP__ -DMODULE -D__KERNEL__ -Wall -Wstrict-prototypes -O6 -c sis900.c `[ -f /usr/include/linux/modversions.h ] && echo -DMODVERSIONS`"
 *  c-indent-level: 4
 *  c-basic-offset: 4
 *  tab-width: 4
 * End:
 */
