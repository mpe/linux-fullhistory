/*
 *      eata.c - Low-level driver for EATA/DMA SCSI host adapters.
 *
 *      22 Nov 1996 rev. 2.30 for linux 2.1.12 and 2.0.26
 *          When CONFIG_PCI is defined, BIOS32 is used to include in the
 *          list of i/o ports to be probed all the PCI SCSI controllers.
 *          The list of i/o ports to be probed can be overwritten by the
 *          "eata=port0, port1,...." boot command line option.
 *          Scatter/gather lists are now allocated by a number of kmalloc
 *          calls, in order to avoid the previous size limit of 64Kb.
 *
 *      16 Nov 1996 rev. 2.20 for linux 2.1.10 and 2.0.25
 *          Added support for EATA 2.0C, PCI, multichannel and wide SCSI.
 *
 *      27 Sep 1996 rev. 2.12 for linux 2.1.0
 *          Portability cleanups (virtual/bus addressing, little/big endian
 *          support).
 *
 *      09 Jul 1996 rev. 2.11 for linux 2.0.4
 *          Number of internal retries is now limited.
 *
 *      16 Apr 1996 rev. 2.10 for linux 1.3.90
 *          New argument "reset_flags" to the reset routine.
 *
 *       6 Jul 1995 rev. 2.01 for linux 1.3.7
 *          Update required by the new /proc/scsi support.
 *
 *      11 Mar 1995 rev. 2.00 for linux 1.2.0
 *          Fixed a bug which prevented media change detection for removable
 *          disk drives.
 *
 *      23 Feb 1995 rev. 1.18 for linux 1.1.94
 *          Added a check for scsi_register returning NULL.
 *
 *      11 Feb 1995 rev. 1.17 for linux 1.1.91
 *          Now DEBUG_RESET is disabled by default.
 *          Register a board even if it does not assert DMA protocol support
 *          (DPT SK2011B does not report correctly the dmasup bit).
 *
 *       9 Feb 1995 rev. 1.16 for linux 1.1.90
 *          Use host->wish_block instead of host->block.
 *          New list of Data Out SCSI commands.
 *
 *       8 Feb 1995 rev. 1.15 for linux 1.1.89
 *          Cleared target_time_out counter while performing a reset.
 *          All external symbols renamed to avoid possible name conflicts.
 *
 *      28 Jan 1995 rev. 1.14 for linux 1.1.86
 *          Added module support.
 *          Log and do a retry when a disk drive returns a target status 
 *          different from zero on a recovered error.
 *
 *      24 Jan 1995 rev. 1.13 for linux 1.1.85
 *          Use optimized board configuration, with a measured performance
 *          increase in the range 10%-20% on i/o throughput.
 *
 *      16 Jan 1995 rev. 1.12 for linux 1.1.81
 *          Fix mscp structure comments (no functional change).
 *          Display a message if check_region detects a port address
 *          already in use.
 *
 *      17 Dec 1994 rev. 1.11 for linux 1.1.74
 *          Use the scsicam_bios_param routine. This allows an easy
 *          migration path from disk partition tables created using 
 *          different SCSI drivers and non optimal disk geometry.
 *
 *      15 Dec 1994 rev. 1.10 for linux 1.1.74
 *          Added support for ISA EATA boards (DPT PM2011, DPT PM2021).
 *          The host->block flag is set for all the detected ISA boards.
 *          The detect routine no longer enforces LEVEL triggering
 *          for EISA boards, it just prints a warning message.
 *
 *      30 Nov 1994 rev. 1.09 for linux 1.1.68
 *          Redo i/o on target status CHECK_CONDITION for TYPE_DISK only.
 *          Added optional support for using a single board at a time.
 *
 *      18 Nov 1994 rev. 1.08 for linux 1.1.64
 *          Forces sg_tablesize = 64 and can_queue = 64 if these
 *          values are not correctly detected (DPT PM2012).
 *
 *      14 Nov 1994 rev. 1.07 for linux 1.1.63  Final BETA release.
 *      04 Aug 1994 rev. 1.00 for linux 1.1.39  First BETA release.
 *
 *
 *          This driver is based on the CAM (Common Access Method Committee)
 *          EATA (Enhanced AT Bus Attachment) rev. 2.0A, using DMA protocol.
 *
 *  Copyright (C) 1994, 1995, 1996 Dario Ballabio (dario@milano.europe.dg.com)
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that redistributions of source
 *  code retain the above copyright notice and this comment without
 *  modification.
 *
 */

/*
 *
 *  Here is a brief description of the DPT SCSI host adapters.
 *  All these boards provide an EATA/DMA compatible programming interface
 *  and are fully supported by this driver in any configuration, including
 *  multiple SCSI channels:
 *
 *  PM2011B/9X -  Entry Level ISA
 *  PM2021A/9X -  High Performance ISA
 *  PM2012A       Old EISA
 *  PM2012B       Old EISA
 *  PM2022A/9X -  Entry Level EISA
 *  PM2122A/9X -  High Performance EISA
 *  PM2322A/9X -  Extra High Performance EISA
 *  PM3021     -  SmartRAID Adapter for ISA
 *  PM3222     -  SmartRAID Adapter for EISA (PM3222W is 16-bit wide SCSI)
 *  PM3224     -  SmartRAID Adapter for PCI  (PM3224W is 16-bit wide SCSI)
 *
 *  The DPT PM2001 provides only the EATA/PIO interface and hence is not
 *  supported by this driver.
 *
 *  This code has been tested with up to 3 Distributed Processing Technology 
 *  PM2122A/9X (DPT SCSI BIOS v002.D1, firmware v05E.0) EISA controllers,
 *  in any combination of private and shared IRQ.
 *  PCI support has been tested using up to 2 DPT PM3224W (DPT SCSI BIOS 
 *  v003.D0, firmware v07G.0).
 *
 *  Multiple ISA, EISA and PCI boards can be configured in the same system.
 *  It is suggested to put all the EISA boards on the same IRQ level, all
 *  the PCI  boards on another IRQ level, while ISA boards cannot share 
 *  interrupts.
 *
 *  If you configure multiple boards on the same IRQ, the interrupt must
 *  be _level_ triggered (not _edge_ triggered).
 *
 *  This driver detects EATA boards by probes at fixed port addresses,
 *  so no BIOS32 or PCI BIOS support is required.
 *  The suggested way to detect a generic EATA PCI board is to force on it
 *  any unused EISA address, even if there are other controllers on the EISA
 *  bus, or even if you system has no EISA bus at all.
 *  Do not force any ISA address on EATA PCI boards.
 *
 *  If PCI bios support is configured into the kernel, BIOS32 is used to 
 *  include in the list of i/o ports to be probed all the PCI SCSI controllers.
 *
 *  Due to a DPT BIOS "feature", it might not be possible to force an EISA
 *  address on more then a single DPT PCI board, so in this case you have to
 *  let the PCI BIOS assign the addresses.
 *
 *  The sequence of detection probes is:
 *
 *  - ISA 0x1F0; 
 *  - PCI SCSI controllers (only if BIOS32 is available);
 *  - EISA/PCI 0x1C88 through 0xFC88 (corresponding to EISA slots 1 to 15);
 *  - ISA  0x170, 0x230, 0x330.
 * 
 *  The above list of detection probes can be totally replaced by the
 *  boot command line option: "eata=port0, port1, port2,...", where the
 *  port0, port1... arguments are ISA/EISA/PCI addresses to be probed.
 *  For example using "eata=0x7410, 0x7450, 0x230", the driver probes
 *  only the two PCI addresses 0x7410 and 0x7450 and the ISA address 0x230,
 *  in this order; "eata=0" totally disables this driver.
 *
 *  The boards are named EATA0, EATA1,... according to the detection order.
 *
 *  In order to support multiple ISA boards in a reliable way,
 *  the driver sets host->wish_block = TRUE for all ISA boards.
 */

#if defined(MODULE)
#include <linux/module.h>
#include <linux/version.h>
#endif

#include <linux/string.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <asm/io.h>
#include <asm/system.h>
#include <asm/byteorder.h>
#include <linux/proc_fs.h>
#include <linux/blk.h>
#include "scsi.h"
#include "hosts.h"
#include "sd.h"
#include <asm/dma.h>
#include <asm/irq.h>
#include "eata.h"
#include<linux/stat.h>
#include<linux/config.h>
#include<linux/bios32.h>
#include<linux/pci.h>

struct proc_dir_entry proc_scsi_eata2x = {
    PROC_SCSI_EATA2X, 6, "eata2x",
    S_IFDIR | S_IRUGO | S_IXUGO, 2
};

/* Subversion values */
#define ISA  0
#define ESA 1

#undef FORCE_CONFIG

#undef  DEBUG_DETECT
#undef  DEBUG_INTERRUPT
#undef  DEBUG_STATISTICS
#undef  DEBUG_RESET

#define MAX_ISA 4
#define MAX_VESA 0 
#define MAX_EISA 15
#define MAX_PCI 16
#define MAX_BOARDS (MAX_ISA + MAX_VESA + MAX_EISA + MAX_PCI)
#define MAX_CHANNEL 4
#define MAX_LUN 32
#define MAX_TARGET 32
#define MAX_IRQ 16
#define MAX_MAILBOXES 64
#define MAX_SGLIST 64
#define MAX_LARGE_SGLIST 252
#define MAX_INTERNAL_RETRIES 64
#define MAX_CMD_PER_LUN 2

#define SKIP 1
#define FALSE 0
#define TRUE 1
#define FREE 0
#define IN_USE   1
#define LOCKED   2
#define IN_RESET 3
#define IGNORE   4
#define NO_DMA  0xff
#define MAXLOOP 200000

#define REG_CMD         7
#define REG_STATUS      7
#define REG_AUX_STATUS  8
#define REG_DATA        0
#define REG_DATA2       1
#define REG_SEE         6
#define REG_LOW         2
#define REG_LM          3
#define REG_MID         4
#define REG_MSB         5
#define REGION_SIZE     9
#define ISA_RANGE       0x0fff
#define EISA_RANGE      0xfc88
#define BSY_ASSERTED      0x80
#define DRQ_ASSERTED      0x08
#define ABSY_ASSERTED     0x01
#define IRQ_ASSERTED      0x02
#define READ_CONFIG_PIO   0xf0
#define SET_CONFIG_PIO    0xf1
#define SEND_CP_PIO       0xf2
#define RECEIVE_SP_PIO    0xf3
#define TRUNCATE_XFR_PIO  0xf4
#define RESET_PIO         0xf9
#define READ_CONFIG_DMA   0xfd
#define SET_CONFIG_DMA    0xfe
#define SEND_CP_DMA       0xff
#define ASOK              0x00
#define ASST              0x01

#define ARRAY_SIZE(arr) (sizeof (arr) / sizeof (arr)[0])

/* "EATA", in Big Endian format */
#define EATA_SIGNATURE 0x41544145

/* Number of valid bytes in the board config structure for EATA 2.0x */
#define EATA_2_0A_SIZE 28
#define EATA_2_0B_SIZE 30
#define EATA_2_0C_SIZE 34

/* Board info structure */
struct eata_info {
   ulong  data_len;     /* Number of valid bytes after this field */
   ulong  sign;         /* ASCII "EATA" signature */
   unchar        :4,    /* unused low nibble */
	  version:4;    /* EATA version, should be 0x1 */
   unchar  ocsena:1,    /* Overlap Command Support Enabled */
	   tarsup:1,    /* Target Mode Supported */
		 :2,
	   dmasup:1,    /* DMA Supported */
	   drqvld:1,    /* DRQ Index (DRQX) is valid */
	      ata:1,    /* This is an ATA device */
	   haaval:1;    /* Host Adapter Address Valid */
   ushort cp_pad_len;   /* Number of pad bytes after cp_len */
   unchar host_addr[4]; /* Host Adapter SCSI ID for channels 3, 2, 1, 0 */
   ulong  cp_len;       /* Number of valid bytes in cp */
   ulong  sp_len;       /* Number of valid bytes in sp */
   ushort queue_size;   /* Max number of cp that can be queued */
   ushort unused;
   ushort scatt_size;   /* Max number of entries in scatter/gather table */
   unchar     irq:4,    /* Interrupt Request assigned to this controller */
	   irq_tr:1,    /* 0 for edge triggered, 1 for level triggered */
	   second:1,    /* 1 if this is a secondary (not primary) controller */
	     drqx:2;    /* DRQ Index (0=DMA0, 1=DMA7, 2=DMA6, 3=DMA5) */
   unchar  sync;        /* 1 if scsi target id 7...0 is running sync scsi */

   /* Structure extension defined in EATA 2.0B */
   unchar  isaena:1,    /* ISA i/o addressing is disabled/enabled */
	 forcaddr:1,    /* Port address has been forced */
         large_sg:1,    /* 1 if large SG lists are supported */
             res1:1,
		 :4;
   unchar  max_id:5,    /* Max SCSI target ID number */
	 max_chan:3;    /* Max SCSI channel number on this board */

   /* Structure extension defined in EATA 2.0C */
   unchar   max_lun;    /* Max SCSI LUN number */
   unchar        :6,
              pci:1,    /* This board is PCI */
             eisa:1;    /* This board is EISA */
   unchar   notused[2];

   ushort ipad[247];
   };

/* Board config structure */
struct eata_config {
   ushort len;          /* Number of bytes following this field */
   unchar edis:1,       /* Disable EATA interface after config command */
	 ocena:1,       /* Overlapped Commands Enabled */
	mdpena:1,       /* Transfer all Modified Data Pointer Messages */
	tarena:1,       /* Target Mode Enabled for this controller */
	      :4;
   unchar cpad[511];
   };

/* Returned status packet structure */
struct mssp {
   unchar adapter_status:7,    /* State related to current command */
		     eoc:1;    /* End Of Command (1 = command completed) */
   unchar target_status;       /* SCSI status received after data transfer */
   unchar unused[2];
   ulong inv_res_len;          /* Number of bytes not transferred */
   Scsi_Cmnd *SCpnt;           /* Address set in cp */
   char mess[12];
   };

struct sg_list {
   unsigned int address;                /* Segment Address */
   unsigned int num_bytes;              /* Segment Length */
   };

/* MailBox SCSI Command Packet */
struct mscp {
   unchar  sreset:1,     /* SCSI Bus Reset Signal should be asserted */
	     init:1,     /* Re-initialize controller and self test */
	   reqsen:1,     /* Transfer Request Sense Data to addr using DMA */
	       sg:1,     /* Use Scatter/Gather */
		 :1,
	   interp:1,     /* The controller interprets cp, not the target */ 
	     dout:1,     /* Direction of Transfer is Out (Host to Target) */
	      din:1;     /* Direction of Transfer is In (Target to Host) */
   unchar sense_len;     /* Request Sense Length */
   unchar unused[4];
   unchar phsunit:1,     /* Send to Target Physical Unit (bypass RAID) */
	  notused:7;
   unchar  target:5,     /* SCSI target ID */
          channel:3;     /* SCSI channel number */
   unchar     lun:5,     /* SCSI logical unit number */
	   luntar:1,     /* This cp is for Target (not LUN) */
	   dispri:1,     /* Disconnect Privilege granted */
	      one:1;     /* 1 */
   unchar mess[3];       /* Massage to/from Target */
   unchar cdb[12];       /* Command Descriptor Block */
   ulong  data_len;      /* If sg=0 Data Length, if sg=1 sglist length */
   Scsi_Cmnd *SCpnt;     /* Address to be returned in sp */
   ulong  data_address;  /* If sg=0 Data Address, if sg=1 sglist address */
   ulong  sp_addr;       /* Address where sp is DMA'ed when cp completes */
   ulong  sense_addr;    /* Address where Sense Data is DMA'ed on error */
   unsigned int index;   /* cp index */
   struct sg_list *sglist;
   };

struct hostdata {
   struct mscp cp[MAX_MAILBOXES];       /* Mailboxes for this board */
   unsigned int cp_stat[MAX_MAILBOXES]; /* FREE, IN_USE, LOCKED, IN_RESET */
   unsigned int last_cp_used;           /* Index of last mailbox used */
   unsigned int iocount;                /* Total i/o done for this board */
   unsigned int multicount;             /* Total ... in second ihdlr loop */
   int board_number;                    /* Number of this board */
   char board_name[16];                 /* Name of this board */
   char board_id[256];                  /* data from INQUIRY on this board */
   int in_reset;                        /* True if board is doing a reset */
   int target_to[MAX_TARGET][MAX_CHANNEL]; /* N. of timeout errors on target */
   int target_redo[MAX_TARGET][MAX_CHANNEL]; /* If TRUE redo i/o on target */
   unsigned int retries;                /* Number of internal retries */
   unsigned long last_retried_pid;      /* Pid of last retried command */
   unsigned char subversion;            /* Bus type, either ISA or EISA/PCI */
   unsigned char protocol_rev;          /* EATA 2.0 rev., 'A' or 'B' or 'C' */
   struct mssp sp[MAX_MAILBOXES];       /* Returned status for this board */
   };

static struct Scsi_Host *sh[MAX_BOARDS + 1];
static const char *driver_name = "EATA";
static unsigned int irqlist[MAX_IRQ], calls[MAX_IRQ];

static unsigned int io_port[MAX_BOARDS + 1] = { 

   /* First ISA */
   0x1f0,

   /* Space for MAX_PCI ports possibly reported by PCI_BIOS */
    SKIP,    SKIP,   SKIP,   SKIP,   SKIP,   SKIP,   SKIP,   SKIP,
    SKIP,    SKIP,   SKIP,   SKIP,   SKIP,   SKIP,   SKIP,   SKIP,

   /* MAX_EISA ports */
   0x1c88, 0x2c88, 0x3c88, 0x4c88, 0x5c88, 0x6c88, 0x7c88, 0x8c88,
   0x9c88, 0xac88, 0xbc88, 0xcc88, 0xdc88, 0xec88, 0xfc88, 

   /* Other (MAX_ISA - 1) ports */
   0x170,  0x230,  0x330,
 
   /* End of list */
   0x0
   };

#define HD(board) ((struct hostdata *) &sh[board]->hostdata)
#define BN(board) (HD(board)->board_name)

#define H2DEV(x) htonl(x)
#define DEV2H(x) H2DEV(x)
#define V2DEV(addr) ((addr) ? H2DEV(virt_to_bus((void *)addr)) : 0)
#define DEV2V(addr) ((addr) ? DEV2H(bus_to_virt((unsigned long)addr)) : 0)

static void eata2x_interrupt_handler(int, void *, struct pt_regs *);
static int do_trace = FALSE;
static int setup_done = FALSE;

static inline int wait_on_busy(unsigned int iobase) {
   unsigned int loop = MAXLOOP;

   while (inb(iobase + REG_AUX_STATUS) & ABSY_ASSERTED)
      if (--loop == 0) return TRUE;

   return FALSE;
}

static inline int do_dma(unsigned int iobase, unsigned int addr, unchar cmd) {

   if (wait_on_busy(iobase)) return TRUE;

   if ((addr = V2DEV(addr))) {
      outb((char) (addr >> 24), iobase + REG_LOW);
      outb((char) (addr >> 16), iobase + REG_LM);
      outb((char) (addr >> 8),  iobase + REG_MID);
      outb((char)  addr,        iobase + REG_MSB);
      }

   outb(cmd, iobase + REG_CMD);
   return FALSE;
}

static inline int read_pio(unsigned int iobase, ushort *start, ushort *end) {
   unsigned int loop = MAXLOOP;
   ushort *p;

   for (p = start; p <= end; p++) {

      while (!(inb(iobase + REG_STATUS) & DRQ_ASSERTED)) 
	 if (--loop == 0) return TRUE;

      loop = MAXLOOP;
      *p = inw(iobase);
      }

   return FALSE;
}

static inline int port_detect(unsigned int port_base, unsigned int j, 
			      Scsi_Host_Template *tpnt) {
   unsigned char irq, dma_channel, subversion, i;
   unsigned char protocol_rev;
   struct eata_info info;
   char *bus_type;

   /* Allowed DMA channels for ISA (0 indicates reserved) */
   unsigned char dma_channel_table[4] = { 5, 6, 7, 0 };

   char name[16];

   sprintf(name, "%s%d", driver_name, j);

   if(check_region(port_base, REGION_SIZE)) {
      printk("%s: address 0x%03x in use, skipping probe.\n", name, port_base);
      return FALSE;
      }

   if (do_dma(port_base, 0, READ_CONFIG_PIO)) return FALSE;

   /* Read the info structure */
   if (read_pio(port_base, (ushort *)&info, (ushort *)&info.ipad[0])) 
      return FALSE;

   /* Check the controller "EATA" signature */
   if (info.sign != EATA_SIGNATURE) return FALSE;

   if (DEV2H(info.data_len) < EATA_2_0A_SIZE) {
      printk("%s: config structure size (%ld bytes) too short, detaching.\n", 
	     name, DEV2H(info.data_len));
      return FALSE;
      }
   else if (DEV2H(info.data_len) == EATA_2_0A_SIZE)
      protocol_rev = 'A';
   else if (DEV2H(info.data_len) == EATA_2_0B_SIZE)
      protocol_rev = 'B';
   else
      protocol_rev = 'C';

   irq = info.irq;

   if (port_base > ISA_RANGE) {

      if (!info.haaval || info.ata || info.drqvld) {
	 printk("%s: unusable EISA/PCI board found (%d%d%d), detaching.\n", 
		name, info.haaval, info.ata, info.drqvld);
	 return FALSE;
	 }

      subversion = ESA;
      dma_channel = NO_DMA;
      }
   else {

      if (!info.haaval || info.ata || !info.drqvld) {
	 printk("%s: unusable ISA board found (%d%d%d), detaching.\n",
		name, info.haaval, info.ata, info.drqvld);
	 return FALSE;
	 }

      subversion = ISA;
      dma_channel = dma_channel_table[3 - info.drqx];
      }

   if (!info.dmasup)
      printk("%s: warning, DMA protocol support not asserted.\n", name);

   if (subversion == ESA && !info.irq_tr)
      printk("%s: warning, LEVEL triggering is suggested for IRQ %u.\n",
	     name, irq);

   /* Board detected, allocate its IRQ if not already done */
   if ((irq >= MAX_IRQ) || (!irqlist[irq] && request_irq(irq,
              eata2x_interrupt_handler, SA_INTERRUPT, driver_name, NULL))) {
      printk("%s: unable to allocate IRQ %u, detaching.\n", name, irq);
      return FALSE;
      }

   if (subversion == ISA && request_dma(dma_channel, driver_name)) {
      printk("%s: unable to allocate DMA channel %u, detaching.\n",
	     name, dma_channel);
      free_irq(irq, NULL);
      return FALSE;
      }

#if defined (FORCE_CONFIG)
   {
   struct eata_config config;

   /* Set board configuration */
   memset((char *)&config, 0, sizeof(struct eata_config));
   config.len = (ushort) htons((ushort)510);
   config.ocena = TRUE;

   if (do_dma(port_base, (unsigned int)&config, SET_CONFIG_DMA)) {
      printk("%s: busy timeout sending configuration, detaching.\n", name);
      return FALSE;
      }
   }
#endif

   sh[j] = scsi_register(tpnt, sizeof(struct hostdata));

   if (sh[j] == NULL) {
      printk("%s: unable to register host, detaching.\n", name);

      if (!irqlist[irq]) free_irq(irq, NULL);

      if (subversion == ISA) free_dma(dma_channel);

      return FALSE;
      }

   sh[j]->io_port = port_base;
   sh[j]->unique_id = port_base;
   sh[j]->n_io_port = REGION_SIZE;
   sh[j]->dma_channel = dma_channel;
   sh[j]->irq = irq;
   sh[j]->sg_tablesize = (ushort) ntohs(info.scatt_size);
   sh[j]->this_id = (ushort) info.host_addr[3];
   sh[j]->can_queue = (ushort) ntohs(info.queue_size);
   sh[j]->cmd_per_lun = MAX_CMD_PER_LUN;

   /* Register the I/O space that we use */
   request_region(sh[j]->io_port, sh[j]->n_io_port, driver_name);

   memset(HD(j), 0, sizeof(struct hostdata));
   HD(j)->subversion = subversion;
   HD(j)->protocol_rev = protocol_rev;
   HD(j)->board_number = j;
   irqlist[irq]++;

   if (HD(j)->subversion == ESA)
      sh[j]->unchecked_isa_dma = FALSE;
   else {
      sh[j]->wish_block = TRUE;
      sh[j]->unchecked_isa_dma = TRUE;
      disable_dma(dma_channel);
      clear_dma_ff(dma_channel);
      set_dma_mode(dma_channel, DMA_MODE_CASCADE);
      enable_dma(dma_channel);
      }

   strcpy(BN(j), name);

   /* DPT PM2012 does not allow to detect sg_tablesize correctly */
   if (sh[j]->sg_tablesize > MAX_SGLIST || sh[j]->sg_tablesize < 2) {
      printk("%s: detect, wrong n. of SG lists %d, fixed.\n",
             BN(j), sh[j]->sg_tablesize);
      sh[j]->sg_tablesize = MAX_SGLIST;
      }

   /* DPT PM2012 does not allow to detect can_queue correctly */
   if (sh[j]->can_queue > MAX_MAILBOXES || sh[j]->can_queue  < 2) {
      printk("%s: detect, wrong n. of Mbox %d, fixed.\n",
             BN(j), sh[j]->can_queue);
      sh[j]->can_queue = MAX_MAILBOXES;
      }

   if (protocol_rev != 'A') {

      if (info.max_chan > 0 && info.max_chan < MAX_CHANNEL)
         sh[j]->max_channel = info.max_chan;

      if (info.max_id > 7 && info.max_id < MAX_TARGET)
         sh[j]->max_id = info.max_id + 1;

      if (info.large_sg && sh[j]->sg_tablesize == MAX_SGLIST)
         sh[j]->sg_tablesize = MAX_LARGE_SGLIST;
      }

   if (protocol_rev == 'C') {

      if (info.max_lun > 7 && info.max_lun < MAX_LUN)
         sh[j]->max_lun = info.max_lun + 1;
      }

   if (subversion == ESA && protocol_rev == 'C' && info.pci) bus_type = "PCI";
   else if (sh[j]->io_port > EISA_RANGE) bus_type = "PCI";
   else if (subversion == ESA) bus_type = "EISA";
   else bus_type = "ISA";

   for (i = 0; i < sh[j]->can_queue; i++)
      if (! ((&HD(j)->cp[i])->sglist = kmalloc(
            sh[j]->sg_tablesize * sizeof(struct sg_list), 
            (sh[j]->unchecked_isa_dma ? GFP_DMA : 0) | GFP_ATOMIC))) {
         printk("%s: kmalloc SGlist failed, mbox %d, detaching.\n", BN(j), i);
         eata2x_release(sh[j]);
         return FALSE;
         }
      
   printk("%s: rev. 2.0%c, %s, PORT 0x%03x, IRQ %u, DMA %u, SG %d, "\
	  "Mbox %d, CmdLun %d.\n", BN(j), HD(j)->protocol_rev, bus_type,
	   sh[j]->io_port, sh[j]->irq, sh[j]->dma_channel,
	   sh[j]->sg_tablesize, sh[j]->can_queue, sh[j]->cmd_per_lun);

   if (sh[j]->max_id > 8 || sh[j]->max_lun > 8)
      printk("%s: wide SCSI support enabled, max_id %u, max_lun %u.\n",
             BN(j), sh[j]->max_id, sh[j]->max_lun);

   for (i = 0; i <= sh[j]->max_channel; i++)
      printk("%s: SCSI channel %u enabled, host target ID %u.\n",
             BN(j), i, info.host_addr[3 - i]);

#if defined (DEBUG_DETECT)
   printk("%s: Vers. 0x%x, ocs %u, tar %u, SYNC 0x%x, sec. %u, "\
          "infol %ld, cpl %ld spl %ld.\n", name, info.version,
          info.ocsena, info.tarsup, info.sync, info.second,
          DEV2H(info.data_len), DEV2H(info.cp_len), DEV2H(info.sp_len));

   if (protocol_rev == 'B' || protocol_rev == 'C')
      printk("%s: isaena %u, forcaddr %u, max_id %u, max_chan %u, "\
             "large_sg %u, res1 %u.\n", name, info.isaena, info.forcaddr,
             info.max_id, info.max_chan, info.large_sg, info.res1);

   if (protocol_rev == 'C')
      printk("%s: max_lun %u, pci %u, eisa %u.\n", name, 
             info.max_lun, info.pci, info.eisa);
#endif

   return TRUE;
}

void eata2x_setup(char *str, int *ints) {
   int i, argc = ints[0];

   if (argc <= 0) return;

   if (argc > MAX_BOARDS) argc = MAX_BOARDS;

   for (i = 0; i < argc; i++) io_port[i] = ints[i + 1]; 
   
   io_port[i] = 0;
   setup_done = TRUE;
   return;
}

static void add_pci_ports(void) {

#if defined(CONFIG_PCI)

   unsigned short i = 0;
   unsigned char bus, devfn;
   unsigned int addr, k;

   if (!pcibios_present()) return;

   for (k = 0; k < MAX_PCI; k++) {

      if (pcibios_find_class(PCI_CLASS_STORAGE_SCSI << 8, i++, &bus, &devfn)
             != PCIBIOS_SUCCESSFUL) break;

      if (pcibios_read_config_dword(bus, devfn, PCI_BASE_ADDRESS_0, &addr)
             != PCIBIOS_SUCCESSFUL) continue;

#if defined(DEBUG_DETECT)
      printk("%s: detect, seq. %d, bus %d, devfn 0x%x, addr 0x%x.\n",
             driver_name, k, bus, devfn, addr);
#endif

      if ((addr & PCI_BASE_ADDRESS_SPACE) != PCI_BASE_ADDRESS_SPACE_IO)
             continue;

      /* Reverse the returned address order */
      io_port[MAX_PCI - k] = 
             (addr & PCI_BASE_ADDRESS_IO_MASK) + PCI_BASE_ADDRESS_0;
      }
#endif

   return;
}

int eata2x_detect(Scsi_Host_Template *tpnt) {
   unsigned long flags;
   unsigned int j = 0, k;

   tpnt->proc_dir = &proc_scsi_eata2x;

   save_flags(flags);
   cli();

   for (k = 0; k < MAX_IRQ; k++) {
      irqlist[k] = 0;
      calls[k] = 0;
      }

   for (k = 0; k < MAX_BOARDS + 1; k++) sh[k] = NULL;

   if (!setup_done) add_pci_ports();

   for (k = 0; io_port[k]; k++) {

      if (io_port[k] == SKIP) continue;

      if (j < MAX_BOARDS && port_detect(io_port[k], j, tpnt)) j++;
      }

   if (j > 0) 
      printk("EATA/DMA 2.0x: Copyright (C) 1994, 1995, 1996 Dario Ballabio.\n");

   restore_flags(flags);
   return j;
}

static inline void build_sg_list(struct mscp *cpp, Scsi_Cmnd *SCpnt) {
   unsigned int k;
   struct scatterlist *sgpnt;

   sgpnt = (struct scatterlist *) SCpnt->request_buffer;

   for (k = 0; k < SCpnt->use_sg; k++) {
      cpp->sglist[k].address = V2DEV(sgpnt[k].address);
      cpp->sglist[k].num_bytes = H2DEV(sgpnt[k].length);
      }

   cpp->data_address = V2DEV(cpp->sglist);
   cpp->data_len = H2DEV((SCpnt->use_sg * sizeof(struct sg_list)));
}

int eata2x_queuecommand(Scsi_Cmnd *SCpnt, void (*done)(Scsi_Cmnd *)) {
   unsigned long flags;
   unsigned int i, j, k;
   struct mscp *cpp;
   struct mssp *spp;

   static const unsigned char data_out_cmds[] = {
      0x0a, 0x2a, 0x15, 0x55, 0x04, 0x07, 0x0b, 0x10, 0x16, 0x18, 0x1d, 
      0x24, 0x2b, 0x2e, 0x30, 0x31, 0x32, 0x38, 0x39, 0x3a, 0x3b, 0x3d, 
      0x3f, 0x40, 0x41, 0x4c, 0xaa, 0xae, 0xb0, 0xb1, 0xb2, 0xb6, 0xea
      };

   save_flags(flags);
   cli();
   /* j is the board number */
   j = ((struct hostdata *) SCpnt->host->hostdata)->board_number;

   if (!done) panic("%s: qcomm, pid %ld, null done.\n", BN(j), SCpnt->pid);

   /* i is the mailbox number, look for the first free mailbox 
      starting from last_cp_used */
   i = HD(j)->last_cp_used + 1;

   for (k = 0; k < sh[j]->can_queue; k++, i++) {

      if (i >= sh[j]->can_queue) i = 0;

      if (HD(j)->cp_stat[i] == FREE) {
	 HD(j)->last_cp_used = i;
	 break;
	 }
      }

   if (k == sh[j]->can_queue) {
      printk("%s: qcomm, no free mailbox, resetting.\n", BN(j));

      if (HD(j)->in_reset) 
	 printk("%s: qcomm, already in reset.\n", BN(j));
      else if (eata2x_reset(SCpnt, SCSI_RESET_SUGGEST_BUS_RESET)
               == SCSI_RESET_SUCCESS) 
	 panic("%s: qcomm, SCSI_RESET_SUCCESS.\n", BN(j));

      SCpnt->result = DID_BUS_BUSY << 16; 
      SCpnt->host_scribble = NULL;
      printk("%s: qcomm, pid %ld, DID_BUS_BUSY, done.\n", BN(j), SCpnt->pid);
      restore_flags(flags);
      done(SCpnt);    
      return 0;
      }

   /* Set pointer to control packet structure */
   cpp = &HD(j)->cp[i];

   memset(cpp, 0, sizeof(struct mscp) - sizeof(struct sg_list *));

   /* Set pointer to status packet structure */
   spp = &HD(j)->sp[i];

   memset(spp, 0, sizeof(struct mssp));

   /* The EATA protocol uses Big Endian format */
   cpp->sp_addr = V2DEV(spp);

   SCpnt->scsi_done = done;
   cpp->index = i;
   SCpnt->host_scribble = (unsigned char *) &cpp->index;

   if (do_trace) printk("%s: qcomm, mbox %d, target %d.%d:%d, pid %ld.\n",
			BN(j), i, SCpnt->channel, SCpnt->target,
                        SCpnt->lun, SCpnt->pid);

   for (k = 0; k < ARRAY_SIZE(data_out_cmds); k++)
     if (SCpnt->cmnd[0] == data_out_cmds[k]) {
	cpp->dout = TRUE;
	break;
	}

   cpp->din = !cpp->dout;
   cpp->reqsen = TRUE;
   cpp->dispri = TRUE;
   cpp->one = TRUE;
   cpp->channel = SCpnt->channel;
   cpp->target = SCpnt->target;
   cpp->lun = SCpnt->lun;  
   cpp->SCpnt = SCpnt;
   cpp->sense_addr = V2DEV(SCpnt->sense_buffer); 
   cpp->sense_len = sizeof SCpnt->sense_buffer;

   if (SCpnt->use_sg) {
      cpp->sg = TRUE;
      build_sg_list(cpp, SCpnt);
      }
   else {
      cpp->data_address = V2DEV(SCpnt->request_buffer);
      cpp->data_len = H2DEV(SCpnt->request_bufflen);
      }

   memcpy(cpp->cdb, SCpnt->cmnd, SCpnt->cmd_len);

   /* Send control packet to the board */
   if (do_dma(sh[j]->io_port, (unsigned int) cpp, SEND_CP_DMA)) {
      SCpnt->result = DID_ERROR << 16; 
      SCpnt->host_scribble = NULL;
      printk("%s: qcomm, target %d.%d:%d, pid %ld, adapter busy, DID_ERROR,"\
             " done.\n", BN(j), SCpnt->channel, SCpnt->target, SCpnt->lun,
             SCpnt->pid);
      restore_flags(flags);
      done(SCpnt);    
      return 0;
      }

   HD(j)->cp_stat[i] = IN_USE;
   restore_flags(flags);
   return 0;
}

int eata2x_abort(Scsi_Cmnd *SCarg) {
   unsigned long flags;
   unsigned int i, j;

   save_flags(flags);
   cli();
   j = ((struct hostdata *) SCarg->host->hostdata)->board_number;

   if (SCarg->host_scribble == NULL) {
      printk("%s: abort, target %d.%d:%d, pid %ld inactive.\n",
	     BN(j), SCarg->channel, SCarg->target, SCarg->lun, SCarg->pid);
      restore_flags(flags);
      return SCSI_ABORT_NOT_RUNNING;
      }

   i = *(unsigned int *)SCarg->host_scribble;
   printk("%s: abort, mbox %d, target %d.%d:%d, pid %ld.\n", 
	  BN(j), i, SCarg->channel, SCarg->target, SCarg->lun, SCarg->pid);

   if (i >= sh[j]->can_queue)
      panic("%s: abort, invalid SCarg->host_scribble.\n", BN(j));

   if (wait_on_busy(sh[j]->io_port)) {
      printk("%s: abort, timeout error.\n", BN(j));
      restore_flags(flags);
      return SCSI_ABORT_ERROR;
      }

   if (HD(j)->cp_stat[i] == FREE) {
      printk("%s: abort, mbox %d is free.\n", BN(j), i);
      restore_flags(flags);
      return SCSI_ABORT_NOT_RUNNING;
      }

   if (HD(j)->cp_stat[i] == IN_USE) {
      printk("%s: abort, mbox %d is in use.\n", BN(j), i);

      if (SCarg != HD(j)->cp[i].SCpnt)
	 panic("%s: abort, mbox %d, SCarg %p, cp SCpnt %p.\n",
	       BN(j), i, SCarg, HD(j)->cp[i].SCpnt);

      if (inb(sh[j]->io_port + REG_AUX_STATUS) & IRQ_ASSERTED)
         printk("%s: abort, mbox %d, interrupt pending.\n", BN(j), i);

      restore_flags(flags);
      return SCSI_ABORT_SNOOZE;
      }

   if (HD(j)->cp_stat[i] == IN_RESET) {
      printk("%s: abort, mbox %d is in reset.\n", BN(j), i);
      restore_flags(flags);
      return SCSI_ABORT_ERROR;
      }

   if (HD(j)->cp_stat[i] == LOCKED) {
      printk("%s: abort, mbox %d is locked.\n", BN(j), i);
      restore_flags(flags);
      return SCSI_ABORT_NOT_RUNNING;
      }
   restore_flags(flags);
   panic("%s: abort, mbox %d, invalid cp_stat.\n", BN(j), i);
}

int eata2x_reset(Scsi_Cmnd *SCarg, unsigned int reset_flags) {
   unsigned long flags;
   unsigned int i, j, time, k, c, limit = 0;
   int arg_done = FALSE;
   Scsi_Cmnd *SCpnt;

   save_flags(flags);
   cli();
   j = ((struct hostdata *) SCarg->host->hostdata)->board_number;
   printk("%s: reset, enter, target %d.%d:%d, pid %ld, reset_flags %u.\n", 
	  BN(j), SCarg->channel, SCarg->target, SCarg->lun, SCarg->pid,
          reset_flags);

   if (SCarg->host_scribble == NULL)
      printk("%s: reset, pid %ld inactive.\n", BN(j), SCarg->pid);

   if (HD(j)->in_reset) {
      printk("%s: reset, exit, already in reset.\n", BN(j));
      restore_flags(flags);
      return SCSI_RESET_ERROR;
      }

   if (wait_on_busy(sh[j]->io_port)) {
      printk("%s: reset, exit, timeout error.\n", BN(j));
      restore_flags(flags);
      return SCSI_RESET_ERROR;
      }

   HD(j)->retries = 0;

   for (c = 0; c <= sh[j]->max_channel; c++)
      for (k = 0; k < sh[j]->max_id; k++) {
         HD(j)->target_redo[k][c] = TRUE;
         HD(j)->target_to[k][c] = 0;
         }

   for (i = 0; i < sh[j]->can_queue; i++) {

      if (HD(j)->cp_stat[i] == FREE) continue;

      if (HD(j)->cp_stat[i] == LOCKED) {
	 HD(j)->cp_stat[i] = FREE;
	 printk("%s: reset, locked mbox %d forced free.\n", BN(j), i);
	 continue;
	 }

      SCpnt = HD(j)->cp[i].SCpnt;
      HD(j)->cp_stat[i] = IN_RESET;
      printk("%s: reset, mbox %d in reset, pid %ld.\n",
	     BN(j), i, SCpnt->pid);

      if (SCpnt == NULL)
	 panic("%s: reset, mbox %d, SCpnt == NULL.\n", BN(j), i);

      if (SCpnt->host_scribble == NULL)
	 panic("%s: reset, mbox %d, garbled SCpnt.\n", BN(j), i);

      if (*(unsigned int *)SCpnt->host_scribble != i) 
	 panic("%s: reset, mbox %d, index mismatch.\n", BN(j), i);

      if (SCpnt->scsi_done == NULL) 
	 panic("%s: reset, mbox %d, SCpnt->scsi_done == NULL.\n", BN(j), i);

      if (SCpnt == SCarg) arg_done = TRUE;
      }

   if (do_dma(sh[j]->io_port, 0, RESET_PIO)) {
      printk("%s: reset, cannot reset, timeout error.\n", BN(j));
      restore_flags(flags);
      return SCSI_RESET_ERROR;
      }

   printk("%s: reset, board reset done, enabling interrupts.\n", BN(j));

#if defined (DEBUG_RESET)
   do_trace = TRUE;
#endif

   HD(j)->in_reset = TRUE;
   sti();
   time = jiffies;
   while ((jiffies - time) < HZ && limit++ < 100000000);
   cli();
   printk("%s: reset, interrupts disabled, loops %d.\n", BN(j), limit);

   for (i = 0; i < sh[j]->can_queue; i++) {

      /* Skip mailboxes already set free by interrupt */
      if (HD(j)->cp_stat[i] != IN_RESET) continue;

      SCpnt = HD(j)->cp[i].SCpnt;
      SCpnt->result = DID_RESET << 16;
      SCpnt->host_scribble = NULL;

      /* This mailbox is still waiting for its interrupt */
      HD(j)->cp_stat[i] = LOCKED;

      printk("%s, reset, mbox %d locked, DID_RESET, pid %ld done.\n",
	     BN(j), i, SCpnt->pid);
      restore_flags(flags);
      SCpnt->scsi_done(SCpnt);
      cli();
      }

   HD(j)->in_reset = FALSE;
   do_trace = FALSE;
   restore_flags(flags);

   if (arg_done) {
      printk("%s: reset, exit, success.\n", BN(j));
      return SCSI_RESET_SUCCESS;
      }
   else {
      printk("%s: reset, exit, wakeup.\n", BN(j));
      return SCSI_RESET_PUNT;
      }
}

static void eata2x_interrupt_handler(int irq, void *dev_id,
                                     struct pt_regs *regs) {
   Scsi_Cmnd *SCpnt;
   unsigned long flags;
   unsigned int i, j, k, c, status, tstatus, loops, total_loops = 0;
   struct mssp *spp;
   struct mscp *cpp;

   save_flags(flags);
   cli();

   if (!irqlist[irq]) {
      printk("%s, ihdlr, irq %d, unexpected interrupt.\n", driver_name, irq);
      restore_flags(flags);
      return;
      }

   if (do_trace) printk("%s: ihdlr, enter, irq %d, calls %d.\n", 
			driver_name, irq, calls[irq]);

   /* Service all the boards configured on this irq */
   for (j = 0; sh[j] != NULL; j++) {

      if (sh[j]->irq != irq) continue;

      loops = 0;

      /* Loop until all interrupts for a board are serviced */
      while (inb(sh[j]->io_port + REG_AUX_STATUS) & IRQ_ASSERTED) {
	 total_loops++;
	 loops++;

	 if (do_trace) printk("%s: ihdlr, start service, count %d.\n",
			      BN(j), HD(j)->iocount);
   
	 /* Read the status register to clear the interrupt indication */
	 inb(sh[j]->io_port + REG_STATUS);
   
	 /* Service all mailboxes of this board */
	 for (i = 0; i < sh[j]->can_queue; i++) {
	    spp = &HD(j)->sp[i];
   
	    /* Check if this mailbox has completed the operation */
	    if (spp->eoc == FALSE) continue;
   
	    spp->eoc = FALSE;
   
	    if (HD(j)->cp_stat[i] == IGNORE) {
	       HD(j)->cp_stat[i] = FREE;
	       continue;
	       }
	    else if (HD(j)->cp_stat[i] == LOCKED) {
	       HD(j)->cp_stat[i] = FREE;
	       printk("%s: ihdlr, mbox %d unlocked, count %d.\n",
		      BN(j), i, HD(j)->iocount);
	       continue;
	       }
	    else if (HD(j)->cp_stat[i] == FREE) {
	       printk("%s: ihdlr, mbox %d is free, count %d.\n", 
		      BN(j), i, HD(j)->iocount);
	       continue;
	       }
	    else if (HD(j)->cp_stat[i] == IN_RESET)
	       printk("%s: ihdlr, mbox %d is in reset.\n", BN(j), i);
	    else if (HD(j)->cp_stat[i] != IN_USE) 
	       panic("%s: ihdlr, mbox %d, invalid cp_stat.\n", BN(j), i);
   
	    HD(j)->cp_stat[i] = FREE;
	    cpp = &HD(j)->cp[i];
	    SCpnt = spp->SCpnt;
   
	    if (SCpnt == NULL)
	       panic("%s: ihdlr, mbox %d, SCpnt == NULL.\n", BN(j), i);
   
	    if (SCpnt != cpp->SCpnt)
	       panic("%s: ihdlr, mbox %d, sp SCpnt %p, cp SCpnt %p.\n",
		     BN(j), i, SCpnt, cpp->SCpnt);
   
	    if (SCpnt->host_scribble == NULL)
	       panic("%s: ihdlr, mbox %d, pid %ld, SCpnt %p garbled.\n",
		     BN(j), i, SCpnt->pid, SCpnt);
   
	    if (*(unsigned int *)SCpnt->host_scribble != i) 
	       panic("%s: ihdlr, mbox %d, pid %ld, index mismatch %d,"\
		     " irq %d.\n", BN(j), i, SCpnt->pid, 
		     *(unsigned int *)SCpnt->host_scribble, irq);
   
	    tstatus = status_byte(spp->target_status);
   
	    switch (spp->adapter_status) {
	       case ASOK:     /* status OK */
   
		  /* Forces a reset if a disk drive keeps returning BUSY */
		  if (tstatus == BUSY && SCpnt->device->type != TYPE_TAPE) 
		     status = DID_ERROR << 16;
   
		  /* If there was a bus reset, redo operation on each target */
		  else if (tstatus != GOOD && SCpnt->device->type == TYPE_DISK
     		           && HD(j)->target_redo[SCpnt->target][SCpnt->channel])
		     status = DID_BUS_BUSY << 16;
   
		  /* Works around a flaw in scsi.c */
		  else if (tstatus == CHECK_CONDITION
			   && SCpnt->device->type == TYPE_DISK
			   && (SCpnt->sense_buffer[2] & 0xf) == RECOVERED_ERROR)
		     status = DID_BUS_BUSY << 16;

		  else
		     status = DID_OK << 16;
   
		  if (tstatus == GOOD)
		     HD(j)->target_redo[SCpnt->target][SCpnt->channel] = FALSE;
   
		  if (spp->target_status && SCpnt->device->type == TYPE_DISK)
		     printk("%s: ihdlr, target %d.%d:%d, pid %ld, "\
                            "target_status 0x%x, sense key 0x%x.\n", BN(j), 
			    SCpnt->channel, SCpnt->target, SCpnt->lun, 
                            SCpnt->pid, spp->target_status, 
                            SCpnt->sense_buffer[2]);
   
		  HD(j)->target_to[SCpnt->target][SCpnt->channel] = 0;
   
                  if (HD(j)->last_retried_pid == SCpnt->pid) HD(j)->retries = 0;

		  break;
	       case ASST:     /* Selection Time Out */
	       case 0x02:     /* Command Time Out   */
   
		  if (HD(j)->target_to[SCpnt->target][SCpnt->channel] > 1)
		     status = DID_ERROR << 16;
		  else {
		     status = DID_TIME_OUT << 16;
		     HD(j)->target_to[SCpnt->target][SCpnt->channel]++;
		     }
   
		  break;

               /* Perform a limited number of internal retries */
	       case 0x03:     /* SCSI Bus Reset Received */
	       case 0x04:     /* Initial Controller Power-up */
   
		  for (c = 0; c <= sh[j]->max_channel; c++) 
		     for (k = 0; k < sh[j]->max_id; k++) 
		        HD(j)->target_redo[k][c] = TRUE;
   
	          if (SCpnt->device->type != TYPE_TAPE
                      && HD(j)->retries < MAX_INTERNAL_RETRIES) {
		     status = DID_BUS_BUSY << 16;
		     HD(j)->retries++;
                     HD(j)->last_retried_pid = SCpnt->pid;
                     }
	          else 
		     status = DID_ERROR << 16;

		  break;
	       case 0x07:     /* Bus Parity Error */
	       case 0x0c:     /* Controller Ram Parity */
	       case 0x05:     /* Unexpected Bus Phase */
	       case 0x06:     /* Unexpected Bus Free */
	       case 0x08:     /* SCSI Hung */
	       case 0x09:     /* Unexpected Message Reject */
	       case 0x0a:     /* SCSI Bus Reset Stuck */
	       case 0x0b:     /* Auto Request-Sense Failed */
	       default:
		  status = DID_ERROR << 16;
		  break;
	       }
   
	    SCpnt->result = status | spp->target_status;
	    HD(j)->iocount++;

	    if (loops > 1) HD(j)->multicount++;

#if defined (DEBUG_INTERRUPT)
	    if (SCpnt->result || do_trace)
#else
	    if ((spp->adapter_status != ASOK && HD(j)->iocount >  1000) ||
		(spp->adapter_status != ASOK && 
		 spp->adapter_status != ASST && HD(j)->iocount <= 1000) ||
		do_trace)
#endif
	       printk("%s: ihdlr, mbox %2d, err 0x%x:%x,"\
		      " target %d.%d:%d, pid %ld, count %d.\n",
		      BN(j), i, spp->adapter_status, spp->target_status,
		      SCpnt->channel, SCpnt->target, SCpnt->lun, SCpnt->pid,
                      HD(j)->iocount);
   
	    /* Set the command state to inactive */
	    SCpnt->host_scribble = NULL;
   
	    restore_flags(flags);
	    SCpnt->scsi_done(SCpnt);
	    cli();

	    }   /* Mailbox loop */

	 }   /* Multiple command loop */

      }   /* Boards loop */

   calls[irq]++;

   if (total_loops == 0) 
     printk("%s: ihdlr, irq %d, no command completed, calls %d.\n",
	    driver_name, irq, calls[irq]);

   if (do_trace) printk("%s: ihdlr, exit, irq %d, calls %d.\n", 
			driver_name, irq, calls[irq]);

#if defined (DEBUG_STATISTICS)
   if ((calls[irq] % 100000) == 10000)
      for (j = 0; sh[j] != NULL; j++)
	 printk("%s: ihdlr, calls %d, count %d, multi %d.\n", BN(j),
		calls[(sh[j]->irq)], HD(j)->iocount, HD(j)->multicount);
#endif

   restore_flags(flags);
   return;
}

int eata2x_release(struct Scsi_Host *shpnt) {
   unsigned long flags;
   unsigned int i, j;

   save_flags(flags);
   cli();

   for (j = 0; sh[j] != NULL && sh[j] != shpnt; j++);
    
   if (sh[j] == NULL) panic("%s: release, invalid Scsi_Host pointer.\n",
                            driver_name);

   for (i = 0; i < sh[j]->can_queue; i++) 
      if ((&HD(j)->cp[i])->sglist) kfree((&HD(j)->cp[i])->sglist);

   if (! --irqlist[sh[j]->irq]) free_irq(sh[j]->irq, NULL);

   if (sh[j]->dma_channel != NO_DMA) free_dma(sh[j]->dma_channel);

   release_region(sh[j]->io_port, sh[j]->n_io_port);
   scsi_unregister(sh[j]);
   restore_flags(flags);
   return FALSE;
}

#if defined(MODULE)
Scsi_Host_Template driver_template = EATA;

#include "scsi_module.c"
#endif
