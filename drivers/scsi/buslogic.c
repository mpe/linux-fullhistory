/*
 *	buslogic.c	Copyright (C) 1993, 1994 David B. Gentzel
 *	Low-level scsi driver for BusLogic adapters
 *	by David B. Gentzel, Whitfield Software Services, Carnegie, PA
 *	    (gentzel@nova.enet.dec.com)
 *	Thanks to BusLogic for providing the necessary documentation
 *
 *	The original version of this driver was derived from aha1542.[ch],
 *	which is Copyright (C) 1992 Tommy Thorn.  Much has been reworked, but
 *	most of basic structure and substantial chunks of code still remain.
 *
 *	Furthermore, many subsequent fixes and improvements to the aha1542
 *	driver have been folded back into this driver.  These changes to
 *	aha1542.[ch] are Copyright (C) 1993, 1994 Eric Youngdale.
 *
 *	Thanks to the following individuals who have made contributions (of
 *	(code, information, support, or testing) to this driver:
 *		Eric Youngdale		Leonard Zubkoff
 *		Tomas Hurka		Andrew Walker
 */

/*
 * TODO:
 *	1. Clean up error handling & reporting.
 *	2. Find out why scatter/gather is limited to 16 requests per command.
 *	3. Test/improve/fix abort & reset functions.
 *	4. Look at command linking.
 *	5. Allow multiple boards to share an IRQ if the bus allows (EISA, MCA,
 *	   and PCI).
 *	6. Avoid using the 445S workaround for board revs >= D.
 */

/*
 * NOTES:
 *    BusLogic (formerly BusTek) manufactures an extensive family of
 *    intelligent, high performance SCSI-2 host adapters.  They all support
 *    command queueing and scatter/gather I/O.  Most importantly, they all
 *    support identical programming interfaces, so a single driver can be used
 *    for all boards.
 *
 *    Actually, they all support TWO identical programming interfaces!  They
 *    have an Adaptec 154x compatible interface (complete with 24 bit
 *    addresses) as well as a "native" 32 bit interface.  As such, the Linux
 *    aha1542 driver can be used to drive them, but with less than optimal
 *    performance (at least for the EISA, VESA, and MCA boards).
 *
 *    Here is the scoop on the various models:
 *	BT-542B - ISA first-party DMA with floppy support.
 *	BT-545S - 542B + FAST SCSI and active termination.
 *	BT-545D - 545S + differential termination.
 *	BT-640A - MCA bus-master with floppy support.
 *	BT-646S - 640A + FAST SCSI and active termination.
 *	BT-646D - 646S + differential termination.
 *	BT-742A - EISA bus-master with floppy support.
 *	BT-747S - 742A + FAST SCSI, active termination, and 2.88M floppy.
 *	BT-747D - 747S + differential termination.
 *	BT-757S - 747S + WIDE SCSI.
 *	BT-757D - 747D + WIDE SCSI.
 *	BT-445S - VESA bus-master FAST SCSI with active termination
 *		  and floppy support.
 *	BT-445C - 445S + enhanced BIOS & firmware options.
 *	BT-946C - PCI bus-master FAST SCSI.
 *	BT-956C - PCI bus-master FAST/WIDE SCSI.
 *
 *    ??? I believe other boards besides the 445 now have a "C" model, but I
 *    have no facts on them.
 *
 *    This driver SHOULD support all of these boards.  It has only been tested
 *    with a 747S, 445S, 946C, and 956C; there is no PCI-specific support as
 *    yet.
 *
 *    Should you require further information on any of these boards, BusLogic
 *    can be reached at (408)492-9090.  Their BBS # is (408)492-1984 (maybe BBS
 *    stands for "Big Brother System"?).
 *
 *    Places flagged with a triple question-mark are things which are either
 *    unfinished, questionable, or wrong.
 */

#ifdef MODULE
#include <linux/module.h>
#endif

#include <linux/string.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/head.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/config.h>

#include <asm/io.h>
#include <asm/system.h>
#include <asm/dma.h>

#include "../block/blk.h"
#include "scsi.h"
#include "hosts.h"
#include "sd.h"
#define BUSLOGIC_PRIVATE_H	/* Get the "private" stuff */
#include "buslogic.h"

#ifndef BUSLOGIC_DEBUG
# define BUSLOGIC_DEBUG 0
#endif

/* ??? Until kmalloc actually implements GFP_DMA, we can't depend on it... */
#undef GFP_DMA

/* If different port addresses are needed (e.g. to install more than two
   cards), you must define BUSLOGIC_PORT_OVERRIDE to be a comma-separated list
   of the addresses which will be checked.  This can also be used to resolve a
   conflict if the port-probing at a standard port causes problems with
   another board. */
/* #define BUSLOGIC_PORT_OVERRIDE 0x330, 0x334, 0x130, 0x134, 0x230, 0x234 */

/* Define this to be either BIOS_TRANSLATION_DEFAULT or BIOS_TRANSLATION_BIG
   if you wish to bypass the test for this, which uses an undocumented port.
   The test is believed to fail on at least some AMI BusLogic clones. */
/* #define BIOS_TRANSLATION_OVERRIDE BIOS_TRANSLATION_BIG */

#define BUSLOGIC_VERSION "1.15"

/* Not a random value - if this is too large, the system hangs for a long time
   waiting for something to happen if a board is not installed. */
/* ??? I don't really like this as it will wait longer on slow machines.
   Perhaps we should base this on the loops_per_second "Bogomips" value? */
#define WAITNEXTTIMEOUT 3000000

/* This is for the scsi_malloc call in buslogic_queuecommand. */
/* ??? I'd up this to 4096, but would we be in danger of using up the
   scsi_malloc memory pool? */
/* This could be a concern, I guess.  It may be possible to fix things so that
   the table generated in sd.c is compatible with the low-level code, but
   don't hold your breath.  -ERY */
#define BUSLOGIC_SG_MALLOC 512

/* Since the SG list is malloced, we have to limit the length. */
#define BUSLOGIC_MAX_SG (BUSLOGIC_SG_MALLOC / sizeof (struct chain))

/* Since the host adapters have room to buffer 32 commands internally, there
   is some virtue in setting BUSLOGIC_MAILBOXES to 32.  The maximum value
   appears to be 255, since the Count parameter to the Initialize Extended
   Mailbox command is limited to one byte. */
#define BUSLOGIC_MAILBOXES 32

#define BUSLOGIC_CMDLUN 4	/* Arbitrary, but seems to work well. */

/* BusLogic boards can be configured for quite a number of port addresses (six
   to be exact), but I generally do not want the driver poking around at
   random.  We allow two port addresses - this allows people to use a BusLogic
   with a MIDI card, which frequently also uses 0x330.

   This can also be overridden on the command line to the kernel, via LILO or
   LOADLIN. */
static unsigned short bases[7] = {
#ifdef BUSLOGIC_PORT_OVERRIDE
    BUSLOGIC_PORT_OVERRIDE,
#else
    0x330, 0x334, /* 0x130, 0x134, 0x230, 0x234, */
#endif
    0
};

#define BIOS_TRANSLATION_DEFAULT 0	/* Default case */
#define BIOS_TRANSLATION_BIG 1		/* Big disk (> 1G) case */

struct hostdata {
    unsigned int bus_type;
    unsigned int bios_translation: 1;	/* BIOS mapping (for compatibility) */
    int last_mbi_used;
    int last_mbo_used;
    char model[7];
    char firmware_rev[6];
    Scsi_Cmnd *sc[BUSLOGIC_MAILBOXES];
    struct mailbox mb[2 * BUSLOGIC_MAILBOXES];
    struct ccb ccbs[BUSLOGIC_MAILBOXES];
};

#define HOSTDATA(host) ((struct hostdata *)&(host)->hostdata)

/* One for each IRQ level (9-15), although 13 will never be used. */
static struct Scsi_Host *host[7] = { NULL, };

static int setup_mailboxes(unsigned int base, struct Scsi_Host *shpnt);
static int restart(struct Scsi_Host *shpnt);

#define INTR_RESET(base) outb(RINT, CONTROL(base))

#define buslogic_printk buslogic_prefix(__PRETTY_FUNCTION__),printk

#if defined(MODULE) && !defined(GFP_DMA)
# define CHECK_DMA_ADDR(isa, addr, badstmt) \
    do { if ((isa) && (addr) > (void *)ISA_DMA_THRESHOLD) badstmt; } while (0)
#else
# define CHECK_DMA_ADDR(isa, addr, badstmt)
#endif

#define CHECK(cond) if (cond) ; else goto fail

#define WAIT(port, allof, noneof) \
    CHECK(wait(port, allof, noneof, WAITNEXTTIMEOUT, FALSE))
#define WAIT_WHILE(port, mask) WAIT(port, 0, mask)
#define WAIT_UNTIL(port, mask) WAIT(port, mask, 0)
#define WAIT_FAST(port, allof, noneof) \
    CHECK(wait(port, allof, noneof, 100, TRUE))
#define WAIT_WHILE_FAST(port, mask) WAIT_FAST(port, 0, mask)
#define WAIT_UNTIL_FAST(port, mask) WAIT_FAST(port, mask, 0)

/* If delay != 0, we use the udelay call to regulate the amount of time we
   wait.

   This is inline as it is always called with constant arguments and hence
   will be very well optimized. */
static __inline__ int wait(unsigned short port,
			   unsigned char allof, unsigned char noneof,
			   unsigned int timeout, int delay)
{
    int bits;

    for (;;) {
	bits = inb(port);
	if ((bits & allof) == allof && (bits & noneof) == 0)
	    return TRUE;
	if (delay)
	    udelay(1000);
	if (--timeout == 0)
	    return FALSE;
    }
}

static void buslogic_prefix(const char *func)
{
    printk("BusLogic SCSI: %s: ", func);
}

static void buslogic_stat(unsigned int base)
{
    int s = inb(STATUS(base)), i = inb(INTERRUPT(base));

    buslogic_printk("status=%02X intrflags=%02X\n", s, i);
}

/* This is a bit complicated, but we need to make sure that an interrupt
   routine does not send something out while we are in the middle of this.
   Fortunately, it is only at boot time that multi-byte messages are ever
   sent. */
static int buslogic_out(unsigned int base, const unsigned char *cmdp,
			size_t len)
{
    unsigned long flags = 0;
    
    if (len == 1) {
	for (;;) {
	    WAIT_WHILE(STATUS(base), CPRBSY);
	    save_flags(flags);
	    cli();
	    if (!(inb(STATUS(base)) & CPRBSY)) {
		outb(*cmdp, COMMAND_PARAMETER(base));
		restore_flags(flags);
		return FALSE;
	    }
	    restore_flags(flags);
	}
    } else {
	save_flags(flags);
	cli();
	while (len--) {
	    WAIT_WHILE(STATUS(base), CPRBSY);
	    outb(*cmdp++, COMMAND_PARAMETER(base));
	}
	restore_flags(flags);
    }
    return FALSE;
  fail:
    restore_flags(flags);
    buslogic_printk("failed(%u): ", len + 1);
    buslogic_stat(base);
    return TRUE;
}

/* Only used at boot time, so we do not need to worry about latency as much
   here.  This waits a very short period of time.  We use this if we are not
   sure whether the board will respond to the command we just sent. */
static int buslogic_in(unsigned int base, unsigned char *cmdp, size_t len)
{
    unsigned long flags;
    
    save_flags(flags);
    cli();
    while (len--) {
	WAIT_UNTIL_FAST(STATUS(base), DIRRDY);
	*cmdp++ = inb(DATA_IN(base));
    }
    restore_flags(flags);
    return FALSE;
  fail:
    restore_flags(flags);
#if (BUSLOGIC_DEBUG & BD_IO)
    buslogic_printk("failed(%u): ", len + 1);
    buslogic_stat(base);
#endif
    return TRUE;
}

static unsigned int makecode(unsigned int haerr, unsigned int scsierr)
{
    unsigned int hosterr;
    const char *errstr = NULL;
#if (BUSLOGIC_DEBUG & BD_ERRORS) && defined(CONFIG_SCSI_CONSTANTS)
    static const char *const buslogic_status[] = {
    /* 00 */	"Command completed normally",
    /* 01-07 */	NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    /* 08-09 */	NULL, NULL,
    /* 0A */	"Linked command completed normally",
    /* 0B */	"Linked command completed normally, interrupt generated",
    /* 0C-0F */	NULL, NULL, NULL, NULL,
    /* 10 */	NULL,
    /* 11 */	"Selection timed out",
    /* 12 */	"Data overrun/underrun",
    /* 13 */	"Unexpected bus free",
    /* 14 */	"Target bus phase sequence failure",
    /* 15 */	"First byte of outgoing MB was invalid",
    /* 16 */	"Invalid CCB Operation Code",
    /* 17 */	"Linked CCB does not have the same LUN",
    /* 18 */	"Invalid Target Direction received from Host",
    /* 19 */	"Duplicate CCB Received in Target Mode",
    /* 1A */	"Invalid CCB or Segment List Parameter",
    /* 1B */	"Auto request sense failed",
    /* 1C */	"SCSI-2 tagged queueing message was rejected by the target",
    /* 1D-1F */	NULL, NULL, NULL,
    /* 20 */	"Host adapter hardware failure",
    /* 21 */	"Target did not respond to SCSI ATN and the HA SCSI bus reset",
    /* 22 */	"Host adapter asserted a SCSI bus reset",
    /* 23 */	"Other SCSI devices asserted a SCSI bus reset",
    };
#endif

    switch (haerr) {
      case 0x00:	/* Normal completion. */
      case 0x0A:	/* Linked command complete without error and linked
			   normally. */
      case 0x0B:	/* Linked command complete without error, interrupt
			   generated. */
	hosterr = DID_OK;
	break;

      case 0x11:	/* Selection time out: the initiator selection or
			   target reselection was not complete within the SCSI
			   time out period. */
	hosterr = DID_TIME_OUT;
	break;

      case 0x14:	/* Target bus phase sequence failure - An invalid bus
			   phase or bus phase sequence was requested by the
			   target.  The host adapter will generate a SCSI
			   Reset Condition, notifying the host with a RSTS
			   interrupt. */
      case 0x21:	/* The target did not respond to SCSI ATN and the host
			   adapter consequently issued a SCSI bus reset to
			   clear up the failure. */
      case 0x22:	/* The host adapter asserted a SCSI bus reset. */
	hosterr = DID_RESET;
	break;

      case 0x12:	/* Data overrun/underrun: the target attempted to
			   transfer more data than was allocated by the Data
			   Length field or the sum of the Scatter/Gather Data
			   Length fields. */
      case 0x13:	/* Unexpected bus free - The target dropped the SCSI
			   BSY at an unexpected time. */
      case 0x15:	/* MBO command was not 00, 01, or 02 - The first byte
			   of the MB was invalid.  This usually indicates a
			   software failure. */
      case 0x16:	/* Invalid CCB Operation Code - The first byte of the
			   CCB was invalid.  This usually indicates a software
			   failure. */
      case 0x17:	/* Linked CCB does not have the same LUN - A
			   subsequent CCB of a set of linked CCB's does not
			   specify the same logical unit number as the
			   first. */
      case 0x18:	/* Invalid Target Direction received from Host - The
			   direction of a Target Mode CCB was invalid. */
      case 0x19:	/* Duplicate CCB Received in Target Mode - More than
			   once CCB was received to service data transfer
			   between the same target LUN and initiator SCSI ID
			   in the same direction. */
      case 0x1A:	/* Invalid CCB or Segment List Parameter - A segment
			   list with a zero length segment or invalid segment
			   list boundaries was received.  A CCB parameter was
			   invalid. */
      case 0x1B:	/* Auto request sense failed. */
      case 0x1C:	/* SCSI-2 tagged queueing message was rejected by the
			   target. */
      case 0x20:	/* The host adapter hardware failed. */
      case 0x23:	/* Other SCSI devices asserted a SCSI bus reset. */
	hosterr = DID_ERROR;	/* ??? Couldn't find any better. */
	break;

      default:
#ifndef CONFIG_SCSI_CONSTANTS
	errstr = "unknown hoststatus";
#endif
	hosterr = DID_ERROR;
	break;
    }
#if (BUSLOGIC_DEBUG & BD_ERRORS)
# ifdef CONFIG_SCSI_CONSTANTS
    if (hosterr != DID_OK) {
	if (haerr < ARRAY_SIZE(buslogic_status))
	    errstr = buslogic_status[haerr];
	if (errstr == NULL)
	    errstr = "unknown hoststatus";
    }
# else
    if (hosterr == DID_ERROR)
	errstr = "";
# endif
#endif
    if (errstr != NULL)
	buslogic_printk("%s (%02X)\n", errstr, haerr);
    return (hosterr << 16) | scsierr;
}

/* ??? this should really be "const struct Scsi_Host *" */
const char *buslogic_info(struct Scsi_Host *shpnt)
{
    return "BusLogic SCSI driver " BUSLOGIC_VERSION;
}

/*
  This is a major rewrite of the interrupt handler to support the newer
  and faster PCI cards.  While the previous interrupt handler was supposed
  to handle multiple incoming becoming available mailboxes during the same
  interrupt, my testing showed that in practice only a single mailbox was
  ever made available.  With the 946C and 956C, multiple incoming mailboxes
  being ready for processing during a single interrupt occurs much more
  frequently, and so care must be taken to avoid race conditions managing
  the Host Adapter Interrupt Register, which can lead to lost interrupts.

  Leonard N. Zubkoff, 23-Mar-95
*/

static void buslogic_interrupt(int irq, struct pt_regs * regs)
{
    int mbi, saved_mbo[BUSLOGIC_MAILBOXES];
    int base, interrupt_flags, found, i;
    struct Scsi_Host *shpnt;
    Scsi_Cmnd *sctmp;
    struct mailbox *mb;
    struct ccb *ccb;

    shpnt = host[irq - 9];
    if (shpnt == NULL)
      panic("buslogic_interrupt: NULL SCSI host entry");

    mb = HOSTDATA(shpnt)->mb;
    ccb = HOSTDATA(shpnt)->ccbs;
    base = shpnt->io_port;

    /*
      This interrupt handler is now specified to use the SA_INTERRUPT
      protocol, so interrupts are inhibited on entry until explicitly
      allowed again.  Read the Host Adapter Interrupt Register, and
      complain if there is no pending interrupt being signaled.
    */

    interrupt_flags = inb(INTERRUPT(base));

    if (!(interrupt_flags & INTV))
      buslogic_printk("interrupt received, but INTV not set\n");

    /*
      Reset the Host Adapter Interrupt Register.  It appears to be
      important that this is only done once per interrupt to avoid
      losing interrupts under heavy loads.
    */

    INTR_RESET(base);

    if (interrupt_flags & RSTS)
      {
	restart(shpnt);
	return;
      }

    /*
      With interrupts still inhibited, scan through the incoming mailboxes
      in strict round robin fashion saving the status information and
      then freeing the mailbox.  A second pass over the completed commands
      will be made separately to complete their processing.
    */

    mbi = HOSTDATA(shpnt)->last_mbi_used + 1;
    if (mbi >= 2*BUSLOGIC_MAILBOXES)
      mbi = BUSLOGIC_MAILBOXES;

    found = 0;

    while (mb[mbi].status != MBX_NOT_IN_USE && found < BUSLOGIC_MAILBOXES)
      {
	int mbo = (struct ccb *)mb[mbi].ccbptr - ccb;

	sctmp = HOSTDATA(shpnt)->sc[mbo];

	/*
	  If sctmp has become NULL, higher level code must have aborted
	  this operation and called the necessary completion routine.
	*/

	if (sctmp != NULL && mb[mbi].status != MBX_COMPLETION_NOT_FOUND)
	  {
	    int result = 0;

	    saved_mbo[found++] = mbo;

	    if (mb[mbi].status != MBX_COMPLETION_OK)
	      result = makecode(ccb[mbo].hastat, ccb[mbo].tarstat);

	    sctmp->result = result;

	    mb[mbi].status = MBX_NOT_IN_USE;
	  }

	HOSTDATA(shpnt)->last_mbi_used = mbi;

	if (++mbi >= 2*BUSLOGIC_MAILBOXES)
	  mbi = BUSLOGIC_MAILBOXES;
      }

    /*
      With interrupts no longer inhibited, iterate over the completed
      commands freeing resources and calling the completion routines.
      Since we exit upon completion of this loop, there is no need to
      inhibit interrupts before exit, as this will be handled by the
      fast interrupt assembly code we return to.
    */

    sti();

    for (i = 0; i < found; i++)
      {
	int mbo = saved_mbo[i];
	sctmp = HOSTDATA(shpnt)->sc[mbo];
	if (sctmp == NULL) continue;
	/*
	  First, free any storage allocated for a scatter/gather
	  data segment list.
	*/
	if (sctmp->host_scribble)
	  scsi_free(sctmp->host_scribble, BUSLOGIC_SG_MALLOC);
	/*
	  Next, mark the SCSI Command as completed so it may be reused
	  for another command by buslogic_queuecommand.  This also signals
	  to buslogic_reset that the command is no longer active.
	*/
	HOSTDATA(shpnt)->sc[mbo] = NULL;
	/*
	  Finally, call the SCSI command completion handler.
	*/
	sctmp->scsi_done(sctmp);
      }
}


/* ??? Why does queuecommand return a value?  scsi.c never looks at it... */
int buslogic_queuecommand(Scsi_Cmnd *scpnt, void (*done)(Scsi_Cmnd *))
{
    static const unsigned char buscmd[] = { CMD_START_SCSI };
    unsigned char direction;
    unsigned char *cmd = (unsigned char *)scpnt->cmnd;
    unsigned char target = scpnt->target;
    unsigned char lun = scpnt->lun;
    void *buff = scpnt->request_buffer;
    int bufflen = scpnt->request_bufflen;
    int mbo;
    unsigned long flags;
    struct Scsi_Host *shpnt = scpnt->host;
    struct mailbox *mb = HOSTDATA(shpnt)->mb;
    struct ccb *ccb;


#if (BUSLOGIC_DEBUG & BD_COMMAND)
    if (target > 1) {
	scpnt->result = DID_TIME_OUT << 16;
	done(scpnt);
	return 0;
    }
#endif

    if (*cmd == REQUEST_SENSE) {
#if (BUSLOGIC_DEBUG & (BD_COMMAND | BD_ERRORS))
	if (bufflen != sizeof scpnt->sense_buffer) {
	    buslogic_printk("wrong buffer length supplied for request sense"
			    " (%d).\n",
			    bufflen);
	}
#endif
	scpnt->result = 0;
	done(scpnt);
	return 0;
    }

#if (BUSLOGIC_DEBUG & BD_COMMAND)
    {
	int i;

	if (*cmd == READ_10 || *cmd == WRITE_10
	    || *cmd == READ_6 || *cmd == WRITE_6)
	    i = *(int *)(cmd + 2);
	else
	    i = -1;
	buslogic_printk("dev %d cmd %02X pos %d len %d ",
			target, *cmd, i, bufflen);
	buslogic_stat(shpnt->io_port);
	buslogic_printk("dumping scsi cmd:");
	for (i = 0; i < scpnt->cmd_len; i++)
	    printk(" %02X", cmd[i]);
	printk("\n");
	if (*cmd == WRITE_10 || *cmd == WRITE_6)
	    return 0;	/* we are still testing, so *don't* write */
    }
#endif

    /* Use the outgoing mailboxes in a round-robin fashion, because this
       is how the host adapter will scan for them. */

    save_flags(flags);
    cli();

    mbo = HOSTDATA(shpnt)->last_mbo_used + 1;
    if (mbo >= BUSLOGIC_MAILBOXES)
	mbo = 0;

    do {
	if (mb[mbo].status == MBX_NOT_IN_USE
	    && HOSTDATA(shpnt)->sc[mbo] == NULL)
	    break;
	mbo++;
	if (mbo >= BUSLOGIC_MAILBOXES)
	    mbo = 0;
    } while (mbo != HOSTDATA(shpnt)->last_mbo_used);

    if (mb[mbo].status != MBX_NOT_IN_USE || HOSTDATA(shpnt)->sc[mbo]) {
	/* ??? Instead of failing, should we enable OMBR interrupts and sleep
	   until we get one? */
	restore_flags(flags);
	buslogic_printk("unable to find empty mailbox.\n");
	goto fail;
    }

    HOSTDATA(shpnt)->sc[mbo] = scpnt;		/* This will effectively
						   prevent someone else from
						   screwing with this cdb. */

    HOSTDATA(shpnt)->last_mbo_used = mbo;

    restore_flags(flags);

#if (BUSLOGIC_DEBUG & BD_COMMAND)
    buslogic_printk("sending command (%d %08X)...", mbo, done);
#endif

    ccb = &HOSTDATA(shpnt)->ccbs[mbo];

    /* This gets trashed for some reason */
    mb[mbo].ccbptr = ccb;

    memset(ccb, 0, sizeof (struct ccb));

    ccb->cdblen = scpnt->cmd_len;		/* SCSI Command Descriptor
						   Block Length */

    direction = 0;
    if (*cmd == READ_10 || *cmd == READ_6)
	direction = 8;
    else if (*cmd == WRITE_10 || *cmd == WRITE_6)
	direction = 16;

    memcpy(ccb->cdb, cmd, ccb->cdblen);

    if (scpnt->use_sg) {
	struct scatterlist *sgpnt;
	struct chain *cptr;
	size_t i;

	ccb->op = CCB_OP_INIT_SG;	/* SCSI Initiator Command
					   w/scatter-gather */
	scpnt->host_scribble
	    = (unsigned char *)scsi_malloc(BUSLOGIC_SG_MALLOC);
	if (scpnt->host_scribble == NULL) {
	    buslogic_printk("unable to allocate DMA memory.\n");
	    goto fail;
	}
	sgpnt = (struct scatterlist *)scpnt->request_buffer;
	cptr = (struct chain *)scpnt->host_scribble;
	if (scpnt->use_sg > shpnt->sg_tablesize) {
	    buslogic_printk("bad segment list, %d > %d.\n",
			    scpnt->use_sg, shpnt->sg_tablesize);
	    goto fail;
	}
	for (i = 0; i < scpnt->use_sg; i++) {
	    CHECK_DMA_ADDR(shpnt->unchecked_isa_dma, sgpnt[i].address,
			   goto baddma);
	    cptr[i].dataptr = sgpnt[i].address;
	    cptr[i].datalen = sgpnt[i].length;
	}
	ccb->datalen = scpnt->use_sg * sizeof (struct chain);
	ccb->dataptr = cptr;
#if (BUSLOGIC_DEBUG & BD_COMMAND)
	{
	    unsigned char *ptr;

	    buslogic_printk("cptr %08X:", cptr);
	    ptr = (unsigned char *)cptr;
	    for (i = 0; i < 18; i++)
		printk(" %02X", ptr[i]);
	    printk("\n");
	}
#endif
    } else {
	ccb->op = CCB_OP_INIT;	/* SCSI Initiator Command */
	scpnt->host_scribble = NULL;
	CHECK_DMA_ADDR(shpnt->unchecked_isa_dma, buff, goto baddma);
	ccb->datalen = bufflen;
	ccb->dataptr = buff;
    }
    ccb->id = target;
    ccb->lun = lun;
    ccb->dir = direction;
    ccb->rsalen = sizeof scpnt->sense_buffer;
    ccb->senseptr = scpnt->sense_buffer;
    /* ccbcontrol, commlinkid, and linkptr are 0 due to above memset. */

#if (BUSLOGIC_DEBUG & BD_COMMAND)
    {
	size_t i;

	buslogic_printk("sending...");
	for (i = 0; i < sizeof(struct ccb) - 10; i++)
	    printk(" %02X", ((unsigned char *)ccb)[i]);
	printk("\n");
    }
#endif

    if (done) {
#if (BUSLOGIC_DEBUG & BD_COMMAND)
	buslogic_printk("now waiting for interrupt: ");
	buslogic_stat(shpnt->io_port);
#endif
	scpnt->scsi_done = done;
	mb[mbo].status = MBX_ACTION_START;
	/* start scsi command */
	buslogic_out(shpnt->io_port, buscmd, sizeof buscmd);
#if (BUSLOGIC_DEBUG & BD_COMMAND)
	buslogic_stat(shpnt->io_port);
#endif
    } else
	buslogic_printk("done can't be NULL.\n");

    while (0) {
#if defined(MODULE) && !defined(GFP_DMA)
      baddma:
	buslogic_printk("address > 16MB used for ISA HA.\n");
#endif
      fail:
	scpnt->result = DID_ERROR << 16;
	done(scpnt);
    }

    return 0;
}

#if 0
static void internal_done(Scsi_Cmnd *scpnt)
{
    scpnt->SCp.Status++;
}

int buslogic_command(Scsi_Cmnd *scpnt)
{
#if (BUSLOGIC_DEBUG & BD_COMMAND)
    buslogic_printk("calling buslogic_queuecommand.\n");
#endif

    buslogic_queuecommand(scpnt, internal_done);

    scpnt->SCp.Status = 0;
    while (!scpnt->SCp.Status)
	barrier();
    return scpnt->result;
}
#endif

/* Initialize mailboxes. */
static int setup_mailboxes(unsigned int base, struct Scsi_Host *shpnt)
{
    size_t i;
    int ok = FALSE;		/* Innocent until proven guilty... */
    struct mailbox *mb = HOSTDATA(shpnt)->mb;
    struct ccb *ccb = HOSTDATA(shpnt)->ccbs;
    struct {
	unsigned char cmd, count;
	void *base PACKED;
    } cmd = { CMD_INITEXTMB, BUSLOGIC_MAILBOXES, mb };

    for (i = 0; i < BUSLOGIC_MAILBOXES; i++) {
	mb[i].status = mb[BUSLOGIC_MAILBOXES + i].status = MBX_NOT_IN_USE;
	mb[i].ccbptr = &ccb[i];
    }
    INTR_RESET(base);	/* reset interrupts, so they don't block */

    if (buslogic_out(base, (unsigned char *)&cmd, sizeof cmd))
	goto fail;
    WAIT_UNTIL(INTERRUPT(base), CMDC);

    ok = TRUE;

    while (0) {
      fail:
	buslogic_printk("failed setting up mailboxes.\n");
    }

    INTR_RESET(base);

    return !ok;
}

static int getconfig(unsigned int base, unsigned char *irq,
		     unsigned char *dma, unsigned char *id,
		     char *bus_type, unsigned short *max_sg,
		     const unsigned char **bios)
{
    unsigned char inquiry_cmd[2];
    unsigned char inquiry_result[4];
    int i;

#if (BUSLOGIC_DEBUG & BD_DETECT)
    buslogic_printk("called\n");
#endif

    i = inb(STATUS(base));
    if (i & DIRRDY)
	i = inb(DATA_IN(base));
    inquiry_cmd[0] = CMD_RETCONF;
    buslogic_out(base, inquiry_cmd, 1);
    if (buslogic_in(base, inquiry_result, 3))
	goto fail;
    WAIT_UNTIL_FAST(INTERRUPT(base), CMDC);
    INTR_RESET(base);
    /* Defer using the DMA value until we know the bus type. */
    *dma = inquiry_result[0];
    switch (inquiry_result[1]) {
      case 0x01:
	*irq = 9;
	break;
      case 0x02:
	*irq = 10;
	break;
      case 0x04:
	*irq = 11;
	break;
      case 0x08:
	*irq = 12;
	break;
      case 0x20:
	*irq = 14;
	break;
      case 0x40:
	*irq = 15;
	break;
      default:
	buslogic_printk("unable to determine BusLogic IRQ level, "
			" disabling board.\n");
	goto fail;
    }
    *id = inquiry_result[2] & 0x7;

    /* I expected Adaptec boards to fail on this, but it doesn't happen... */
    inquiry_cmd[0] = CMD_INQEXTSETUP;
    inquiry_cmd[1] = 4;
    if (buslogic_out(base, inquiry_cmd, 2))
	goto fail;
    if (buslogic_in(base, inquiry_result, inquiry_cmd[1]))
	goto fail;
    WAIT_UNTIL_FAST(INTERRUPT(base), CMDC);
    if (inb(STATUS(base)) & CMDINV)
	goto fail;
    INTR_RESET(base);

    *bus_type = inquiry_result[0];
    CHECK(*bus_type == 'A' || *bus_type == 'E' || *bus_type == 'M');

    *bios = (const unsigned char *)((unsigned int)inquiry_result[1] << 12);

    *max_sg = (inquiry_result[3] << 8) | inquiry_result[2];

    /* We only need a DMA channel for ISA boards.  Some other types of boards
       (such as the 747S) have an option to report a DMA channel even though
       none is used (for compatibility with Adaptec drivers which require a
       DMA channel).  We ignore this. */
    if (*bus_type == 'A')
	switch (*dma) {
	  case 0:	/* This indicates that no DMA channel is used. */
	    *dma = 0;
	    break;
	  case 0x20:
	    *dma = 5;
	    break;
	  case 0x40:
	    *dma = 6;
	    break;
	  case 0x80:
	    *dma = 7;
	    break;
	  default:
	    buslogic_printk("unable to determine BusLogic DMA channel,"
			    " disabling board.\n");
	    goto fail;
	}
    else
	*dma = 0;

    while (0) {
      fail:
#if (BUSLOGIC_DEBUG & BD_DETECT)
	buslogic_printk("query board settings\n");
#endif
	return TRUE;
    }

    return FALSE;
}

/* Query the board.  This acts both as part of the detection sequence and as a
   means to get necessary configuration information. */
static int buslogic_query(unsigned int base, unsigned char *trans,
			  unsigned char *irq, unsigned char *dma,
			  unsigned char *id, char *bus_type,
			  unsigned short *max_sg, const unsigned char **bios,
			  char *model, char *firmware_rev)
{
    unsigned char inquiry_cmd[2];
    unsigned char inquiry_result[6];
    unsigned char geo;
    unsigned int i;

#if (BUSLOGIC_DEBUG & BD_DETECT)
    buslogic_printk("called\n");
#endif

    /* Quick and dirty test for presence of the card. */
    if (inb(STATUS(base)) == 0xFF)
	goto fail;

    /* Check the GEOMETRY port early for quick bailout on Adaptec boards. */
    geo = inb(GEOMETRY(base));
#if (BUSLOGIC_DEBUG & BD_DETECT)
    buslogic_printk("geometry bits: %02X\n", geo);
#endif
    /* Here is where we tell the men from the boys (i.e. Adaptec's don't
       support the GEOMETRY port, the men do :-) */
    if (geo == 0xFF)
	goto fail;

    /* In case some other card was probing here, reset interrupts. */
    INTR_RESET(base);

    /* Reset the adapter.  I ought to make a hard reset, but it's not really
       necessary. */
    outb(RSOFT | RINT/* | RSBUS*/, CONTROL(base));

    /* Wait a little bit for things to settle down. */
    i = jiffies + 2;
    while (i > jiffies);

    /* Expect INREQ and HARDY, any of the others are bad. */
    WAIT(STATUS(base), INREQ | HARDY, DACT | DFAIL | CMDINV | DIRRDY | CPRBSY);

    /* Shouldn't have generated any interrupts during reset. */
    if (inb(INTERRUPT(base)) & INTRMASK)
	goto fail;

    /* Getting the BusLogic firmware revision level is a bit tricky.  We get
       the first two digits (d.d) from CMD_INQUIRY and then use two undocumented
       commands to get the remaining digit and letter (d.ddl as in 3.31C). */

    inquiry_cmd[0] = CMD_INQUIRY;
    buslogic_out(base, inquiry_cmd, 1);
    if (buslogic_in(base, inquiry_result, 4))
	goto fail;
    /* Reading port should reset DIRRDY. */
    if (inb(STATUS(base)) & DIRRDY)
	goto fail;
    WAIT_UNTIL_FAST(INTERRUPT(base), CMDC);
    INTR_RESET(base);
    firmware_rev[0] = inquiry_result[2];
    firmware_rev[1] = '.';
    firmware_rev[2] = inquiry_result[3];
    firmware_rev[3] = '\0';
#if 0
    buslogic_printk("inquiry bytes: %02X(%c) %02X(%c)\n",
		    inquiry_result[0], inquiry_result[0],
		    inquiry_result[1], inquiry_result[1]);
#endif

    if (getconfig(base, irq, dma, id, bus_type, max_sg, bios))
	goto fail;

    /* Set up defaults */
#ifdef BIOS_TRANSLATION_OVERRIDE
    *trans = BIOS_TRANSLATION_OVERRIDE;
#else
    *trans = BIOS_TRANSLATION_DEFAULT;
#endif
    model[0] = '\0';
    model[6] = 0;

    /* ??? Begin undocumented command use.
       These may not be supported by clones. */

    do {
	/* ??? It appears as though AMI BusLogic clones don't implement this
	   feature.  As an experiment, if we read a 00 we ignore the GEO_GT_1GB
	   bit and skip all further undocumented commands. */
	if (geo == 0x00)
	    break;
#ifndef BIOS_TRANSLATION_OVERRIDE
	*trans = ((geo & GEO_GT_1GB)
		  ? BIOS_TRANSLATION_BIG : BIOS_TRANSLATION_DEFAULT);
#endif

	inquiry_cmd[0] = CMD_VER_NO_LAST;
	buslogic_out(base, inquiry_cmd, 1);
	if (buslogic_in(base, inquiry_result, 1))
	    break;
	WAIT_UNTIL_FAST(INTERRUPT(base), CMDC);
	INTR_RESET(base);
	firmware_rev[3] = inquiry_result[0];
	firmware_rev[4] = '\0';

	inquiry_cmd[0] = CMD_VER_NO_LETTER;
	buslogic_out(base, inquiry_cmd, 1);
	if (buslogic_in(base, inquiry_result, 1))
	    break;
	WAIT_UNTIL_FAST(INTERRUPT(base), CMDC);
	INTR_RESET(base);
	firmware_rev[4] = inquiry_result[0];
	firmware_rev[5] = '\0';

	/* Use undocumented command to get model number and revision. */

	inquiry_cmd[0] = CMD_RET_MODEL_NO;
	inquiry_cmd[1] = 6;
	buslogic_out(base, inquiry_cmd, 2);
	if (buslogic_in(base, inquiry_result, inquiry_cmd[1]))
	    break;
	WAIT_UNTIL_FAST(INTERRUPT(base), CMDC);
	INTR_RESET(base);
	memcpy(model, inquiry_result, 5);
	model[5] = '\0';
	model[6] = inquiry_result[5];
    } while (0);

    /* ??? End undocumented command use. */

    /* bus_type from getconfig doesn't differentiate between EISA/VESA.  We
       override using the model number here. */
    switch (*bus_type) {
      case 'E':
	switch (model[0]) {
	  case '4':
	    *bus_type = 'V';
	    break;
	  case '9':
	    *bus_type = 'P';
	    break;
	  case '7':
	    break;
	  default:
	    *bus_type = 'X';
	    break;
	}
	break;
      default:
	break;
    }

    while (0) {
      fail:
#if (BUSLOGIC_DEBUG & BD_DETECT)
	buslogic_printk("query board settings\n");
#endif
	return TRUE;
    }

    return FALSE;
}

/* return non-zero on detection */
int buslogic_detect(Scsi_Host_Template *tpnt)
{
    unsigned char dma;
    unsigned char irq;
    unsigned int base;
    unsigned char id;
    char bus_type;
    unsigned short max_sg;
    unsigned char bios_translation;
    unsigned long flags;
    const unsigned char *bios;
    char *model;
    char *firmware_rev;
    struct Scsi_Host *shpnt;
    size_t indx;
    int unchecked_isa_dma;
    int count = 0;

#if (BUSLOGIC_DEBUG & BD_DETECT)
    buslogic_printk("called\n");
#endif

    tpnt->can_queue = BUSLOGIC_MAILBOXES;
    for (indx = 0; bases[indx] != 0; indx++)
	if (!check_region(bases[indx], 4)) {
	    shpnt = scsi_register(tpnt, sizeof (struct hostdata));

	    base = bases[indx];

	    model = HOSTDATA(shpnt)->model;
	    firmware_rev = HOSTDATA(shpnt)->firmware_rev;
	    if (buslogic_query(base, &bios_translation, &irq, &dma, &id,
			       &bus_type, &max_sg, &bios, model, firmware_rev))
		goto unregister;

#if (BUSLOGIC_DEBUG & BD_DETECT)
	    buslogic_stat(base);
#endif

	    /* Only type 'A' (AT/ISA) bus adapters use unchecked DMA. */
	    unchecked_isa_dma = (bus_type == 'A');
#ifndef CONFIG_NO_BUGGY_BUSLOGIC
	    /* There is a hardware bug in the BT-445S prior to revision D.
	       When the BIOS is enabled and you have more than 16MB of memory,
	       the card mishandles memory transfers over 16MB which (if viewed
	       as a 24-bit address) overlap with the BIOS address space.  For
	       example if you have the BIOS located at physical address
	       0xDC000 and a DMA transfer from the card to RAM starts at
	       physical address 0x10DC000 then the transfer is messed up.  To
	       be more precise every fourth byte of the transfer is messed up.
	       (This analysis courtesy of Tomas Hurka, author of the NeXTSTEP
	       BusLogic driver.) */

	    if (bus_type == 'V'				    /* 445 */
		&& firmware_rev[0] <= '3'		    /* S */
		&& bios != NULL) {			    /* BIOS enabled */
#if 1
		/* Now that LNZ's forbidden_addr stuff is in the higher level
		   scsi code, we can use this instead. */
		/* Avoid addresses which "mirror" the BIOS for DMA. */
		shpnt->forbidden_addr = (unsigned long)bios;
		shpnt->forbidden_size = 16 * 1024;
#else
		/* Use double-buffering. */
		unchecked_isa_dma = TRUE;
#endif
	    }
#endif

	    CHECK_DMA_ADDR(unchecked_isa_dma, shpnt, goto unregister);

	    if (setup_mailboxes(base, shpnt))
		goto unregister;

	    /* Set the Bus on/off-times as not to ruin floppy performance.
	       CMD_BUSOFF_TIME is a noop for EISA boards (and possibly
	       others???). */
	    if (bus_type != 'E' && bus_type != 'P') {
		/* The default ON/OFF times for BusLogic adapters is 7/4. */
		static const unsigned char oncmd[] = { CMD_BUSON_TIME, 7 };
		static const unsigned char offcmd[] = { CMD_BUSOFF_TIME, 5 };

		INTR_RESET(base);
		buslogic_out(base, oncmd, sizeof oncmd);
		WAIT_UNTIL(INTERRUPT(base), CMDC);
		INTR_RESET(base);
		buslogic_out(base, offcmd, sizeof offcmd);
		WAIT_UNTIL(INTERRUPT(base), CMDC);
		while (0) {
		  fail:
		    buslogic_printk("setting bus on/off-time failed.\n");
		}
		INTR_RESET(base);
	    }

	    buslogic_printk("configuring %s HA at port 0x%03X, IRQ %u",
			    (bus_type == 'A' ? "ISA"
			     : (bus_type == 'E' ? "EISA"
				: (bus_type == 'M' ? "MCA"
				   : (bus_type == 'P' ? "PCI"
				      : (bus_type == 'V' ? "VESA"
					 : (bus_type == 'X' ? "EISA/VESA/PCI"
					    : "Unknown")))))),
			    base, irq);
	    if (bios != NULL)
		printk(", BIOS 0x%05X", (unsigned int)bios);
	    if (dma != 0)
		printk(", DMA %u", dma);
	    printk(", ID %u\n", id);
	    buslogic_printk("Model Number: %s",
			    (model[0] ? model : "Unknown"));
	    if (model[0])
		printk(" (revision %d)", model[6]);
	    printk("\n");
	    buslogic_printk("firmware revision: %s\n", firmware_rev);

#if (BUSLOGIC_DEBUG & BD_DETECT)
	    buslogic_stat(base);
#endif

#if (BUSLOGIC_DEBUG & BD_DETECT)
	    buslogic_printk("enable interrupt channel %d.\n", irq);
#endif

	    save_flags(flags);
	    cli();
	    if (request_irq(irq, buslogic_interrupt,
			    SA_INTERRUPT, "buslogic")) {
		buslogic_printk("unable to allocate IRQ for "
				"BusLogic controller.\n");
		restore_flags(flags);
		goto unregister;
	    }

	    if (dma) {
		if (request_dma(dma, "buslogic")) {
		    buslogic_printk("unable to allocate DMA channel for "
				    "BusLogic controller.\n");
		    free_irq(irq);
		    restore_flags(flags);
		    goto unregister;
		}

		/* The DMA-Controller.  We need to fool with this because we
		   want to be able to use an ISA BusLogic without having to
		   have the BIOS enabled. */
		set_dma_mode(dma, DMA_MODE_CASCADE);
		enable_dma(dma);
	    }

	    host[irq - 9] = shpnt;
	    shpnt->this_id = id;
	    shpnt->unchecked_isa_dma = unchecked_isa_dma;
	    /* Have to keep cmd_per_lun at 1 for ISA machines otherwise lots
	       of memory gets sucked up for bounce buffers.  */
	    shpnt->cmd_per_lun = (unchecked_isa_dma ? 1 : BUSLOGIC_CMDLUN);
	    shpnt->sg_tablesize = max_sg;
	    if (shpnt->sg_tablesize > BUSLOGIC_MAX_SG)
		shpnt->sg_tablesize = BUSLOGIC_MAX_SG;
	    /* ??? shpnt->base should really be "const unsigned char *"... */
	    shpnt->base = (unsigned char *)bios;
	    shpnt->io_port = base;
	    shpnt->n_io_port = 4;	/* Number of bytes of I/O space used */
	    shpnt->dma_channel = dma;
	    shpnt->irq = irq;
	    HOSTDATA(shpnt)->bios_translation = bios_translation;
	    if (bios_translation == BIOS_TRANSLATION_BIG)
		buslogic_printk("using extended bios translation.\n");
	    HOSTDATA(shpnt)->last_mbi_used = 2 * BUSLOGIC_MAILBOXES - 1;
	    HOSTDATA(shpnt)->last_mbo_used = BUSLOGIC_MAILBOXES - 1;
	    memset(HOSTDATA(shpnt)->sc, 0, sizeof HOSTDATA(shpnt)->sc);
	    restore_flags(flags);

#if 0
	    {
		unsigned char buf[8];
		unsigned char cmd[]
		    = { READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		size_t i;

#if (BUSLOGIC_DEBUG & BD_DETECT)
		buslogic_printk("*** READ CAPACITY ***\n");
#endif
		for (i = 0; i < sizeof buf; i++)
		    buf[i] = 0x87;
		for (i = 0; i < 2; i++)
		    if (!buslogic_command(i, cmd, buf, sizeof buf)) {
			buslogic_printk("LU %u sector_size %d device_size %d\n",
					i, *(int *)(buf + 4), *(int *)buf);
		    }

#if (BUSLOGIC_DEBUG & BD_DETECT)
		buslogic_printk("*** NOW RUNNING MY OWN TEST ***\n");
#endif
		for (i = 0; i < 4; i++) {
		    static buffer[512];

		    cmd[0] = READ_10;
		    cmd[1] = 0;
		    xany2scsi(cmd + 2, i);
		    cmd[6] = 0;
		    cmd[7] = 0;
		    cmd[8] = 1;
		    cmd[9] = 0;
		    buslogic_command(0, cmd, buffer, sizeof buffer);
		}
	    }
#endif

	    request_region(bases[indx], 4,"buslogic");
	    /* Register the IO ports that we use */
	    count++;
	    continue;
	  unregister:
	    scsi_unregister(shpnt);
	}
    return count;
}

static int restart(struct Scsi_Host *shpnt)
{
    unsigned int i;
    unsigned int count = 0;
#if 0
    static const unsigned char buscmd[] = { CMD_START_SCSI };
#endif

    for (i = 0; i < BUSLOGIC_MAILBOXES; i++)
	if (HOSTDATA(shpnt)->sc[i]
	    && !HOSTDATA(shpnt)->sc[i]->device->soft_reset) {
#if 0
	    HOSTDATA(shpnt)->mb[i].status
		= MBX_ACTION_START;	/* Indicate ready to restart... */
#endif
	    count++;
	}

    buslogic_printk("potential to restart %d stalled commands...\n", count);
#if 0
    /* start scsi command */
    if (count)
	buslogic_out(shpnt->host->io_port, buscmd, sizeof buscmd);
#endif
    return 0;
}

/* ??? The abort command for the aha1542 does not leave the device in a clean
   state where it is available to be used again.  As it is not clear whether
   the same problem exists with BusLogic boards, we will enable this and see
   if it works. */
int buslogic_abort(Scsi_Cmnd *scpnt)
{
#if 1
    static const unsigned char buscmd[] = { CMD_START_SCSI };
    struct mailbox *mb;
    int mbi, mbo, last_mbi;
    unsigned long flags;
    unsigned int i;

    buslogic_printk("%X %X\n",
		    inb(STATUS(scpnt->host->io_port)),
		    inb(INTERRUPT(scpnt->host->io_port)));

    save_flags(flags);
    cli();
    mb = HOSTDATA(scpnt->host)->mb;
    last_mbi = HOSTDATA(scpnt->host)->last_mbi_used;
    mbi = last_mbi + 1;
    if (mbi >= 2 * BUSLOGIC_MAILBOXES)
	mbi = BUSLOGIC_MAILBOXES;

    do {
	if (mb[mbi].status != MBX_NOT_IN_USE)
	    break;
	last_mbi = mbi;
	mbi++;
	if (mbi >= 2 * BUSLOGIC_MAILBOXES)
	    mbi = BUSLOGIC_MAILBOXES;
    } while (mbi != HOSTDATA(scpnt->host)->last_mbi_used);

    if (mb[mbi].status != MBX_NOT_IN_USE) {
	buslogic_printk("lost interrupt discovered on irq %d, "
			" - attempting to recover...\n",
			scpnt->host->irq);
	HOSTDATA(scpnt->host)->last_mbi_used = last_mbi;
	buslogic_interrupt(scpnt->host->irq, NULL);
	restore_flags(flags);
	return SCSI_ABORT_SUCCESS;
    }
    restore_flags(flags);

    /* OK, no lost interrupt.  Try looking to see how many pending commands we
       think we have. */
    for (i = 0; i < BUSLOGIC_MAILBOXES; i++)
	if (HOSTDATA(scpnt->host)->sc[i]) {
	    if (HOSTDATA(scpnt->host)->sc[i] == scpnt) {
		buslogic_printk("timed out command pending for %4.4X.\n",
				scpnt->request.dev);
		if (HOSTDATA(scpnt->host)->mb[i].status != MBX_NOT_IN_USE) {
		    buslogic_printk("OGMB still full - restarting...\n");
		    buslogic_out(scpnt->host->io_port, buscmd, sizeof buscmd);
		}
	    } else
		buslogic_printk("other pending command: %4.4X\n",
				scpnt->request.dev);
	}
#endif

#if (BUSLOGIC_DEBUG & BD_ABORT)
    buslogic_printk("called\n");
#endif

#if 1
    /* This section of code should be used carefully - some devices cannot
       abort a command, and this merely makes it worse. */
    save_flags(flags);
    cli();
    for (mbo = 0; mbo < BUSLOGIC_MAILBOXES; mbo++)
	if (scpnt == HOSTDATA(scpnt->host)->sc[mbo]) {
	    mb[mbo].status = MBX_ACTION_ABORT;
	    buslogic_out(scpnt->host->io_port, buscmd, sizeof buscmd);
	    break;
	}
    restore_flags(flags);
#endif

    return SCSI_ABORT_SNOOZE;
}

/* We do not implement a reset function here, but the upper level code assumes
   that it will get some kind of response for the command in scpnt.  We must
   oblige, or the command will hang the SCSI system.  For a first go, we assume
   that the BusLogic notifies us with all of the pending commands (it does
   implement soft reset, after all). */
int buslogic_reset(Scsi_Cmnd *scpnt)
{
    static const unsigned char buscmd[] = { CMD_START_SCSI };
    unsigned int i;

#if (BUSLOGIC_DEBUG & BD_RESET)
    buslogic_printk("called\n");
#endif
#if 0
    /* This does a scsi reset for all devices on the bus. */
    outb(RSBUS, CONTROL(scpnt->host->io_port));
#else
    /* This does a selective reset of just the one device. */
    /* First locate the ccb for this command. */
    for (i = 0; i < BUSLOGIC_MAILBOXES; i++)
	if (HOSTDATA(scpnt->host)->sc[i] == scpnt) {
	    HOSTDATA(scpnt->host)->ccbs[i].op = CCB_OP_BUS_RESET;

	    /* Now tell the BusLogic to flush all pending commands for this
	       target. */
	    buslogic_out(scpnt->host->io_port, buscmd, sizeof buscmd);

	    /* Here is the tricky part.  What to do next.  Do we get an
	       interrupt for the commands that we aborted with the specified
	       target, or do we generate this on our own?  Try it without first
	       and see what happens. */
	    buslogic_printk("sent BUS DEVICE RESET to target %d.\n",
			    scpnt->target);

	    /* If the first does not work, then try the second.  I think the
	       first option is more likely to be correct.  Free the command
	       block for all commands running on this target... */
#if 1
	    for (i = 0; i < BUSLOGIC_MAILBOXES; i++)
		if (HOSTDATA(scpnt->host)->sc[i]
		    && HOSTDATA(scpnt->host)->sc[i]->target == scpnt->target) {
		    Scsi_Cmnd *sctmp = HOSTDATA(scpnt->host)->sc[i];

		    sctmp->result = DID_RESET << 16;
		    if (sctmp->host_scribble)
			scsi_free(sctmp->host_scribble, BUSLOGIC_SG_MALLOC);
		    buslogic_printk("sending DID_RESET for target %d.\n",
				    scpnt->target);
		    sctmp->scsi_done(scpnt);

		    HOSTDATA(scpnt->host)->sc[i] = NULL;
		    HOSTDATA(scpnt->host)->mb[i].status = MBX_NOT_IN_USE;
		}
	    return SCSI_RESET_SUCCESS;
#else
	    return SCSI_RESET_PENDING;
#endif
	}
#endif
    /* No active command at this time, so this means that each time we got some
       kind of response the last time through.  Tell the mid-level code to
       request sense information in order to decide what to do next. */
    return SCSI_RESET_PUNT;
}

/* ??? This is probably not correct for series "C" boards.  I believe these
   support separate mappings for each disk.  We would need to issue a
   CMD_READ_FW_LOCAL_RAM command to check for the particular drive being
   queried.  Note that series "C" boards can be differentiated by having
   HOSTDATA(disk->device->host)->firmware_rev[0] >= '4'. */
int buslogic_biosparam(Disk *disk, int dev, int *ip)
{
    unsigned int size = disk->capacity;

    /* ip[0] == heads, ip[1] == sectors, ip[2] == cylinders */
    if (HOSTDATA(disk->device->host)->bios_translation == BIOS_TRANSLATION_BIG
	&& size >= 0x200000) {		/* 1GB */
	if (size >= 0x400000) {		/* 2GB */
#if 0	/* ??? Used in earlier kernels, but disagrees with BusLogic info. */
	    if (mb >= 0x800000) {	/* 4GB */
		ip[0] = 256;
		ip[1] = 64;
	    } else {
		ip[0] = 256;
		ip[1] = 32;
	    }
#else
	    ip[0] = 256;
	    ip[1] = 64;
#endif
	} else {
	    ip[0] = 128;
	    ip[1] = 32;
	}
    } else {
	ip[0] = 64;
	ip[1] = 32;
    }
    ip[2] = size / (ip[0] * ip[1]);
/*    if (ip[2] > 1024)
	ip[2] = 1024; */
    return 0;
}

/* called from init/main.c */
void buslogic_setup(char *str, int *ints)
{
    static const unsigned short valid_bases[]
	= { 0x130, 0x134, 0x230, 0x234, 0x330, 0x334 };
    static size_t setup_idx = 0;
    size_t i;

    if (setup_idx >= ARRAY_SIZE(bases) - 1) {
	buslogic_printk("called too many times.  Bad LILO params?\n");
	return;
    }
    if (ints[0] != 1) {
	buslogic_printk("malformed command line.\n");
	buslogic_printk("usage: buslogic=<portbase>\n");
	return;
    }
    for (i = 0; i < ARRAY_SIZE(valid_bases); i++)
	if (valid_bases[i] == ints[1]) {
	    bases[setup_idx++] = ints[1];
	    bases[setup_idx] = 0;
	    return;
	}
    buslogic_printk("invalid base 0x%X specified.\n", ints[i]);
}

#ifdef MODULE
/* Eventually this will go into an include file, but that's later... */
Scsi_Host_Template driver_template = BUSLOGIC;

# include "scsi_module.c"
#endif
