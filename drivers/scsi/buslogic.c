/*
 *	buslogic.c	(C) 1993, 1994 David B. Gentzel
 *	Low-level scsi driver for BusLogic adapters
 *	by David B. Gentzel, Whitfield Software Services, Carnegie, PA
 *	    (gentzel@nova.enet.dec.com)
 *	Thanks to BusLogic for providing the necessary documentation
 *
 *	The original version of this driver was derived from aha1542.[ch] which
 *	is Copyright (C) 1992 Tommy Thorn.  Much has been reworked, but most of
 *	basic structure and substantial chunks of code still remain.
 */

/*
 * TODO:
 *	1. Cleanup error handling & reporting.
 *	2. Find out why scatter/gather is limited to 16 requests per command.
 *	3. Add multiple outstanding requests.
 *	4. See if we can make good use of having more than one command per lun.
 *	5. Test/improve/fix abort & reset functions.
 *	6. Look at command linking.
 *	7. Allow multiple boards to share an IRQ if the bus allows (e.g. EISA).
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
 *	BT-445S - VESA bus-master FAST SCSI with active termination and floppy
 *		  support.
 *	BT-640A - MCA bus-master with floppy support.
 *	BT-646S - 640A + FAST SCSI and active termination.
 *	BT-646D - 646S + differential termination.
 *	BT-742A - EISA bus-master with floppy support.
 *	BT-747S - 742A + FAST SCSI, active termination, and 2.88M floppy.
 *	BT-747D - 747S + differential termination.
 *	BT-757S - 747S + WIDE SCSI.
 *	BT-757D - 747D + WIDE SCSI.
 *	BT-946C - PCI bus-master FAST SCSI. (??? Nothing else known.)
 *
 *    Should you require further information on any of these boards, BusLogic
 *    can be reached at (408)492-9090.
 *
 *    This driver SHOULD support all of these boards.  It has only been tested
 *    with a 747S and 445S.
 *
 *    Places flagged with a triple question-mark are things which are either
 *    unfinished, questionable, or wrong.
 */

#include <linux/string.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/head.h>
#include <linux/types.h>
#include <linux/ioport.h>
#include <linux/delay.h>

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
# define BUSLOGIC_DEBUG UD_ABORT
#endif

#define BUSLOGIC_VERSION "1.00"

/* Not a random value - if this is too large, the system hangs for a long time
   waiting for something to happen if a board is not installed. */
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

/* The DMA-Controller.  We need to fool with this because we want to be able to
   use an ISA BusLogic without having to have the BIOS enabled. */
#define DMA_MODE_REG 0xD6
#define DMA_MASK_REG 0xD4
#define	CASCADE 0xC0

#define BUSLOGIC_MAILBOXES 16	/* ??? Arbitrary? */

/* BusLogic boards can be configured for quite a number of port addresses (six
   to be exact), but I generally do not want the driver poking around at
   random.  We allow two port addresses - this allows people to use a BusLogic
   with a MIDI card, which frequently also uses 0x330.  If different port
   addresses are needed (e.g. to install more than two cards), you must define
   BUSLOGIC_PORT_OVERRIDE to be a list of the addresses which will be checked.
   This can also be used to resolve a conflict if the port-probing at a
   standard port causes problems with another board. */
static const unsigned int bases[] = {
#ifdef BUSLOGIC_PORT_OVERRIDE
    BUSLOGIC_PORT_OVERRIDE
#else
    0x330, 0x334, /* 0x130, 0x134, 0x230, 0x234 */
#endif
};

#define BIOS_TRANSLATION_DEFAULT 0	/* Default case */
#define BIOS_TRANSLATION_BIG 1		/* Big disk (> 1G) case */

struct hostdata {
    unsigned char bus_type;
    int bios_translation;	/* Mapping bios uses - for compatibility */
    size_t last_mbi_used;
    size_t last_mbo_used;
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

#define buslogic_printk buslogic_prefix(),printk

#define CHECK(cond) if (cond) ; else goto fail

#define WAIT(port, mask, allof, noneof) \
    CHECK(wait(port, mask, allof, noneof, WAITNEXTTIMEOUT, FALSE))
#define WAIT_WHILE(port, mask) WAIT(port, mask, 0, mask)
#define WAIT_UNTIL(port, mask) WAIT(port, mask, mask, 0)
#define WAIT_FAST(port, mask, allof, noneof) \
    CHECK(wait(port, mask, allof, noneof, 100, TRUE))
#define WAIT_WHILE_FAST(port, mask) WAIT_FAST(port, mask, 0, mask)
#define WAIT_UNTIL_FAST(port, mask) WAIT_FAST(port, mask, mask, 0)

/* If delay != 0, we use the udelay call to regulate the amount of time we
   wait. */
static __inline__ int wait(unsigned short port, unsigned char mask,
			   unsigned char allof, unsigned char noneof,
			   unsigned int timeout, int delay)
{
    int bits;

    for (;;) {
	bits = inb(port) & mask;
	if ((bits & allof) == allof && (bits & noneof) == 0)
	    return TRUE;
	if (delay)
	    udelay(1000);
	if (--timeout == 0)
	    return FALSE;
    }
}

static void buslogic_prefix(void)
{
    printk("BusLogic SCSI: ");
}

#if BUSLOGIC_DEBUG
static void buslogic_stat(unsigned int base)
{
    int s = inb(STATUS(base)), i = inb(INTERRUPT(base));

    printk("status=%02X intrflags=%02X\n", s, i);
}
#else
# define buslogic_stat(base)
#endif

/* This is a bit complicated, but we need to make sure that an interrupt
   routine does not send something out while we are in the middle of this.
   Fortunately, it is only at boot time that multi-byte messages are ever
   sent. */
static int buslogic_out(unsigned int base, const unsigned char *cmdp,
			size_t len)
{
    if (len == 1) {
	for (;;) {
	    WAIT_WHILE(STATUS(base), CPRBSY);
	    cli();
	    if (!(inb(STATUS(base)) & CPRBSY)) {
		outb(*cmdp, COMMAND_PARAMETER(base));
		sti();
		return FALSE;
	    }
	    sti();
	}
    } else {
	cli();
	while (len--) {
	    WAIT_WHILE(STATUS(base), CPRBSY);
	    outb(*cmdp++, COMMAND_PARAMETER(base));
	}
	sti();
    }
    return FALSE;
  fail:
    sti();
    buslogic_printk("buslogic_out failed(%u): ", len + 1);
    buslogic_stat(base);
    return TRUE;
}

/* Only used at boot time, so we do not need to worry about latency as much
   here. */
static int buslogic_in(unsigned int base, unsigned char *cmdp, size_t len)
{
    cli();
    while (len--) {
	WAIT_UNTIL(STATUS(base), DIRRDY);
	*cmdp++ = inb(DATA_IN(base));
    }
    sti();
    return FALSE;
  fail:
    sti();
    buslogic_printk("buslogic_in failed(%u): ", len + 1);
    buslogic_stat(base);
    return TRUE;
}

#if 0
/* Similar to buslogic_in, except that we wait a very short period of time.
   We use this if we know the board is alive and awake, but we are not sure
   whether the board will respond the the command we are about to send. */
static int buslogic_in_fast(unsigned int base, unsigned char *cmdp, size_t len)
{
    cli();
    while (len--) {
	WAIT_UNTIL_FAST(STATUS(base), DIRRDY);
	*cmdp++ = inb(DATA_IN(base));
    }
    sti();
    return FALSE;
  fail:
    sti();
    return TRUE;
}
#endif

static unsigned int makecode(unsigned int hosterr, unsigned int scsierr)
{
    switch (hosterr) {
      case 0x00:	/* Normal completion. */
      case 0x0A:	/* Linked command complete without error and linked
			   normally. */
      case 0x0B:	/* Linked command complete without error, interrupt
			   generated. */
	hosterr = DID_OK;
	break;

      case 0x11:	/* Selection time out: the initiator selection or
			   target reselection was not complete within the SCSI
			   Time out period. */
	hosterr = DID_TIME_OUT;
	break;

      case 0x14:	/* Target bus phase sequence failure - An invalid bus
			   phase or bus phase sequence was requested by the
			   target.  The host adapter will generate a SCSI
			   Reset Condition, notifying the host with a RSTS
			   interrupt. */
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
      case 0x21:	/* The target did not respond to SCSI ATN and the host
			   adapter consequently issued a SCSI bus reset to
			   clear up the failure. */
      case 0x22:	/* The host adapter asserted a SCSI bus reset. */
      case 0x23:	/* Other SCSI devices asserted a SCSI bus reset. */
#if BUSLOGIC_DEBUG
	buslogic_printk("%X %X\n", hosterr, scsierr);
#endif
	hosterr = DID_ERROR;	/* ??? Couldn't find any better. */
	break;

      default:
	buslogic_printk("makecode: unknown hoststatus %X\n", hosterr);
	break;
    }
    return (hosterr << 16) | scsierr;
}

static int test_port(unsigned int base, struct Scsi_Host *shpnt)
{
    unsigned int i;
    unsigned char inquiry_cmd[] = { CMD_INQUIRY };
    unsigned char inquiry_result[4];
    unsigned char *cmdp;
    int len;
    volatile int debug = 0;

    /* Quick and dirty test for presence of the card. */
    if (inb(STATUS(base)) == 0xFF)
	return TRUE;

    /* Reset the adapter.  I ought to make a hard reset, but it's not really
       necessary. */

#if BUSLOGIC_DEBUG
    buslogic_printk("test_port called\n");
#endif

    /* In case some other card was probing here, reset interrupts. */
    INTR_RESET(base);	/* reset interrupts, so they don't block */

    outb(RSOFT | RINT/* | RSBUS*/, CONTROL(base));

    /* Wait a little bit for things to settle down. */
    i = jiffies + 2;
    while (i > jiffies);

    debug = 1;
    /* Expect INREQ and HARDY, any of the others are bad. */
    WAIT(STATUS(base), STATMASK, INREQ | HARDY,
	 DACT | DFAIL | CMDINV | DIRRDY | CPRBSY);

    debug = 2;
    /* Shouldn't have generated any interrupts during reset. */
    if (inb(INTERRUPT(base)) & INTRMASK)
	goto fail;

    /* Perform a host adapter inquiry instead so we do not need to set up the
       mailboxes ahead of time. */
    buslogic_out(base, inquiry_cmd, 1);

    debug = 3;
    len = 4;
    cmdp = &inquiry_result[0];
    while (len--) {
	WAIT(STATUS(base), DIRRDY, DIRRDY, 0);
	*cmdp++ = inb(DATA_IN(base));
    }

    debug = 4;
    /* Reading port should reset DIRRDY. */
    if (inb(STATUS(base)) & DIRRDY)
	goto fail;

    debug = 5;
    /* When CMDC, command is completed, and we're though testing. */
    WAIT_UNTIL(INTERRUPT(base), CMDC);

    /* now initialize adapter. */

    debug = 6;
    /* Clear interrupts. */
    outb(RINT, CONTROL(base));

    debug = 7;

    return FALSE;				/* 0 = ok */
  fail:
    return TRUE;				/* 1 = not ok */
}

const char *buslogic_info(void)
{
    return "BusLogic SCSI Driver version " BUSLOGIC_VERSION;
}

/* A "high" level interrupt handler. */
static void buslogic_interrupt(int junk)
{
    void (*my_done)(Scsi_Cmnd *) = NULL;
    int errstatus, mbistatus = MBX_NOT_IN_USE, number_serviced, found;
    size_t mbi, mbo = 0;
    struct Scsi_Host *shpnt;
    Scsi_Cmnd *sctmp;
    int irqno, base, flag;
    int needs_restart;
    struct mailbox *mb;
    struct ccb *ccb;

    /* Magic - this -2 is only required for slow interrupt handlers */
    irqno = ((int *)junk)[-2];

    shpnt = host[irqno - 9];
    if (!shpnt)
	panic("buslogic.c: NULL SCSI host entry");

    mb = HOSTDATA(shpnt)->mb;
    ccb = HOSTDATA(shpnt)->ccbs;
    base = shpnt->io_port;

#if BUSLOGIC_DEBUG
    flag = inb(INTERRUPT(base));

    buslogic_printk("buslogic_interrupt: ");
    if (!(flag & INTV))
	printk("no interrupt? ");
    if (flag & IMBL)
	printk("IMBL ");
    if (flag & MBOR)
	printk("MBOR ");
    if (flag & CMDC)
	printk("CMDC ");
    if (flag & RSTS)
	printk("RSTS ");
    printk("status %02X\n", inb(STATUS(base)));
#endif

    number_serviced = 0;
    needs_restart = 0;

    for (;;) {
	flag = inb(INTERRUPT(base));

	/* Check for unusual interrupts.  If any of these happen, we should
	   probably do something special, but for now just printing a message
	   is sufficient.  A SCSI reset detected is something that we really
	   need to deal with in some way. */
	if (flag & ~IMBL) {
	    if (flag & MBOR)
		printk("MBOR ");
	    if (flag & CMDC)
		printk("CMDC ");
	    if (flag & RSTS) {
		needs_restart = 1;
		printk("RSTS ");
	    }
	}

	INTR_RESET(base);

	cli();

	mbi = HOSTDATA(shpnt)->last_mbi_used + 1;
	if (mbi >= 2 * BUSLOGIC_MAILBOXES)
	    mbi = BUSLOGIC_MAILBOXES;

	/* I use the "found" variable as I like to keep cli/sti pairs at the
	   same block level.  Debugging dropped sti's is no fun... */

	found = FALSE;
	do {
	    if (mb[mbi].status != MBX_NOT_IN_USE) {
		found = TRUE;
		break;
	    }
	    mbi++;
	    if (mbi >= 2 * BUSLOGIC_MAILBOXES)
		mbi = BUSLOGIC_MAILBOXES;
	} while (mbi != HOSTDATA(shpnt)->last_mbi_used);

	if (found) {
	    mbo = (struct ccb *)mb[mbi].ccbptr - ccb;
	    mbistatus = mb[mbi].status;
	    mb[mbi].status = MBX_NOT_IN_USE;
	    HOSTDATA(shpnt)->last_mbi_used = mbi;
	}

	sti();

	if (!found) {
	    /* Hmm, no mail.  Must have read it the last time around. */
	    if (!number_serviced && !needs_restart)
		buslogic_printk("interrupt received, but no mail.\n");
	    /* We detected a reset.  Restart all pending commands for devices
	       that use the hard reset option. */
	    if (needs_restart)
		restart(shpnt);
	    return;
	}

#if BUSLOGIC_DEBUG
	if (ccb[mbo].tarstat || ccb[mbo].hastat)
	    buslogic_printk("buslogic_interrupt: returning %08X (status %d)\n",
			    ((int)ccb[mbo].hastat << 16) | ccb[mbo].tarstat,
			    mb[mbi].status);
#endif

	if (mbistatus == MBX_COMPLETION_NOT_FOUND)
	    continue;

#if BUSLOGIC_DEBUG
	buslogic_printk("...done %u %u\n", mbo, mbi);
#endif

	sctmp = HOSTDATA(shpnt)->sc[mbo];

	if (!sctmp || !sctmp->scsi_done) {
	    buslogic_printk("buslogic_interrupt: Unexpected interrupt\n");
	    buslogic_printk("tarstat=%02X, hastat=%02X id=%d lun=%d ccb#=%d\n",
			    ccb[mbo].tarstat, ccb[mbo].hastat,
			    ccb[mbo].id, ccb[mbo].lun, mbo);
	    return;
	}

	my_done = sctmp->scsi_done;
	if (sctmp->host_scribble)
	    scsi_free(sctmp->host_scribble, BUSLOGIC_SG_MALLOC);

#if 0	/* ??? */
	/* Fetch the sense data, and tuck it away, in the required slot.  The
	   BusLogic automatically fetches it, and there is no guarantee that we
	   will still have it in the cdb when we come back. */
	if (ccb[mbo].tarstat == 2)	/* ??? */
	    memcpy(sctmp->sense_buffer, &ccb[mbo].cdb[ccb[mbo].cdblen],
		   sizeof sctmp->sense_buffer);
#endif

	/* ??? more error checking left out here */
	if (mbistatus != MBX_COMPLETION_OK)
	    /* ??? This is surely wrong, but I don't know what's right. */
	    errstatus = makecode(ccb[mbo].hastat, ccb[mbo].tarstat);
	else
	    errstatus = 0;

#if BUSLOGIC_DEBUG
	if (errstatus)
	    buslogic_printk("error: %08X %04X %04X\n",
			    errstatus, ccb[mbo].hastat, ccb[mbo].tarstat);

	if (status_byte(ccb[mbo].tarstat) == CHECK_CONDITION) {
	    size_t i;

	    buslogic_printk("buslogic_interrupt: sense:");
	    for (i = 0; i < sizeof sctmp->sense_buffer; i++)
		printk(" %02X", sctmp->sense_buffer[i]);
	    printk("\n");
	}

	if (errstatus)
	    buslogic_printk("buslogic_interrupt: returning %08X\n", errstatus);
#endif

	sctmp->result = errstatus;
	HOSTDATA(shpnt)->sc[mbo] = NULL;	/* This effectively frees up
						   the mailbox slot, as far as
						   queuecommand is
						   concerned. */
	my_done(sctmp);
	number_serviced++;
    }
}

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
    struct mailbox *mb;
    struct ccb *ccb;

#if BUSLOGIC_DEBUG
    if (target > 1) {
	scpnt->result = DID_TIME_OUT << 16;
	done(scpnt);
	return 0;
    }
#endif

    if (*cmd == REQUEST_SENSE) {
#ifndef DEBUG
	if (bufflen != sizeof scpnt->sense_buffer) {
	    buslogic_printk("Wrong buffer length supplied for request sense"
			    " (%d)\n",
			    bufflen);
	}
#endif
	scpnt->result = 0;
	done(scpnt);
	return 0;
    }

#if BUSLOGIC_DEBUG
    {
	int i;

	if (*cmd == READ_10 || *cmd == WRITE_10
	    || *cmd == READ_6 || *cmd == WRITE_6)
	    i = *(int *)(cmd + 2);
	else
	    i = -1;
	buslogic_printk("buslogic_queuecommand:"
			" dev %d cmd %02X pos %d len %d ",
			target, *cmd, i, bufflen);
	buslogic_stat(scpnt->host->io_port);
	buslogic_printk("buslogic_queuecommand: dumping scsi cmd:");
	for (i = 0; i < (COMMAND_SIZE(*cmd)); i++)
	    printk(" %02X", cmd[i]);
	printk("\n");
	if (*cmd == WRITE_10 || *cmd == WRITE_6)
	    return 0;	/* we are still testing, so *don't* write */
    }
#endif

    mb = HOSTDATA(scpnt->host)->mb;
    ccb = HOSTDATA(scpnt->host)->ccbs;

    /* Use the outgoing mailboxes in a round-robin fashion, because this
       is how the host adapter will scan for them. */

    cli();

    mbo = HOSTDATA(scpnt->host)->last_mbo_used + 1;
    if (mbo >= BUSLOGIC_MAILBOXES)
	mbo = 0;

    do {
	if (mb[mbo].status == MBX_NOT_IN_USE
	    && HOSTDATA(scpnt->host)->sc[mbo] == NULL)
	    break;
	mbo++;
	if (mbo >= BUSLOGIC_MAILBOXES)
	    mbo = 0;
    } while (mbo != HOSTDATA(scpnt->host)->last_mbo_used);

    if (mb[mbo].status != MBX_NOT_IN_USE
	|| HOSTDATA(scpnt->host)->sc[mbo]) {
	/* ??? Instead of panicing, should we enable OMBR interrupts and
	   sleep until we get one? */
	panic("buslogic.c: unable to find empty mailbox");
    }

    HOSTDATA(scpnt->host)->sc[mbo] = scpnt;	/* This will effectively
						   prevent someone else from
						   screwing with this cdb. */

    HOSTDATA(scpnt->host)->last_mbo_used = mbo;

    sti();

#if BUSLOGIC_DEBUG
    buslogic_printk("sending command (%d %08X)...", mbo, done);
#endif

    /* This gets trashed for some reason */
    mb[mbo].ccbptr = &ccb[mbo];

    memset(&ccb[mbo], 0, sizeof (struct ccb));

    ccb[mbo].cdblen = COMMAND_SIZE(*cmd);	/* SCSI Command Descriptor
						   Block Length */

    direction = 0;
    if (*cmd == READ_10 || *cmd == READ_6)
	direction = 8;
    else if (*cmd == WRITE_10 || *cmd == WRITE_6)
	direction = 16;

    memcpy(ccb[mbo].cdb, cmd, ccb[mbo].cdblen);

    if (scpnt->use_sg) {
	struct scatterlist *sgpnt;
	struct chain *cptr;
	size_t i;

	ccb[mbo].op = CCB_OP_INIT_SG;	/* SCSI Initiator Command
					   w/scatter-gather */
	scpnt->host_scribble
	    = (unsigned char *)scsi_malloc(BUSLOGIC_SG_MALLOC);
	if (scpnt->host_scribble == NULL)
	    panic("buslogic.c: unable to allocate DMA memory");
	sgpnt = (struct scatterlist *)scpnt->request_buffer;
	cptr = (struct chain *)scpnt->host_scribble;
	if (scpnt->use_sg > scpnt->host->sg_tablesize) {
	    buslogic_printk("buslogic_queuecommand: bad segment list,"
			    " %d > %d\n",
			    scpnt->use_sg, scpnt->host->sg_tablesize);
	    panic("buslogic.c: bad segment list");
	}
	for (i = 0; i < scpnt->use_sg; i++) {
	    cptr[i].dataptr = sgpnt[i].address;
	    cptr[i].datalen = sgpnt[i].length;
	}
	ccb[mbo].datalen = scpnt->use_sg * sizeof (struct chain);
	ccb[mbo].dataptr = cptr;
#if BUSLOGIC_DEBUG
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
	ccb[mbo].op = CCB_OP_INIT;	/* SCSI Initiator Command */
	scpnt->host_scribble = NULL;
	ccb[mbo].datalen = bufflen;
	ccb[mbo].dataptr = buff;
    }
    ccb[mbo].id = target;
    ccb[mbo].lun = lun;
    ccb[mbo].dir = direction;
    ccb[mbo].rsalen = sizeof scpnt->sense_buffer;
    ccb[mbo].senseptr = scpnt->sense_buffer;
    ccb[mbo].linkptr = NULL;
    ccb[mbo].commlinkid = 0;

#if BUSLOGIC_DEBUG
    {
	size_t i;

	buslogic_printk("buslogic_queuecommand: sending...");
	for (i = 0; i < sizeof ccb[mbo] - 10; i++)
	    printk(" %02X", ((unsigned char *)&ccb[mbo])[i]);
	printk("\n");
    }
#endif

    if (done) {
#if BUSLOGIC_DEBUG
	buslogic_printk("buslogic_queuecommand: now waiting for interrupt: ");
	buslogic_stat(scpnt->host->io_port);
#endif
	scpnt->scsi_done = done;
	mb[mbo].status = MBX_ACTION_START;
	/* start scsi command */
	buslogic_out(scpnt->host->io_port, buscmd, sizeof buscmd);
#if BUSLOGIC_DEBUG
	buslogic_stat(scpnt->host->io_port);
#endif
    } else
	buslogic_printk("buslogic_queuecommand: done can't be NULL\n");

    return 0;
}

#if 0
static void internal_done(Scsi_Cmnd *scpnt)
{
    scpnt->SCp.Status++;
}

int buslogic_command(Scsi_Cmnd *scpnt)
{
#if BUSLOGIC_DEBUG
    buslogic_printk("buslogic_command: ..calling buslogic_queuecommand\n");
#endif

    buslogic_queuecommand(scpnt, internal_done);

    scpnt->SCp.Status = 0;
    while (!scpnt->SCp.Status)
	continue;
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

    /* If this fails, this must be an Adaptec board */
    if (buslogic_out(base, (unsigned char *)&cmd, sizeof cmd))
	goto must_be_adaptec;

    /* Wait until host adapter is done messing around, and then check to see
       if the command was accepted.  If it failed, this must be an Adaptec
       board. */
    WAIT_UNTIL(STATUS(base), HARDY);
    if (inb(STATUS(base)) & CMDINV)
	goto must_be_adaptec;

    WAIT_UNTIL(INTERRUPT(base), CMDC);
    while (0) {
      fail:
	buslogic_printk("buslogic_detect: failed setting up mailboxes\n");
    }
    ok = TRUE;
  must_be_adaptec:
    INTR_RESET(base);
    if (!ok)
	printk("- must be Adaptec\n");	/* So that the adaptec detect looks
					   clean */
    return ok;
}

static int getconfig(unsigned int base, unsigned char *irq,
		     unsigned char *dma, unsigned char *id,
		     unsigned char *bus_type, unsigned short *max_sg)
{
    unsigned char inquiry_cmd[2];
    unsigned char inquiry_result[4];
    int i;

    i = inb(STATUS(base));
    if (i & DIRRDY)
	i = inb(DATA_IN(base));
    inquiry_cmd[0] = CMD_RETCONF;
    buslogic_out(base, inquiry_cmd, 1);
    buslogic_in(base, inquiry_result, 3);
    WAIT_UNTIL(INTERRUPT(base), CMDC);
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
	buslogic_printk("Unable to determine BusLogic IRQ level."
			"  Disabling board.\n");
	return TRUE;
    }
    *id = inquiry_result[2] & 0x7;

    inquiry_cmd[0] = CMD_INQEXTSETUP;
    inquiry_cmd[1] = 4;
    if (buslogic_out(base, inquiry_cmd, 2)
	|| buslogic_in(base, inquiry_result, 4))
	return TRUE;
    WAIT_UNTIL(INTERRUPT(base), CMDC);
    INTR_RESET(base);

#ifdef BUSLOGIC_BUS_TYPE_OVERRIDE
    *bus_type = BUS_TYPE_OVERRIDE;
#else
    *bus_type = inquiry_result[0];
#endif
    CHECK(*bus_type == 'A' || *bus_type == 'E' || *bus_type == 'M');
#ifdef BUSLOGIC_BUS_TYPE_OVERRIDE
    if (inquiry_result[0] != BUS_TYPE_OVERRIDE)
	buslogic_printk("Overriding bus type %c with %c\n",
			inquiry_result[0], BUS_TYPE_OVERRIDE);
#endif
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
	    buslogic_printk("Unable to determine BusLogic DMA channel."
			    "  Disabling board.\n");
	    return TRUE;
	}
    else
	*dma = 0;

    while (0) {
      fail:
	buslogic_printk("buslogic_detect: query board settings\n");
	return TRUE;
    }

    return FALSE;
}

static int get_translation(unsigned int base)
{
    /* ??? Unlike UltraStor, I see no way of determining whether > 1G mapping
       has been enabled.  However, it appears that BusLogic uses a mapping
       scheme which varies with the disk size when > 1G mapping is enabled.
       For disks <= 1G, this mapping is the same regardless of the setting of
       > 1G mapping.  Therefore, we should be safe in always assuming that > 1G
       mapping has been enabled. */
    return BIOS_TRANSLATION_BIG;
}

/* Query the board to find out the model. */
static int buslogic_query(unsigned int base, int *trans)
{
    static const unsigned char inquiry_cmd[] = { CMD_INQUIRY };
    unsigned char inquiry_result[4];
    int i;

    i = inb(STATUS(base));
    if (i & DIRRDY)
	i = inb(DATA_IN(base));
    buslogic_out(base, inquiry_cmd, sizeof inquiry_cmd);
    buslogic_in(base, inquiry_result, 4);
    WAIT_UNTIL(INTERRUPT(base), CMDC);
    INTR_RESET(base);

#if 1	/* ??? Temporary */
    buslogic_printk("Inquiry Bytes: %02X %02X %02X %02X\n",
		    inquiry_result[0], inquiry_result[1],
		    inquiry_result[2], inquiry_result[3]);
#endif

    while (0) {
      fail:
	buslogic_printk("buslogic_query: query board settings\n");
	return TRUE;
    }

    *trans = get_translation(base);

    return FALSE;
}

/* return non-zero on detection */
int buslogic_detect(Scsi_Host_Template *tpnt)
{
    unsigned char dma;
    unsigned char irq;
    unsigned int base = 0;
    unsigned char id;
    unsigned char bus_type;
    unsigned short max_sg;
    int trans;
    struct Scsi_Host *shpnt = NULL;
    int count = 0;
    int indx;

#if BUSLOGIC_DEBUG
    buslogic_printk("buslogic_detect:\n");
#endif

    for (indx = 0; indx < ARRAY_SIZE(bases); indx++)
	if (!check_region(bases[indx], 3)) {
	    shpnt = scsi_register(tpnt, sizeof (struct hostdata));

	    base = bases[indx];

	    if (test_port(base, shpnt))
		goto unregister;

	    /* Set the Bus on/off-times as not to ruin floppy performance. */
	    {
		/* The default ON/OFF times for BusLogic adapters is 7/4. */
		static const unsigned char oncmd[] = { CMD_BUSON_TIME, 7 };
		static const unsigned char offcmd[] = { CMD_BUSOFF_TIME, 5 };

		INTR_RESET(base);
		buslogic_out(base, oncmd, sizeof oncmd);
		WAIT_UNTIL(INTERRUPT(base), CMDC);
		/* CMD_BUSOFF_TIME is a noop for EISA boards, but as there is
		   no way to to differentiate EISA from VESA we send it
		   unconditionally. */
		INTR_RESET(base);
		buslogic_out(base, offcmd, sizeof offcmd);
		WAIT_UNTIL(INTERRUPT(base), CMDC);
		while (0) {
		  fail:
		    buslogic_printk("buslogic_detect:"
				    " setting bus on/off-time failed\n");
		}
		INTR_RESET(base);
	    }

	    if (buslogic_query(base, &trans))
		goto unregister;

	    if (getconfig(base, &irq, &dma, &id, &bus_type, &max_sg))
		goto unregister;

#if BUSLOGIC_DEBUG
	    buslogic_stat(base);
#endif
	    /* Here is where we tell the men from the boys (i.e. an Adaptec
	       will fail in setup_mailboxes, the men will not :-) */
	    if (!setup_mailboxes(base, shpnt))
		goto unregister;

	    printk("Configuring BusLogic %s HA at port 0x%03X, IRQ %u",
		   (bus_type == 'A' ? "ISA"
		    : (bus_type == 'E' ? "EISA/VESA" : "MCA")),
		   base, irq);
	    if (dma != 0)
		printk(", DMA %u", dma);
	    printk(", ID %u\n", id);

#if BUSLOGIC_DEBUG
	    buslogic_stat(base);
#endif

#if BUSLOGIC_DEBUG
	    buslogic_printk("buslogic_detect: enable interrupt channel %d\n",
			    irq);
#endif

	    cli();
	    if (request_irq(irq, buslogic_interrupt, 0, "buslogic")) {
		buslogic_printk("Unable to allocate IRQ for "
				"BusLogic controller.\n");
		sti();
		goto unregister;
	    }

	    if (dma) {
		if (request_dma(dma,"buslogic")) {
		    buslogic_printk("Unable to allocate DMA channel for "
				    "BusLogic controller.\n");
		    free_irq(irq);
		    sti();
		    goto unregister;
		}

		if (dma >= 5) {
		    outb((dma - 4) | CASCADE, DMA_MODE_REG);
		    outb(dma - 4, DMA_MASK_REG);
		}
	    }

	    host[irq - 9] = shpnt;
	    shpnt->this_id = id;
#ifdef CONFIG_NO_BUGGY_BUSLOGIC
	    /* Only type 'A' (AT/ISA) bus adapters use unchecked DMA. */
	    shpnt->unchecked_isa_dma = (bus_type == 'A');
#else
	    /* Bugs in the firmware of the 445S with >16M.  This does not seem
	       to affect Revision E boards with firmware 3.37. */
	    shpnt->unchecked_isa_dma = 1;
#endif
	    shpnt->sg_tablesize = max_sg;
	    if (shpnt->sg_tablesize > BUSLOGIC_MAX_SG)
		shpnt->sg_tablesize = BUSLOGIC_MAX_SG;
	    /* ??? If we can dynamically allocate the mailbox arrays, I'll
	       probably bump up this number. */
	    shpnt->hostt->can_queue = BUSLOGIC_MAILBOXES;
	    /* No known way to determine BIOS base address, but we don't
	       care since we don't use it anyway. */
	    shpnt->base = NULL;
	    shpnt->io_port = base;
	    shpnt->dma_channel = dma;
	    shpnt->irq = irq;
	    HOSTDATA(shpnt)->bios_translation = trans;
	    if (trans == BIOS_TRANSLATION_BIG)
		buslogic_printk("Using extended bios translation.\n");
	    HOSTDATA(shpnt)->last_mbi_used = 2 * BUSLOGIC_MAILBOXES - 1;
	    HOSTDATA(shpnt)->last_mbo_used = BUSLOGIC_MAILBOXES - 1;
	    memset(HOSTDATA(shpnt)->sc, 0, sizeof HOSTDATA(shpnt)->sc);
	    sti();

#if 0
	    {
		unsigned char buf[8];
		unsigned char cmd[]
		    = { READ_CAPACITY, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
		size_t i;

#if BUSLOGIC_DEBUG
		buslogic_printk("*** READ CAPACITY ***\n");
#endif
		for (i = 0; i < sizeof buf; i++)
		    buf[i] = 0x87;
		for (i = 0; i < 2; i++)
		    if (!buslogic_command(i, cmd, buf, sizeof buf)) {
			buslogic_printk("buslogic_detect: LU %u "
					"sector_size %d device_size %d\n",
					i, *(int *)(buf + 4), *(int *)buf);
		    }

#if BUSLOGIC_DEBUG
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

	    snarf_region(bases[indx], 3);	/* Register the IO ports that
						   we use */
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

    buslogic_printk("Potential to restart %d stalled commands...\n", count);
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
    size_t mbi, mbo;
    unsigned int i;

    buslogic_printk("buslogic_abort: %X %X\n",
		    inb(STATUS(scpnt->host->io_port)),
		    inb(INTERRUPT(scpnt->host->io_port)));

    cli();
    mb = HOSTDATA(scpnt->host)->mb;
    mbi = HOSTDATA(scpnt->host)->last_mbi_used + 1;
    if (mbi >= 2 * BUSLOGIC_MAILBOXES)
	mbi = BUSLOGIC_MAILBOXES;

    do {
	if (mb[mbi].status != MBX_NOT_IN_USE)
	    break;
	mbi++;
	if (mbi >= 2 * BUSLOGIC_MAILBOXES)
	    mbi = BUSLOGIC_MAILBOXES;
    } while (mbi != HOSTDATA(scpnt->host)->last_mbi_used);
    sti();

    if (mb[mbi].status != MBX_NOT_IN_USE) {
	buslogic_printk("Lost interrupt discovered on irq %d"
			" - attempting to recover\n",
			scpnt->host->irq);
	{
	    int intval[3];

	    intval[0] = scpnt->host->irq;
	    buslogic_interrupt((int)&intval[2]);
	    return SCSI_ABORT_SUCCESS;
	}
    }

    /* OK, no lost interrupt.  Try looking to see how many pending commands we
       think we have. */
    for (i = 0; i < BUSLOGIC_MAILBOXES; i++)
	if (HOSTDATA(scpnt->host)->sc[i]) {
	    if (HOSTDATA(scpnt->host)->sc[i] == scpnt) {
		buslogic_printk("Timed out command pending for %4.4X\n",
				scpnt->request.dev);
		if (HOSTDATA(scpnt->host)->mb[i].status != MBX_NOT_IN_USE) {
		    buslogic_printk("OGMB still full - restarting\n");
		    buslogic_out(scpnt->host->io_port, buscmd, sizeof buscmd);
		}
	    } else
		buslogic_printk("Other pending command %4.4X\n",
				scpnt->request.dev);
	}
#endif

#if (BUSLOGIC_DEBUG & BD_ABORT)
    buslogic_printk("buslogic_abort\n");
#endif

#if 1
    /* This section of code should be used carefully - some devices cannot
       abort a command, and this merely makes it worse. */
    cli();
    for (mbo = 0; mbo < BUSLOGIC_MAILBOXES; mbo++)
	if (scpnt == HOSTDATA(scpnt->host)->sc[mbo]) {
	    mb[mbo].status = MBX_ACTION_ABORT;
	    buslogic_out(scpnt->host->io_port, buscmd, sizeof buscmd);
	    break;
	}
    sti();
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

#if BUSLOGIC_DEBUG
    buslogic_printk("buslogic_reset\n");
#endif
#if 0
    /* This does a scsi reset for all devices on the bus. */
    outb(RSBUS, CONTROL(scpnt->host->io_port));
#else
    /* This does a selective reset of just the one device. */
    /* First locate the ccb for this command. */
    for (i = 0; i < BUSLOGIC_MAILBOXES; i++)
	if (HOSTDATA(scpnt->host)->sc[i] == scpnt) {
	    HOSTDATA(scpnt->host)->ccbs[i].op = 0x81;	/* ??? BUS DEVICE
							   RESET */

	    /* Now tell the BusLogic to flush all pending commands for this
	       target. */
	    buslogic_out(scpnt->host->io_port, buscmd, sizeof buscmd);

	    /* Here is the tricky part.  What to do next.  Do we get an
	       interrupt for the commands that we aborted with the specified
	       target, or do we generate this on our own?  Try it without first
	       and see what happens. */
	    buslogic_printk("Sent BUS DEVICE RESET to target %d\n",
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
		    printk("Sending DID_RESET for target %d\n", scpnt->target);
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

int buslogic_biosparam(Disk *disk, int dev, int *ip)
{
    /* ??? This truncates.  Should we round up to next MB? */
    unsigned int mb = disk->capacity >> 11;

    /* ip[0] == heads, ip[1] == sectors, ip[2] == cylinders */
    if (HOSTDATA(disk->device->host)->bios_translation == BIOS_TRANSLATION_BIG
	&& mb > 1024) {
	if (mb > 4096) {
	    ip[0] = 256;
	    ip[1] = 64;
	    ip[2] = mb >> 3;
/*	    if (ip[2] > 1024)
		ip[2] = 1024; */
	} else if (mb > 2048) {
	    ip[0] = 256;
	    ip[1] = 32;
	    ip[2] = mb >> 2;
	} else {
	    ip[0] = 128;
	    ip[1] = 32;
	    ip[2] = mb >> 1;
	}
    } else {
	ip[0] = 64;
	ip[1] = 32;
	ip[2] = mb;
/*	if (ip[2] > 1024)
	    ip[2] = 1024; */
    }
    return 0;
}
