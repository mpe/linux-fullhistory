/*
 * mac_scsi.c   -- Device dependent functions for the Macintosh NCR5380 SCSI 
 *		   port (MacII style machines, no DMA).
 *
 * based on:
 */

/*
 * atari_scsi.c -- Device dependent functions for the Atari generic SCSI port
 *
 * Copyright 1994 Roman Hodek <Roman.Hodek@informatik.uni-erlangen.de>
 *
 *   Loosely based on the work of Robert De Vries' team and added:
 *    - working real DMA
 *    - Falcon support (untested yet!)   ++bjoern fixed and now it works
 *    - lots of extensions and bug fixes.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 */


#include <linux/config.h>
#include <linux/module.h>

#define NDEBUG_ABORT	0x800000
#define NDEBUG_TAGS	0x1000000
#define NDEBUG_MERGING	0x2000000

#define NDEBUG (0)

#define AUTOSENSE
/* MSch: Tested the pseudo-DMA code on Atari for the Mac68k port ... */
/* Defining neither PSEUDO_DMA nor REAL_DMA -> PIO transfer, sloooow ! */
/*#define	REAL_DMA*/ /* never supported on NCR5380 Macs */
/*
 * Usage: define PSEUDO_DMA to use hardware-handshaked PIO mode (TBI)
 *        undef  PSEUDO_DMA to use pure PIO mode
 */

/*#define PSEUDO_DMA*/	/* currently gives trouble on some Macs */

#ifdef PSEUDO_DMA
#define EMULATE_PSEUDO_DMA
#define DMA_WORKS_RIGHT
#define UNSAFE
#endif

/* Support tagged queuing? (on devices that are able to... :-) */
#define	SUPPORT_TAGS
#define	MAX_TAGS 32

#include <linux/types.h>
#include <linux/stddef.h>
#include <linux/ctype.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/blk.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <asm/setup.h>
#include <asm/machw.h>
#include <asm/macints.h>
#include <asm/macintosh.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/bitops.h>

#include "scsi.h"
#include "hosts.h"
#include "mac_scsi.h"
#include "NCR5380.h"
#include "constants.h"
#include <asm/io.h>

#include <linux/stat.h>

struct proc_dir_entry proc_scsi_mac = {
    PROC_SCSI_MAC, 8, "mac_5380",
    S_IFDIR | S_IRUGO | S_IXUGO, 2
};

/*
 * Define RBV_HACK to run the SCSI driver on RBV Macs; undefine to
 * try getting better interrupt latency
 */
#define RBV_HACK

#ifdef RBV_HACK
#define	ENABLE_IRQ()	mac_turnon_irq( IRQ_MAC_SCSI ); 
#define	DISABLE_IRQ()	mac_turnoff_irq( IRQ_MAC_SCSI );
#else
#define	ENABLE_IRQ()	mac_enable_irq( IRQ_MAC_SCSI ); 
#define	DISABLE_IRQ()	mac_disable_irq( IRQ_MAC_SCSI );
#endif

#define HOSTDATA_DMALEN		(((struct NCR5380_hostdata *) \
				(mac_scsi_host->hostdata))->dma_len)

/* Time (in jiffies) to wait after a reset; the SCSI standard calls for 250ms,
 * we usually do 0.5s to be on the safe side. But Toshiba CD-ROMs once more
 * need ten times the standard value... */
#ifndef CONFIG_MAC_SCSI_TOSHIBA_DELAY
#define	AFTER_RESET_DELAY	(HZ/2)
#else
#define	AFTER_RESET_DELAY	(5*HZ/2)
#endif

/***************************** Prototypes *****************************/

#ifdef REAL_DMA
static int scsi_dma_is_ignored_buserr( unsigned char dma_stat );
static void mac_scsi_fetch_restbytes( void );
static long mac_scsi_dma_residual( struct Scsi_Host *instance );
static int mac_classify_cmd( Scsi_Cmnd *cmd );
static unsigned long mac_dma_xfer_len( unsigned long wanted_len,
                                       Scsi_Cmnd *cmd, int write_flag );
#endif
#ifdef PSEUDO_DMA
static int mac_pdma_read(struct Scsi_Host *instance, unsigned char *dst,
			   int len);
static int mac_pdma_write(struct Scsi_Host *instance, unsigned char *src,
			   int len);
static unsigned long mac_dma_xfer_len( unsigned long wanted_len,
                                         Scsi_Cmnd *cmd, int write_flag );
#endif
static void scsi_mac_intr( int irq, void *dummy, struct pt_regs *fp);
static void mac_scsi_reset_boot( void );
static unsigned char mac_scsi_reg_read( unsigned char reg );
static void mac_scsi_reg_write( unsigned char reg, unsigned char value);

/************************* End of Prototypes **************************/


static struct Scsi_Host *mac_scsi_host = NULL;
#if 0
static unsigned char (*mac_scsi_reg_read)( unsigned char reg );
static void (*mac_scsi_reg_write)( unsigned char reg, unsigned char value );
#endif

#ifdef REAL_DMA
static unsigned long	mac_dma_residual, mac_dma_startaddr;
static short		mac_dma_active;
/* pointer to the dribble buffer */
static char		*mac_dma_buffer = NULL;
/* precalculated physical address of the dribble buffer */
static unsigned long	mac_dma_phys_buffer;
/* != 0 tells the int handler to copy data from the dribble buffer */
static char		*mac_dma_orig_addr;
/* size of the dribble buffer; 4k seems enough, since the Falcon cannot use
 * scatter-gather anyway, so most transfers are 1024 byte only. In the rare
 * cases where requests to physical contiguous buffers have been merged, this
 * request is <= 4k (one page). So I don't think we have to split transfers
 * just due to this buffer size...
 */
#define	MAC_BUFFER_SIZE	(4096)
#if 1  /* FIXME: is that an issue for Macs?? */
/* mask for address bits that can't be used with the ST-DMA */
static unsigned long	mac_dma_stram_mask;
#define STRAM_ADDR(a)	(((a) & mac_dma_stram_mask) == 0)
#endif
/* number of bytes to cut from a transfer to handle NCR overruns */
static int mac_read_overruns = 0;
#endif
#ifdef PSEUDO_DMA
static unsigned long	mac_pdma_residual, mac_pdma_startaddr, mac_pdma_current;
static short		mac_pdma_active;
/* FIXME: is that an issue for Macs?? */
/* mask for address bits that can't be used with the ST-DMA */
static unsigned long	mac_dma_stram_mask;
#define STRAM_ADDR(a)	(((a) & mac_dma_stram_mask) == 0)
static int mac_read_overruns = 0;
#endif
static int setup_can_queue = -1;
static int setup_cmd_per_lun = -1;
static int setup_sg_tablesize = -1;
#ifdef SUPPORT_TAGS
static int setup_use_tagged_queuing = -1;
#endif
static int setup_hostid = -1;


#if defined(REAL_DMA)

#if 0  /* FIXME */
static int scsi_dma_is_ignored_buserr( unsigned char dma_stat )
{
	int i;
	unsigned long	addr = SCSI_DMA_READ_P( dma_addr ), end_addr;

	if (dma_stat & 0x01) {

		/* A bus error happens when DMA-ing from the last page of a
		 * physical memory chunk (DMA prefetch!), but that doesn't hurt.
		 * Check for this case:
		 */
		
		for( i = 0; i < boot_info.num_memory; ++i ) {
			end_addr = boot_info.memory[i].addr +
				boot_info.memory[i].size;
			if (end_addr <= addr && addr <= end_addr + 4)
				return( 1 );
		}
	}
	return( 0 );
}


/* Dead code... wasn't called anyway :-) and causes some trouble, because at
 * end-of-DMA, both SCSI ints are triggered simultaneously, so the NCR int has
 * to clear the DMA int pending bit before it allows other level 6 interrupts.
 */
static void scsi_dma_buserr (int irq, void *dummy, struct pt_regs *fp)
{
	unsigned char	dma_stat = tt_scsi_dma.dma_ctrl;

	/* Don't do anything if a NCR interrupt is pending. Probably it's just
	 * masked... */
	if (mac_irq_pending( IRQ_MAC_SCSI ))
		return;
	
	printk("Bad SCSI DMA interrupt! dma_addr=0x%08lx dma_stat=%02x dma_cnt=%08lx\n",
	       SCSI_DMA_READ_P(dma_addr), dma_stat, SCSI_DMA_READ_P(dma_cnt));
	if (dma_stat & 0x80) {
		if (!scsi_dma_is_ignored_buserr( dma_stat ))
			printk( "SCSI DMA bus error -- bad DMA programming!\n" );
	}
	else {
		/* Under normal circumstances we never should get to this point,
		 * since both interrupts are triggered simultaneously and the 5380
		 * int has higher priority. When this irq is handled, that DMA
		 * interrupt is cleared. So a warning message is printed here.
		 */
		printk( "SCSI DMA intr ?? -- this shouldn't happen!\n" );
	}
}
#endif

#endif

void restore_irq(struct pt_regs *regs)
{
	unsigned long flags;

	save_flags(flags);
	flags = (flags & ~0x0700) | (regs->sr & 0x0700);
	restore_flags(flags);
}

static int polled_scsi_on = 0;
static unsigned char *mac_scsi_regp = NULL;

void scsi_mac_polled (void)
{
	if (polled_scsi_on)
	{
		if(NCR5380_read(BUS_AND_STATUS_REG)&BASR_IRQ)
		{
			printk("SCSI poll\n");
			scsi_mac_intr(IRQ_MAC_SCSI, NULL, NULL);
		}
	}
}

static void scsi_mac_intr (int irq, void *dummy, struct pt_regs *fp)
{
	unsigned long flags;
#ifdef REAL_DMA
	int dma_stat;

	dma_stat = mac_scsi_dma.dma_ctrl;

	INT_PRINTK("scsi%d: NCR5380 interrupt, DMA status = %02x\n",
		   mac_scsi_host->host_no, dma_stat & 0xff);

	/* Look if it was the DMA that has interrupted: First possibility
	 * is that a bus error occurred...
	 */
	if (dma_stat & 0x80) {
		if (!scsi_dma_is_ignored_buserr( dma_stat )) {
			printk(KERN_ERR "SCSI DMA caused bus error near 0x%08lx\n",
			       SCSI_DMA_READ_P(dma_addr));
			printk(KERN_CRIT "SCSI DMA bus error -- bad DMA programming!");
		}
	}

	/* If the DMA is active but not finished, we have the the case
	 * that some other 5380 interrupt occurred within the DMA transfer.
	 * This means we have residual bytes, if the desired end address
	 * is not yet reached. Maybe we have to fetch some bytes from the
	 * rest data register, too. The residual must be calculated from
	 * the address pointer, not the counter register, because only the
	 * addr reg counts bytes not yet written and pending in the rest
	 * data reg!
	 */
	if ((dma_stat & 0x02) && !(dma_stat & 0x40)) {
		mac_dma_residual = HOSTDATA_DMALEN - (SCSI_DMA_READ_P( dma_addr ) -
												mac_dma_startaddr);

		DMA_PRINTK("SCSI DMA: There are %ld residual bytes.\n",
			   mac_dma_residual);

		if ((signed int)mac_dma_residual < 0)
			mac_dma_residual = 0;
		if ((dma_stat & 1) == 0) {
			/* After read operations, we maybe have to
			   transport some rest bytes */
			mac_scsi_fetch_restbytes();
		}
		else {
			/* There seems to be a nasty bug in some SCSI-DMA/NCR
			   combinations: If a target disconnects while a write
			   operation is going on, the address register of the
			   DMA may be a few bytes farer than it actually read.
			   This is probably due to DMA prefetching and a delay
			   between DMA and NCR.  Experiments showed that the
			   dma_addr is 9 bytes to high, but this could vary.
			   The problem is, that the residual is thus calculated
			   wrong and the next transfer will start behind where
			   it should.  So we round up the residual to the next
			   multiple of a sector size, if it isn't already a
			   multiple and the originally expected transfer size
			   was.  The latter condition is there to ensure that
			   the correction is taken only for "real" data
			   transfers and not for, e.g., the parameters of some
			   other command.  These shouldn't disconnect anyway.
			   */
			if (mac_dma_residual & 0x1ff) {
				DMA_PRINTK("SCSI DMA: DMA bug corrected, "
					   "difference %ld bytes\n",
					   512 - (mac_dma_residual & 0x1ff));
				mac_dma_residual = (mac_dma_residual + 511) & ~0x1ff;
			}
		}
		mac_scsi_dma.dma_ctrl = 0;
	}

	/* If the DMA is finished, fetch the rest bytes and turn it off */
	if (dma_stat & 0x40) {
		atari_dma_residual = 0;
		if ((dma_stat & 1) == 0)
			atari_scsi_fetch_restbytes();
		tt_scsi_dma.dma_ctrl = 0;
	}

#endif /* REAL_DMA */

#ifdef PSEUDO_DMA
	/* determine if there is any residual for the current transfer */
	if (mac_pdma_active) {
	  /* probably EOP interrupt, signaling i.e. target disconnect.
	   * We must figure out the residual from the source/destination
	   * pointers here ... */
	  /* Should check bus status here to make sure it wasn't reselect or reset */
	  mac_pdma_residual = HOSTDATA_DMALEN - (mac_pdma_current - mac_pdma_startaddr);
	  mac_pdma_active = 0;
	}
#endif

#ifdef RBV_HACK
	mac_turnoff_irq( IRQ_MAC_SCSI );
#else
	mac_disable_irq( IRQ_MAC_SCSI );
#endif

	save_flags(flags);
#ifndef RBV_HACK	/* interferes with level triggered RBV IRQs ?? */
	restore_irq(fp);
#endif

	if ( irq == IRQ_IDX(IRQ_MAC_SCSI) )
		NCR5380_intr (irq, dummy, fp);

	restore_flags(flags);

	/* To be sure the int is not masked */
#ifdef RBV_HACK
	mac_turnon_irq( IRQ_MAC_SCSI );
#else
	mac_enable_irq( IRQ_MAC_SCSI );
#endif

	/* Clear the IRQ */
	via_scsi_clear();
}


#ifdef REAL_DMA
static void mac_scsi_fetch_restbytes( void )
{
	int nr;
	char	*src, *dst;

	/* fetch rest bytes in the DMA register */
	dst = (char *)SCSI_DMA_READ_P( dma_addr );
	if ((nr = ((long)dst & 3))) {
		/* there are 'nr' bytes left for the last long address before the
		   DMA pointer */
		dst = (char *)( (unsigned long)dst & ~3 );
		DMA_PRINTK("SCSI DMA: there are %d rest bytes for phys addr 0x%08lx",
			   nr, (long)dst);
		dst = (char *)PTOV(dst);  /* The content of the DMA pointer
					   * is a physical address! */
		DMA_PRINTK(" = virt addr 0x%08lx\n", (long)dst);
		for( src = (char *)&mac_scsi_dma.dma_restdata; nr > 0; --nr )
			*dst++ = *src++;
	}
}
#endif /* REAL_DMA */

#if 0  /* FIXME : how is the host ID determined on a Mac? */
#define	RTC_READ(reg)				\
    ({	unsigned char	__val;			\
		outb(reg,&tt_rtc.regsel);	\
		__val = tt_rtc.data;		\
		__val;				\
	})

#define	RTC_WRITE(reg,val)			\
    do {					\
		outb(reg,&tt_rtc.regsel);	\
		tt_rtc.data = (val);		\
	} while(0)
#endif
				   
int mac_scsi_detect (Scsi_Host_Template *host)
{
	static int called = 0;
	struct Scsi_Host *instance;

	if (!MACH_IS_MAC || called)
		return( 0 );

	if (macintosh_config->scsi_type != MAC_SCSI_OLD)
		return( 0 );

	host->proc_dir = &proc_scsi_mac;

	/* testing: IIfx SCSI without IOP ?? */
	if (macintosh_config->ident == MAC_MODEL_IIFX)
		mac_scsi_regp = via1_regp+0x8000;
	else
		mac_scsi_regp = via1_regp+0x10000;

#if 0  /* maybe if different SCSI versions show up ? */
	mac_scsi_reg_read  = IS_A_TT() ? atari_scsi_tt_reg_read :
					   atari_scsi_falcon_reg_read;
	mac_scsi_reg_write = IS_A_TT() ? atari_scsi_tt_reg_write :
#endif					   atari_scsi_falcon_reg_write;

	/* setup variables */
	host->can_queue =
		(setup_can_queue > 0) ? setup_can_queue :
		MAC_SCSI_CAN_QUEUE;
	host->cmd_per_lun =
		(setup_cmd_per_lun > 0) ? setup_cmd_per_lun :
		MAC_SCSI_CMD_PER_LUN;
	host->sg_tablesize =
		(setup_sg_tablesize >= 0) ? setup_sg_tablesize : MAC_SCSI_SG_TABLESIZE;

	if (setup_hostid >= 0)
		host->this_id = setup_hostid;
	else {
		/* use 7 as default */
		host->this_id = 7;
	}

#ifdef SUPPORT_TAGS
	if (setup_use_tagged_queuing < 0)
		setup_use_tagged_queuing = DEFAULT_USE_TAGGED_QUEUING;
#endif

	instance = scsi_register (host, sizeof (struct NCR5380_hostdata));
	mac_scsi_host = instance;

	/* truncation of machspec bit not critical as instance->irq never used */
#if 0	/* Might work; problem was only with Falcon lock */
	instance->irq = IRQ_MAC_SCSI;
#else
	instance->irq = 0;
#endif
	mac_scsi_reset_boot();
	NCR5380_init (instance, 0);

       	/* This int is actually "pseudo-slow", i.e. it acts like a slow
	 * interrupt after having cleared the pending flag for the DMA
	 * interrupt. */
	request_irq(IRQ_MAC_SCSI, scsi_mac_intr, IRQ_TYPE_SLOW,
	            "SCSI NCR5380", scsi_mac_intr);
#ifdef REAL_DMA
	tt_scsi_dma.dma_ctrl = 0;
	atari_dma_residual = 0;

	if (is_brokenscsi) {
		/* While the read overruns (described by Drew Eckhardt in
		 * NCR5380.c) never happened on TTs, they do in fact on the Medusa
		 * (This was the cause why SCSI didn't work right for so long
		 * there.) Since handling the overruns slows down a bit, I turned
		 * the #ifdef's into a runtime condition.
		 *
		 * In principle it should be sufficient to do max. 1 byte with
		 * PIO, but there is another problem on the Medusa with the DMA
		 * rest data register. So 'atari_read_overruns' is currently set
		 * to 4 to avoid having transfers that aren't a multiple of 4. If
		 * the rest data bug is fixed, this can be lowered to 1.
		 */
		mac_read_overruns = 4;
	}
#endif /* REAL_DMA */

#ifdef PSEUDO_DMA
	mac_pdma_residual = 0;
	mac_pdma_active   = 0;
#endif

	printk(KERN_INFO "scsi%d: options CAN_QUEUE=%d CMD_PER_LUN=%d SCAT-GAT=%d "
#ifdef SUPPORT_TAGS
			"TAGGED-QUEUING=%s "
#endif
			"HOSTID=%d",
			instance->host_no, instance->hostt->can_queue,
			instance->hostt->cmd_per_lun,
			instance->hostt->sg_tablesize,
#ifdef SUPPORT_TAGS
			setup_use_tagged_queuing ? "yes" : "no",
#endif
			instance->hostt->this_id );
	NCR5380_print_options (instance);
	printk ("\n");

	called = 1;
	return( 1 );
}

#ifdef MODULE
int mac_scsi_release (struct Scsi_Host *sh)
{
       	free_irq(IRQ_MAC_SCSI, scsi_mac_intr);
	if (mac_dma_buffer)
		scsi_init_free (mac_dma_buffer, MAC_BUFFER_SIZE);
	return 1;
}
#endif

void mac_scsi_setup( char *str, int *ints )
{
	/* Format of mac5380 parameter is:
	 *   mac5380=<can_queue>,<cmd_per_lun>,<sg_tablesize>,<hostid>,<use_tags>
	 * Negative values mean don't change.
	 */
	
	/* Grmbl... the standard parameter parsing can't handle negative numbers
	 * :-( So let's do it ourselves!
	 */

	int i = ints[0]+1, fact;

	while( str && (isdigit(*str) || *str == '-') && i <= 10) {
		if (*str == '-')
			fact = -1, ++str;
		else
			fact = 1;
		ints[i++] = simple_strtoul( str, NULL, 0 ) * fact;
		if ((str = strchr( str, ',' )) != NULL)
			++str;
	}
	ints[0] = i-1;
	
	if (ints[0] < 1) {
		printk( "mac_scsi_setup: no arguments!\n" );
		return;
	}

	if (ints[0] >= 1) {
		if (ints[1] > 0)
			/* no limits on this, just > 0 */
			setup_can_queue = ints[1];
	}
	if (ints[0] >= 2) {
		if (ints[2] > 0)
			setup_cmd_per_lun = ints[2];
	}
	if (ints[0] >= 3) {
		if (ints[3] >= 0) {
			setup_sg_tablesize = ints[3];
			/* Must be <= SG_ALL (255) */
			if (setup_sg_tablesize > SG_ALL)
				setup_sg_tablesize = SG_ALL;
		}
	}
	if (ints[0] >= 4) {
		/* Must be between 0 and 7 */
		if (ints[4] >= 0 && ints[4] <= 7)
			setup_hostid = ints[4];
		else if (ints[4] > 7)
			printk( "mac_scsi_setup: invalid host ID %d !\n", ints[4] );
	}
#ifdef SUPPORT_TAGS
	if (ints[0] >= 5) {
		if (ints[5] >= 0)
			setup_use_tagged_queuing = !!ints[5];
	}
#endif
}

int mac_scsi_reset( Scsi_Cmnd *cmd, unsigned int reset_flags)
{
	int		rv;
	struct NCR5380_hostdata *hostdata =
		(struct NCR5380_hostdata *)cmd->host->hostdata;

	/* For doing the reset, SCSI interrupts must be disabled first,
	 * since the 5380 raises its IRQ line while _RST is active and we
	 * can't disable interrupts completely, since we need the timer.
	 */
	/* And abort a maybe active DMA transfer */
       	mac_turnoff_irq( IRQ_MAC_SCSI );
#ifdef REAL_DMA
       	mac_scsi_dma.dma_ctrl = 0;
#endif /* REAL_DMA */
#ifdef PSEUDO_DMA
	mac_pdma_active = 0;
#endif

	rv = NCR5380_reset(cmd, reset_flags);

	/* Re-enable ints */
       	mac_turnon_irq( IRQ_MAC_SCSI );

	return( rv );
}

	
static void mac_scsi_reset_boot( void )
{
	unsigned long end;
	
	/*
	 * Do a SCSI reset to clean up the bus during initialization. No messing
	 * with the queues, interrupts, or locks necessary here.
	 */

	printk( "Macintosh SCSI: resetting the SCSI bus..." );

	/* switch off SCSI IRQ - catch an interrupt without IRQ bit set else */
       	mac_turnoff_irq( IRQ_MAC_SCSI );

	/* get in phase */
	NCR5380_write( TARGET_COMMAND_REG,
		      PHASE_SR_TO_TCR( NCR5380_read(STATUS_REG) ));

	/* assert RST */
	NCR5380_write( INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_RST );
	/* The min. reset hold time is 25us, so 40us should be enough */
	udelay( 50 );
	/* reset RST and interrupt */
	NCR5380_write( INITIATOR_COMMAND_REG, ICR_BASE );
	NCR5380_read( RESET_PARITY_INTERRUPT_REG );

	for( end = jiffies + AFTER_RESET_DELAY; time_before(jiffies, end); )
		barrier();

	/* switch on SCSI IRQ again */
       	mac_turnon_irq( IRQ_MAC_SCSI );

	printk( " done\n" );
}


const char * mac_scsi_info (struct Scsi_Host *host)
{
	/* mac_scsi_detect() is verbose enough... */
	static const char string[] = "Macintosh NCR5380 SCSI";
	return string;
}


#if defined(REAL_DMA)

unsigned long mac_scsi_dma_setup( struct Scsi_Host *instance, void *data,
				   unsigned long count, int dir )
{
	unsigned long addr = VTOP( data );

	DMA_PRINTK("scsi%d: setting up dma, data = %p, phys = %lx, count = %ld, "
		   "dir = %d\n", instance->host_no, data, addr, count, dir);

	if (!STRAM_ADDR(addr)) {
		/* If we have a non-DMAable address on a Falcon, use the dribble
		 * buffer; 'orig_addr' != 0 in the read case tells the interrupt
		 * handler to copy data from the dribble buffer to the originally
		 * wanted address.
		 */
		if (dir)
			memcpy( mac_dma_buffer, data, count );
		else
			mac_dma_orig_addr = data;
		addr = mac_dma_phys_buffer;
	}
	
	mac_dma_startaddr = addr;	/* Needed for calculating residual later. */
  
	/* Cache cleanup stuff: On writes, push any dirty cache out before sending
	 * it to the peripheral. (Must be done before DMA setup, since at least
	 * the ST-DMA begins to fill internal buffers right after setup. For
	 * reads, invalidate any cache, may be altered after DMA without CPU
	 * knowledge.
	 * 
	 * ++roman: For the Medusa, there's no need at all for that cache stuff,
	 * because the hardware does bus snooping (fine!).
	 */
	dma_cache_maintenance( addr, count, dir );

	if (count == 0)
		printk(KERN_NOTICE "SCSI warning: DMA programmed for 0 bytes !\n");

	mac_scsi_dma.dma_ctrl = dir;
	SCSI_DMA_WRITE_P( dma_addr, addr );
	SCSI_DMA_WRITE_P( dma_cnt, count );
	mac_scsi_dma.dma_ctrl = dir | 2;

	return( count );
}


static long mac_scsi_dma_residual( struct Scsi_Host *instance )
{
	return( mac_dma_residual );
}


#define	CMD_SURELY_BLOCK_MODE	0
#define	CMD_SURELY_BYTE_MODE	1
#define	CMD_MODE_UNKNOWN		2

static int mac_classify_cmd( Scsi_Cmnd *cmd )
{
	unsigned char opcode = cmd->cmnd[0];
	
	if (opcode == READ_DEFECT_DATA || opcode == READ_LONG ||
		opcode == READ_BUFFER)
		return( CMD_SURELY_BYTE_MODE );
	else if (opcode == READ_6 || opcode == READ_10 ||
		 opcode == 0xa8 /* READ_12 */ || opcode == READ_REVERSE ||
		 opcode == RECOVER_BUFFERED_DATA) {
		/* In case of a sequential-access target (tape), special care is
		 * needed here: The transfer is block-mode only if the 'fixed' bit is
		 * set! */
		if (cmd->device->type == TYPE_TAPE && !(cmd->cmnd[1] & 1))
			return( CMD_SURELY_BYTE_MODE );
		else
			return( CMD_SURELY_BLOCK_MODE );
	}
	else
		return( CMD_MODE_UNKNOWN );
}

#endif	/* REAL_DMA */

#if defined(REAL_DMA) || defined(PSEUDO_DMA)
/* This function calculates the number of bytes that can be transferred via
 * DMA. On the TT, this is arbitrary, but on the Falcon we have to use the
 * ST-DMA chip. There are only multiples of 512 bytes possible and max.
 * 255*512 bytes :-( This means also, that defining READ_OVERRUNS is not
 * possible on the Falcon, since that would require to program the DMA for
 * n*512 - atari_read_overrun bytes. But it seems that the Falcon doesn't have
 * the overrun problem, so this question is academic :-)
 */

static unsigned long mac_dma_xfer_len( unsigned long wanted_len,
					Scsi_Cmnd *cmd,
					int write_flag )
{
	unsigned long	possible_len, limit;

#if defined(REAL_DMA)
	if (IS_A_TT())
		/* TT SCSI DMA can transfer arbitrary #bytes */
		return( wanted_len );

	/* ST DMA chip is stupid -- only multiples of 512 bytes! (and max.
	 * 255*512 bytes, but this should be enough)
	 *
	 * ++roman: Aaargl! Another Falcon-SCSI problem... There are some commands
	 * that return a number of bytes which cannot be known beforehand. In this
	 * case, the given transfer length is an "allocation length". Now it
	 * can happen that this allocation length is a multiple of 512 bytes and
	 * the DMA is used. But if not n*512 bytes really arrive, some input data
	 * will be lost in the ST-DMA's FIFO :-( Thus, we have to distinguish
	 * between commands that do block transfers and those that do byte
	 * transfers. But this isn't easy... there are lots of vendor specific
	 * commands, and the user can issue any command via the
	 * SCSI_IOCTL_SEND_COMMAND.
	 *
	 * The solution: We classify SCSI commands in 1) surely block-mode cmd.s,
	 * 2) surely byte-mode cmd.s and 3) cmd.s with unknown mode. In case 1)
	 * and 3), the thing to do is obvious: allow any number of blocks via DMA
	 * or none. In case 2), we apply some heuristic: Byte mode is assumed if
	 * the transfer (allocation) length is < 1024, hoping that no cmd. not
	 * explicitly known as byte mode have such big allocation lengths...
	 * BTW, all the discussion above applies only to reads. DMA writes are
	 * unproblematic anyways, since the targets aborts the transfer after
	 * receiving a sufficient number of bytes.
	 *
	 * Another point: If the transfer is from/to an non-ST-RAM address, we
	 * use the dribble buffer and thus can do only STRAM_BUFFER_SIZE bytes.
	 */

	if (write_flag) {
		/* Write operation can always use the DMA, but the transfer size must
		 * be rounded up to the next multiple of 512 (atari_dma_setup() does
		 * this).
		 */
		possible_len = wanted_len;
	}
	else {
		/* Read operations: if the wanted transfer length is not a multiple of
		 * 512, we cannot use DMA, since the ST-DMA cannot split transfers
		 * (no interrupt on DMA finished!)
		 */
		if (wanted_len & 0x1ff)
			possible_len = 0;
		else {
			/* Now classify the command (see above) and decide whether it is
			 * allowed to do DMA at all */
			switch( falcon_classify_cmd( cmd )) {
			  case CMD_SURELY_BLOCK_MODE:
				possible_len = wanted_len;
				break;
			  case CMD_SURELY_BYTE_MODE:
				possible_len = 0; /* DMA prohibited */
				break;
			  case CMD_MODE_UNKNOWN:
			  default:
				/* For unknown commands assume block transfers if the transfer
				 * size/allocation length is >= 1024 */
				possible_len = (wanted_len < 1024) ? 0 : wanted_len;
				break;
			}
		}
	}

	/* Last step: apply the hard limit on DMA transfers */
	limit = (atari_dma_buffer && !STRAM_ADDR( VTOP(cmd->SCp.ptr) )) ?
		    STRAM_BUFFER_SIZE : 255*512;
	if (possible_len > limit)
		possible_len = limit;

	if (possible_len != wanted_len)
		DMA_PRINTK("Sorry, must cut DMA transfer size to %ld bytes "
			   "instead of %ld\n", possible_len, wanted_len);

#else /* REAL_DMA */
	possible_len = wanted_len;
#endif
	
	return( possible_len );
}
#endif	/* REAL_DMA || PSEUDO_DMA */


/* 
 * FIXME !!!
 */

/* NCR5380 register access functions
 */

static unsigned char mac_scsi_reg_read( unsigned char reg )
{
	return( mac_scsi_regp[reg << 4] );
}

static void mac_scsi_reg_write( unsigned char reg, unsigned char value )
{
	mac_scsi_regp[reg << 4] = value;
}

#include "mac_NCR5380.c"

#ifdef PSEUDO_DMA

/*
 * slightly optimized PIO transfer routines, experimental!
 * command may time out if interrupts are left enabled
 */

static inline int mac_pdma_read (struct Scsi_Host *instance, unsigned char *dst,    int len)
{
    register unsigned char *d = dst;
    register i = len;
    register unsigned char p, tmp;

#if (NDEBUG & NDEBUG_PSEUDO_DMA)
    printk("pdma_read: reading %d bytes to %p\n", len, dst);
#endif

    mac_pdma_residual = len;
    if (mac_pdma_active) {
      printk("pseudo-DMA already active in pread!\n");
      return -1;
    }
    mac_pdma_active = 1;
    mac_pdma_startaddr = (unsigned long) dst;
    mac_pdma_current   = (unsigned long) dst;

    /* 
     * Get the phase from the bus (sanity check) 
     * Hopefully, the phase bits are valid here ...
     */
    p = NCR5380_read(STATUS_REG) & PHASE_MASK;
    if (!(p & SR_IO)) {
      PDMA_PRINTK("NCR5380_pread: initial phase mismatch!\n");
      NCR_PRINT_PHASE(NDEBUG_ANY);
      return -1;
    }

    /* 
     * The NCR5380 chip will only drive the SCSI bus when the 
     * phase specified in the appropriate bits of the TARGET COMMAND
     * REGISTER match the STATUS REGISTER
     */

#if 0	/* done in transfer_dma */
    p = PHASE_DATAIN;
    NCR5380_write(TARGET_COMMAND_REG, PHASE_SR_TO_TCR(p));
#endif

    for (; i; --i) {
	HSH_PRINTK(" read %d ..", i);
        /* check if we were interrupted ... */
        if (!mac_pdma_active) {
	  printk("pwrite: interrupt detected!\n");
	  break;
        }

	/* 
	 * Wait for assertion of REQ, after which the phase bits will be 
	 * valid 
	 */
	while (!((tmp = NCR5380_read(STATUS_REG)) & SR_REQ))
		barrier();

	HSH_PRINTK(" REQ ..");

	/* Check for phase mismatch */	
	if ((tmp & PHASE_MASK) != p) {
            if (!mac_pdma_active) 
	    	printk("scsi%d : phase mismatch after interrupt\n", instance->host_no);
	    else
	    	printk("scsi%d : phase mismatch w/o interrupt\n", instance->host_no);
	    NCR_PRINT_PHASE(NDEBUG_ANY);
	    break;
	}

	/* Do actual transfer from SCSI bus to memory */
	*d = NCR5380_read(CURRENT_SCSI_DATA_REG);

	d++;
	
	/* Handshake ... */

	/* Assert ACK */
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | ICR_ASSERT_ACK);
	HSH_PRINTK(" ACK ..");

	/* Wait for REQ to be dropped */
	while (NCR5380_read(STATUS_REG) & SR_REQ)
		barrier();
	HSH_PRINTK(" /REQ ..");

	/* Drop ACK */
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);
	HSH_PRINTK(" /ACK !\n");

	mac_pdma_current = (unsigned long) d;
	mac_pdma_residual--;
    }

#if (NDEBUG & NDEBUG_PSEUDO_DMA)
    printk("pdma_read: read at %d bytes to %p\n", i, dst);
#endif

    if (mac_pdma_residual)
      printk("pread: leaving with residual %ld of %ld\n", 
      		mac_pdma_residual, len);
    mac_pdma_active = 0;

    /* ?? */
#if 0
    NCR5380_write(MODE_REG, MR_BASE);
    NCR5380_read(RESET_PARITY_INTERRUPT_REG);
#endif
    return 0;
}
		
static inline int mac_pdma_write (struct Scsi_Host *instance, unsigned char *src,    int len)
{
    register unsigned char *s = src;
    register i = len;
    register unsigned char p, tmp;

#if (NDEBUG & NDEBUG_PSEUDO_DMA)
    printk("pdma_write: writing %d bytes from %p\n", len, src);
#endif

    mac_pdma_residual = len;
    if (mac_pdma_active) {
      printk("pseudo-DMA already active in pwrite!\n");
      return -1;
    }
    mac_pdma_active = 1;
    mac_pdma_startaddr = (unsigned long) src;
    mac_pdma_current   = (unsigned long) src;

    /* 
     * Get the phase from the bus (sanity check) 
     */
    p = NCR5380_read(STATUS_REG) & PHASE_MASK;
    if (p & SR_IO) {
      printk("NCR5380_pwrite: initial phase mismatch!\n");
      NCR_PRINT_PHASE(NDEBUG_ANY);
      return -1;
    }

    /* 
     * The NCR5380 chip will only drive the SCSI bus when the 
     * phase specified in the appropriate bits of the TARGET COMMAND
     * REGISTER match the STATUS REGISTER
     */

#if 0	/* already done in transfer_dma */ 
    p = PHASE_DATAOUT;
    NCR5380_write(TARGET_COMMAND_REG, PHASE_SR_TO_TCR(p));
#endif

    for (; i; --i) {
        /* check if we were interrupted ... */
        if (!mac_pdma_active) {
	  printk("pwrite: interrupt detected!\n");
	  break;
        }

       	/* 
	 * Wait for assertion of REQ, after which the phase bits will be 
	 * valid 
	 */
	while (!((tmp = NCR5380_read(STATUS_REG)) & SR_REQ));

	/* Check for phase mismatch */	
	if ((tmp & PHASE_MASK) != p) {
            if (!mac_pdma_active) 
	    	printk("scsi%d : phase mismatch after interrupt\n", instance->host_no);
	    else
	    	printk("scsi%d : phase mismatch w/o interrupt\n", instance->host_no);
	    NCR_PRINT_PHASE(NDEBUG_ANY);
	    /* should we signal an error here?? */
	    break;
	}

	/* Do actual transfer to SCSI bus from memory */

	NCR5380_write(OUTPUT_DATA_REG, *s);

	s++;
	
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE |
		      ICR_ASSERT_DATA);

	/* Handshake ... assert ACK */
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE | 
		      ICR_ASSERT_DATA | ICR_ASSERT_ACK);

	/* ... wait for REQ to be dropped */
	while (NCR5380_read(STATUS_REG) & SR_REQ);

	/* and drop ACK (and DATA) ! */
	NCR5380_write(INITIATOR_COMMAND_REG, ICR_BASE);

	mac_pdma_current = (unsigned long) s;
	mac_pdma_residual--;
    }

#if (NDEBUG & NDEBUG_PSEUDO_DMA)
    printk("pdma_write: write at %d bytes from %p\n", i, src);
#endif

    if (mac_pdma_residual)
      printk("pwrite: leaving with residual %ld of len %ld \n", 
      		mac_pdma_residual, len);
    mac_pdma_active = 0;

    return 0;

}
#endif /* PSEUDO_DMA */

#ifdef MODULE
Scsi_Host_Template driver_template = MAC_SCSI;

#include "scsi_module.c"
#endif
