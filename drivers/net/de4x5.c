/*  de4x5.c: A DIGITAL DE425/DE434/DE435/DE450/DE500 ethernet driver for Linux.

    Copyright 1994, 1995 Digital Equipment Corporation.

    This software may be used and distributed according to the terms of
    the GNU Public License, incorporated herein by reference.

    This driver is written for the Digital Equipment Corporation series
    of EtherWORKS ethernet cards:

        DE425 TP/COAX EISA
	DE434 TP PCI
	DE435 TP/COAX/AUI PCI
	DE450 TP/COAX/AUI PCI
	DE500 10/100 PCI Fasternet

    The driver has been tested on a relatively busy network using the DE425,
    DE434, DE435 and DE500 cards and benchmarked with 'ttcp': it transferred
    16M of data to a DECstation 5000/200 as follows:

                TCP           UDP
             TX     RX     TX     RX
    DE425   1030k  997k   1170k  1128k
    DE434   1063k  995k   1170k  1125k
    DE435   1063k  995k   1170k  1125k
    DE500   1063k  998k   1170k  1125k  in 10Mb/s mode

    All  values are typical (in   kBytes/sec) from a  sample  of 4 for  each
    measurement. Their error is +/-20k on a quiet (private) network and also
    depend on what load the CPU has.

    The author may    be reached  at  davies@wanton.lkg.dec.com  or  Digital
    Equipment Corporation, 550 King Street, Littleton MA 01460.

    =========================================================================
    This driver has been written  substantially  from scratch, although  its
    inheritance of style and stack interface from 'ewrk3.c' and in turn from
    Donald Becker's 'lance.c' should be obvious.

    Upto 15 EISA cards can be supported under this driver, limited primarily
    by the available IRQ lines.  I have  checked different configurations of
    multiple depca, EtherWORKS 3 cards and de4x5 cards and  have not found a
    problem yet (provided you have at least depca.c v0.38) ...

    PCI support has been added  to allow the driver  to work with the DE434,
    DE435, DE450 and DE500 cards. The I/O accesses are a bit of a kludge due
    to the differences in the EISA and PCI CSR address offsets from the base
    address.

    The ability to load  this driver as a  loadable module has been included
    and used  extensively during the  driver development (to save those long
    reboot sequences).  Loadable module support  under PCI and EISA has been
    achieved by letting the driver autoprobe as if it were compiled into the
    kernel, except that there is no autoprobing of the IRQ lines. This is of
    no great  consequence except do make sure  you're not sharing interrupts
    with  anything that cannot  accommodate  interrupt sharing! The existing
    register_netdevice() code will only allow one device to be registered at
    a time. 

    ************************************************************************
    For now, please only use the 'io=??' assignment (see  2. below, ?? != 0)
    when loading a module.
    ************************************************************************

    Essentially, the I/O address and IRQ information  are ignored and filled
    in later by  the PCI BIOS   during the PCI  probe.  Note  that the board
    should be in the system at boot time so that its I/O address and IRQ are
    allocated by the PCI BIOS automatically. 

    To utilise this ability, you have to do 8 things:

    0) have a copy of the loadable modules code installed on your system.
    1) copy de4x5.c from the  /linux/drivers/net directory to your favourite
    temporary directory.
    2) edit the source code near line 3779 to reflect the I/O address you're
    using (only  if you want to manually  load the module),  or assign these
    when loading by:

                   insmod de4x5.o io=0xghh         where g = bus number
		                                        hh = device number   

    3) compile  de4x5.c, but include -DMODULE in  the command line to ensure
    that the correct bits are compiled (see end of source code).
    4) if you are wanting to add a new  card, goto 5. Otherwise, recompile a
    kernel with the de4x5 configuration turned off and reboot.
    5) insmod de4x5.o [io=0xghh]
    6) run the net startup bits for your new eth?? interface(s) manually 
    (usually /etc/rc.inet[12] at boot time). 
    7) enjoy!

    To unload a module, turn off the associated interface(s) 
    'ifconfig eth?? down' then 'rmmod de4x5'.

    Automedia detection is included so that in  principal you can disconnect
    from, e.g.  TP, reconnect  to BNC  and  things will still work  (after a
    pause whilst the   driver figures out   where its media went).  My tests
    using ping showed that it appears to work....

    By  default,  the driver will  now   autodetect any  DECchip based card.
    Should you have a need to restrict the driver to DIGITAL only cards, you
    can compile with a  DEC_ONLY define, or if  loading as a module, use the
    'dec_only=1'  parameter. However, this  "feature" is in no way supported
    nor  tested in this  driver  and the user  may use  it  at his/her  sole
    discretion.   I have had  2 conflicting reports that  my  driver will or
    won't work  with Znyx.   Try  Donald Becker's 'tulip.c' if   this driver
    doesn't work for you. I will not be supporting Znyx  and SMC cards since
    I have  no information on  them and  can't test  them in  a system (this
    applies most particularly to the DC21140 based cards).

    I've changed the timing routines to  use the kernel timer and scheduling
    functions  so that the  hangs  and other assorted problems that occurred
    while autosensing the  media  should be gone.  A  bonus  for the DC21040
    auto  media sense algorithm is  that it can now  use one that is more in
    line with the  rest (the DC21040  chip doesn't  have a hardware  timer).
    The downside is the 1 'jiffies' (10ms) resolution.

    IEEE 802.3u MII interface code has  been added in anticipation that some
    products may use it in the future.

    The SMC9332 card  has a non-compliant SROM  which needs fixing -  I have
    patched this  driver to detect it  because the SROM format used complies
    to a previous DEC-STD format.

    I have removed the buffer copies needed for receive on Intels.  I cannot
    remove them for   Alphas since  the  Tulip hardware   only does longword
    aligned  DMA transfers  and  the  Alphas get   alignment traps with  non
    longword aligned data copies (which makes them really slow). No comment.

    TO DO:
    ------


    Revision History
    ----------------

    Version   Date        Description
  
      0.1     17-Nov-94   Initial writing. ALPHA code release.
      0.2     13-Jan-95   Added PCI support for DE435's.
      0.21    19-Jan-95   Added auto media detection.
      0.22    10-Feb-95   Fix interrupt handler call <chris@cosy.sbg.ac.at>.
                          Fix recognition bug reported by <bkm@star.rl.ac.uk>.
			  Add request/release_region code.
			  Add loadable modules support for PCI.
			  Clean up loadable modules support.
      0.23    28-Feb-95   Added DC21041 and DC21140 support. 
                          Fix missed frame counter value and initialisation.
			  Fixed EISA probe.
      0.24    11-Apr-95   Change delay routine to use <linux/udelay>.
                          Change TX_BUFFS_AVAIL macro.
			  Change media autodetection to allow manual setting.
			  Completed DE500 (DC21140) support.
      0.241   18-Apr-95   Interim release without DE500 Autosense Algorithm.
      0.242   10-May-95   Minor changes.
      0.30    12-Jun-95   Timer fix for DC21140.
                          Portability changes.
			  Add ALPHA changes from <jestabro@ant.tay1.dec.com>.
			  Add DE500 semi automatic autosense.
			  Add Link Fail interrupt TP failure detection.
			  Add timer based link change detection.
			  Plugged a memory leak in de4x5_queue_pkt().
      0.31    13-Jun-95   Fixed PCI stuff for 1.3.1.
      0.32    26-Jun-95   Added verify_area() calls in de4x5_ioctl() from a
                          suggestion by <heiko@colossus.escape.de>.
      0.33     8-Aug-95   Add shared interrupt support (not released yet).
      0.331   21-Aug-95   Fix de4x5_open() with fast CPUs.
                          Fix de4x5_interrupt().
                          Fix dc21140_autoconf() mess.
			  No shared interrupt support.
      0.332   11-Sep-95   Added MII management interface routines.
      0.40     5-Mar-96   Fix setup frame timeout <maartenb@hpkuipc.cern.ch>.
                          Add kernel timer code (h/w is too flaky).
			  Add MII based PHY autosense.
			  Add new multicasting code.
			  Add new autosense algorithms for media/mode 
			  selection using kernel scheduling/timing.
			  Re-formatted.
			  Made changes suggested by <jeff@router.patch.net>:
			    Change driver to detect all DECchip based cards
			    with DEC_ONLY restriction a special case.
			    Changed driver to autoprobe as a module. No irq
			    checking is done now - assume BIOS is good!
			  Added SMC9332 detection <manabe@Roy.dsl.tutics.ac.jp>
      0.41    21-Mar-96   Don't check for get_hw_addr checksum unless DEC card
                          only <niles@axp745gsfc.nasa.gov>
			  Fix for multiple PCI cards reported by <jos@xos.nl>
			  Duh, put the SA_SHIRQ flag into request_interrupt().
			  Fix SMC ethernet address in enet_det[].
			  Print chip name instead of "UNKNOWN" during boot.
      0.42    26-Apr-96   Fix MII write TA bit error.
                          Fix bug in dc21040 and dc21041 autosense code.
			  Remove buffer copies on receive for Intels.
			  Change sk_buff handling during media disconnects to
			   eliminate DUP packets.
			  Add dynamic TX thresholding.
			  Change all chips to use perfect multicast filtering.
			  Fix alloc_device() bug <jari@markkus2.fimr.fi>
      0.43   21-Jun-96    Fix unconnected media TX retry bug.
                          Add Accton to the list of broken cards.
			  Fix TX under-run bug for non DC21140 chips.
			  Fix boot command probe bug in alloc_device() as
			   reported by <koen.gadeyne@barco.com> and 
			   <orava@nether.tky.hut.fi>.
			  Add cache locks to prevent a race condition as
			   reported by <csd@microplex.com> and 
			   <baba@beckman.uiuc.edu>.
			  Upgraded alloc_device() code.

    =========================================================================
*/

static const char *version = "de4x5.c:v0.43 96/6/21 davies@wanton.lkg.dec.com\n";

#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/malloc.h>
#include <linux/bios32.h>
#include <linux/pci.h>
#include <linux/delay.h>
#include <asm/bitops.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/segment.h>

#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>

#include <linux/time.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/ctype.h>

#include "de4x5.h"

#define c_char const char

/*
** MII Information
*/
struct phy_table {
    int reset;              /* Hard reset required? */
    int id;                 /* IEEE OUI */
    int ta;                 /* One cycle TA time - 802.3u is confusing here */
    struct {                /* Non autonegotiation (parallel) speed det. */
	int reg;
	int mask;
	int value;
    } spd;
};

struct mii_phy {
    int reset;              /* Hard reset required? */
    int id;                 /* IEEE OUI */
    int ta;                 /* One cycle TA time */
    struct {                /* Non autonegotiation (parallel) speed det. */
	int reg;
	int mask;
	int value;
    } spd;
    int addr;               /* MII address for the PHY */
};

#define DE4X5_MAX_PHY 8     /* Allow upto 8 attached PHY devices per board */

/*
** Define the know universe of PHY devices that can be
** recognised by this driver
*/
static struct phy_table phy_info[] = {
    {0, NATIONAL_TX, 1, {0x19, 0x40, 0x00}},   /* National TX */
    {1, BROADCOM_T4, 1, {0x10, 0x02, 0x02}},   /* Broadcom T4 */
    {0, SEEQ_T4    , 1, {0x12, 0x10, 0x10}},   /* SEEQ T4 */
    {0, CYPRESS_T4 , 1, {0x05, 0x20, 0x20}}    /* Cypress T4 */
};

/*
** Define special SROM detection cases
*/
static c_char enet_det[][ETH_ALEN] = {
    {0x00, 0x00, 0xc0, 0x00, 0x00, 0x00},
    {0x00, 0x00, 0xe8, 0x00, 0x00, 0x00}
};

#define SMC    1
#define ACCTON 2


#ifdef DE4X5_DEBUG
static int de4x5_debug = DE4X5_DEBUG;
#else
static int de4x5_debug = 1;
#endif

#ifdef DE4X5_AUTOSENSE              /* Should be done on a per adapter basis */
static int de4x5_autosense = DE4X5_AUTOSENSE;
#else
static int de4x5_autosense = AUTO;  /* Do auto media/mode sensing */
#endif
#define DE4X5_AUTOSENSE_MS 250      /* msec autosense tick (DE500) */

#ifdef DE4X5_FULL_DUPLEX            /* Should be done on a per adapter basis */
static s32 de4x5_full_duplex = 1;
#else
static s32 de4x5_full_duplex = 0;
#endif

#define DE4X5_NDA 0xffe0            /* No Device (I/O) Address */

/*
** Ethernet PROM defines
*/
#define PROBE_LENGTH    32
#define ETH_PROM_SIG    0xAA5500FFUL

/*
** Ethernet Info
*/
#define PKT_BUF_SZ	1536            /* Buffer size for each Tx/Rx buffer */
#define IEEE802_3_SZ    1518            /* Packet + CRC */
#define MAX_PKT_SZ   	1514            /* Maximum ethernet packet length */
#define MAX_DAT_SZ   	1500            /* Maximum ethernet data length */
#define MIN_DAT_SZ   	1               /* Minimum ethernet data length */
#define PKT_HDR_LEN     14              /* Addresses and data length info */
#define FAKE_FRAME_LEN  (MAX_PKT_SZ + 1)
#define QUEUE_PKT_TIMEOUT (3*HZ)        /* 3 second timeout */


#define CRC_POLYNOMIAL_BE 0x04c11db7UL  /* Ethernet CRC, big endian */
#define CRC_POLYNOMIAL_LE 0xedb88320UL  /* Ethernet CRC, little endian */

/*
** EISA bus defines
*/
#define DE4X5_EISA_IO_PORTS   0x0c00    /* I/O port base address, slot 0 */
#define DE4X5_EISA_TOTAL_SIZE 0x100     /* I/O address extent */

#define MAX_EISA_SLOTS 16
#define EISA_SLOT_INC 0x1000

#define DE4X5_SIGNATURE {"DE425","DE434","DE435","DE450","DE500"}
#define DE4X5_NAME_LENGTH 8

/*
** PCI Bus defines
*/
#define PCI_MAX_BUS_NUM      8
#define DE4X5_PCI_TOTAL_SIZE 0x80       /* I/O address extent */
#define DE4X5_CLASS_CODE     0x00020000 /* Network controller, Ethernet */

/*
** Memory Alignment. Each descriptor is 4 longwords long. To force a
** particular alignment on the TX descriptor, adjust DESC_SKIP_LEN and
** DESC_ALIGN. ALIGN aligns the start address of the private memory area
** and hence the RX descriptor ring's first entry. 
*/
#define ALIGN4      ((u_long)4 - 1)     /* 1 longword align */
#define ALIGN8      ((u_long)8 - 1)     /* 2 longword align */
#define ALIGN16     ((u_long)16 - 1)    /* 4 longword align */
#define ALIGN32     ((u_long)32 - 1)    /* 8 longword align */
#define ALIGN64     ((u_long)64 - 1)    /* 16 longword align */
#define ALIGN128    ((u_long)128 - 1)   /* 32 longword align */

#define ALIGN         ALIGN32           /* Keep the DC21040 happy... */
#define CACHE_ALIGN   CAL_16LONG
#define DESC_SKIP_LEN DSL_0             /* Must agree with DESC_ALIGN */
/*#define DESC_ALIGN    u32 dummy[4];  / * Must agree with DESC_SKIP_LEN */
#define DESC_ALIGN

#ifndef DEC_ONLY                        /* See README.de4x5 for using this */
static int dec_only = 0;
#else
static int dec_only = 1;
#endif

/*
** DE4X5 IRQ ENABLE/DISABLE
*/
#define ENABLE_IRQs { \
    imr |= lp->irq_en;\
    outl(imr, DE4X5_IMR);               /* Enable the IRQs */\
}

#define DISABLE_IRQs {\
    imr = inl(DE4X5_IMR);\
    imr &= ~lp->irq_en;\
    outl(imr, DE4X5_IMR);               /* Disable the IRQs */\
}

#define UNMASK_IRQs {\
    imr |= lp->irq_mask;\
    outl(imr, DE4X5_IMR);               /* Unmask the IRQs */\
}

#define MASK_IRQs {\
    imr = inl(DE4X5_IMR);\
    imr &= ~lp->irq_mask;\
    outl(imr, DE4X5_IMR);               /* Mask the IRQs */\
}

/*
** DE4X5 START/STOP
*/
#define START_DE4X5 {\
    omr = inl(DE4X5_OMR);\
    omr |= OMR_ST | OMR_SR;\
    outl(omr, DE4X5_OMR);               /* Enable the TX and/or RX */\
}

#define STOP_DE4X5 {\
    omr = inl(DE4X5_OMR);\
    omr &= ~(OMR_ST|OMR_SR);\
    outl(omr, DE4X5_OMR);               /* Disable the TX and/or RX */ \
}

/*
** DE4X5 SIA RESET
*/
#define RESET_SIA outl(0, DE4X5_SICR);  /* Reset SIA connectivity regs */

/*
** DE500 AUTOSENSE TIMER INTERVAL (MILLISECS)
*/
#define DE4X5_AUTOSENSE_MS  250

/*
** SROM Structure
*/
struct de4x5_srom {
    char sub_vendor_id[2];
    char sub_system_id[2];
    char reserved[12];
    char id_block_crc;
    char reserved2;
    char version;
    char num_adapters;
    char ieee_addr[6];
    char info[100];
    short chksum;
};
#define SUB_VENDOR_ID 0x500a

/*
** DE4X5 Descriptors. Make sure that all the RX buffers are contiguous
** and have sizes of both a power of 2 and a multiple of 4.
** A size of 256 bytes for each buffer could be chosen because over 90% of
** all packets in our network are <256 bytes long and 64 longword alignment
** is possible. 1536 showed better 'ttcp' performance. Take your pick. 32 TX
** descriptors are needed for machines with an ALPHA CPU.
*/
#define NUM_RX_DESC 8                   /* Number of RX descriptors   */
#define NUM_TX_DESC 32                  /* Number of TX descriptors   */
#define RX_BUFF_SZ  1536                /* Power of 2 for kmalloc and */
                                        /* Multiple of 4 for DC21040  */
                                        /* Allows 512 byte alignment  */
struct de4x5_desc {
    volatile s32 status;
    u32 des1;
    u32 buf;
    u32 next;
    DESC_ALIGN
};

/*
** The DE4X5 private structure
*/
#define DE4X5_PKT_STAT_SZ 16
#define DE4X5_PKT_BIN_SZ  128            /* Should be >=100 unless you
                                            increase DE4X5_PKT_STAT_SZ */

struct de4x5_private {
    char adapter_name[80];                  /* Adapter name                 */
    struct de4x5_desc rx_ring[NUM_RX_DESC]; /* RX descriptor ring           */
    struct de4x5_desc tx_ring[NUM_TX_DESC]; /* TX descriptor ring           */
    struct sk_buff *tx_skb[NUM_TX_DESC];    /* TX skb for freeing when sent */
    struct sk_buff *rx_skb[NUM_RX_DESC];    /* RX skb's                     */
    int rx_new, rx_old;                     /* RX descriptor ring pointers  */
    int tx_new, tx_old;                     /* TX descriptor ring pointers  */
    char setup_frame[SETUP_FRAME_LEN];      /* Holds MCA and PA info.       */
    char frame[64];                         /* Min sized packet for loopback*/
    struct enet_statistics stats;           /* Public stats                 */
    struct {
	u_int bins[DE4X5_PKT_STAT_SZ];      /* Private stats counters       */
	u_int unicast;
	u_int multicast;
	u_int broadcast;
	u_int excessive_collisions;
	u_int tx_underruns;
	u_int excessive_underruns;
	u_int rx_runt_frames;
	u_int rx_collision;
	u_int rx_dribble;
	u_int rx_overflow;
    } pktStats;
    char rxRingSize;
    char txRingSize;
    int  bus;                               /* EISA or PCI                  */
    int  bus_num;                           /* PCI Bus number               */
    int  state;                             /* Adapter OPENED or CLOSED     */
    int  chipset;                           /* DC21040, DC21041 or DC21140  */
    s32  irq_mask;                          /* Interrupt Mask (Enable) bits */
    s32  irq_en;                            /* Summary interrupt bits       */
    int  media;                             /* Media (eg TP), mode (eg 100B)*/
    int  c_media;                           /* Remember the last media conn */
    int  linkOK;                            /* Link is OK                   */
    int  autosense;                         /* Allow/disallow autosensing   */
    int  tx_enable;                         /* Enable descriptor polling    */
    int  lostMedia;                         /* Possibly lost media          */
    int  setup_f;                           /* Setup frame filtering type   */
    int  local_state;                       /* State within a 'media' state */
    struct mii_phy phy[DE4X5_MAX_PHY];      /* List of attached PHY devices */
    int  active;                            /* Index to active PHY device   */
    int  mii_cnt;                           /* Number of attached PHY's     */
    int  timeout;                           /* Scheduling counter           */
    struct timer_list timer;                /* Timer info for kernel        */
    int tmp;                                /* Temporary global per card    */
    struct {
	void *priv;                         /* Original kmalloc'd mem addr  */
	void *buf;                          /* Original kmalloc'd mem addr  */
	int lock;                           /* Lock the cache accesses      */
	s32 csr0;                           /* Saved Bus Mode Register      */
	s32 csr6;                           /* Saved Operating Mode Reg.    */
	s32 csr7;                           /* Saved IRQ Mask Register      */
	s32 csr13;                          /* Saved SIA Connectivity Reg.  */
	s32 csr14;                          /* Saved SIA TX/RX Register     */
	s32 csr15;                          /* Saved SIA General Register   */
	int save_cnt;                       /* Flag if state already saved  */
	struct sk_buff *skb;                /* Save the (re-ordered) skb's  */
    } cache;
};

/*
** Kludge to get around the fact that the CSR addresses have different
** offsets in the PCI and EISA boards. Also note that the ethernet address
** PROM is accessed differently.
*/
static struct bus_type {
    int bus;
    int bus_num;
    int device;
    int chipset;
    struct de4x5_srom srom;
    int autosense;
} bus;

/*
** The transmit ring full condition is described by the tx_old and tx_new
** pointers by:
**    tx_old            = tx_new    Empty ring
**    tx_old            = tx_new+1  Full ring
**    tx_old+txRingSize = tx_new+1  Full ring  (wrapped condition)
*/
#define TX_BUFFS_AVAIL ((lp->tx_old<=lp->tx_new)?\
			lp->tx_old+lp->txRingSize-lp->tx_new-1:\
			lp->tx_old               -lp->tx_new-1)

#define TX_PKT_PENDING (lp->tx_old != lp->tx_new)

/*
** Public Functions
*/
static int     de4x5_open(struct device *dev);
static int     de4x5_queue_pkt(struct sk_buff *skb, struct device *dev);
static void    de4x5_interrupt(int irq, void *dev_id, struct pt_regs *regs);
static int     de4x5_close(struct device *dev);
static struct  enet_statistics *de4x5_get_stats(struct device *dev);
static void    set_multicast_list(struct device *dev);
static int     de4x5_ioctl(struct device *dev, struct ifreq *rq, int cmd);

/*
** Private functions
*/
static int     de4x5_hw_init(struct device *dev, u_long iobase);
static int     de4x5_init(struct device *dev);
static int     de4x5_sw_reset(struct device *dev);
static int     de4x5_rx(struct device *dev);
static int     de4x5_tx(struct device *dev);
static int     de4x5_ast(struct device *dev);
static int     de4x5_txur(struct device *dev);

static int     autoconf_media(struct device *dev);
static void    create_packet(struct device *dev, char *frame, int len);
static void    de4x5_us_delay(u32 usec);
static void    de4x5_ms_delay(u32 msec);
static void    load_packet(struct device *dev, char *buf, u32 flags, struct sk_buff *skb);
static int     dc21040_autoconf(struct device *dev);
static int     dc21041_autoconf(struct device *dev);
static int     dc21140m_autoconf(struct device *dev);
static int     de4x5_suspect_state(struct device *dev, int timeout, int prev_state, int (*fn)(struct device *, int), int (*asfn)(struct device *));
static int     dc21040_state(struct device *dev, int csr13, int csr14, int csr15, int timeout, int next_state, int suspect_state, int (*fn)(struct device *, int));
static int     test_media(struct device *dev, s32 irqs, s32 irq_mask, s32 csr13, s32 csr14, s32 csr15, s32 msec);
static int     test_sym_link(struct device *dev, int msec);
static int     test_mii_reg(struct device *dev, int reg, int mask, int pol, long msec);
static int     is_spd_100(struct device *dev);
static int     is_100_up(struct device *dev);
static int     is_10_up(struct device *dev);
static int     is_anc_capable(struct device *dev);
static int     ping_media(struct device *dev, int msec);
static struct sk_buff *de4x5_alloc_rx_buff(struct device *dev, int index, int len);
static void    de4x5_free_rx_buffs(struct device *dev);
static void    de4x5_free_tx_buffs(struct device *dev);
static void    de4x5_save_skbs(struct device *dev);
static void    de4x5_restore_skbs(struct device *dev);
static void    de4x5_cache_state(struct device *dev, int flag);
static void    de4x5_put_cache(struct device *dev, struct sk_buff *skb);
static void    de4x5_putb_cache(struct device *dev, struct sk_buff *skb);
static struct  sk_buff *de4x5_get_cache(struct device *dev);
static void    de4x5_setup_intr(struct device *dev);
static void    de4x5_init_connection(struct device *dev);
static int     de4x5_reset_phy(struct device *dev);
static void    reset_init_sia(struct device *dev, s32 sicr, s32 strr, s32 sigr);
static int     test_ans(struct device *dev, s32 irqs, s32 irq_mask, s32 msec);
static int     test_tp(struct device *dev, s32 msec);
static int     EISA_signature(char *name, s32 eisa_id);
static int     PCI_signature(char *name, struct bus_type *lp);
static void    DevicePresent(u_long iobase);
static int     de4x5_bad_srom(struct bus_type *lp);
static short   srom_rd(u_long address, u_char offset);
static void    srom_latch(u_int command, u_long address);
static void    srom_command(u_int command, u_long address);
static void    srom_address(u_int command, u_long address, u_char offset);
static short   srom_data(u_int command, u_long address);
/*static void    srom_busy(u_int command, u_long address);*/
static void    sendto_srom(u_int command, u_long addr);
static int     getfrom_srom(u_long addr);
static int     mii_rd(u_char phyreg, u_char phyaddr, u_long ioaddr);
static void    mii_wr(int data, u_char phyreg, u_char phyaddr, u_long ioaddr);
static int     mii_rdata(u_long ioaddr);
static void    mii_wdata(int data, int len, u_long ioaddr);
static void    mii_ta(u_long rw, u_long ioaddr);
static int     mii_swap(int data, int len);
static void    mii_address(u_char addr, u_long ioaddr);
static void    sendto_mii(u32 command, int data, u_long ioaddr);
static int     getfrom_mii(u32 command, u_long ioaddr);
static int     mii_get_oui(u_char phyaddr, u_long ioaddr);
static int     mii_get_phy(struct device *dev);
static void    SetMulticastFilter(struct device *dev);
static int     get_hw_addr(struct device *dev);

static void    eisa_probe(struct device *dev, u_long iobase);
static void    pci_probe(struct device *dev, u_long iobase);
static struct  device *alloc_device(struct device *dev, u_long iobase);
static struct  device *insert_device(struct device *dev, u_long iobase,
				     int (*init)(struct device *));
static char    *build_setup_frame(struct device *dev, int mode);
static void    disable_ast(struct device *dev);
static void    enable_ast(struct device *dev, u32 time_out);
static long    de4x5_switch_to_srl(struct device *dev);
static long    de4x5_switch_to_mii(struct device *dev);
static void    timeout(struct device *dev, void (*fn)(u_long data), u_long data, u_long msec);
static int     de4x5_dev_index(char *s);
static void    de4x5_dbg_open(struct device *dev);
static void    de4x5_dbg_mii(struct device *dev, int k);
static void    de4x5_dbg_media(struct device *dev);
static void    de4x5_dbg_srom(struct de4x5_srom *p);
static void    de4x5_dbg_rx(struct sk_buff *skb, int len);
static int     de4x5_strncmp(char *a, char *b, int n);

#ifdef MODULE
int  init_module(void);
void cleanup_module(void);
static int autoprobed = 0, loading_module = 1;
# else
static int autoprobed = 0, loading_module = 0;
#endif /* MODULE */

static char name[DE4X5_NAME_LENGTH + 1];
static int num_de4x5s = 0, num_eth = 0;

/*
** Miscellaneous defines...
*/
#define RESET_DE4X5 {\
    int i;\
    i=inl(DE4X5_BMR);\
    de4x5_ms_delay(1);\
    outl(i | BMR_SWR, DE4X5_BMR);\
    de4x5_ms_delay(1);\
    outl(i, DE4X5_BMR);\
    de4x5_ms_delay(1);\
    for (i=0;i<5;i++) {inl(DE4X5_BMR); de4x5_ms_delay(1);}\
    de4x5_ms_delay(1);\
}


/*
** Autoprobing in modules is allowed here. See the top of the file for
** more info. Until I fix (un)register_netdevice() we won't be able to use it
** though.
*/
int
de4x5_probe(struct device *dev)
{
    int tmp = num_de4x5s, status = -ENODEV;
    u_long iobase = dev->base_addr;

    eisa_probe(dev, iobase);
    pci_probe(dev, iobase);
    
    if ((tmp == num_de4x5s) && (iobase != 0) && loading_module) {
	printk("%s: de4x5_probe() cannot find device at 0x%04lx.\n", dev->name, 
	       iobase);
    }

    /*
    ** Walk the device list to check that at least one device
    ** initialised OK
    */
    for (; (dev->priv == NULL) && (dev->next != NULL); dev = dev->next);
    
    if (dev->priv) status = 0;
    if (iobase == 0) autoprobed = 1;
    
    return status;
}

static int
de4x5_hw_init(struct device *dev, u_long iobase)
{
    struct bus_type *lp = &bus;
    int tmpbus, tmpchs, status=0;
    int i, media = *((char *)&(lp->srom) + *((char *)&(lp->srom) + 19) * 3);
    char *tmp;
    
    /* Ensure we're not sleeping */
    if (lp->chipset == DC21041) {
	outl(0, PCI_CFDA);
	de4x5_ms_delay(10);
    }
    
    RESET_DE4X5;
    
    if ((inl(DE4X5_STS) & (STS_TS | STS_RS)) != 0) {
	return -ENXIO;                       /* Hardware could not reset */
    }
    
    /* 
    ** Now find out what kind of DC21040/DC21041/DC21140 board we have.
    */
    if (lp->bus == PCI) {
	PCI_signature(name, lp);
    } else {
	EISA_signature(name, EISA_ID0);
    }
    
    if (*name == '\0') {                     /* Not found a board signature */
	return -ENXIO;
    }
    
    dev->base_addr = iobase;
    if (lp->bus == EISA) {
	printk("%s: %s at %04lx (EISA slot %ld)", 
	       dev->name, name, iobase, ((iobase>>12)&0x0f));
    } else {                                 /* PCI port address */
	printk("%s: %s at %04lx (PCI bus %d, device %d)", dev->name, name,
	       iobase, lp->bus_num, lp->device);
    }
    
    printk(", h/w address ");
    status = get_hw_addr(dev);
    for (i = 0; i < ETH_ALEN - 1; i++) {     /* get the ethernet addr. */
	printk("%2.2x:", dev->dev_addr[i]);
    }
    printk("%2.2x,\n", dev->dev_addr[i]);
    
    tmpbus = lp->bus;
    tmpchs = lp->chipset;
    
    if (status != 0) {
	printk("      which has an Ethernet PROM CRC error.\n");
	return -ENXIO;
    } else {
	struct de4x5_private *lp;
	
	/* 
	** Reserve a section of kernel memory for the adapter
	** private area and the TX/RX descriptor rings.
	*/
	dev->priv = (void *) kmalloc(sizeof(struct de4x5_private) + ALIGN, 
				     GFP_KERNEL);
	if (dev->priv == NULL) {
	    return -ENOMEM;
	}
	
	/*
	** Align to a longword boundary
	*/
	tmp = dev->priv;
	dev->priv = (void *)(((u_long)dev->priv + ALIGN) & ~ALIGN);
	lp = (struct de4x5_private *)dev->priv;
	memset(dev->priv, 0, sizeof(struct de4x5_private));
	lp->bus = tmpbus;
	lp->chipset = tmpchs;
	lp->cache.priv = tmp;

	/*
	** Check for an MII interface
	*/
	if (media & MEDIA_MII) {                   /* MII interface? */
	    if (!mii_get_phy(dev)) {
		printk("%s: MII search failed, no device found when one was expected\n", dev->name);
		return -ENXIO;
	    }
	} else {
	    mii_get_phy(dev);                      /* Search the MII anyway! */
	}
	
	/*
	** Choose correct autosensing in case someone messed up
	*/
	if (de4x5_autosense & AUTO) {
	    lp->autosense = AUTO;
	} else {
	    if (lp->chipset != DC21140) {
		if ((lp->chipset == DC21040) && (de4x5_autosense & TP_NW)) {
		    de4x5_autosense = TP;
		}
		if ((lp->chipset == DC21041) && (de4x5_autosense & BNC_AUI)) {
		    de4x5_autosense = BNC;
		}
		lp->autosense = de4x5_autosense & 0x001f;
	    } else {
		lp->autosense = de4x5_autosense & 0x00c0;
	    }
	}
	
	sprintf(lp->adapter_name,"%s (%s)", name, dev->name);
	
	/*
	** Set up the RX descriptor ring (Intels)
	** Allocate contiguous receive buffers, long word aligned (Alphas) 
	*/
#if !defined(__alpha__) && !defined(DE4X5_DO_MEMCPY)
	for (i=0; i<NUM_RX_DESC; i++) {
	    lp->rx_ring[i].status = 0;
	    lp->rx_ring[i].des1 = RX_BUFF_SZ;
	    lp->rx_ring[i].buf = 0;
	    lp->rx_ring[i].next = 0;
	    lp->rx_skb[i] = (struct sk_buff *) 1;     /* Dummy entry */
	}

#else
	if ((tmp = (void *)kmalloc(RX_BUFF_SZ * NUM_RX_DESC + ALIGN, 
				   GFP_KERNEL)) == NULL) {
	    kfree(lp->cache.priv);
	    return -ENOMEM;
	}

	lp->cache.buf = tmp;
	tmp = (char *)(((u_long) tmp + ALIGN) & ~ALIGN);
	for (i=0; i<NUM_RX_DESC; i++) {
	    lp->rx_ring[i].status = 0;
	    lp->rx_ring[i].des1 = RX_BUFF_SZ;
	    lp->rx_ring[i].buf = virt_to_bus(tmp + i * RX_BUFF_SZ);
	    lp->rx_ring[i].next = 0;
	    lp->rx_skb[i] = (struct sk_buff *) 1;     /* Dummy entry */
	}
#endif

	barrier();
	    
	request_region(iobase, (lp->bus == PCI ? DE4X5_PCI_TOTAL_SIZE :
				DE4X5_EISA_TOTAL_SIZE), 
		       lp->adapter_name);
	    
	lp->rxRingSize = NUM_RX_DESC;
	lp->txRingSize = NUM_TX_DESC;
	    
	/* Write the end of list marker to the descriptor lists */
	lp->rx_ring[lp->rxRingSize - 1].des1 |= RD_RER;
	lp->tx_ring[lp->txRingSize - 1].des1 |= TD_TER;
	    
	/* Tell the adapter where the TX/RX rings are located. */
	outl(virt_to_bus(lp->rx_ring), DE4X5_RRBA);
	outl(virt_to_bus(lp->tx_ring), DE4X5_TRBA);
	    
	/* Initialise the IRQ mask and Enable/Disable */
	lp->irq_mask = IMR_RIM | IMR_TIM | IMR_TUM | IMR_UNM;
	lp->irq_en   = IMR_NIM | IMR_AIM;

	/* Create a loopback packet frame for later media probing */
	create_packet(dev, lp->frame, sizeof(lp->frame));

	/* Initialise the adapter state */
	lp->state = CLOSED;

	printk("      and requires IRQ%d (provided by %s).\n", dev->irq,
	       ((lp->bus == PCI) ? "PCI BIOS" : "EISA CNFG"));

    }
    
    if (de4x5_debug > 0) {
	printk(version);
    }
    
    /* The DE4X5-specific entries in the device structure. */
    dev->open = &de4x5_open;
    dev->hard_start_xmit = &de4x5_queue_pkt;
    dev->stop = &de4x5_close;
    dev->get_stats = &de4x5_get_stats;
    dev->set_multicast_list = &set_multicast_list;
    dev->do_ioctl = &de4x5_ioctl;
    
    dev->mem_start = 0;
    
    /* Fill in the generic field of the device structure. */
    ether_setup(dev);
    
    /* Let the adapter sleep to save power */
    if (lp->chipset == DC21041) {
	outl(0, DE4X5_SICR);
	outl(CFDA_PSM, PCI_CFDA);
    }
    
    return status;
}


static int
de4x5_open(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    int i, status = 0;
    s32 omr;
    
    /* Allocate the RX buffers */
    for (i=0; i<lp->rxRingSize; i++) {
	if (de4x5_alloc_rx_buff(dev, i, 0) == NULL) {
	    de4x5_free_rx_buffs(dev);
	    return -EAGAIN;
	}
    }

    /*
    ** Wake up the adapter
    */
    if (lp->chipset == DC21041) {
	outl(0, PCI_CFDA);
	de4x5_ms_delay(10);
    }

    /* 
    ** Re-initialize the DE4X5... 
    */
    status = de4x5_init(dev);
    
    lp->state = OPEN;
    de4x5_dbg_open(dev);
    
    if (request_irq(dev->irq, (void *)de4x5_interrupt, SA_SHIRQ, 
		                                     lp->adapter_name, dev)) {
	printk("de4x5_open(): Requested IRQ%d is busy\n",dev->irq);
	status = -EAGAIN;
    } else {
	dev->tbusy = 0;                         
	dev->start = 1;
	dev->interrupt = UNMASK_INTERRUPTS;
	dev->trans_start = jiffies;
	
	START_DE4X5;
	
	de4x5_setup_intr(dev);
    }
    
    if (de4x5_debug > 1) {
	printk("\tsts:  0x%08x\n", inl(DE4X5_STS));
	printk("\tbmr:  0x%08x\n", inl(DE4X5_BMR));
	printk("\timr:  0x%08x\n", inl(DE4X5_IMR));
	printk("\tomr:  0x%08x\n", inl(DE4X5_OMR));
	printk("\tsisr: 0x%08x\n", inl(DE4X5_SISR));
	printk("\tsicr: 0x%08x\n", inl(DE4X5_SICR));
	printk("\tstrr: 0x%08x\n", inl(DE4X5_STRR));
	printk("\tsigr: 0x%08x\n", inl(DE4X5_SIGR));
    }
    
    MOD_INC_USE_COUNT;
    
    return status;
}

/*
** Initialize the DE4X5 operating conditions. NB: a chip problem with the
** DC21140 requires using perfect filtering mode for that chip. Since I can't
** see why I'd want > 14 multicast addresses, I have changed all chips to use
** the perfect filtering mode. Keep the DMA burst length at 8: there seems
** to be data corruption problems if it is larger (UDP errors seen from a
** ttcp source).
*/
static int
de4x5_init(struct device *dev)
{  
    /* Lock out other processes whilst setting up the hardware */
    set_bit(0, (void *)&dev->tbusy);
    
    de4x5_sw_reset(dev);
    
    /* Autoconfigure the connected port */
    autoconf_media(dev);
    
    return 0;
}

static int
de4x5_sw_reset(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    int i, j, status = 0;
    s32 bmr, omr;
    
    /* Select the MII or SRL port now and RESET the MAC */
    if (lp->phy[lp->active].id == 0) {
	de4x5_switch_to_srl(dev);
    } else {
	de4x5_switch_to_mii(dev);
    }
    
    /* 
    ** Set the programmable burst length to 8 longwords for all the DC21140
    ** Fasternet chips and 4 longwords for all others: DMA errors result
    ** without these values. Cache align 16 long.
    */
    bmr = (lp->chipset==DC21140 ? PBL_8 : PBL_4) | DESC_SKIP_LEN | CACHE_ALIGN;
    outl(bmr, DE4X5_BMR);
    
    omr = inl(DE4X5_OMR) & ~OMR_PR;             /* Turn off promiscuous mode */
    if (lp->chipset == DC21140) {
	omr |= (OMR_SDP | OMR_SB);
    }
    lp->setup_f = PERFECT;
    outl(virt_to_bus(lp->rx_ring), DE4X5_RRBA);
    outl(virt_to_bus(lp->tx_ring), DE4X5_TRBA);
    
    lp->rx_new = lp->rx_old = 0;
    lp->tx_new = lp->tx_old = 0;
    
    for (i = 0; i < lp->rxRingSize; i++) {
	lp->rx_ring[i].status = R_OWN;
    }
    
    for (i = 0; i < lp->txRingSize; i++) {
	lp->tx_ring[i].status = 0;
    }
    
    barrier();
    
    /* Build the setup frame depending on filtering mode */
    SetMulticastFilter(dev);
    
    load_packet(dev, lp->setup_frame, PERFECT_F|TD_SET|SETUP_FRAME_LEN, NULL);
    outl(omr|OMR_ST, DE4X5_OMR);
    
    /* Poll for setup frame completion (adapter interrupts are disabled now) */
    sti();                                       /* Ensure timer interrupts */
    for (j=0, i=0;(i<500) && (j==0);i++) {       /* Upto 500ms delay */
	udelay(1000);
	if (lp->tx_ring[lp->tx_new].status >= 0) j=1;
    }
    outl(omr, DE4X5_OMR);                        /* Stop everything! */
    
    if (j == 0) {
	printk("%s: Setup frame timed out, status %08x\n", dev->name, 
	       inl(DE4X5_STS));
	status = -EIO;
    }
    
    lp->tx_new = (++lp->tx_new) % lp->txRingSize;
    lp->tx_old = lp->tx_new;
    
    return status;
}

/* 
** Writes a socket buffer address to the next available transmit descriptor
*/
static int
de4x5_queue_pkt(struct sk_buff *skb, struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    int status = 0;

    if (skb == NULL) {
	dev_tint(dev);
	return 0;
    }

    set_bit(0, (void*)&dev->tbusy);              /* Stop send re-tries */
    if (lp->tx_enable == NO) {                   /* Cannot send for now */
	return -1;                                
    }
    
    /*
    ** Clean out the TX ring asynchronously to interrupts - sometimes the
    ** interrupts are lost by delayed descriptor status updates relative to
    ** the irq assertion, especially with a busy PCI bus.
    */
    cli();
    de4x5_tx(dev);
    sti();

    /* Test if cache is already locked - requeue skb if so */
    if (set_bit(0, (void *)&lp->cache.lock) && !dev->interrupt) return -1;

    /* Transmit descriptor ring full or stale skb */
    if (dev->tbusy || lp->tx_skb[lp->tx_new]) {
	if (dev->interrupt) {
	    de4x5_putb_cache(dev, skb);          /* Requeue the buffer */
	} else {
	    de4x5_put_cache(dev, skb);
	}
	if (de4x5_debug > 1) {
	    printk("%s: transmit busy, lost media or stale skb found:\n  STS:%08x\n  tbusy:%ld\n  lostMedia:%d\n  IMR:%08x\n  OMR:%08x\n Stale skb: %s\n",dev->name, inl(DE4X5_STS), dev->tbusy, lp->lostMedia, inl(DE4X5_IMR), inl(DE4X5_OMR), (lp->tx_skb[lp->tx_new] ? "YES" : "NO"));
	}
    } else if (skb->len > 0) {
	/* If we already have stuff queued locally, use that first */
	if (lp->cache.skb && !dev->interrupt) {
	    de4x5_put_cache(dev, skb);
	    skb = de4x5_get_cache(dev);
	}

	while (skb && !dev->tbusy && !lp->tx_skb[lp->tx_new]) {
	    set_bit(0, (void*)&dev->tbusy);
	    cli();
	    if (TX_BUFFS_AVAIL) {           /* Fill in a Tx ring entry */
		load_packet(dev, skb->data, 
			    TD_IC | TD_LS | TD_FS | skb->len, skb);
		outl(POLL_DEMAND, DE4X5_TPD);/* Start the TX */
		
		lp->tx_new = (++lp->tx_new) % lp->txRingSize;
		dev->trans_start = jiffies;
		    
		if (TX_BUFFS_AVAIL) {
		    dev->tbusy = 0;         /* Another pkt may be queued */
		}
		skb = de4x5_get_cache(dev);
	    }
	    sti();
	}
	if (skb && (dev->tbusy || lp->tx_skb[lp->tx_new])) {
	    de4x5_putb_cache(dev, skb);
	}
    }
    
    lp->cache.lock = 0;

    return status;
}

/*
** The DE4X5 interrupt handler. 
** 
** I/O Read/Writes through intermediate PCI bridges are never 'posted',
** so that the asserted interrupt always has some real data to work with -
** if these I/O accesses are ever changed to memory accesses, ensure the
** STS write is read immediately to complete the transaction if the adapter
** is not on bus 0. Lost interrupts can still occur when the PCI bus load
** is high and descriptor status bits cannot be set before the associated
** interrupt is asserted and this routine entered.
*/
static void
de4x5_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
    struct device *dev = (struct device *)dev_id;
    struct de4x5_private *lp;
    s32 imr, omr, sts, limit;
    u_long iobase;
    
    if (dev == NULL) {
	printk ("de4x5_interrupt(): irq %d for unknown device.\n", irq);
	return;
    }
    lp = (struct de4x5_private *)dev->priv;
    iobase = dev->base_addr;
	
    if (dev->interrupt)
      printk("%s: Re-entering the interrupt handler.\n", dev->name);
	
    DISABLE_IRQs;                        /* Ensure non re-entrancy */
    dev->interrupt = MASK_INTERRUPTS;
	
    for (limit=0; limit<8; limit++) {
	sts = inl(DE4X5_STS);            /* Read IRQ status */
	outl(sts, DE4X5_STS);            /* Reset the board interrupts */
	    
	if (!(sts & lp->irq_mask)) break;/* All done */
	    
	if (sts & (STS_RI | STS_RU))     /* Rx interrupt (packet[s] arrived) */
	  de4x5_rx(dev);
	    
	if (sts & (STS_TI | STS_TU))     /* Tx interrupt (packet sent) */
	  de4x5_tx(dev); 
	    
	if (sts & STS_LNF) {             /* TP Link has failed */
	    lp->lostMedia = LOST_MEDIA_THRESHOLD + 1;
	    lp->irq_mask &= ~IMR_LFM;
	}
	    
	if (sts & STS_UNF) {             /* Transmit underrun */
	    de4x5_txur(dev);
	}
	    
	if (sts & STS_SE) {              /* Bus Error */
	    STOP_DE4X5;
	    printk("%s: Fatal bus error occurred, sts=%#8x, device stopped.\n",
		   dev->name, sts);
	    return;
	}
    }

    /* Load the TX ring with any locally stored packets */
    if (!set_bit(0, (void *)&lp->cache.lock)) {
	while (lp->cache.skb && !dev->tbusy && lp->tx_enable) {
	    de4x5_queue_pkt(de4x5_get_cache(dev), dev);
	}
	lp->cache.lock = 0;
    }

    dev->interrupt = UNMASK_INTERRUPTS;
    ENABLE_IRQs;
    
    return;
}

static int
de4x5_rx(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int i, entry;
    s32 status;
    char *buf;
    
    for (entry=lp->rx_new; lp->rx_ring[entry].status>=0;entry=lp->rx_new) {
	status = lp->rx_ring[entry].status;
	
	if (status & RD_FS) {                 /* Remember the start of frame */
	    lp->rx_old = entry;
	}
	
	if (status & RD_LS) {                 /* Valid frame status */
	    lp->linkOK++;
	    if (status & RD_ES) {	      /* There was an error. */
		lp->stats.rx_errors++;        /* Update the error stats. */
		if (status & (RD_RF | RD_TL)) lp->stats.rx_frame_errors++;
		if (status & RD_CE)           lp->stats.rx_crc_errors++;
		if (status & RD_OF)           lp->stats.rx_fifo_errors++;
		if (status & RD_TL)           lp->stats.rx_length_errors++;
		if (status & RD_RF)           lp->pktStats.rx_runt_frames++;
		if (status & RD_CS)           lp->pktStats.rx_collision++;
		if (status & RD_DB)           lp->pktStats.rx_dribble++;
		if (status & RD_OF)           lp->pktStats.rx_overflow++;
	    } else {                          /* A valid frame received */
		struct sk_buff *skb;
		short pkt_len = (short)(lp->rx_ring[entry].status >> 16) - 4;
		
		if ((skb = de4x5_alloc_rx_buff(dev, entry, pkt_len)) == NULL) {
		    printk("%s: Insufficient memory; nuking packet.\n", 
			                                            dev->name);
		    lp->stats.rx_dropped++;   /* Really, deferred. */
		    break;
		}
		de4x5_dbg_rx(skb, pkt_len);

	/* Push up the protocol stack */
		skb->protocol=eth_type_trans(skb,dev);
		netif_rx(skb);
		    
		/* Update stats */
		lp->stats.rx_packets++;
		for (i=1; i<DE4X5_PKT_STAT_SZ-1; i++) {
		    if (pkt_len < (i*DE4X5_PKT_BIN_SZ)) {
			lp->pktStats.bins[i]++;
			i = DE4X5_PKT_STAT_SZ;
		    }
		}
		buf = skb->data;              /* Look at the dest addr */
		if (buf[0] & 0x01) {          /* Multicast/Broadcast */
		    if ((*(s32 *)&buf[0] == -1) && (*(s16 *)&buf[4] == -1)) {
			lp->pktStats.broadcast++;
		    } else {
			lp->pktStats.multicast++;
		    }
		} else if ((*(s32 *)&buf[0] == *(s32 *)&dev->dev_addr[0]) &&
			   (*(s16 *)&buf[4] == *(s16 *)&dev->dev_addr[4])) {
		    lp->pktStats.unicast++;
		}
		
		lp->pktStats.bins[0]++;       /* Duplicates stats.rx_packets */
		if (lp->pktStats.bins[0] == 0) { /* Reset counters */
		    memset((char *)&lp->pktStats, 0, sizeof(lp->pktStats));
		}
	    }
	    
	    /* Change buffer ownership for this frame, back to the adapter */
	    for (;lp->rx_old!=entry;lp->rx_old=(++lp->rx_old)%lp->rxRingSize) {
		lp->rx_ring[lp->rx_old].status = R_OWN;
		barrier();
	    }
	    lp->rx_ring[entry].status = R_OWN;
	    barrier();
	}
	
	/*
	** Update entry information
	*/
	lp->rx_new = (++lp->rx_new) % lp->rxRingSize;
    }
    
    return 0;
}

/*
** Buffer sent - check for TX buffer errors.
*/
static int
de4x5_tx(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    int entry;
    s32 status;
    
    for (entry = lp->tx_old; entry != lp->tx_new; entry = lp->tx_old) {
	status = lp->tx_ring[entry].status;
	if (status < 0) {                     /* Buffer not sent yet */
	    break;
	} else if (status != 0x7fffffff) {    /* Not setup frame */
	    if (status & TD_ES) {             /* An error happened */
		lp->stats.tx_errors++; 
		if (status & TD_NC) lp->stats.tx_carrier_errors++;
		if (status & TD_LC) lp->stats.tx_window_errors++;
		if (status & TD_UF) lp->stats.tx_fifo_errors++;
		if (status & TD_LC) lp->stats.collisions++;
		if (status & TD_EC) lp->pktStats.excessive_collisions++;
		if (status & TD_DE) lp->stats.tx_aborted_errors++;
	    
		if (status & (TD_LO | TD_NC | TD_EC | TD_LF)) {
		    lp->lostMedia++;
		}
		if (TX_PKT_PENDING) {
		    outl(POLL_DEMAND, DE4X5_TPD);/* Restart a stalled TX */
		}
	    } else {                      /* Packet sent */
		lp->stats.tx_packets++;
		lp->lostMedia = 0;        /* Remove transient problem */
		lp->linkOK++;
	    }
	    /* Free the buffer. */
	    if (lp->tx_skb[entry] != NULL) {
		dev_kfree_skb(lp->tx_skb[entry], FREE_WRITE);
		lp->tx_skb[entry] = NULL;
	    }
	}
	
	/* Update all the pointers */
	lp->tx_old = (++lp->tx_old) % lp->txRingSize;
    }

    if (TX_BUFFS_AVAIL && dev->tbusy) {  /* Any resources available? */
	dev->tbusy = 0;                  /* Clear TX busy flag */
	if (dev->interrupt) mark_bh(NET_BH);
    }
	
    return 0;
}

static int
de4x5_ast(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int next_tick = DE4X5_AUTOSENSE_MS;
    
    disable_ast(dev);
    
    if (lp->chipset == DC21140) {
	next_tick = dc21140m_autoconf(dev);
    } else if (lp->chipset == DC21041) {
	next_tick = dc21041_autoconf(dev);
    } else if (lp->chipset == DC21040) {
	next_tick = dc21040_autoconf(dev);
    }
    lp->linkOK = 0;
    enable_ast(dev, next_tick);
    
    return 0;
}

static int
de4x5_txur(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int iobase = dev->base_addr;
    int omr;

    omr = inl(DE4X5_OMR);
    if (!(omr & OMR_SF) || (lp->chipset==DC21041) || (lp->chipset==DC21040)) {
	omr &= ~(OMR_ST|OMR_SR);
	outl(omr, DE4X5_OMR);
	while (inl(DE4X5_STS) & STS_TS);
	if ((omr & OMR_TR) < OMR_TR) {
	    omr += 0x4000;
	} else {
	    omr |= OMR_SF;
	}
	outl(omr | OMR_ST | OMR_SR, DE4X5_OMR);
    }
    
    return 0;
}

static int
de4x5_close(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    s32 imr, omr;
    
    disable_ast(dev);
    dev->start = 0;
    dev->tbusy = 1;
    
    if (de4x5_debug > 1) {
	printk("%s: Shutting down ethercard, status was %8.8x.\n",
	       dev->name, inl(DE4X5_STS));
    }
    
    /* 
    ** We stop the DE4X5 here... mask interrupts and stop TX & RX
    */
    DISABLE_IRQs;
    STOP_DE4X5;
    
    /* Free the associated irq */
    free_irq(dev->irq, dev);
    lp->state = CLOSED;

    /* Free any socket buffers */
    de4x5_free_rx_buffs(dev);
    de4x5_free_tx_buffs(dev);
    
    MOD_DEC_USE_COUNT;
    
    /* Put the adapter to sleep to save power */
    if (lp->chipset == DC21041) {
	outl(0, DE4X5_SICR);
	outl(CFDA_PSM, PCI_CFDA);
    }
    
    return 0;
}

static struct enet_statistics *
de4x5_get_stats(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    
    lp->stats.rx_missed_errors = (int)(inl(DE4X5_MFC) & (MFC_OVFL | MFC_CNTR));
    
    return &lp->stats;
}

static void
load_packet(struct device *dev, char *buf, u32 flags, struct sk_buff *skb)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    
    lp->tx_ring[lp->tx_new].buf = virt_to_bus(buf);
    lp->tx_ring[lp->tx_new].des1 &= TD_TER;
    lp->tx_ring[lp->tx_new].des1 |= flags;
    lp->tx_skb[lp->tx_new] = skb;
    barrier();
    lp->tx_ring[lp->tx_new].status = T_OWN;
    barrier();
    
    return;
}

/*
** Set or clear the multicast filter for this adaptor.
*/
static void
set_multicast_list(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    
    /* First, double check that the adapter is open */
    if (lp->state == OPEN) {
	if (dev->flags & IFF_PROMISC) {         /* set promiscuous mode */
	    u32 omr;
	    omr = inl(DE4X5_OMR);
	    omr |= OMR_PR;
	    outl(omr, DE4X5_OMR);
	} else { 
	    SetMulticastFilter(dev);
	    load_packet(dev, lp->setup_frame, TD_IC | PERFECT_F | TD_SET | 
			                                SETUP_FRAME_LEN, NULL);
	    
	    lp->tx_new = (++lp->tx_new) % lp->txRingSize;
	    outl(POLL_DEMAND, DE4X5_TPD);       /* Start the TX */
	    dev->trans_start = jiffies;
	}
    }
    
    return;
}

/*
** Calculate the hash code and update the logical address filter
** from a list of ethernet multicast addresses.
** Little endian crc one liner from Matt Thomas, DEC.
*/
static void
SetMulticastFilter(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    struct dev_mc_list *dmi=dev->mc_list;
    u_long iobase = dev->base_addr;
    int i, j, bit, byte;
    u16 hashcode;
    u32 omr, crc, poly = CRC_POLYNOMIAL_LE;
    char *pa;
    unsigned char *addrs;
    
    omr = inl(DE4X5_OMR);
    omr &= ~(OMR_PR | OMR_PM);
    pa = build_setup_frame(dev, ALL);        /* Build the basic frame */
    
    if ((dev->flags & IFF_ALLMULTI) || (dev->mc_count > 14)) {
	omr |= OMR_PM;                       /* Pass all multicasts */
    } else if (lp->setup_f == HASH_PERF) {   /* Hash Filtering */
	for (i=0;i<dev->mc_count;i++) {      /* for each address in the list */
	    addrs=dmi->dmi_addr;
	    dmi=dmi->next;
	    if ((*addrs & 0x01) == 1) {      /* multicast address? */ 
		crc = 0xffffffff;            /* init CRC for each address */
		for (byte=0;byte<ETH_ALEN;byte++) {/* for each address byte */
		                             /* process each address bit */ 
		    for (bit = *addrs++,j=0;j<8;j++, bit>>=1) {
			crc = (crc >> 1) ^ (((crc ^ bit) & 0x01) ? poly : 0);
		    }
		}
		hashcode = crc & HASH_BITS;  /* hashcode is 9 LSb of CRC */
		
		byte = hashcode >> 3;        /* bit[3-8] -> byte in filter */
		bit = 1 << (hashcode & 0x07);/* bit[0-2] -> bit in byte */
		
		byte <<= 1;                  /* calc offset into setup frame */
		if (byte & 0x02) {
		    byte -= 1;
		}
		lp->setup_frame[byte] |= bit;
	    }
	}
    } else {                                 /* Perfect filtering */
	for (j=0; j<dev->mc_count; j++) {
	    addrs=dmi->dmi_addr;
	    dmi=dmi->next;
	    for (i=0; i<ETH_ALEN; i++) { 
		*(pa + (i&1)) = *addrs++;
		if (i & 0x01) pa += 4;
	    }
	}
    }
    outl(omr, DE4X5_OMR);
    
    return;
}

/*
** EISA bus I/O device probe. Probe from slot 1 since slot 0 is usually
** the motherboard. Upto 15 EISA devices are supported.
*/
static void
eisa_probe(struct device *dev, u_long ioaddr)
{
    int i, maxSlots, status;
    u_short vendor, device;
    s32 cfid;
    u_long iobase;
    struct bus_type *lp = &bus;
    char name[DE4X5_STRLEN];
    struct device *tmp;
    
    if (!ioaddr && autoprobed) return;     /* Been here before ! */
    
    lp->bus = EISA;
    
    if (ioaddr == 0) {                     /* Autoprobing */
	iobase = EISA_SLOT_INC;            /* Get the first slot address */
	i = 1;
	maxSlots = MAX_EISA_SLOTS;
    } else {                               /* Probe a specific location */
	iobase = ioaddr;
	i = (ioaddr >> 12);
	maxSlots = i + 1;
    }
    
    for (status = -ENODEV; (i<maxSlots) && (dev!=NULL); i++, iobase+=EISA_SLOT_INC) {
	if (EISA_signature(name, EISA_ID)) {
	    cfid = inl(PCI_CFID);
	    device = (u_short)(cfid >> 16);
	    vendor = (u_short) cfid;
	    
	    lp->chipset = device;
	    DevicePresent(EISA_APROM);
	    /* Write the PCI Configuration Registers */
	    outl(PCI_COMMAND_IO | PCI_COMMAND_MASTER, PCI_CFCS);
	    outl(0x00006000, PCI_CFLT);
	    outl(iobase, PCI_CBIO);
	    
	    if (check_region(iobase, DE4X5_EISA_TOTAL_SIZE) == 0) {
		if ((tmp = alloc_device(dev, iobase)) != NULL) {
		    if ((status = de4x5_hw_init(tmp, iobase)) == 0) {
			num_de4x5s++;
		    }
		}
	    } else if (autoprobed) {
		printk("%s: region already allocated at 0x%04lx.\n", dev->name,iobase);
	    }
	}
    }
    
    return;
}

/*
** PCI bus I/O device probe
** NB: PCI I/O accesses and Bus Mastering are enabled by the PCI BIOS, not
** the driver. Some PCI BIOS's, pre V2.1, need the slot + features to be
** enabled by the user first in the set up utility. Hence we just check for
** enabled features and silently ignore the card if they're not.
**
** STOP PRESS: Some BIOS's __require__ the driver to enable the bus mastering
** bit. Here, check for I/O accesses and then set BM. If you put the card in
** a non BM slot, you're on your own (and complain to the PC vendor that your
** PC doesn't conform to the PCI standard)!
*/
#define PCI_DEVICE    (dev_num << 3)
#define PCI_LAST_DEV  32

static void
pci_probe(struct device *dev, u_long ioaddr)
{
    u_char irq;
    u_char pb, pbus, dev_num, dnum, dev_fn;
    u_short vendor, device, index, status;
    u_int class = DE4X5_CLASS_CODE;
    u_int iobase;
    struct bus_type *lp = &bus;
    struct device *tmp;

    if ((!ioaddr || !loading_module) && autoprobed) return;
    
    if (!pcibios_present()) return;          /* No PCI bus in this machine! */
    
    lp->bus = PCI;
    
    if ((ioaddr < 0x1000) && loading_module) {
	pbus = (u_short)(ioaddr >> 8);
	dnum = (u_short)(ioaddr & 0xff);
    } else {
	pbus = 0;
	dnum = 0;
    }

    for (index=0; 
	 (pcibios_find_class(class, index, &pb, &dev_fn)!= PCIBIOS_DEVICE_NOT_FOUND);
	 index++) {
	dev_num = PCI_SLOT(dev_fn);
	
	if ((!pbus && !dnum) || ((pbus == pb) && (dnum == dev_num))) {
	    pcibios_read_config_word(pb, PCI_DEVICE, PCI_VENDOR_ID, &vendor);
	    pcibios_read_config_word(pb, PCI_DEVICE, PCI_DEVICE_ID, &device);
	    if (!(is_DC21040 || is_DC21041 || is_DC21140)) continue;

	    /* Set the device number information */
	    lp->device = dev_num;
	    lp->bus_num = pb;
	    
	    /* Set the chipset information */
	    lp->chipset = device;
	    
	    /* Get the board I/O address */
	    pcibios_read_config_dword(pb, PCI_DEVICE, PCI_BASE_ADDRESS_0, &iobase);
	    iobase &= CBIO_MASK;

	    /* Fetch the IRQ to be used */
	    pcibios_read_config_byte(pb, PCI_DEVICE, PCI_INTERRUPT_LINE, &irq);
	    if ((irq == 0) || (irq == (u_char) 0xff)) continue;
	    
	    /* Check if I/O accesses and Bus Mastering are enabled */
	    pcibios_read_config_word(pb, PCI_DEVICE, PCI_COMMAND, &status);
	    if (!(status & PCI_COMMAND_IO)) continue;
	    if (!(status & PCI_COMMAND_MASTER)) {
		status |= PCI_COMMAND_MASTER;
		pcibios_write_config_word(pb, PCI_DEVICE, PCI_COMMAND, status);
		pcibios_read_config_word(pb, PCI_DEVICE, PCI_COMMAND, &status);
	    }
	    if (!(status & PCI_COMMAND_MASTER)) continue;

	    DevicePresent(DE4X5_APROM);
	    if (check_region(iobase, DE4X5_PCI_TOTAL_SIZE) == 0) {
		if ((tmp = alloc_device(dev, iobase)) != NULL) {
		    tmp->irq = irq;
		    if ((status = de4x5_hw_init(tmp, iobase)) == 0) {
			num_de4x5s++;
		    }
		}
	    } else if (autoprobed) {
		printk("%s: region already allocated at 0x%04x.\n", dev->name, 
		       (u_short)iobase);
	    }
	}
    }
    
    return;
}

/*
** Search the entire 'eth' device list for a fixed probe. If a match isn't
** found then check for an autoprobe or unused device location. If they
** are not available then insert a new device structure at the end of
** the current list.
*/
static struct device *
alloc_device(struct device *dev, u_long iobase)
{
    struct device *adev = NULL;
    int fixed = 0, new_dev = 0;

    num_eth = de4x5_dev_index(dev->name);
    if (loading_module) return dev;
    
    while (1) {
	if (((dev->base_addr == DE4X5_NDA) || (dev->base_addr==0)) && !adev) {
	    adev=dev;
	} else if ((dev->priv == NULL) && (dev->base_addr==iobase)) {
	    fixed = 1;
	} else {
	    if (dev->next == NULL) {
		new_dev = 1;
	    } else if (strncmp(dev->next->name, "eth", 3) != 0) {
		new_dev = 1;
	    }
	}
	if ((dev->next == NULL) || new_dev || fixed) break;
	dev = dev->next;
	num_eth++;
    }
    if (adev && !fixed) {
	dev = adev;
	num_eth = de4x5_dev_index(dev->name);
	new_dev = 0;
    }

    if (((dev->next == NULL) &&  
	((dev->base_addr != DE4X5_NDA) && (dev->base_addr != 0)) && !fixed) ||
	new_dev) {
	num_eth++;                         /* New device */
	dev = insert_device(dev, iobase, de4x5_probe);
    }
    
    return dev;
}

/*
** If at end of eth device list and can't use current entry, malloc
** one up. If memory could not be allocated, print an error message.
*/
static struct device *
insert_device(struct device *dev, u_long iobase, int (*init)(struct device *))
{
    struct device *new;

    new = (struct device *)kmalloc(sizeof(struct device)+8, GFP_KERNEL);
    if (new == NULL) {
	printk("eth%d: Device not initialised, insufficient memory\n",num_eth);
	return NULL;
    } else {
	new->next = dev->next;
	dev->next = new;
	dev = dev->next;               /* point to the new device */
	dev->name = (char *)(dev + 1);
	if (num_eth > 9999) {
	    sprintf(dev->name,"eth????");/* New device name */
	} else {
	    sprintf(dev->name,"eth%d", num_eth);/* New device name */
	}
	dev->base_addr = iobase;       /* assign the io address */
	dev->init = init;              /* initialisation routine */
    }

    return dev;
}

static int
de4x5_dev_index(char *s)
{
    int i=0, j=0;

    for (;*s; s++) {
	if (isdigit(*s)) {
	    j=1;
	    i = (i * 10) + (*s - '0');
	} else if (j) break;
    }

    return i;
}

/*
** Auto configure the media here rather than setting the port at compile
** time. This routine is called by de4x5_init() and when a loss of media is
** detected (excessive collisions, loss of carrier, no carrier or link fail
** [TP] or no recent receive activity) to check whether the user has been 
** sneaky and changed the port on us.
*/
static int
autoconf_media(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    int next_tick = DE4X5_AUTOSENSE_MS;;
    
    lp->linkOK = 0;
    lp->c_media = AUTO;                     /* Bogus last media */
    disable_ast(dev);
    inl(DE4X5_MFC);                         /* Zero the lost frames counter */
    lp->media = INIT;
    if (lp->chipset == DC21040) {
	next_tick = dc21040_autoconf(dev);
    } else if (lp->chipset == DC21041) {
	next_tick = dc21041_autoconf(dev);
    } else if (lp->chipset == DC21140) {
	next_tick = dc21140m_autoconf(dev);
    }
    enable_ast(dev, next_tick);
    
    return (lp->media);
}

/*
** Autoconfigure the media when using the DC21040. AUI cannot be distinguished
** from BNC as the port has a jumper to set thick or thin wire. When set for
** BNC, the BNC port will indicate activity if it's not terminated correctly.
** The only way to test for that is to place a loopback packet onto the
** network and watch for errors. Since we're messing with the interrupt mask
** register, disable the board interrupts and do not allow any more packets to
** be queued to the hardware. Re-enable everything only when the media is
** found.
** I may have to "age out" locally queued packets so that the higher layer
** timeouts don't effectively duplicate packets on the network.
*/
static int
dc21040_autoconf(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    int next_tick = DE4X5_AUTOSENSE_MS;
    s32 imr;
    
    switch (lp->media) {
      case INIT:
	DISABLE_IRQs;
	lp->tx_enable = NO;
	lp->timeout = -1;
	de4x5_save_skbs(dev);
	if ((lp->autosense == AUTO) || (lp->autosense == TP)) {
	    lp->media = TP;
	} else if ((lp->autosense == BNC) || (lp->autosense == AUI) || (lp->autosense == BNC_AUI)) {
	    lp->media = BNC_AUI;
	} else if (lp->autosense == EXT_SIA) {
	    lp->media = EXT_SIA;
	} else {
	    lp->media = NC;
	}
	lp->local_state = 0;
	next_tick = dc21040_autoconf(dev);
	break;
	
      case TP:
	next_tick = dc21040_state(dev, 0x8f01, 0xffff, 0x0000, 3000, BNC_AUI, 
		                                         TP_SUSPECT, test_tp);
	break;
	
      case TP_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, TP, test_tp, dc21040_autoconf);
	break;
	
      case BNC:
      case AUI:
      case BNC_AUI:
	next_tick = dc21040_state(dev, 0x8f09, 0x0705, 0x0006, 3000, EXT_SIA, 
		                                  BNC_AUI_SUSPECT, ping_media);
	break;
	
      case BNC_AUI_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, BNC_AUI, ping_media, dc21040_autoconf);
	break;
	
      case EXT_SIA:
	next_tick = dc21040_state(dev, 0x3041, 0x0000, 0x0006, 3000, 
		                              NC, EXT_SIA_SUSPECT, ping_media);
	break;
	
      case EXT_SIA_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, EXT_SIA, ping_media, dc21040_autoconf);
	break;
	
      case NC:
#ifndef __alpha__
	reset_init_sia(dev, 0x8f01, 0xffff, 0x0000);
#else
	/* JAE: for Alpha, default to BNC/AUI, *not* TP */
	reset_init_sia(dev, 0x8f09, 0x0705, 0x0006);
#endif  /* i386 */
	if (lp->media != lp->c_media) {
	    de4x5_dbg_media(dev);
	    lp->c_media = lp->media;
	}
	lp->media = INIT;
	lp->tx_enable = NO;
	break;
    }
    
    return next_tick;
}

static int
dc21040_state(struct device *dev, int csr13, int csr14, int csr15, int timeout,
	      int next_state, int suspect_state, 
	      int (*fn)(struct device *, int))
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int next_tick = DE4X5_AUTOSENSE_MS;
    int linkBad;

    switch (lp->local_state) {
      case 0:
	reset_init_sia(dev, csr13, csr14, csr15);
	lp->local_state++;
	next_tick = 500;
	break;
	    
      case 1:
	if (!lp->tx_enable) {
	    linkBad = fn(dev, timeout);
	    if (linkBad < 0) {
		next_tick = linkBad & ~TIMER_CB;
	    } else {
		if (linkBad && (lp->autosense == AUTO)) {
		    lp->local_state = 0;
		    lp->media = next_state;
		} else {
		    de4x5_init_connection(dev);
		}
	    }
	} else if (!lp->linkOK && (lp->autosense == AUTO)) {
	    lp->media = suspect_state;
	    next_tick = 3000;
	}
	break;
    }
    
    return next_tick;
}

static int
de4x5_suspect_state(struct device *dev, int timeout, int prev_state,
		      int (*fn)(struct device *, int),
		      int (*asfn)(struct device *))
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int next_tick = DE4X5_AUTOSENSE_MS;
    int linkBad;

    switch (lp->local_state) {
      case 1:
	if (lp->linkOK && !LOST_MEDIA) {
	    lp->media = prev_state;
	} else {
	    lp->local_state++;
	    next_tick = asfn(dev);
	}
	break;

      case 2:
	linkBad = fn(dev, timeout);
	if (linkBad < 0) {
	    next_tick = linkBad & ~TIMER_CB;
	} else if (!linkBad) {
	    lp->local_state--;
	    lp->media = prev_state;
	} else {
	    lp->media = INIT;
	}
    }

    return next_tick;
}

/*
** Autoconfigure the media when using the DC21041. AUI needs to be tested
** before BNC, because the BNC port will indicate activity if it's not
** terminated correctly. The only way to test for that is to place a loopback
** packet onto the network and watch for errors. Since we're messing with
** the interrupt mask register, disable the board interrupts and do not allow
** any more packets to be queued to the hardware. Re-enable everything only
** when the media is found.
*/
static int
dc21041_autoconf(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    s32 sts, irqs, irq_mask, imr, omr;
    int next_tick = DE4X5_AUTOSENSE_MS;
    
    switch (lp->media) {
      case INIT:
	DISABLE_IRQs;
	lp->tx_enable = NO;
	lp->timeout = -1;
	de4x5_save_skbs(dev);          /* Save non transmitted skb's */
	if ((lp->autosense == AUTO) || (lp->autosense == TP_NW)) {
	    lp->media = TP;            /* On chip auto negotiation is broken */
	} else if (lp->autosense == TP) {
	    lp->media = TP;
	} else if (lp->autosense == BNC) {
	    lp->media = BNC;
	} else if (lp->autosense == AUI) {
	    lp->media = AUI;
	} else {
	    lp->media = NC;
	}
	lp->local_state = 0;
	next_tick = dc21041_autoconf(dev);
	break;
	
      case TP_NW:
	if (lp->timeout < 0) {
	    omr = inl(DE4X5_OMR);/* Set up full duplex for the autonegotiate */
	    outl(omr | OMR_FD, DE4X5_OMR);
	}
	irqs = STS_LNF | STS_LNP;
	irq_mask = IMR_LFM | IMR_LPM;
	sts = test_media(dev, irqs, irq_mask, 0xef01, 0xffff, 0x0008, 2400);
	if (sts < 0) {
	    next_tick = sts & ~TIMER_CB;
	} else {
	    if (sts & STS_LNP) {
		lp->media = ANS;
	    } else {
		lp->media = AUI;
	    }
	    next_tick = dc21041_autoconf(dev);
	}
	break;
	
      case ANS:
	if (!lp->tx_enable) {
	    irqs = STS_LNP;
	    irq_mask = IMR_LPM;
	    sts = test_ans(dev, irqs, irq_mask, 3000);
	    if (sts < 0) {
		next_tick = sts & ~TIMER_CB;
	    } else {
		if (!(sts & STS_LNP) && (lp->autosense == AUTO)) {
		    lp->media = TP;
		    next_tick = dc21041_autoconf(dev);
		} else {
		    lp->local_state = 1;
		    de4x5_init_connection(dev);
		}
	    }
	} else if (!lp->linkOK && (lp->autosense == AUTO)) {
	    lp->media = ANS_SUSPECT;
	    next_tick = 3000;
	}
	break;
	
      case ANS_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, ANS, test_tp, dc21041_autoconf);
	break;
	
      case TP:
	if (!lp->tx_enable) {
	    if (lp->timeout < 0) {
		omr = inl(DE4X5_OMR);          /* Set up half duplex for TP */
		outl(omr & ~OMR_FD, DE4X5_OMR);
	    }
	    irqs = STS_LNF | STS_LNP;
	    irq_mask = IMR_LFM | IMR_LPM;
	    sts = test_media(dev,irqs, irq_mask, 0xef01, 0xff3f, 0x0008, 2400);
	    if (sts < 0) {
		next_tick = sts & ~TIMER_CB;
	    } else {
		if (!(sts & STS_LNP) && (lp->autosense == AUTO)) {
		    if (inl(DE4X5_SISR) & SISR_NRA) {
			lp->media = AUI;       /* Non selected port activity */
		    } else {
			lp->media = BNC;
		    }
		    next_tick = dc21041_autoconf(dev);
		} else {
		    lp->local_state = 1;
		    de4x5_init_connection(dev);
		}
	    }
	} else if (!lp->linkOK && (lp->autosense == AUTO)) {
	    lp->media = TP_SUSPECT;
	    next_tick = 3000;
	}
	break;
	
      case TP_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, TP, test_tp, dc21041_autoconf);
	break;
	
      case AUI:
	if (!lp->tx_enable) {
	    if (lp->timeout < 0) {
		omr = inl(DE4X5_OMR);          /* Set up half duplex for AUI */
		outl(omr & ~OMR_FD, DE4X5_OMR);
	    }
	    irqs = 0;
	    irq_mask = 0;
	    sts = test_media(dev,irqs, irq_mask, 0xef09, 0xf73d, 0x000e, 1000);
	    if (sts < 0) {
		next_tick = sts & ~TIMER_CB;
	    } else {
		if (!(inl(DE4X5_SISR) & SISR_SRA) && (lp->autosense == AUTO)) {
		    lp->media = BNC;
		    next_tick = dc21041_autoconf(dev);
		} else {
		    lp->local_state = 1;
		    de4x5_init_connection(dev);
		}
	    }
	} else if (!lp->linkOK && (lp->autosense == AUTO)) {
	    lp->media = AUI_SUSPECT;
	    next_tick = 3000;
	}
	break;
	
      case AUI_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, AUI, ping_media, dc21041_autoconf);
	break;
	
      case BNC:
	switch (lp->local_state) {
	  case 0:
	    if (lp->timeout < 0) {
		omr = inl(DE4X5_OMR);          /* Set up half duplex for BNC */
		outl(omr & ~OMR_FD, DE4X5_OMR);
	    }
	    irqs = 0;
	    irq_mask = 0;
	    sts = test_media(dev,irqs, irq_mask, 0xef09, 0xf73d, 0x0006, 1000);
	    if (sts < 0) {
		next_tick = sts & ~TIMER_CB;
	    } else {
		if (!(inl(DE4X5_SISR) & SISR_SRA) && (lp->autosense == AUTO)) {
		    lp->media = NC;
		} else {
		    lp->local_state++;         /* Ensure media connected */
		    next_tick = dc21041_autoconf(dev);
		}
	    }
	    break;
	    
	  case 1:
	    if (!lp->tx_enable) {
		if ((sts = ping_media(dev, 3000)) < 0) {
		    next_tick = sts & ~TIMER_CB;
		} else {
		    if (sts) {
			lp->local_state = 0;
			lp->media = NC;
		    } else {
			de4x5_init_connection(dev);
		    }
		}
	    } else if (!lp->linkOK && (lp->autosense == AUTO)) {
		lp->media = BNC_SUSPECT;
		next_tick = 3000;
	    }
	    break;
	}
	break;
	
      case BNC_SUSPECT:
	next_tick = de4x5_suspect_state(dev, 1000, BNC, ping_media, dc21041_autoconf);
	break;
	
      case NC:
	omr = inl(DE4X5_OMR);    /* Set up full duplex for the autonegotiate */
	outl(omr | OMR_FD, DE4X5_OMR);
	reset_init_sia(dev, 0xef01, 0xffff, 0x0008);/* Initialise the SIA */
	if (lp->media != lp->c_media) {
	    de4x5_dbg_media(dev);
	    lp->c_media = lp->media;
	}
	lp->media = INIT;
	lp->tx_enable = NO;
	break;
    }
    
    return next_tick;
}

/*
** Some autonegotiation chips are broken in that they do not return the
** acknowledge bit (anlpa & MII_ANLPA_ACK) in the link partner advertisement
** register, except at the first power up negotiation.
*/
static int
dc21140m_autoconf(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int ana, anlpa, cap, cr, slnk, sr, iobase = dev->base_addr;
    int next_tick = DE4X5_AUTOSENSE_MS;
    u_long imr, omr;
    
    switch(lp->media) {
      case INIT: 
	DISABLE_IRQs;
	lp->tx_enable = FALSE;
	lp->timeout = -1;
	if ((next_tick = de4x5_reset_phy(dev)) < 0) {
	    next_tick &= ~TIMER_CB;
	} else {
	    de4x5_save_skbs(dev);          /* Save non transmitted skb's */
	    lp->tmp = MII_SR_ASSC;         /* Fake out the MII speed set */
	    SET_10Mb;
	    if (lp->autosense == _100Mb) {
		lp->media = _100Mb;
	    } else if (lp->autosense == _10Mb) {
		lp->media = _10Mb;
	    } else if ((lp->autosense == AUTO) && 
		                     ((sr=is_anc_capable(dev)) & MII_SR_ANC)) {
		ana = (((sr >> 6) & MII_ANA_TAF) | MII_ANA_CSMA);
		ana &= (de4x5_full_duplex ? ~0 : ~MII_ANA_FDAM);
		mii_wr(ana, MII_ANA, lp->phy[lp->active].addr, DE4X5_MII);
		lp->media = ANS;
	    } else if (lp->autosense == AUTO) {
		lp->media = SPD_DET;
	    } else if (is_spd_100(dev) && is_100_up(dev)) {
		lp->media = _100Mb;
	    } else {
		lp->media = NC;
	    }
	    lp->local_state = 0;
	    next_tick = dc21140m_autoconf(dev);
	}
	break;
	
      case ANS:
	switch (lp->local_state) {
	  case 0:
	    if (lp->timeout < 0) {
		mii_wr(MII_CR_ASSE | MII_CR_RAN, MII_CR, lp->phy[lp->active].addr, DE4X5_MII);
	    }
	    cr = test_mii_reg(dev, MII_CR, MII_CR_RAN, FALSE, 500);
	    if (cr < 0) {
		next_tick = cr & ~TIMER_CB;
	    } else {
		if (cr) {
		    lp->local_state = 0;
		    lp->media = SPD_DET;
		} else {
		    lp->local_state++;
		}
		next_tick = dc21140m_autoconf(dev);
	    }
	    break;
	    
	  case 1:
	    if ((sr=test_mii_reg(dev, MII_SR, MII_SR_ASSC, TRUE, 2000)) < 0) {
		next_tick = sr & ~TIMER_CB;
	    } else {
		lp->media = SPD_DET;
		lp->local_state = 0;
		if (sr) {                         /* Success! */
		    lp->tmp = MII_SR_ASSC;
		    anlpa = mii_rd(MII_ANLPA, lp->phy[lp->active].addr, DE4X5_MII);
		    ana = mii_rd(MII_ANA, lp->phy[lp->active].addr, DE4X5_MII);
		    if (!(anlpa & MII_ANLPA_RF) && 
			 (cap = anlpa & MII_ANLPA_TAF & ana)) {
			if (cap & MII_ANA_100M) {
			    de4x5_full_duplex = ((ana & anlpa & MII_ANA_FDAM & MII_ANA_100M) ? TRUE : FALSE);
			    lp->media = _100Mb;
			} else if (cap & MII_ANA_10M) {
			    de4x5_full_duplex = ((ana & anlpa & MII_ANA_FDAM & MII_ANA_10M) ? TRUE : FALSE);

			    lp->media = _10Mb;
			}
		    }
		}                       /* Auto Negotiation failed to finish */
		next_tick = dc21140m_autoconf(dev);
	    }                           /* Auto Negotiation failed to start */
	    break;
	}
	break;
	
      case SPD_DET:                              /* Choose 10Mb/s or 100Mb/s */
        if (lp->timeout < 0) {
	    lp->tmp = (lp->phy[lp->active].id ? MII_SR_LKS :
		                                  (~inl(DE4X5_GEP) & GEP_LNP));
	    SET_100Mb_PDET;
	}
        if ((slnk = test_sym_link(dev, 6200)) < 0) {
	    next_tick = slnk & ~TIMER_CB;
	} else {
	    if (is_spd_100(dev) && is_100_up(dev)) {
		lp->media = _100Mb;
	    } else if ((!is_spd_100(dev) && (is_10_up(dev) & lp->tmp))) {
		lp->media = _10Mb;
	    } else {
		lp->media = NC;
	    }
	    next_tick = dc21140m_autoconf(dev);
	}
	break;
	
      case _100Mb:                               /* Set 100Mb/s */
	next_tick = 3000;
	if (!lp->tx_enable) {
	    SET_100Mb;
	    de4x5_init_connection(dev);
	} else {
	    if (!lp->linkOK && (lp->autosense == AUTO)) {
		if (!(is_spd_100(dev) && is_100_up(dev))) {
		    lp->media = INIT;
		    next_tick = DE4X5_AUTOSENSE_MS;
		}
	    }
	}
	break;
	
      case _10Mb:                                /* Set 10Mb/s */
	next_tick = 3000;
	if (!lp->tx_enable) {
	    SET_10Mb;
	    de4x5_init_connection(dev);
	} else {
	    if (!lp->linkOK && (lp->autosense == AUTO)) {
		if (!(!is_spd_100(dev) && is_10_up(dev))) {
		    lp->media = INIT;
		    next_tick = DE4X5_AUTOSENSE_MS;
		}
	    }
	}
	break;
	
      case NC:
        if (lp->media != lp->c_media) {
	    de4x5_dbg_media(dev);
	    lp->c_media = lp->media;
	}
	lp->media = INIT;
	lp->tx_enable = FALSE;
	break;
    }
    
    return next_tick;
}

static void
de4x5_init_connection(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;

    if (lp->media != lp->c_media) {
        de4x5_dbg_media(dev);
	lp->c_media = lp->media;          /* Stop scrolling media messages */
    }
    de4x5_restore_skbs(dev);
    cli();
    de4x5_rx(dev);
    de4x5_setup_intr(dev);
    lp->lostMedia = 0;
    lp->tx_enable = YES;
    dev->tbusy = 0;
    sti();
    outl(POLL_DEMAND, DE4X5_TPD);
    mark_bh(NET_BH);

    return;
}

static int
de4x5_reset_phy(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    int next_tick = 0;

    if (lp->phy[lp->active].id) {
	if (lp->timeout < 0) {
	    outl(GEP_HRST, DE4X5_GEP);           /* Hard RESET the PHY dev. */
	    udelay(1000);                        /* Assert for 1ms */
	    outl(0x00, DE4X5_GEP);
	    udelay(2000);                        /* Wait for 2ms */
	    mii_wr(MII_CR_RST, MII_CR, lp->phy[lp->active].addr, DE4X5_MII);
	}
	next_tick = test_mii_reg(dev, MII_CR, MII_CR_RST, FALSE, 500);
    }
    
    return next_tick;
}

static int
test_media(struct device *dev, s32 irqs, s32 irq_mask, s32 csr13, s32 csr14, s32 csr15, s32 msec)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    s32 sts, csr12;
    
    if (lp->timeout < 0) {
	lp->timeout = msec/100;
	reset_init_sia(dev, csr13, csr14, csr15);

	/* set up the interrupt mask */
	outl(irq_mask, DE4X5_IMR);

	/* clear all pending interrupts */
	sts = inl(DE4X5_STS);
	outl(sts, DE4X5_STS);
	
	/* clear csr12 NRA and SRA bits */
	if (lp->chipset == DC21041) {
	    csr12 = inl(DE4X5_SISR);
	    outl(csr12, DE4X5_SISR);
	}
    }
    
    sts = inl(DE4X5_STS) & ~TIMER_CB;
    
    if (!(sts & irqs) && --lp->timeout) {
	sts = 100 | TIMER_CB;
    } else {
	lp->timeout = -1;
    }
    
    return sts;
}

static int
test_tp(struct device *dev, s32 msec)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    int sisr;
    
    if (lp->timeout < 0) {
	lp->timeout = msec/100;
    }
    
    sisr = (inl(DE4X5_SISR) & ~TIMER_CB) & (SISR_LKF | SISR_NCR);

    if (sisr && --lp->timeout) {
	sisr = 100 | TIMER_CB;
    } else {
	lp->timeout = -1;
    }
    
    return sisr;
}

static int
test_sym_link(struct device *dev, int msec)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int iobase = dev->base_addr;
    int gep = 0;
    
    if (lp->timeout < 0) {
	lp->timeout = msec/100;
    }
    
    if (lp->phy[lp->active].id) {
	gep = ((is_100_up(dev) && is_spd_100(dev)) ? GEP_SLNK : 0);
    } else {
	gep = (~inl(DE4X5_GEP) & (GEP_SLNK | GEP_LNP));
    }
    if (!(gep & GEP_SLNK) && --lp->timeout) {
	gep = 100 | TIMER_CB;
    } else {
	lp->timeout = -1;
    }
    
    return gep;
}

/*
**
**
*/
static int
test_mii_reg(struct device *dev, int reg, int mask, int pol, long msec)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int test, iobase = dev->base_addr;
    
    if (lp->timeout < 0) {
	lp->timeout = msec/100;
    }
    
    if (pol) pol = ~0;
    reg = mii_rd((u_char)reg, lp->phy[lp->active].addr, DE4X5_MII) & mask;
    test = (reg ^ pol) & mask;
    
    if (test && --lp->timeout) {
	reg = 100 | TIMER_CB;
    } else {
	lp->timeout = -1;
    }
    
    return reg;
}

static int
is_spd_100(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    int spd;
    
    if (lp->phy[lp->active].id) {
	spd = mii_rd(lp->phy[lp->active].spd.reg, lp->phy[lp->active].addr, DE4X5_MII);
	spd = ~(spd ^ lp->phy[lp->active].spd.value);
	spd &= lp->phy[lp->active].spd.mask;
    } else {
	spd = ((~inl(DE4X5_GEP)) & GEP_SLNK);
    }
    
    return spd;
}

static int
is_100_up(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    
    if (lp->phy[lp->active].id) {
	/* Double read for sticky bits & temporary drops */
	mii_rd(MII_SR, lp->phy[lp->active].addr, DE4X5_MII);
	return (mii_rd(MII_SR, lp->phy[lp->active].addr, DE4X5_MII) & MII_SR_LKS);
    } else {
	return ((~inl(DE4X5_GEP)) & GEP_SLNK);
    }
}

static int
is_10_up(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    
    if (lp->phy[lp->active].id) {
	/* Double read for sticky bits & temporary drops */
	mii_rd(MII_SR, lp->phy[lp->active].addr, DE4X5_MII);
	return (mii_rd(MII_SR, lp->phy[lp->active].addr, DE4X5_MII) & MII_SR_LKS);
    } else {
	return ((~inl(DE4X5_GEP)) & GEP_LNP);
    }
}

static int
is_anc_capable(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    
    if (lp->phy[lp->active].id) {
	return (mii_rd(MII_SR, lp->phy[lp->active].addr, DE4X5_MII));
    } else {
	return 0;
    }
}

/*
** Send a packet onto the media and watch for send errors that indicate the
** media is bad or unconnected.
*/
static int
ping_media(struct device *dev, int msec)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    int sisr;
    
    if (lp->timeout < 0) {
	lp->timeout = msec/100;
	
	lp->tmp = lp->tx_new;                /* Remember the ring position */
	load_packet(dev, lp->frame, TD_LS | TD_FS | sizeof(lp->frame), NULL);
	lp->tx_new = (++lp->tx_new) % lp->txRingSize;
	outl(POLL_DEMAND, DE4X5_TPD);
    }
    
    sisr = inl(DE4X5_SISR);

    if ((!(sisr & SISR_NCR)) && (lp->tx_ring[lp->tmp].status < 0) && (--lp->timeout)) {
	sisr = 100 | TIMER_CB;
    } else {
	if ((!(sisr & SISR_NCR)) && 
	    !(lp->tx_ring[lp->tmp].status & (T_OWN | TD_ES)) && lp->timeout) {
	    sisr = 0;
	} else {
	    sisr = 1;
	}
	lp->timeout = -1;
    }
    
    return sisr;
}

/*
** This function does 2 things: on Intels it kmalloc's another buffer to
** replace the one about to be passed up. On Alpha's it kmallocs a buffer
** into which the packet is copied.
*/
static struct sk_buff *
de4x5_alloc_rx_buff(struct device *dev, int index, int len)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    struct sk_buff *p;

#if !defined(__alpha__) && !defined(DE4X5_DO_MEMCPY)
    struct sk_buff *ret;
    u_long i=0, tmp;

    p = dev_alloc_skb(IEEE802_3_SZ + ALIGN + 2);
    if (!p) return NULL;

    p->dev = dev;
    tmp = virt_to_bus(p->data);
    i = ((tmp + ALIGN) & ~ALIGN) - tmp;
    skb_reserve(p, i);
    lp->rx_ring[index].buf = tmp + i;

    ret = lp->rx_skb[index];
    lp->rx_skb[index] = p;

    if ((u_long) ret > 1) {
	skb_put(ret, len);
    }

    return ret;

#else
    if (lp->state != OPEN) return (struct sk_buff *)1; /* Fake out the open */

    p = dev_alloc_skb(len + 2);
    if (!p) return NULL;

    p->dev = dev;
    skb_reserve(p, 2);	                               /* Align */
    if (index < lp->rx_old) {                          /* Wrapped buffer */
	short tlen = (lp->rxRingSize - lp->rx_old) * RX_BUFF_SZ;
	memcpy(skb_put(p,tlen), bus_to_virt(lp->rx_ring[lp->rx_old].buf),tlen);
	memcpy(skb_put(p,len-tlen), bus_to_virt(lp->rx_ring[0].buf), len-tlen);
    } else {                                           /* Linear buffer */
	memcpy(skb_put(p,len), bus_to_virt(lp->rx_ring[lp->rx_old].buf),len);
    }
		    
    return p;
#endif
}

static void
de4x5_free_rx_buffs(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int i;

    for (i=0; i<lp->rxRingSize; i++) {
	if ((u_long) lp->rx_skb[i] > 1) {
	    dev_kfree_skb(lp->rx_skb[i], FREE_WRITE);
	}
	lp->rx_ring[i].status = 0;
	lp->rx_skb[i] = (struct sk_buff *)1;    /* Dummy entry */
    }

    return;
}

static void
de4x5_free_tx_buffs(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int i;

    for (i=0; i<lp->txRingSize; i++) {
	if (lp->tx_skb[i]) {
	    dev_kfree_skb(lp->tx_skb[i], FREE_WRITE);
	    lp->tx_skb[i] = NULL;
	}
	lp->tx_ring[i].status = 0;
    }

    /* Unload the locally queued packets */
    while (lp->cache.skb) {
	dev_kfree_skb(de4x5_get_cache(dev), FREE_WRITE);
    }

    return;
}

/*
** When a user pulls a connection, the DECchip can end up in a
** 'running - waiting for end of transmission' state. This means that we
** have to perform a chip soft reset to ensure that we can synchronize
** the hardware and software and make any media probes using a loopback
** packet meaningful.
*/
static void
de4x5_save_skbs(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    s32 omr;

    if (!lp->cache.save_cnt) {
	STOP_DE4X5;
	de4x5_tx(dev);                          /* Flush any sent skb's */
	de4x5_free_tx_buffs(dev);
	de4x5_cache_state(dev, DE4X5_SAVE_STATE);
	de4x5_sw_reset(dev);
	de4x5_cache_state(dev, DE4X5_RESTORE_STATE);
	lp->cache.save_cnt++;
	START_DE4X5;
    }

    return;
}

static void
de4x5_restore_skbs(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    s32 omr;

    if (lp->cache.save_cnt) {
	STOP_DE4X5;
	de4x5_cache_state(dev, DE4X5_SAVE_STATE);
	de4x5_sw_reset(dev);
	de4x5_cache_state(dev, DE4X5_RESTORE_STATE);
	lp->cache.save_cnt--;
	START_DE4X5;
    }
        
    return;
}

static void
de4x5_cache_state(struct device *dev, int flag)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    s32 gep;

    switch(flag) {
      case DE4X5_SAVE_STATE:
	lp->cache.csr0 = inl(DE4X5_BMR);
	lp->cache.csr6 = (inl(DE4X5_OMR) & ~(OMR_ST | OMR_SR));
	lp->cache.csr7 = inl(DE4X5_IMR);
	if (lp->chipset != DC21140) {
	    lp->cache.csr13 = inl(DE4X5_SICR);
	    lp->cache.csr14 = inl(DE4X5_STRR);
	    lp->cache.csr15 = inl(DE4X5_SIGR);
	}
	break;

      case DE4X5_RESTORE_STATE:
	outl(lp->cache.csr0, DE4X5_BMR);
	outl(lp->cache.csr6, DE4X5_OMR);
	outl(lp->cache.csr7, DE4X5_IMR);
	if (lp->chipset == DC21140) {
	    outl(GEP_INIT, DE4X5_GEP);
	    gep = (lp->media == _100Mb ? GEP_MODE : 0);
	    if (!lp->phy[lp->active].id && !de4x5_full_duplex) {
		gep |= GEP_FDXD;
	    }
	    outl(gep, DE4X5_GEP);
	} else {
	    reset_init_sia(dev, lp->cache.csr13, lp->cache.csr14, 
			                                      lp->cache.csr15);
	}
	break;
    }

    return;
}

static void
de4x5_put_cache(struct device *dev, struct sk_buff *skb)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    struct sk_buff *p;

    if (lp->cache.skb) {
	for (p=lp->cache.skb; p->next; p=p->next);
	p->next = skb;
    } else {
	lp->cache.skb = skb;
    }
    skb->next = NULL;

    return;
}

static void
de4x5_putb_cache(struct device *dev, struct sk_buff *skb)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    struct sk_buff *p = lp->cache.skb;

    lp->cache.skb = skb;
    skb->next = p;

    return;
}

static struct sk_buff *
de4x5_get_cache(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    struct sk_buff *p = lp->cache.skb;

    if (p) {
	lp->cache.skb = p->next;
	p->next = NULL;
    }

    return p;
}

/*
** Check the Auto Negotiation State. Return OK when a link pass interrupt
** is received and the auto-negotiation status is NWAY OK.
*/
static int
test_ans(struct device *dev, s32 irqs, s32 irq_mask, s32 msec)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    s32 sts, ans;
    
    if (lp->timeout < 0) {
	lp->timeout = msec/100;
	outl(irq_mask, DE4X5_IMR);
	
	/* clear all pending interrupts */
	sts = inl(DE4X5_STS);
	outl(sts, DE4X5_STS);
    }
    
    ans = inl(DE4X5_SISR) & SISR_ANS;
    sts = inl(DE4X5_STS) & ~TIMER_CB;
    
    if (!(sts & irqs) && (ans ^ ANS_NWOK) && --lp->timeout) {
	sts = 100 | TIMER_CB;
    } else {
	lp->timeout = -1;
    }
    
    return sts;
}

static void
de4x5_setup_intr(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    s32 imr, sts;
    
    if (inl(DE4X5_OMR) & OMR_SR) {   /* Only unmask if TX/RX is enabled */
	imr = 0;
	UNMASK_IRQs;
	sts = inl(DE4X5_STS);        /* Reset any pending (stale) interrupts */
	outl(sts, DE4X5_STS);
	ENABLE_IRQs;
    }
    
    return;
}

/*
**
*/
static void
reset_init_sia(struct device *dev, s32 sicr, s32 strr, s32 sigr)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    u_long iobase = dev->base_addr;
    
    RESET_SIA;
    outl(sigr, DE4X5_SIGR);
    outl(strr, DE4X5_STRR);
    outl(sicr, DE4X5_SICR);

    return;
}

/*
** Create a loopback ethernet packet with an invalid CRC
*/
static void
create_packet(struct device *dev, char *frame, int len)
{
    int i;
    char *buf = frame;
    
    for (i=0; i<ETH_ALEN; i++) {             /* Use this source address */
	*buf++ = dev->dev_addr[i];
    }
    for (i=0; i<ETH_ALEN; i++) {             /* Use this destination address */
	*buf++ = dev->dev_addr[i];
    }
    
    *buf++ = 0;                              /* Packet length (2 bytes) */
    *buf++ = 1;
    
    return;
}

/*
** Known delay in microseconds
*/
static void
de4x5_us_delay(u32 usec)
{
    udelay(usec);
    
    return;
}

/*
** Known delay in milliseconds, in millisecond steps.
*/
static void
de4x5_ms_delay(u32 msec)
{
    u_int i;
    
    for (i=0; i<msec; i++) {
	de4x5_us_delay(1000);
    }
    
    return;
}


/*
** Look for a particular board name in the EISA configuration space
*/
static int
EISA_signature(char *name, s32 eisa_id)
{
    c_char *signatures[] = DE4X5_SIGNATURE;
    char ManCode[DE4X5_STRLEN];
    union {
	s32 ID;
	char Id[4];
    } Eisa;
    int i, status = 0, siglen = sizeof(signatures)/sizeof(c_char *);
    
    *name = '\0';
    Eisa.ID = inl(eisa_id);
    
    ManCode[0]=(((Eisa.Id[0]>>2)&0x1f)+0x40);
    ManCode[1]=(((Eisa.Id[1]&0xe0)>>5)+((Eisa.Id[0]&0x03)<<3)+0x40);
    ManCode[2]=(((Eisa.Id[2]>>4)&0x0f)+0x30);
    ManCode[3]=((Eisa.Id[2]&0x0f)+0x30);
    ManCode[4]=(((Eisa.Id[3]>>4)&0x0f)+0x30);
    ManCode[5]='\0';
    
    for (i=0;i<siglen;i++) {
	if (strstr(ManCode, signatures[i]) != NULL) {
	    strcpy(name,ManCode);
	    status = 1;
	    break;
	}
    }
    
    return status;                         /* return the device name string */
}

/*
** Look for a particular board name in the PCI configuration space
*/
static int
PCI_signature(char *name, struct bus_type *lp)
{
    c_char *de4x5_signatures[] = DE4X5_SIGNATURE;
    int i, status = 0, siglen = sizeof(de4x5_signatures)/sizeof(c_char *);
    
    if (lp->chipset == DC21040) {
	strcpy(name, "DE434/5");
    } else {
	int i = *((char *)&lp->srom + 19) * 3;
	if (lp->chipset == DC21041) {
	    strncpy(name, (char *)&lp->srom + 26 + i, 8);
	} else if (lp->chipset == DC21140) {
	    strncpy(name, (char *)&lp->srom + 26 + i, 8);
	}
    }
    name[8] = '\0';
    for (i=0; i<siglen; i++) {
	if (strstr(name,de4x5_signatures[i])!=NULL) break;
    }
    if (i == siglen) {
	if (dec_only) {
	    *name = '\0';
	} else {                        /* Use chip name to avoid confusion */
	    strcpy(name, (((lp->chipset == DC21040) ? "DC21040" :
			   ((lp->chipset == DC21041) ? "DC21041" :
			    ((lp->chipset == DC21140) ? "DC21140" : "UNKNOWN"
			     )))));
	}
    }
    
    return status;
}

/*
** Set up the Ethernet PROM counter to the start of the Ethernet address on
** the DC21040, else  read the SROM for the other chips.
*/
static void
DevicePresent(u_long aprom_addr)
{
    int i;
    struct bus_type *lp = &bus;
    
    if (lp->chipset == DC21040) {
	outl(0, aprom_addr);           /* Reset Ethernet Address ROM Pointer */
    } else {                           /* Read new srom */
	short *p = (short *)&lp->srom;
	for (i=0; i<(sizeof(struct de4x5_srom)>>1); i++) {
	    *p++ = srom_rd(aprom_addr, i);
	}
	de4x5_dbg_srom((struct de4x5_srom *)&lp->srom);
    }
    
    return;
}

static int
get_hw_addr(struct device *dev)
{
    u_long iobase = dev->base_addr;
    int broken, i, k, tmp, status = 0;
    u_short j,chksum;
    struct bus_type *lp = &bus;

    broken = de4x5_bad_srom(lp);
    for (i=0,k=0,j=0;j<3;j++) {
	k <<= 1;
	if (k > 0xffff) k-=0xffff;
	
	if (lp->bus == PCI) {
	    if (lp->chipset == DC21040) {
		while ((tmp = inl(DE4X5_APROM)) < 0);
		k += (u_char) tmp;
		dev->dev_addr[i++] = (u_char) tmp;
		while ((tmp = inl(DE4X5_APROM)) < 0);
		k += (u_short) (tmp << 8);
		dev->dev_addr[i++] = (u_char) tmp;
	    } else if (!broken) {
		dev->dev_addr[i] = (u_char) lp->srom.ieee_addr[i]; i++;
		dev->dev_addr[i] = (u_char) lp->srom.ieee_addr[i]; i++;
	    } else if ((broken == SMC) || (broken == ACCTON)) {
		dev->dev_addr[i] = *((u_char *)&lp->srom + i); i++;
		dev->dev_addr[i] = *((u_char *)&lp->srom + i); i++;
	    }
	} else {
	    k += (u_char) (tmp = inb(EISA_APROM));
	    dev->dev_addr[i++] = (u_char) tmp;
	    k += (u_short) ((tmp = inb(EISA_APROM)) << 8);
	    dev->dev_addr[i++] = (u_char) tmp;
	}
	
	if (k > 0xffff) k-=0xffff;
    }
    if (k == 0xffff) k=0;
    
    if (lp->bus == PCI) {
	if (lp->chipset == DC21040) {
	    while ((tmp = inl(DE4X5_APROM)) < 0);
	    chksum = (u_char) tmp;
	    while ((tmp = inl(DE4X5_APROM)) < 0);
	    chksum |= (u_short) (tmp << 8);
	    if ((k != chksum) && (dec_only)) status = -1;
	}
    } else {
	chksum = (u_char) inb(EISA_APROM);
	chksum |= (u_short) (inb(EISA_APROM) << 8);
	if ((k != chksum) && (dec_only)) status = -1;
    }

    return status;
}

/*
** Test for enet addresses in the first 32 bytes. The built-in strncmp
** didn't seem to work here...?
*/
static int
de4x5_bad_srom(struct bus_type *lp)
{
    int i, status = 0;

    for (i=0; i<sizeof(enet_det)/ETH_ALEN; i++) {
	if (!de4x5_strncmp((char *)&lp->srom, (char *)&enet_det[i], 3) &&
	    !de4x5_strncmp((char *)&lp->srom+0x10, (char *)&enet_det[i], 3)) {
	    if (i == 0) {
		status = SMC;
	    } else if (i == 1) {
		status = ACCTON;
	    }
	    break;
	}
    }

    return status;
}

static int
de4x5_strncmp(char *a, char *b, int n)
{
    int ret=0;

    for (;n && !ret;n--) {
	ret = *a++ - *b++;
    }

    return ret;
}

/*
** SROM Read
*/
static short
srom_rd(u_long addr, u_char offset)
{
    sendto_srom(SROM_RD | SROM_SR, addr);
    
    srom_latch(SROM_RD | SROM_SR | DT_CS, addr);
    srom_command(SROM_RD | SROM_SR | DT_IN | DT_CS, addr);
    srom_address(SROM_RD | SROM_SR | DT_CS, addr, offset);
    
    return srom_data(SROM_RD | SROM_SR | DT_CS, addr);
}

static void
srom_latch(u_int command, u_long addr)
{
    sendto_srom(command, addr);
    sendto_srom(command | DT_CLK, addr);
    sendto_srom(command, addr);
    
    return;
}

static void
srom_command(u_int command, u_long addr)
{
    srom_latch(command, addr);
    srom_latch(command, addr);
    srom_latch((command & 0x0000ff00) | DT_CS, addr);
    
    return;
}

static void
srom_address(u_int command, u_long addr, u_char offset)
{
    int i;
    char a;
    
    a = (char)(offset << 2);
    for (i=0; i<6; i++, a <<= 1) {
	srom_latch(command | ((a < 0) ? DT_IN : 0), addr);
    }
    de4x5_us_delay(1);
    
    i = (getfrom_srom(addr) >> 3) & 0x01;
    if (i != 0) {
	printk("Bad SROM address phase.....\n");
    }
    
    return;
}

static short
srom_data(u_int command, u_long addr)
{
    int i;
    short word = 0;
    s32 tmp;
    
    for (i=0; i<16; i++) {
	sendto_srom(command  | DT_CLK, addr);
	tmp = getfrom_srom(addr);
	sendto_srom(command, addr);
	
	word = (word << 1) | ((tmp >> 3) & 0x01);
    }
    
    sendto_srom(command & 0x0000ff00, addr);
    
    return word;
}

/*
static void
srom_busy(u_int command, u_long addr)
{
   sendto_srom((command & 0x0000ff00) | DT_CS, addr);
   
   while (!((getfrom_srom(addr) >> 3) & 0x01)) {
       de4x5_ms_delay(1);
   }
   
   sendto_srom(command & 0x0000ff00, addr);
   
   return;
}
*/

static void
sendto_srom(u_int command, u_long addr)
{
    outl(command, addr);
    udelay(1);
    
    return;
}

static int
getfrom_srom(u_long addr)
{
    s32 tmp;
    
    tmp = inl(addr);
    udelay(1);
    
    return tmp;
}

/*
** MII Read/Write
*/

static int
mii_rd(u_char phyreg, u_char phyaddr, u_long ioaddr)
{
    mii_wdata(MII_PREAMBLE,  2, ioaddr);   /* Start of 34 bit preamble...    */
    mii_wdata(MII_PREAMBLE, 32, ioaddr);   /* ...continued                   */
    mii_wdata(MII_STRD, 4, ioaddr);        /* SFD and Read operation         */
    mii_address(phyaddr, ioaddr);          /* PHY address to be accessed     */
    mii_address(phyreg, ioaddr);           /* PHY Register to read           */
    mii_ta(MII_STRD, ioaddr);              /* Turn around time - 2 MDC       */
    
    return mii_rdata(ioaddr);              /* Read data                      */
}

static void
mii_wr(int data, u_char phyreg, u_char phyaddr, u_long ioaddr)
{
    mii_wdata(MII_PREAMBLE,  2, ioaddr);   /* Start of 34 bit preamble...    */
    mii_wdata(MII_PREAMBLE, 32, ioaddr);   /* ...continued                   */
    mii_wdata(MII_STWR, 4, ioaddr);        /* SFD and Write operation        */
    mii_address(phyaddr, ioaddr);          /* PHY address to be accessed     */
    mii_address(phyreg, ioaddr);           /* PHY Register to write          */
    mii_ta(MII_STWR, ioaddr);              /* Turn around time - 2 MDC       */
    data = mii_swap(data, 16);             /* Swap data bit ordering         */
    mii_wdata(data, 16, ioaddr);           /* Write data                     */
    
    return;
}

static int
mii_rdata(u_long ioaddr)
{
    int i;
    s32 tmp = 0;
    
    for (i=0; i<16; i++) {
	tmp <<= 1;
	tmp |= getfrom_mii(MII_MRD | MII_RD, ioaddr);
    }
    
    return tmp;
}

static void
mii_wdata(int data, int len, u_long ioaddr)
{
    int i;
    
    for (i=0; i<len; i++) {
	sendto_mii(MII_MWR | MII_WR, data, ioaddr);
	data >>= 1;
    }
    
    return;
}

static void
mii_address(u_char addr, u_long ioaddr)
{
    int i;
    
    addr = mii_swap(addr, 5);
    for (i=0; i<5; i++) {
	sendto_mii(MII_MWR | MII_WR, addr, ioaddr);
	addr >>= 1;
    }
    
    return;
}

static void
mii_ta(u_long rw, u_long ioaddr)
{
    if (rw == MII_STWR) {
	sendto_mii(MII_MWR | MII_WR, 1, ioaddr);  
	sendto_mii(MII_MWR | MII_WR, 0, ioaddr);  
    } else {
	getfrom_mii(MII_MRD | MII_RD, ioaddr);        /* Tri-state MDIO */
    }
    
    return;
}

static int
mii_swap(int data, int len)
{
    int i, tmp = 0;
    
    for (i=0; i<len; i++) {
	tmp <<= 1;
	tmp |= (data & 1);
	data >>= 1;
    }
    
    return tmp;
}

static void
sendto_mii(u32 command, int data, u_long ioaddr)
{
    u32 j;
    
    j = (data & 1) << 17;
    outl(command | j, ioaddr);
    udelay(1);
    outl(command | MII_MDC | j, ioaddr);
    udelay(1);
    
    return;
}

static int
getfrom_mii(u32 command, u_long ioaddr)
{
    outl(command, ioaddr);
    udelay(1);
    outl(command | MII_MDC, ioaddr);
    udelay(1);
    
    return ((inl(ioaddr) >> 19) & 1);
}

/*
** Here's 3 ways to calculate the OUI from the ID registers. One's a brain
** dead approach, 2 aren't (clue: mine isn't!).
*/
static int
mii_get_oui(u_char phyaddr, u_long ioaddr)
{
/*
    union {
	u_short reg;
	u_char breg[2];
    } a;
    int i, r2, r3, ret=0;*/
    int r2, r3;

    /* Read r2 and r3 */
    r2 = mii_rd(MII_ID0, phyaddr, ioaddr);
    r3 = mii_rd(MII_ID1, phyaddr, ioaddr);
                                                /* SEEQ and Cypress way * /
    / * Shuffle r2 and r3 * /
    a.reg=0;
    r3 = ((r3>>10)|(r2<<6))&0x0ff;
    r2 = ((r2>>2)&0x3fff);

    / * Bit reverse r3 * /
    for (i=0;i<8;i++) {
	ret<<=1;
	ret |= (r3&1);
	r3>>=1;
    }

    / * Bit reverse r2 * /
    for (i=0;i<16;i++) {
	a.reg<<=1;
	a.reg |= (r2&1);
	r2>>=1;
    }

    / * Swap r2 bytes * /
    i=a.breg[0];
    a.breg[0]=a.breg[1];
    a.breg[1]=i;

    return ((a.reg<<8)|ret); */                 /* SEEQ and Cypress way */
/*    return ((r2<<6)|(u_int)(r3>>10)); */      /* NATIONAL and BROADCOM way */
    return r2;                                  /* (I did it) My way */
}

static int
mii_get_phy(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int iobase = dev->base_addr;
    int i, j, k, limit=sizeof(phy_info)/sizeof(struct phy_table);
    int id;
    
    /* Issue a hard PHY reset - Broadcom is screwed up otherwise */
    outl(GEP_HRST, DE4X5_GEP);
    udelay(1000);                                  /* Assert for 1ms */
    outl(0x00, DE4X5_GEP);
    udelay(2000);                                  /* Wait for 2ms */
    
    /* Search the MII address space for possible PHY devices */
    lp->active = 0;
    for (lp->mii_cnt=0, i=1; i<DE4X5_MAX_MII; i++) {
	id = mii_get_oui(i, DE4X5_MII); 
	if ((id == 0) || (id == -1)) continue;     /* Valid ID? */
	for (j=0; j<limit; j++) {                  /* Search PHY table */
	    if (id != phy_info[j].id) continue;    /* ID match? */
	    for (k=0; lp->phy[k].id && (k < DE4X5_MAX_PHY); k++);
	    if (k < DE4X5_MAX_PHY) {
		memcpy((char *)&lp->phy[k],
		       (char *)&phy_info[j], sizeof(struct phy_table));
		lp->phy[k].addr = i;
		lp->mii_cnt++;
	    } else {
		i = DE4X5_MAX_MII;                 /* Stop the search */
		j = limit;
	    }
	}
    }
    if (lp->phy[lp->active].id) {                  /* Reset the PHY devices */
	for (k=0; lp->phy[k].id && (k < DE4X5_MAX_PHY); k++) { /*For each PHY*/
	    mii_wr(MII_CR_RST, MII_CR, lp->phy[k].addr, DE4X5_MII);
	    while (mii_rd(MII_CR, lp->phy[k].addr, DE4X5_MII) & MII_CR_RST);
	    
	    de4x5_dbg_mii(dev, k);
	}
    }
    
    return lp->mii_cnt;
}

static char *
build_setup_frame(struct device *dev, int mode)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int i;
    char *pa = lp->setup_frame;
    
    /* Initialise the setup frame */
    if (mode == ALL) {
	memset(lp->setup_frame, 0, SETUP_FRAME_LEN);
    }
    
    if (lp->setup_f == HASH_PERF) {
	for (pa=lp->setup_frame+IMPERF_PA_OFFSET, i=0; i<ETH_ALEN; i++) {
	    *(pa + i) = dev->dev_addr[i];                 /* Host address */
	    if (i & 0x01) pa += 2;
	}
	*(lp->setup_frame + (HASH_TABLE_LEN >> 3) - 3) = 0x80;
    } else {
	for (i=0; i<ETH_ALEN; i++) { /* Host address */
	    *(pa + (i&1)) = dev->dev_addr[i];
	    if (i & 0x01) pa += 4;
	}
	for (i=0; i<ETH_ALEN; i++) { /* Broadcast address */
	    *(pa + (i&1)) = (char) 0xff;
	    if (i & 0x01) pa += 4;
	}
    }
    
    return pa;                     /* Points to the next entry */
}

static void
enable_ast(struct device *dev, u32 time_out)
{
    timeout(dev, (void *)&de4x5_ast, (u_long)dev, time_out);
    
    return;
}

static void
disable_ast(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    
    del_timer(&lp->timer);
    
    return;
}

static long
de4x5_switch_to_mii(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int iobase = dev->base_addr;
    long omr;
    
    /* Assert the OMR_PS bit in CSR6 */
    omr = (inl(DE4X5_OMR) & ~(OMR_PS | OMR_HBD | OMR_TTM | OMR_PCS | OMR_SCR));
    omr |= (OMR_PS | OMR_HBD);
    outl(omr, DE4X5_OMR);
    
    /* Soft Reset */
    RESET_DE4X5;
    
    /* Restore the GEP */
    if (lp->chipset == DC21140) {
	outl(GEP_INIT, DE4X5_GEP);
	outl(0, DE4X5_GEP);
    }
    
    /* Restore CSR6 */
    outl(omr, DE4X5_OMR);
    
    return omr;
}

static long
de4x5_switch_to_srl(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int iobase = dev->base_addr;
    long omr;
    
    /* Deassert the OMR_PS bit in CSR6 */
    omr = (inl(DE4X5_OMR) & ~(OMR_PS | OMR_HBD | OMR_TTM | OMR_PCS | OMR_SCR));
    outl(omr, DE4X5_OMR);
    
    /* Soft Reset */
    RESET_DE4X5;
    
    /* Restore the GEP */
    if (lp->chipset == DC21140) {
	outl(GEP_INIT, DE4X5_GEP);
	outl(0, DE4X5_GEP);
    }
    
    /* Restore CSR6 */
    outl(omr, DE4X5_OMR);
    
    return omr;
}

static void
timeout(struct device *dev, void (*fn)(u_long data), u_long data, u_long msec)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int dt;
    
    /* First, cancel any pending timer events */
    del_timer(&lp->timer);
    
    /* Convert msec to ticks */
    dt = (msec * HZ) / 1000;
    if (dt==0) dt=1;
    
    /* Set up timer */
    lp->timer.expires = jiffies + dt;
    lp->timer.function = fn;
    lp->timer.data = data;
    add_timer(&lp->timer);
    
    return;
}

static void
de4x5_dbg_open(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int i;
    
    if (de4x5_debug > 1) {
	printk("%s: de4x5 opening with irq %d\n",dev->name,dev->irq);
	printk("\tphysical address: ");
	for (i=0;i<6;i++) {
	    printk("%2.2x:",(short)dev->dev_addr[i]);
	}
	printk("\n");
	printk("Descriptor head addresses:\n");
	printk("\t0x%8.8lx  0x%8.8lx\n",(u_long)lp->rx_ring,(u_long)lp->tx_ring);
	printk("Descriptor addresses:\nRX: ");
	for (i=0;i<lp->rxRingSize-1;i++){
	    if (i < 3) {
		printk("0x%8.8lx  ",(u_long)&lp->rx_ring[i].status);
	    }
	}
	printk("...0x%8.8lx\n",(u_long)&lp->rx_ring[i].status);
	printk("TX: ");
	for (i=0;i<lp->txRingSize-1;i++){
	    if (i < 3) {
		printk("0x%8.8lx  ", (u_long)&lp->tx_ring[i].status);
	    }
	}
	printk("...0x%8.8lx\n", (u_long)&lp->tx_ring[i].status);
	printk("Descriptor buffers:\nRX: ");
	for (i=0;i<lp->rxRingSize-1;i++){
	    if (i < 3) {
		printk("0x%8.8x  ",lp->rx_ring[i].buf);
	    }
	}
	printk("...0x%8.8x\n",lp->rx_ring[i].buf);
	printk("TX: ");
	for (i=0;i<lp->txRingSize-1;i++){
	    if (i < 3) {
		printk("0x%8.8x  ", lp->tx_ring[i].buf);
	    }
	}
	printk("...0x%8.8x\n", lp->tx_ring[i].buf);
	printk("Ring size: \nRX: %d\nTX: %d\n", 
	       (short)lp->rxRingSize, 
	       (short)lp->txRingSize); 
    }
    
    return;
}

static void
de4x5_dbg_mii(struct device *dev, int k)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    int iobase = dev->base_addr;
    
    if (de4x5_debug > 2) {
	printk("\nMII CR:  %x\n",mii_rd(MII_CR,lp->phy[k].addr,DE4X5_MII));
	printk("MII SR:  %x\n",mii_rd(MII_SR,lp->phy[k].addr,DE4X5_MII));
	printk("MII ID0: %x\n",mii_rd(MII_ID0,lp->phy[k].addr,DE4X5_MII));
	printk("MII ID1: %x\n",mii_rd(MII_ID1,lp->phy[k].addr,DE4X5_MII));
	if (lp->phy[k].id != BROADCOM_T4) {
	    printk("MII ANA: %x\n",mii_rd(0x04,lp->phy[k].addr,DE4X5_MII));
	    printk("MII ANC: %x\n",mii_rd(0x05,lp->phy[k].addr,DE4X5_MII));
	}
	printk("MII 16:  %x\n",mii_rd(0x10,lp->phy[k].addr,DE4X5_MII));
	if (lp->phy[k].id != BROADCOM_T4) {
	    printk("MII 17:  %x\n",mii_rd(0x11,lp->phy[k].addr,DE4X5_MII));
	    printk("MII 18:  %x\n",mii_rd(0x12,lp->phy[k].addr,DE4X5_MII));
	} else {
	    printk("MII 20:  %x\n",mii_rd(0x14,lp->phy[k].addr,DE4X5_MII));
	}
    }
    
    return;
}

static void
de4x5_dbg_media(struct device *dev)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    
    if (lp->media != lp->c_media) {
	if (de4x5_debug > 0) {
	    if (lp->chipset != DC21140) {
		printk("%s: media is %s\n", dev->name,
		       (lp->media == NC  ? "unconnected!" :
			(lp->media == TP  ? "TP." :
			 (lp->media == ANS ? "TP/Nway." :
			  (lp->media == BNC ? "BNC." : 
			   (lp->media == BNC_AUI ? "BNC/AUI." : 
			    (lp->media == EXT_SIA ? "EXT SIA." : 
			     "???."
			     )))))));
	    } else {
		printk("%s: mode is %s\n", dev->name,
		    (lp->media == NC ? "link down or incompatible connection.":
		     (lp->media == _100Mb  ? "100Mb/s." :
		      (lp->media == _10Mb   ? "10Mb/s." :
		       "\?\?\?"
		       ))));
	    }
	}
	lp->c_media = lp->media;
    }
    
    return;
}

static void
de4x5_dbg_srom(struct de4x5_srom *p)
{
    int i;

    if (de4x5_debug > 1) {
	printk("Sub-system Vendor ID: %04x\n", (u_short)*(p->sub_vendor_id));
	printk("Sub-system ID:        %04x\n", (u_short)*(p->sub_system_id));
	printk("ID Block CRC:         %02x\n", (u_char)(p->id_block_crc));

	printk("Hardware Address:     ");
	for (i=0;i<ETH_ALEN-1;i++) {
	    printk("%02x:", (u_char)*(p->ieee_addr+i));
	}
	printk("%02x\n", (u_char)*(p->ieee_addr+i));
	printk("CRC checksum:         %04x\n", (u_short)(p->chksum));
	for (i=0; i<64; i++) {
	    printk("%3d %04x\n", i<<1, (u_short)*((u_short *)p+i));
	}
    }

    return;
}

static void
de4x5_dbg_rx(struct sk_buff *skb, int len)
{
    int i, j;

    if (de4x5_debug > 2) {
	printk("R: %02x:%02x:%02x:%02x:%02x:%02x <- %02x:%02x:%02x:%02x:%02x:%02x len/SAP:%02x%02x [%d]\n",
	       (u_char)skb->data[0],
	       (u_char)skb->data[1],
	       (u_char)skb->data[2],
	       (u_char)skb->data[3],
	       (u_char)skb->data[4],
	       (u_char)skb->data[5],
	       (u_char)skb->data[6],
	       (u_char)skb->data[7],
	       (u_char)skb->data[8],
	       (u_char)skb->data[9],
	       (u_char)skb->data[10],
	       (u_char)skb->data[11],
	       (u_char)skb->data[12],
	       (u_char)skb->data[13],
	       len);
	if (de4x5_debug > 3) {
	    for (j=0; len>0;j+=16, len-=16) {
		printk("    %03x: ",j);
		for (i=0; i<16 && i<len; i++) {
		    printk("%02x ",(u_char)skb->data[i+j]);
		}
		printk("\n");
	    }
	}
    }

    return;
}

/*
** Perform IOCTL call functions here. Some are privileged operations and the
** effective uid is checked in those cases.
*/
static int
de4x5_ioctl(struct device *dev, struct ifreq *rq, int cmd)
{
    struct de4x5_private *lp = (struct de4x5_private *)dev->priv;
    struct de4x5_ioctl *ioc = (struct de4x5_ioctl *) &rq->ifr_data;
    u_long iobase = dev->base_addr;
    int i, j, status = 0;
    s32 omr;
    union {
	u8  addr[(HASH_TABLE_LEN * ETH_ALEN)];
	u16 sval[(HASH_TABLE_LEN * ETH_ALEN) >> 1];
	u32 lval[(HASH_TABLE_LEN * ETH_ALEN) >> 2];
    } tmp;
    
    switch(ioc->cmd) {
      case DE4X5_GET_HWADDR:           /* Get the hardware address */
	ioc->len = ETH_ALEN;
	status = verify_area(VERIFY_WRITE, (void *)ioc->data, ioc->len);
	if (status)
	  break;
	for (i=0; i<ETH_ALEN; i++) {
	    tmp.addr[i] = dev->dev_addr[i];
	}
	memcpy_tofs(ioc->data, tmp.addr, ioc->len);
	
	break;
      case DE4X5_SET_HWADDR:           /* Set the hardware address */
	status = verify_area(VERIFY_READ, (void *)ioc->data, ETH_ALEN);
	if (status)
	  break;
	status = -EPERM;
	if (!suser())
	  break;
	status = 0;
	memcpy_fromfs(tmp.addr, ioc->data, ETH_ALEN);
	for (i=0; i<ETH_ALEN; i++) {
	    dev->dev_addr[i] = tmp.addr[i];
	}
	build_setup_frame(dev, PHYS_ADDR_ONLY);
	/* Set up the descriptor and give ownership to the card */
	while (set_bit(0, (void *)&dev->tbusy) != 0);/* Wait for lock to free*/
	load_packet(dev, lp->setup_frame, TD_IC | PERFECT_F | TD_SET | 
		                                        SETUP_FRAME_LEN, NULL);
	lp->tx_new = (++lp->tx_new) % lp->txRingSize;
	outl(POLL_DEMAND, DE4X5_TPD);                /* Start the TX */
	dev->tbusy = 0;                              /* Unlock the TX ring */
	
	break;
      case DE4X5_SET_PROM:             /* Set Promiscuous Mode */
	if (suser()) {
	    omr = inl(DE4X5_OMR);
	    omr |= OMR_PR;
	    outl(omr, DE4X5_OMR);
	} else {
	    status = -EPERM;
	}
	
	break;
      case DE4X5_CLR_PROM:             /* Clear Promiscuous Mode */
	if (suser()) {
	    omr = inl(DE4X5_OMR);
	    omr &= ~OMR_PR;
	    outb(omr, DE4X5_OMR);
	} else {
	    status = -EPERM;
	}
	
	break;
      case DE4X5_SAY_BOO:              /* Say "Boo!" to the kernel log file */
	printk("%s: Boo!\n", dev->name);
	
	break;
      case DE4X5_GET_MCA:              /* Get the multicast address table */
	ioc->len = (HASH_TABLE_LEN >> 3);
	status = verify_area(VERIFY_WRITE, ioc->data, ioc->len);
	if (!status) {
	    memcpy_tofs(ioc->data, lp->setup_frame, ioc->len); 
	}
	
	break;
      case DE4X5_SET_MCA:              /* Set a multicast address */
	if (suser()) {
	    /******* FIX ME! ********/
	    if (ioc->len != HASH_TABLE_LEN) {         /* MCA changes */
		if (!(status = verify_area(VERIFY_READ, (void *)ioc->data, ETH_ALEN * ioc->len))) {
		    memcpy_fromfs(tmp.addr, ioc->data, ETH_ALEN * ioc->len);
		    set_multicast_list(dev);
		}
	    } else {
		set_multicast_list(dev);
	    }
	} else {
	    status = -EPERM;
	}
	
	break;
      case DE4X5_CLR_MCA:              /* Clear all multicast addresses */
	if (suser()) {
	    /******* FIX ME! ********/
	    set_multicast_list(dev);
	} else {
	    status = -EPERM;
	}
	
	break;
      case DE4X5_MCA_EN:               /* Enable pass all multicast addressing */
	if (suser()) {
	    omr = inl(DE4X5_OMR);
	    omr |= OMR_PM;
	    outl(omr, DE4X5_OMR);
	} else {
	    status = -EPERM;
	}
	
	break;
      case DE4X5_GET_STATS:            /* Get the driver statistics */
	ioc->len = sizeof(lp->pktStats);
	status = verify_area(VERIFY_WRITE, (void *)ioc->data, ioc->len);
	if (status)
	  break;
	
	cli();
	memcpy_tofs(ioc->data, &lp->pktStats, ioc->len); 
	sti();
	
	break;
      case DE4X5_CLR_STATS:            /* Zero out the driver statistics */
	if (suser()) {
	    cli();
	    memset(&lp->pktStats, 0, sizeof(lp->pktStats));
	    sti();
	} else {
	    status = -EPERM;
	}
	
	break;
      case DE4X5_GET_OMR:              /* Get the OMR Register contents */
	tmp.addr[0] = inl(DE4X5_OMR);
	if (!(status = verify_area(VERIFY_WRITE, (void *)ioc->data, 1))) {
	    memcpy_tofs(ioc->data, tmp.addr, 1);
	}
	
	break;
      case DE4X5_SET_OMR:              /* Set the OMR Register contents */
	if (suser()) {
	    if (!(status = verify_area(VERIFY_READ, (void *)ioc->data, 1))) {
		memcpy_fromfs(tmp.addr, ioc->data, 1);
		outl(tmp.addr[0], DE4X5_OMR);
	    }
	} else {
	    status = -EPERM;
	}
	
	break;
      case DE4X5_GET_REG:              /* Get the DE4X5 Registers */
	j = 0;
	tmp.lval[0] = inl(DE4X5_STS); j+=4;
	tmp.lval[1] = inl(DE4X5_BMR); j+=4;
	tmp.lval[2] = inl(DE4X5_IMR); j+=4;
	tmp.lval[3] = inl(DE4X5_OMR); j+=4;
	tmp.lval[4] = inl(DE4X5_SISR); j+=4;
	tmp.lval[5] = inl(DE4X5_SICR); j+=4;
	tmp.lval[6] = inl(DE4X5_STRR); j+=4;
	tmp.lval[7] = inl(DE4X5_SIGR); j+=4;
	ioc->len = j;
	if (!(status = verify_area(VERIFY_WRITE, (void *)ioc->data, ioc->len))) {
	    memcpy_tofs(ioc->data, tmp.addr, ioc->len);
	}
	break;
	
#define DE4X5_DUMP              0x0f /* Dump the DE4X5 Status */
	
      case DE4X5_DUMP:
	j = 0;
	tmp.addr[j++] = dev->irq;
	for (i=0; i<ETH_ALEN; i++) {
	    tmp.addr[j++] = dev->dev_addr[i];
	}
	tmp.addr[j++] = lp->rxRingSize;
	tmp.lval[j>>2] = (long)lp->rx_ring; j+=4;
	tmp.lval[j>>2] = (long)lp->tx_ring; j+=4;
	
	for (i=0;i<lp->rxRingSize-1;i++){
	    if (i < 3) {
		tmp.lval[j>>2] = (long)&lp->rx_ring[i].status; j+=4;
	    }
	}
	tmp.lval[j>>2] = (long)&lp->rx_ring[i].status; j+=4;
	for (i=0;i<lp->txRingSize-1;i++){
	    if (i < 3) {
		tmp.lval[j>>2] = (long)&lp->tx_ring[i].status; j+=4;
	    }
	}
	tmp.lval[j>>2] = (long)&lp->tx_ring[i].status; j+=4;
	
	for (i=0;i<lp->rxRingSize-1;i++){
	    if (i < 3) {
		tmp.lval[j>>2] = (s32)lp->rx_ring[i].buf; j+=4;
	    }
	}
	tmp.lval[j>>2] = (s32)lp->rx_ring[i].buf; j+=4;
	for (i=0;i<lp->txRingSize-1;i++){
	    if (i < 3) {
		tmp.lval[j>>2] = (s32)lp->tx_ring[i].buf; j+=4;
	    }
	}
	tmp.lval[j>>2] = (s32)lp->tx_ring[i].buf; j+=4;
	
	for (i=0;i<lp->rxRingSize;i++){
	    tmp.lval[j>>2] = lp->rx_ring[i].status; j+=4;
	}
	for (i=0;i<lp->txRingSize;i++){
	    tmp.lval[j>>2] = lp->tx_ring[i].status; j+=4;
	}
	
	tmp.lval[j>>2] = inl(DE4X5_BMR);  j+=4;
	tmp.lval[j>>2] = inl(DE4X5_TPD);  j+=4;
	tmp.lval[j>>2] = inl(DE4X5_RPD);  j+=4;
	tmp.lval[j>>2] = inl(DE4X5_RRBA); j+=4;
	tmp.lval[j>>2] = inl(DE4X5_TRBA); j+=4;
	tmp.lval[j>>2] = inl(DE4X5_STS);  j+=4;
	tmp.lval[j>>2] = inl(DE4X5_OMR);  j+=4;
	tmp.lval[j>>2] = inl(DE4X5_IMR);  j+=4;
	tmp.lval[j>>2] = lp->chipset; j+=4; 
	if (lp->chipset == DC21140) {
	    tmp.lval[j>>2] = inl(DE4X5_GEP);  j+=4;
	} else {
	    tmp.lval[j>>2] = inl(DE4X5_SISR); j+=4;
	    tmp.lval[j>>2] = inl(DE4X5_SICR); j+=4;
	    tmp.lval[j>>2] = inl(DE4X5_STRR); j+=4;
	    tmp.lval[j>>2] = inl(DE4X5_SIGR); j+=4; 
	}
	tmp.lval[j>>2] = lp->phy[lp->active].id; j+=4; 
	if (lp->phy[lp->active].id) {
	    tmp.lval[j>>2] = lp->active; j+=4; 
	    tmp.lval[j>>2]=mii_rd(MII_CR,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    tmp.lval[j>>2]=mii_rd(MII_SR,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    tmp.lval[j>>2]=mii_rd(MII_ID0,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    tmp.lval[j>>2]=mii_rd(MII_ID1,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    if (lp->phy[lp->active].id != BROADCOM_T4) {
		tmp.lval[j>>2]=mii_rd(MII_ANA,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
		tmp.lval[j>>2]=mii_rd(MII_ANLPA,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    }
	    tmp.lval[j>>2]=mii_rd(0x10,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    if (lp->phy[lp->active].id != BROADCOM_T4) {
		tmp.lval[j>>2]=mii_rd(0x11,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
		tmp.lval[j>>2]=mii_rd(0x12,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    } else {
		tmp.lval[j>>2]=mii_rd(0x14,lp->phy[lp->active].addr,DE4X5_MII); j+=4;
	    }
	}
	
	tmp.addr[j++] = lp->txRingSize;
	tmp.addr[j++] = dev->tbusy;
	
	ioc->len = j;
	if (!(status = verify_area(VERIFY_WRITE, (void *)ioc->data, ioc->len))) {
	    memcpy_tofs(ioc->data, tmp.addr, ioc->len);
	}
	
	break;
      default:
	status = -EOPNOTSUPP;
    }
    
    return status;
}

#ifdef MODULE
/*
** Note now that module autoprobing is allowed under EISA and PCI. The
** IRQ lines will not be auto-detected; instead I'll rely on the BIOSes
** to "do the right thing".
**
** NB: Current register_netdevice() code does not permit assigning io=0 as
** this driver will autoprobe all instances of acceptable DECchips. The
** cleanup_module() code needs work still....(just to unload modules owned
** by this driver).
*/
static char devicename[9] = { 0, };
static struct device thisDE4X5 = {
    devicename,   /* device name inserted by /linux/drivers/net/net_init.c  */
    0, 0, 0, 0,
    0, 0,         /* I/O address, IRQ                                       */
    0, 0, 0, NULL, de4x5_probe };

static int io=0x0b; /* EDIT THESE LINES FOR YOUR CONFIGURATION              */

int
init_module(void)
{
    struct device *p  = (struct device *)&thisDE4X5;
    
    thisDE4X5.base_addr = io;                   /* Now autoprobe the module */
    thisDE4X5.irq = 0;
    
    for (; p!=NULL; p=p->next) {
	if (register_netdev(p) != 0)
	  return -EIO;
    }
    io=0;
    return 0;
}

void
cleanup_module(void)
{
    struct de4x5_private *lp = (struct de4x5_private *) thisDE4X5.priv;
    struct device *p  = (struct device *)&thisDE4X5;
    int keep_loaded = 0;
    
    for (; p!=NULL; p=p->next) {
	keep_loaded += (p->flags & IFF_UP);     /* Is an interface up?       */
    }
    
    if (keep_loaded) {
	printk("de4x5: Cannot unload modules - %d interface%s%s still active.\n",
	       keep_loaded, (keep_loaded>1 ? "s ": " "),
	       (keep_loaded>1 ? "are": "is"));
	return;
    }
    
    for (p=thisDE4X5.next; p!=NULL; p=p->next) {
	if (p->priv) {                          /* Private area allocated?   */
	    struct de4x5_private *lp = (struct de4x5_private *)p->priv;
	    if (lp->cache.buf) {                /* MAC buffers allocated?    */
		kfree(lp->cache.buf);           /* Free the MAC buffers      */
	    }
	    release_region(p->base_addr, (lp->bus == PCI ? 
					  DE4X5_PCI_TOTAL_SIZE :
					  DE4X5_EISA_TOTAL_SIZE));
	    kfree(lp->cache.priv);              /* Free the private area     */
	}
	unregister_netdev(p);
	kfree(p);                               /* Free the device structure */
    }

    if (thisDE4X5.priv) {
	if (lp->cache.buf) {                    /* Are MAC buffers allocated */
            kfree(lp->cache.buf);
	}
	release_region(thisDE4X5.base_addr,
		      (lp->bus == PCI ? 
		       DE4X5_PCI_TOTAL_SIZE :
		       DE4X5_EISA_TOTAL_SIZE));
	kfree(lp->cache.priv);
	thisDE4X5.priv = NULL;
    }
    unregister_netdev(&thisDE4X5);

    return;
}
#endif /* MODULE */


/*
 * Local variables:
 *  compile-command: "gcc -D__KERNEL__ -I/linux/include -Wall -Wstrict-prototypes -fomit-frame-pointer -fno-strength-reduce -malign-loops=2 -malign-jumps=2 -malign-functions=2 -O2 -m486 -c de4x5.c"
 *
 *  compile-command: "gcc -D__KERNEL__ -DMODULE -I/linux/include -Wall -Wstrict-prototypes -fomit-frame-pointer -fno-strength-reduce -malign-loops=2 -malign-jumps=2 -malign-functions=2 -O2 -m486 -c de4x5.c"
 * End:
 */
