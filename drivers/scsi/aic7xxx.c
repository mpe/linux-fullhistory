/*
 *  @(#)aic7xxx.c 1.34 94/11/30 jda
 *
 *  Adaptec 274x/284x/294x device driver for Linux.
 *  Copyright (c) 1994 The University of Calgary Department of Computer Science.
 *  
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *  
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *  
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *  Sources include the Adaptec 1740 driver (aha1740.c), the
 *  Ultrastor 24F driver (ultrastor.c), various Linux kernel
 *  source, the Adaptec EISA config file (!adp7771.cfg), the
 *  Adaptec AHA-2740A Series User's Guide, the Linux Kernel
 *  Hacker's Guide, Writing a SCSI Device Driver for Linux,
 *  the Adaptec 1542 driver (aha1542.c), the Adaptec EISA
 *  overlay file (adp7770.ovl), the Adaptec AHA-2740 Series
 *  Technical Reference Manual, the Adaptec AIC-7770 Data
 *  Book, the ANSI SCSI specification, the ANSI SCSI-2
 *  specification (draft 10c), ...
 *
 *  On a twin-bus adapter card, channel B is ignored.  Rationale:
 *  it would greatly complicate the sequencer and host driver code,
 *  and both busses are multiplexed on to the EISA bus anyway.  So
 *  I don't really see any technical advantage to supporting both.
 *
 *  As well, multiple adapter card using the same IRQ level are
 *  not supported.  It doesn't make sense to configure the cards
 *  this way from a performance standpoint.  Not to mention that
 *  the kernel would have to support two devices per registered IRQ.
 */

#include <stdarg.h>
#include <asm/io.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/ioport.h>
#include <linux/bios32.h>
#include <linux/delay.h>
#include <linux/pci.h>

#include "../block/blk.h"
#include "sd.h"
#include "scsi.h"
#include "hosts.h"
#include "aic7xxx.h"

/*
 *  There should be a specific return value for this in scsi.h, but
 *  it seems that most drivers ignore it.
 */
#define DID_UNDERFLOW	DID_ERROR

/* EISA/VL-bus stuff */

#define MINSLOT		1
#define MAXSLOT		15
#define SLOTBASE(x)	((x) << 12)

#define MAXIRQ		15

/* AIC-7770 offset definitions */

#define O_MINREG(x)	((x) + 0xc00)		/* i/o range to reserve */
#define O_MAXREG(x)	((x) + 0xcbf)

#define O_SCSISEQ(x)	((x) + 0xc00)		/* scsi sequence control */
#define O_SCSISIGI(x)	((x) + 0xc03)		/* scsi control signal read */
#define O_SCSISIGO(x)	((x) + 0xc03)		/* scsi control signal write */
#define O_SCSIID(x)	((x) + 0xc05)		/* scsi id */
#define O_SSTAT0(x)	((x) + 0xc0b)		/* scsi status register 0 */
#define O_CLRSINT1(x)	((x) + 0xc0c)		/* clear scsi interrupt 1 */
#define O_SSTAT1(x)	((x) + 0xc0c)		/* scsi status register 1 */
#define O_SELID(x)	((x) + 0xc19)		/* [re]selection id */
#define O_SBLKCTL(x)	((x) + 0xc1f)		/* scsi block control */
#define O_SEQCTL(x)	((x) + 0xc60)		/* sequencer control */
#define O_SEQRAM(x)	((x) + 0xc61)		/* sequencer ram data */
#define O_SEQADDR(x)	((x) + 0xc62)		/* sequencer address (W) */
#define O_BIDx(x)	((x) + 0xc80)		/* board id */
#define O_BCTL(x)	((x) + 0xc84)		/* board control */
#define O_HCNTRL(x)	((x) + 0xc87)		/* host control */
#define O_SCBPTR(x)	((x) + 0xc90)		/* scb pointer */
#define O_INTSTAT(x)	((x) + 0xc91)		/* interrupt status */
#define O_ERROR(x)	((x) + 0xc92)		/* hard error */
#define O_CLRINT(x)	((x) + 0xc92)		/* clear interrupt status */
#define O_SCBCNT(x)	((x) + 0xc9a)		/* scb auto increment */
#define O_QINFIFO(x)	((x) + 0xc9b)		/* queue in fifo */
#define O_QINCNT(x)	((x) + 0xc9c)		/* queue in count */
#define O_QOUTFIFO(x)	((x) + 0xc9d)		/* queue out fifo */
#define O_QOUTCNT(x)	((x) + 0xc9e)		/* queue out count */
#define O_SCBARRAY(x)	((x) + 0xca0)		/* scb array start */

/* AIC-7870-only definitions */

#define O_DSPCISTATUS(x) ((x) + 0xc86)		/* ??? */

/* host adapter offset definitions */

#define HA_REJBYTE(x)	((x) + 0xc31)		/* 1st message in byte */
#define HA_MSG_FLAGS(x)	((x) + 0xc35)		/* outgoing message flag */
#define HA_MSG_LEN(x)	((x) + 0xc36)		/* outgoing message length */
#define HA_MSG_START(x)	((x) + 0xc37)		/* outgoing message body */
#define HA_ARG_1(x)	((x) + 0xc4c)		/* sdtr <-> rate parameters */
#define HA_ARG_2(x)	((x) + 0xc4d)
#define HA_RETURN_1(x)	((x) + 0xc4c)
#define HA_RETURN_2(x)	((x) + 0xc4d)
#define HA_SIGSTATE(x)	((x) + 0xc4e)		/* value in SCSISIGO */
#define HA_NEEDSDTR(x)	((x) + 0xc4f)		/* synchronous negotiation? */
#define HA_SCBCOUNT(x)	((x) + 0xc56)		/* number of hardware SCBs */

#define HA_SCSICONF(x)	((x) + 0xc5a)		/* SCSI config register */
#define HA_INTDEF(x)	((x) + 0xc5c)		/* interrupt def'n register */
#define HA_HOSTCONF(x)	((x) + 0xc5d)		/* host config def'n register */

/* debugging code */

/*
#define AIC7XXX_DEBUG
*/

/*
 *  If a parity error occurs during a data transfer phase, run the
 *  command to completion - it's easier that way - making a note
 *  of the error condition in this location.  This then will modify
 *  a DID_OK status into a DID_PARITY one for the higher-level SCSI
 *  code.
 */
#define aic7xxx_parity(cmd)	((cmd)->SCp.Status)

/*
 *  Since the sequencer code DMAs the scatter-gather structures
 *  directly from memory, we use this macro to assert that the
 *  kernel structure hasn't changed.
 */
#define SG_STRUCT_CHECK(sg) \
	((char *)&(sg).address - (char *)&(sg) != 0 ||	\
	 (char *)&(sg).length  - (char *)&(sg) != 8 ||	\
	 sizeof((sg).address) != 4 ||			\
	 sizeof((sg).length)  != 4 ||			\
	 sizeof(sg)	      != 12)

/*
 *  "Static" structures.  Note that these are NOT initialized
 *  to zero inside the kernel - we have to initialize them all
 *  explicitly.
 *
 *  We support a maximum of one adapter card per IRQ level (see the
 *  rationale for this above).  On an interrupt, use the IRQ as an
 *  index into aic7xxx_boards[] to locate the card information.
 */
static struct Scsi_Host *aic7xxx_boards[MAXIRQ + 1];

/*
 *  The maximum number of SCBs we could have for ANY type
 *  of card.  DON'T FORGET TO CHANGE THE SCB MASK IN THE
 *  SEQUENCER CODE IF THIS IS MODIFIED!
 */
#define AIC7XXX_MAXSCB	16

struct aic7xxx_host {
	int base;					/* card base address */
	int maxscb;					/* hardware SCBs */
	int startup;					/* intr type check */
	int extended;					/* extended xlate? */
	volatile int unpause;				/* value for HCNTRL */
	volatile Scsi_Cmnd *SCB_array[AIC7XXX_MAXSCB];	/* active commands */
};

struct aic7xxx_host_config {
	int irq;					/* IRQ number */
	int base;					/* I/O base */
	int maxscb;					/* hardware SCBs */
	int unpause;					/* value for HCNTRL */
	int scsi_id;					/* host SCSI id */
	int extended;					/* extended xlate? */
};

struct aic7xxx_scb {
	unsigned char control;
	unsigned char target_channel_lun;		/* 4/1/3 bits */
	unsigned char SG_segment_count;
	unsigned char SG_list_pointer[4];
	unsigned char SCSI_cmd_pointer[4];
	unsigned char SCSI_cmd_length;
	unsigned char RESERVED[2];			/* must be zero */
	unsigned char target_status;
	unsigned char residual_data_count[3];
	unsigned char residual_SG_segment_count;
	unsigned char data_pointer[4];
	unsigned char data_count[3];
#if 0
	/*
	 *  No real point in transferring this to the
	 *  SCB registers.
	 */
	unsigned char RESERVED[6];
#endif
};

/*
 *  NB.  This table MUST be ordered shortest period first.
 */
static struct {
	short period;
	short rate;
	char *english;
} aic7xxx_synctab[] = {
	100,	0,	"10.0",
	125,	1,	"8.0",
	150,	2,	"6.67",
	175,	3,	"5.7",
	200,	4,	"5.0",
	225,	5,	"4.4",
	250,	6,	"4.0",
	275,	7,	"3.6"
};

static int aic7xxx_synctab_max =
	sizeof(aic7xxx_synctab) / sizeof(aic7xxx_synctab[0]);

enum aha_type {
	T_NONE,
	T_274X,
	T_284X,
	T_294X,
	T_MAX
};

#ifdef AIC7XXX_DEBUG

	extern int vsprintf(char *, const char *, va_list);

	static
	void debug(const char *fmt, ...)
	{
		va_list ap;
		char buf[256];

		va_start(ap, fmt);
		  vsprintf(buf, fmt, ap);
		  printk(buf);
		va_end(ap);
	}

	static
	void debug_config(enum aha_type type, struct aic7xxx_host_config *p)
	{
		int ioport2, ioport3;

		static char *BRT[T_MAX][16] = {
			{ },					/* T_NONE */
			{
				"2",   "???", "???", "12",	/* T_274X */
				"???", "???", "???", "28",
				"???", "???", "???", "44",
				"???", "???", "???", "60"
			},
			{
				"2",  "4",  "8",  "12",		/* T_284X */
				"16", "20", "24", "28",
				"32", "36", "40", "44",
				"48", "52", "56", "60"
			},
			{
				"???", "???", "???", "???",	/* T_294X */
				"???", "???", "???", "???",
				"???", "???", "???", "???",
				"???", "???", "???", "???"
			}
		};
		static int DFT[4] = {
			0, 50, 75, 100
		};
		static int SST[4] = {
			256, 128, 64, 32
		};

		ioport2 = inb(HA_HOSTCONF(p->base));
		ioport3 = inb(HA_SCSICONF(p->base));

		switch (type) {
		    case T_274X:
			printk("AHA274X AT EISA SLOT %d:\n", p->base >> 12);
			break;
		    case T_284X:
			printk("AHA284X AT SLOT %d:\n", p->base >> 12);
			break;
		    case T_294X:
			printk("AHA294X (PCI-bus):\n");
			break;
		    default:
			panic("aic7xxx debug_config: internal error\n");
		}

		printk("    irq %d\n"
		       "    bus release time %s bclks\n"
		       "    data fifo threshold %d%%\n",
		       p->irq,
		       BRT[type][(ioport2 >> 2) & 0xf],
		       DFT[(ioport2 >> 6) & 0x3]);

		printk("    SCSI CHANNEL A:\n"
		       "        scsi id %d\n"
		       "        scsi bus parity check %sabled\n"
		       "        scsi selection timeout %d ms\n"
		       "        scsi bus reset at power-on %sabled\n",
		       ioport3 & 0x7,
		       (ioport3 & 0x20) ? "en" : "dis",
		       SST[(ioport3 >> 3) & 0x3],
		       (ioport3 & 0x40) ? "en" : "dis");

		if (type == T_274X) {
			printk("        scsi bus termination %sabled\n",
			       (ioport3 & 0x80) ? "en" : "dis");
		}
	}

	static
	void debug_rate(int base, int rate)
	{
		int target = inb(O_SCSIID(base)) >> 4;

		if (rate) {
			printk("aic7xxx: target %d now synchronous at %sMb/s\n",
			       target,
			       aic7xxx_synctab[(rate >> 4) & 0x7].english);
		} else {
			printk("aic7xxx: target %d using asynchronous mode\n",
			       target);
		}
	}

#else

#	define debug(fmt, args...)
#	define debug_config(x)
#	define debug_rate(x,y)

#endif AIC7XXX_DEBUG

/*
 *  XXX - these options apply unilaterally to _all_ 274x/284x/294x
 *	  cards in the system.  This should be fixed, but then,
 *	  does anyone really have more than one in a machine?
 */
static int aic7xxx_extended = 0;		/* extended translation on? */

void aic7xxx_setup(char *s, int *dummy)
{
	int i;
	char *p;

	static struct {
		char *name;
		int *flag;
	} options[] = {
		"extended",	&aic7xxx_extended,
		NULL
	};

	for (p = strtok(s, ","); p; p = strtok(NULL, ",")) {
		for (i = 0; options[i].name; i++)
			if (!strcmp(options[i].name, p))
				*(options[i].flag) = !0;
	}
}

static
void aic7xxx_getscb(int base, struct aic7xxx_scb *scb)
{
	/*
	 *  This is almost identical to aic7xxx_putscb().
	 */
	outb(0x80, O_SCBCNT(base));	/* SCBAUTO */

	asm volatile("cld\n\t"
		     "rep\n\t"
		     "insb"
		     : /* no output */
		     :"D" (scb), "c" (sizeof(*scb)), "d" (O_SCBARRAY(base))
		     :"di", "cx", "dx");

	outb(0, O_SCBCNT(base));
}

/*
 *  How much data should be transferred for this SCSI command?  Stop
 *  at segment sg_last if it's a scatter-gather command so we can
 *  compute underflow easily.
 */
static
unsigned aic7xxx_length(Scsi_Cmnd *cmd, int sg_last)
{
	int i, segments;
	unsigned length;
	struct scatterlist *sg;

	segments = cmd->use_sg - sg_last;
	sg = (struct scatterlist *)cmd->buffer;

	if (cmd->use_sg) {
		for (i = length = 0;
		     i < cmd->use_sg && i < segments;
		     i++)
		{
			length += sg[i].length;
		}
	} else
		length = cmd->request_bufflen;

	return(length);
}

static
void aic7xxx_sg_check(Scsi_Cmnd *cmd)
{
	int i;
	struct scatterlist *sg = (struct scatterlist *)cmd->buffer;

	if (cmd->use_sg) {
		for (i = 0; i < cmd->use_sg; i++)
			if ((unsigned)sg[i].length > 0xffff)
				panic("aic7xxx_sg_check: s/g segment > 64k\n");
	}
}

static
void aic7xxx_to_scsirate(unsigned char *rate,
			 unsigned char transfer,
			 unsigned char offset)
{
	int i;

	transfer *= 4;

	for (i = 0; i < aic7xxx_synctab_max-1; i++) {

		if (transfer == aic7xxx_synctab[i].period) {
			*rate = (aic7xxx_synctab[i].rate << 4) | (offset & 0xf);
			return;
		}

		if (transfer > aic7xxx_synctab[i].period &&
		    transfer < aic7xxx_synctab[i+1].period)
		{
			*rate = (aic7xxx_synctab[i+1].rate << 4) |
				(offset & 0xf);
			return;
		}
	}
	*rate = 0;
}

/*
 *  Pause the sequencer and wait for it to actually stop - this
 *  is important since the sequencer can disable pausing for critical
 *  sections.
 */
#define PAUSE_SEQUENCER(p)	\
	do {								\
		outb(0xe, O_HCNTRL(p->base));	/* IRQMS|PAUSE|INTEN */	\
									\
		while ((inb(O_HCNTRL(p->base)) & 0x4) == 0)		\
			;						\
	} while (0)

/*
 *  Unpause the sequencer.  Unremarkable, yet done often enough to
 *  warrant an easy way to do it.
 */
#define UNPAUSE_SEQUENCER(p)	\
	outb(p->unpause, O_HCNTRL(p->base))	/* IRQMS|INTEN */

/*
 *  See comments in aic7xxx_loadram() wrt this.
 */
#define RESTART_SEQUENCER(p)	\
	do {							\
		do {						\
			outb(0x2, O_SEQCTL(p->base));		\
								\
		} while (inb(O_SEQADDR(p->base)) != 0 &&	\
			 inb(O_SEQADDR(p->base) + 1) != 0);	\
								\
		UNPAUSE_SEQUENCER(p);				\
	} while (0)

/*
 *  Since we declared this using SA_INTERRUPT, interrupts should
 *  be disabled all through this function unless we say otherwise.
 */
static
void aic7xxx_isr(int irq)
{
	int base, intstat;
	struct aic7xxx_host *p;
	
	p = (struct aic7xxx_host *)aic7xxx_boards[irq]->hostdata;
	base = p->base;

	/*
	 *  Check the startup flag - if no commands have been queued,
	 *  we probably have the interrupt type set wrong.  Reverse
	 *  the stored value and the active one in the host control
	 *  register.
	 */
	if (p->startup) {
		p->unpause ^= 0x8;
		outb(inb(O_HCNTRL(p->base)) ^ 0x8, O_HCNTRL(p->base));
		return;
	}

	/*
	 *  Handle all the interrupt sources - especially for SCSI
	 *  interrupts, we won't get a second chance at them.
	 */
	intstat = inb(O_INTSTAT(base));

	if (intstat & 0x8) {				/* BRKADRINT */

		panic("aic7xxx_isr: brkadrint, error = 0x%x, seqaddr = 0x%x\n",
		      inb(O_ERROR(base)), inw(O_SEQADDR(base)));
	}

	if (intstat & 0x4) {				/* SCSIINT */

		int scbptr = inb(O_SCBPTR(base));
		int status = inb(O_SSTAT1(base));
		Scsi_Cmnd *cmd;

		cmd = (Scsi_Cmnd *)p->SCB_array[scbptr];
		if (!cmd) {
			printk("aic7xxx_isr: no command for scb (scsiint)\n");
			/*
			 *  Turn off the interrupt and set status
			 *  to zero, so that it falls through the
			 *  reset of the SCSIINT code.
			 */
			outb(status, O_CLRSINT1(base));
			UNPAUSE_SEQUENCER(p);
			outb(0x4, O_CLRINT(base));	/* undocumented */
			status = 0;
		}
		p->SCB_array[scbptr] = NULL;

		/*
		 *  Only the SCSI Status 1 register has information
		 *  about exceptional conditions that we'd have a
		 *  SCSIINT about; anything in SSTAT0 will be handled
		 *  by the sequencer.  Note that there can be multiple
		 *  bits set.
		 */
		if (status & 0x80) {			/* SELTO */
			/*
			 *  Hardware selection timer has expired.  Turn
			 *  off SCSI selection sequence.
			 */
			outb(0, O_SCSISEQ(base));
			cmd->result = DID_TIME_OUT << 16;

			/*
			 *  If there's an active message, it belongs to the
			 *  command that is getting punted - remove it.
			 */
			outb(0, HA_MSG_FLAGS(base));

			/*
			 *  Shut off the offending interrupt sources, reset
			 *  the sequencer address to zero and unpause it,
			 *  then call the high-level SCSI completion routine.
			 *
			 *  WARNING!  This is a magic sequence!  After many
			 *  hours of guesswork, turning off the SCSI interrupts
			 *  in CLRSINT? does NOT clear the SCSIINT bit in
			 *  INTSTAT.  By writing to the (undocumented, unused
			 *  according to the AIC-7770 manual) third bit of
			 *  CLRINT, you can clear INTSTAT.  But, if you do it
			 *  while the sequencer is paused, you get a BRKADRINT
			 *  with an Illegal Host Address status, so the
			 *  sequencer has to be restarted first.
			 */
			outb(0x80, O_CLRSINT1(base));	/* CLRSELTIMO */
			RESTART_SEQUENCER(p);

			outb(0x4, O_CLRINT(base));	/* undocumented */
			cmd->scsi_done(cmd);
		}

		if (status & 0x4) {			/* SCSIPERR */
			/*
			 *  A parity error has occurred during a data
			 *  transfer phase.  Flag it and continue.
			 */
			printk("aic7xxx: parity error on target %d, lun %d\n",
			       cmd->target,
			       cmd->lun);
			aic7xxx_parity(cmd) = DID_PARITY;

			/*
			 *  Clear interrupt and resume as above.
			 */
			outb(0x4, O_CLRSINT1(base));	/* CLRSCSIPERR */
			UNPAUSE_SEQUENCER(p);

			outb(0x4, O_CLRINT(base));	/* undocumented */
		}

		if ((status & (0x8|0x4)) == 0 && status) {
			/*
			 *  We don't know what's going on.  Turn off the
			 *  interrupt source and try to continue.
			 */
			printk("aic7xxx_isr: sstat1 = 0x%x\n", status);
			outb(status, O_CLRSINT1(base));
			UNPAUSE_SEQUENCER(p);
			outb(0x4, O_CLRINT(base));	/* undocumented */
		}
	}

	if (intstat & 0x2) {				/* CMDCMPLT */

		int complete, old_scbptr;
		struct aic7xxx_scb scb;
		unsigned actual;
		Scsi_Cmnd *cmd;

		/*
		 *  The sequencer will continue running when it
		 *  issues this interrupt.  There may be >1 commands
		 *  finished, so loop until we've processed them all.
		 */
		do {
			complete = inb(O_QOUTFIFO(base));

			cmd = (Scsi_Cmnd *)p->SCB_array[complete];
			if (!cmd) {
				printk("aic7xxx warning: "
				       "no command for scb (cmdcmplt)\n");
				continue;
			}
			p->SCB_array[complete] = NULL;
			
			PAUSE_SEQUENCER(p);

			/*
			 *  After pausing the sequencer (and waiting
			 *  for it to stop), save its SCB pointer, then
			 *  write in our completed one and read the SCB
			 *  registers.  Afterwards, restore the saved
			 *  pointer, unpause the sequencer and call the
			 *  higher-level completion function - unpause
			 *  first since we have no idea how long done()
			 *  will take.
			 */
			old_scbptr = inb(O_SCBPTR(base));
			outb(complete, O_SCBPTR(base));

			aic7xxx_getscb(base, &scb);
			outb(old_scbptr, O_SCBPTR(base));

			UNPAUSE_SEQUENCER(p);

			cmd->result = scb.target_status |
				     (aic7xxx_parity(cmd) << 16);

			/*
			 *  Did we underflow?  At this time, there's only
			 *  one other driver that bothers to check for this,
			 *  and cmd->underflow seems to be set rather half-
			 *  heartedly in the higher-level SCSI code.
			 */
			actual = aic7xxx_length(cmd,
						scb.residual_SG_segment_count);

			actual -= ((scb.residual_data_count[2] << 16) |
				   (scb.residual_data_count[1] <<  8) |
				   (scb.residual_data_count[0]));

			if (actual < cmd->underflow) {
				printk("aic7xxx: target %d underflow - "
				       "wanted (at least) %u, got %u\n",
				       cmd->target, cmd->underflow, actual);

				cmd->result = scb.target_status |
					     (DID_UNDERFLOW << 16);
			}

			cmd->scsi_done(cmd);

			/*
			 *  Clear interrupt status before checking
			 *  the output queue again.  This eliminates
			 *  a race condition whereby a command could
			 *  complete between the queue poll and the
			 *  interrupt clearing, so notification of the
			 *  command being complete never made it back
			 *  up to the kernel.
			 */
			outb(0x2, O_CLRINT(base));	/* CLRCMDINT */

		} while (inb(O_QOUTCNT(base)));
	}

	if (intstat & 0x1) {				/* SEQINT */

		unsigned char transfer, offset, rate;

		/*
		 *  Although the sequencer is paused immediately on
		 *  a SEQINT, an interrupt for a SCSIINT or a CMDCMPLT
		 *  condition will have unpaused the sequencer before
		 *  this point.
		 */
		PAUSE_SEQUENCER(p);

		switch (intstat & 0xf0) {
		    case 0x00:
			panic("aic7xxx_isr: unknown scsi bus phase\n");
		    case 0x10:
			debug("aic7xxx_isr warning: "
			      "issuing message reject, 1st byte 0x%x\n",
			      inb(HA_REJBYTE(base)));
			break;
		    case 0x20:
			panic("aic7xxx_isr: reconnecting target %d "
			      "didn't issue IDENTIFY message\n",
			      (inb(O_SELID(base)) >> 4) & 0xf);
		    case 0x30:
			debug("aic7xxx_isr: sequencer couldn't find match "
			      "for reconnecting target %d - issuing ABORT\n",
			      (inb(O_SELID(base)) >> 4) & 0xf);
			break;
		    case 0x40:
			transfer = inb(HA_ARG_1(base));
			offset = inb(HA_ARG_2(base));
			aic7xxx_to_scsirate(&rate, transfer, offset);
			outb(rate, HA_RETURN_1(base));
			debug_rate(base, rate);
			break;
		    default:
			debug("aic7xxx_isr: seqint, "
			      "intstat = 0x%x, scsisigi = 0x%x\n",
			      intstat, inb(O_SCSISIGI(base)));
			break;
		}

		outb(0x1, O_CLRINT(base));		/* CLRSEQINT */
		UNPAUSE_SEQUENCER(p);
	}
}

/*
 *  Probing for EISA boards: it looks like the first two bytes
 *  are a manufacturer code - three characters, five bits each:
 *
 *		 BYTE 0   BYTE 1   BYTE 2   BYTE 3
 *		?1111122 22233333 PPPPPPPP RRRRRRRR
 *
 *  The characters are baselined off ASCII '@', so add that value
 *  to each to get the real ASCII code for it.  The next two bytes
 *  appear to be a product and revision number, probably vendor-
 *  specific.  This is what is being searched for at each port,
 *  and what should probably correspond to the ID= field in the
 *  ECU's .cfg file for the card - if your card is not detected,
 *  make sure your signature is listed in the array.
 *
 *  The fourth byte's lowest bit seems to be an enabled/disabled
 *  flag (rest of the bits are reserved?).
 */

static
enum aha_type aic7xxx_probe(int slot, int s_base)
{
	int i;
	unsigned char buf[4];

	static struct {
		int n;
		unsigned char signature[sizeof(buf)];
		enum aha_type type;
	} S[] = {
		4, { 0x04, 0x90, 0x77, 0x71 }, T_274X,	/* host adapter 274x */
		4, { 0x04, 0x90, 0x77, 0x70 }, T_274X,	/* motherboard 274x  */
		4, { 0x04, 0x90, 0x77, 0x56 }, T_284X,	/* 284x, BIOS enabled */
	};

	for (i = 0; i < sizeof(buf); i++) {
		/*
		 *  The VL-bus cards need to be primed by
		 *  writing before a signature check.
		 */
		outb(0x80 + i, s_base);
		buf[i] = inb(s_base + i);
	}

	for (i = 0; i < sizeof(S)/sizeof(S[0]); i++) {
		if (!memcmp(buf, S[i].signature, S[i].n)) {
			/*
			 *  Signature match on enabled card?
			 */
			if (inb(s_base + 4) & 1)
				return(S[i].type);
			printk("aic7xxx disabled at slot %d, ignored\n", slot);
		}
	}
	return(T_NONE);
}

/*
 *  Return ' ' for plain 274x, 'T' for twin-channel, 'W' for
 *  wide channel, '?' for anything else.
 */

static
char aic7xxx_type(int base)
{
	/*
	 *  AIC-7770/7870s can be wired so that, on chip reset,
	 *  the SCSI Block Control register indicates how many
	 *  busses the chip is configured for.  The two high bits
	 *  set indicate a 294x.
	 */
	switch (inb(O_SBLKCTL(base))) {
	    case 0:
	    case 0xc0:
		return(' ');
	    case 2:
	    case 0xc2:
		return('W');
	    case 8:
		return('T');
	    default:
		printk("aic7xxx has unknown bus configuration\n");
		return('?');
	}
}

static
void aic7xxx_loadram(int base)
{
	static unsigned char seqprog[] = {
		/*
		 *  Each sequencer instruction is 29 bits
		 *  long (fill in the excess with zeroes)
		 *  and has to be loaded from least -> most
		 *  significant byte, so this table has the
		 *  byte ordering reversed.
		 */
#		include "aic7xxx_seq.h"
	};

	/*
	 *  When the AIC-7770 is paused (as on chip reset), the
	 *  sequencer address can be altered and a sequencer
	 *  program can be loaded by writing it, byte by byte, to
	 *  the sequencer RAM port - the Adaptec documentation
	 *  recommends using REP OUTSB to do this, hence the inline
	 *  assembly.  Since the address autoincrements as we load
	 *  the program, reset it back to zero afterward.  Disable
	 *  sequencer RAM parity error detection while loading, and
	 *  make sure the LOADRAM bit is enabled for loading.
	 */
	outb(0x83, O_SEQCTL(base));	/* PERRORDIS|SEQRESET|LOADRAM */

	asm volatile("cld\n\t"
		     "rep\n\t"
		     "outsb"
		     : /* no output */
		     :"S" (seqprog), "c" (sizeof(seqprog)), "d" (O_SEQRAM(base))
		     :"si", "cx", "dx");

	/*
	 *  WARNING!  This is a magic sequence!  After extensive
	 *  experimentation, it seems that you MUST turn off the
	 *  LOADRAM bit before you play with SEQADDR again, else
	 *  you will end up with parity errors being flagged on
	 *  your sequencer program.  (You would also think that
	 *  turning off LOADRAM and setting SEQRESET to reset the
	 *  address to zero would work, but you need to do it twice
	 *  for it to take effect on the address.  Timing problem?)
	 */
	outb(0, O_SEQCTL(base));
	do {
		/*
		 *  Actually, reset it until
		 *  the address shows up as
		 *  zero just to be safe..
		 */
		outb(0x2, O_SEQCTL(base));	/* SEQRESET */

	} while (inb(O_SEQADDR(base)) != 0 && inb(O_SEQADDR(base) + 1) != 0);
}

static
void aha274x_config(struct aic7xxx_host_config *p, va_list ap)
{
	int base = va_arg(ap, int);

	/*
	 *  Give the AIC-7770 a reset - reading the 274x's registers
	 *  returns zeroes unless you do.  This forces a pause of the
	 *  Sequencer.
	 */
	outb(1, O_HCNTRL(base));	/* CHIPRST */

	p->base = base;
	p->irq = inb(HA_INTDEF(base)) & 0xf;
	p->scsi_id = inb(HA_SCSICONF(base)) & 0x7;

	/*
	 *  This value for HCNTRL may be changed in the ISR if we
	 *  catch a spurious interrupt right away.
	 */
	p->unpause = 0xa;

	/*
	 *  XXX - these are values that I don't know how to query
	 *	  the hardware for.  Apparently some revision E
	 *	  '7770s can have more SCBs, and I don't know how
	 *	  to get the "extended translation" flag from the
	 *	  EISA data area.
	 */
	p->maxscb = 4;
	p->extended = aic7xxx_extended;

	/*
	 *  A reminder until this can be detected automatically.
	 */
	printk("aha274x: extended translation %sabled\n",
		p->extended ? "en" : "dis");
}

static
void aha284x_config(struct aic7xxx_host_config *p, va_list ap)
{
	int base = va_arg(ap, int);

	/*
	 *  Give the AIC-7770 a reset - this forces a pause of the
	 *  Sequencer and returns everything to default values.
	 */
	outb(1, O_HCNTRL(base));	/* CHIPRST */

	p->base = base;
	p->unpause = 0x2;
	p->irq = inb(HA_INTDEF(base)) & 0xf;
	p->scsi_id = inb(HA_SCSICONF(base)) & 0x7;

	/*
	 *  XXX - these are values that I don't know how to query
	 *	  the hardware for.  Apparently some revision E
	 *	  '7770s can have more SCBs, and I don't know how
	 *	  to get the "extended translation" flag from the
	 *	  onboard memory.
	 */
	p->maxscb = 4;
	p->extended = aic7xxx_extended;

	/*
	 *  A reminder until this can be detected automatically.
	 */
	printk("aha284x: extended translation %sabled\n",
		p->extended ? "en" : "dis");
}

static
void aha294x_config(struct aic7xxx_host_config *p, va_list ap)
{
	int error;
	unsigned long io_port;
	unsigned char bus, device_fn, irq;

	bus = va_arg(ap, unsigned char);
	device_fn = va_arg(ap, unsigned char);

	/*
	 *  Read esundry information from PCI BIOS.
	 */
	error = pcibios_read_config_dword(bus,
					  device_fn,
					  PCI_BASE_ADDRESS_0,
					  &io_port);
	if (error) {
		panic("aha294x_config: error %s reading i/o port\n",
		      pcibios_strerror(error));
	}

	error = pcibios_read_config_byte(bus,
					 device_fn,
					 PCI_INTERRUPT_LINE,
					 &irq);
	if (error) {
		panic("aha294x_config: error %s reading irq\n",
		      pcibios_strerror(error));
	}

	/*
	 *  Make the base I/O register look like EISA and VL-bus.
	 */
	p->base = io_port - 0xc01;

	/*
	 *  Give the AIC-7870 a reset - this forces a pause of the
	 *  Sequencer and returns everything to default values.
	 */
	outb(1, O_HCNTRL(p->base));	/* CHIPRST */

	p->irq = irq;
	p->maxscb = 16;
	p->unpause = 0xa;

	/*
	 *  XXX - these are values that I don't know how to query
	 *	  the hardware for, so for now the SCSI host ID is
	 *	  hardwired to 7, and the "extended translation"
	 *	  flag is taken from boot-time flags.
	 */
	p->scsi_id = 7;
	p->extended = aic7xxx_extended;

	/*
	 *  XXX - force data fifo threshold to 100%.  Why does this
	 *	  need to be done?
	 */
#	define	DFTHRESH	3

	outb(DFTHRESH << 6, O_DSPCISTATUS(p->base));
	outb(p->scsi_id | (DFTHRESH << 6), HA_SCSICONF(p->base));

	/*
	 *  A reminder until this can be detected automatically.
	 */
	printk("aha294x: extended translation %sabled\n",
		p->extended ? "en" : "dis");
}

static
int aic7xxx_register(Scsi_Host_Template *template, enum aha_type type, ...)
{
	va_list ap;
	int i, base;
	struct Scsi_Host *host;
	struct aic7xxx_host *p;
	struct aic7xxx_host_config config;

	va_start(ap, type);
	
	switch (type) {
	    case T_274X:
		aha274x_config(&config, ap);
		break;
	    case T_284X:
		aha284x_config(&config, ap);
		break;
	    case T_294X:
		aha294x_config(&config, ap);
		break;
	    default:
		panic("aic7xxx_register: internal error\n");
	}
	va_end(ap);

	base = config.base;

	/*
	 *  The IRQ level in i/o port 4 maps directly onto the real
	 *  IRQ number.  If it's ok, register it with the kernel.
	 *
	 *  NB. the Adaptec documentation says the IRQ number is only
	 *	in the lower four bits; the ECU information shows the
	 *	high bit being used as well.  Which is correct?
	 */
	if (config.irq < 9 || config.irq > 15) {
		printk("aic7xxx uses unsupported IRQ level, ignoring\n");
		return(0);
	}
	
	/*
	 *  Lock out other contenders for our i/o space.
	 */
	snarf_region(O_MINREG(base), O_MAXREG(base)-O_MINREG(base));

	/*
	 *  Any card-type-specific adjustments before we register
	 *  the scsi host(s).
	 */
	switch (aic7xxx_type(base)) {
	    case 'T':
		printk("aic7xxx warning: ignoring channel B of 274x-twin\n");
		break;
	    case ' ':
		break;
	    default:
		printk("aic7xxx is an unsupported type, ignoring\n");
		return(0);
	}

	/*
	 *  Before registry, make sure that the offsets of the
	 *  struct scatterlist are what the sequencer will expect,
	 *  otherwise disable scatter-gather altogether until someone
	 *  can fix it.  This is important since the sequencer will
	 *  DMA elements of the SG array in while executing commands.
	 */
	if (template->sg_tablesize != SG_NONE) {
		struct scatterlist sg;

		if (SG_STRUCT_CHECK(sg)) {
			printk("aic7xxx warning: kernel scatter-gather "
			       "structures changed, disabling it\n");
			template->sg_tablesize = SG_NONE;
		}
	}
	
	/*
	 *  Register each "host" and fill in the returned Scsi_Host
	 *  structure as best we can.  Some of the parameters aren't
	 *  really relevant for bus types beyond ISA, and none of the
	 *  high-level SCSI code looks at it anyway.. why are the fields
	 *  there?  Also save the pointer so that we can find the
	 *  information when an IRQ is triggered.
	 */
	host = scsi_register(template, sizeof(struct aic7xxx_host));
	host->can_queue = config.maxscb;
	host->this_id = config.scsi_id;
	host->irq = config.irq;

	aic7xxx_boards[config.irq] = host;
	
	p = (struct aic7xxx_host *)host->hostdata;
	for (i = 0; i < AIC7XXX_MAXSCB; i++)
		p->SCB_array[i] = NULL;

	p->base = config.base;
	p->maxscb = config.maxscb;
	p->extended = config.extended;

	/*
	 *  The interrupt trigger is different depending
	 *  on whether the card is EISA or VL-bus - sometimes.
	 *  The startup variable will be cleared once the first
	 *  command is queued, and is checked in the isr to
	 *  try and detect when the interrupt type is set
	 *  incorrectly, triggering an interrupt immediately.
	 *  This is now just set on a per-card-type basis.
	 */
	p->unpause = config.unpause;
	p->startup = !0;

	/*
	 *  Register IRQ with the kernel _after_ the host information
	 *  is set up, in case we take an interrupt right away, due to
	 *  the interrupt type being set wrong.
	 */
	if (request_irq(config.irq, aic7xxx_isr, SA_INTERRUPT, "aic7xxx")) {
		printk("aic7xxx couldn't register irq %d, ignoring\n",
		       config.irq);
		return(0);
	}

	/*
	 *  Print out debugging information before re-enabling
	 *  the card - a lot of registers on it can't be read
	 *  when the sequencer is active.
	 */
#ifdef AIC7XXX_DEBUG
	debug_config(type, &config);
#endif

	/*
	 *  Load the sequencer program, then re-enable the board -
	 *  resetting the AIC-7770 disables it, leaving the lights
	 *  on with nobody home.  On the PCI bus you *may* be home,
	 *  but then your mailing address is dynamically assigned
	 *  so no one can find you anyway :-)
	 */
	aic7xxx_loadram(base);
	if (type != T_294X)
		outb(1, O_BCTL(base));	/* ENABLE */

	/*
	 *  Set the host adapter registers to indicate that synchronous
	 *  negotiation should be attempted the first time the targets
	 *  are communicated with.  Also initialize the active message
	 *  flag to indicate that there is no message.
	 */
	outb(0xff, HA_NEEDSDTR(base));
	outb(0, HA_MSG_FLAGS(base));

	/*
	 *  For reconnecting targets, the sequencer code needs to
	 *  know how many SCBs it has to search through.
	 */
	outb(config.maxscb, HA_SCBCOUNT(base));

	/*
	 *  Unpause the sequencer before returning and enable
	 *  interrupts - we shouldn't get any until the first
	 *  command is sent to us by the high-level SCSI code.
	 */
	UNPAUSE_SEQUENCER(p);
	return(1);
}

int aic7xxx_detect(Scsi_Host_Template *template)
{
	enum aha_type type;
	int found = 0, slot, base;

	/*
	 *  EISA/VL-bus card signature probe.
	 */
	for (slot = MINSLOT; slot <= MAXSLOT; slot++) {

		base = SLOTBASE(slot);
		
		if (check_region(O_MINREG(base),
				 O_MAXREG(base)-O_MINREG(base)))
		{
			/*
			 *  Some other driver has staked a
			 *  claim to this i/o region already.
			 */
			continue;
		}

		type = aic7xxx_probe(slot, O_BIDx(base));

		if (type != T_NONE) {
			/*
			 *  We "find" a 274x if we locate the card
			 *  signature and we can set it up and register
			 *  it with the kernel without incident.
			 */
			found += aic7xxx_register(template, type, base);
		}
	}

	/*
	 *  PCI-bus probe.
	 */
	if (pcibios_present()) {
		int index = 0;
		unsigned char bus, device_fn;

		while (!pcibios_find_device(PCI_VENDOR_ID_ADAPTEC,
					    PCI_DEVICE_ID_ADAPTEC_2940,
					    index,
					    &bus,
					    &device_fn))
		{
			found += aic7xxx_register(template, T_294X,
						  bus, device_fn);
			index += 1;
		}
	}

	template->name = (char *)aic7xxx_info(NULL);
	return(found);
}

const char *aic7xxx_info(struct Scsi_Host *notused)
{
	return("Adaptec AHA274x/284x/294x (EISA/VL-bus/PCI -> Fast SCSI) "
	       AIC7XXX_SEQ_VERSION "/"
	       AIC7XXX_H_VERSION "/"
	       "1.34");
}

static
void aic7xxx_buildscb(struct aic7xxx_host *p,
		      Scsi_Cmnd *cmd,
		      struct aic7xxx_scb *scb)
{
	void *addr;
	unsigned length;

	memset(scb, 0, sizeof(*scb));

	/*
	 *  NB. channel selection (bit 3) is always zero.
	 */
	scb->target_channel_lun = ((cmd->target << 4) & 0xf0) |
				   (cmd->lun & 0x7);

	/*
	 *  The interpretation of request_buffer and request_bufflen
	 *  changes depending on whether or not use_sg is zero; a
	 *  non-zero use_sg indicates the number of elements in the
	 *  scatter-gather array.
	 *
	 *  The AIC-7770 can't support transfers of any sort larger
	 *  than 2^24 (three-byte count) without backflips.  For what
	 *  the kernel is doing, this shouldn't occur.  I hope.
	 */
	length = aic7xxx_length(cmd, 0);

	/*
	 *  The sequencer code cannot yet handle scatter-gather segments
	 *  larger than 64k (two-byte length).  The 1.1.x kernels, however,
	 *  have a four-byte length field in the struct scatterlist, so
	 *  make sure we don't exceed 64k on these kernels for now.
	 */
	aic7xxx_sg_check(cmd);

	if (length > 0xffffff) {
		panic("aic7xxx_buildscb: can't transfer > 2^24 - 1 bytes\n");
	}

	/*
	 *  XXX - this relies on the host data being stored in a
	 *	  little-endian format.
	 */
	addr = cmd->cmnd;
	scb->SCSI_cmd_length = cmd->cmd_len;
	memcpy(scb->SCSI_cmd_pointer, &addr, sizeof(scb->SCSI_cmd_pointer));

	if (cmd->use_sg) {
#if 0
		debug("aic7xxx_buildscb: SG used, %d segments, length %u\n",
		      cmd->use_sg,
		      length);
#endif
		scb->SG_segment_count = cmd->use_sg;
		memcpy(scb->SG_list_pointer,
		       &cmd->request_buffer,
		       sizeof(scb->SG_list_pointer));
	} else {
		scb->SG_segment_count = 0;
		memcpy(scb->data_pointer,
		       &cmd->request_buffer,
		       sizeof(scb->data_pointer));
		memcpy(scb->data_count,
		       &cmd->request_bufflen,
		       sizeof(scb->data_count));
	}
}

static
void aic7xxx_putscb(int base, struct aic7xxx_scb *scb)
{
	/*
	 *  By turning on the SCB auto increment, any reference
	 *  to the SCB I/O space postincrements the SCB address
	 *  we're looking at.  So turn this on and dump the relevant
	 *  portion of the SCB to the card.
	 */
	outb(0x80, O_SCBCNT(base));	/* SCBAUTO */

	asm volatile("cld\n\t"
		     "rep\n\t"
		     "outsb"
		     : /* no output */
		     :"S" (scb), "c" (sizeof(*scb)), "d" (O_SCBARRAY(base))
		     :"si", "cx", "dx");

	outb(0, O_SCBCNT(base));
}

int aic7xxx_queue(Scsi_Cmnd *cmd, void (*fn)(Scsi_Cmnd *))
{
	long flags;
	int empty, old_scbptr;
	struct aic7xxx_host *p;
	struct aic7xxx_scb scb;

#if 0
	debug("aic7xxx_queue: cmd 0x%x (size %u), target %d, lun %d\n",
	      cmd->cmnd[0],
	      cmd->cmd_len,
	      cmd->target,
	      cmd->lun);
#endif

	p = (struct aic7xxx_host *)cmd->host->hostdata;

	/*
	 *  Construct the SCB beforehand, so the sequencer is
	 *  paused a minimal amount of time.
	 */
	aic7xxx_buildscb(p, cmd, &scb);

	/*
	 *  Clear the startup flag - we can now legitimately
	 *  expect interrupts.
	 */
	p->startup = 0;

	/*
	 *  This is a critical section, since we don't want the
	 *  interrupt routine mucking with the host data or the
	 *  card.  Since the kernel documentation is vague on
	 *  whether or not we are in a cli/sti pair already, save
	 *  the flags to be on the safe side.
	 */
	save_flags(flags);
	cli();

	/*
	 *  Find a free slot in the SCB array to load this command
	 *  into.  Since can_queue is set to the maximum number of
	 *  SCBs for the card, we should always find one.
	 */
	for (empty = 0; empty < p->maxscb; empty++)
		if (!p->SCB_array[empty])
			break;
	if (empty == p->maxscb)
		panic("aic7xxx_queue: couldn't find a free scb\n");

	/*
	 *  Pause the sequencer so we can play with its registers -
	 *  wait for it to acknowledge the pause.
	 *
	 *  XXX - should the interrupts be left on while doing this?
	 */
	PAUSE_SEQUENCER(p);

	/*
	 *  Save the SCB pointer and put our own pointer in - this
	 *  selects one of the four banks of SCB registers.  Load
	 *  the SCB, then write its pointer into the queue in FIFO
	 *  and restore the saved SCB pointer.
	 */
	old_scbptr = inb(O_SCBPTR(p->base));
	outb(empty, O_SCBPTR(p->base));
	
	aic7xxx_putscb(p->base, &scb);

	outb(empty, O_QINFIFO(p->base));
	outb(old_scbptr, O_SCBPTR(p->base));

	/*
	 *  Make sure the Scsi_Cmnd pointer is saved, the struct it
	 *  points to is set up properly, and the parity error flag
	 *  is reset, then unpause the sequencer and watch the fun
	 *  begin.
	 */
	cmd->scsi_done = fn;
	p->SCB_array[empty] = cmd;
	aic7xxx_parity(cmd) = DID_OK;

	UNPAUSE_SEQUENCER(p);

	restore_flags(flags);
	return(0);
}

/* return values from aic7xxx_kill */

enum k_state {
	k_ok,				/* scb found and message sent */
	k_busy,				/* message already present */
	k_absent,			/* couldn't locate scb */
	k_disconnect,			/* scb found, but disconnected */
};

/*
 *  This must be called with interrupts disabled - it's going to
 *  be messing around with the host data, and an interrupt being
 *  fielded in the middle could get ugly.
 *
 *  Since so much of the abort and reset code is shared, this
 *  function performs more magic than it really should.  If the
 *  command completes ok, then it will call scsi_done with the
 *  result code passed in.  The unpause parameter controls whether
 *  or not the sequencer gets unpaused - the reset function, for
 *  instance, may want to do something more aggressive.
 *
 *  Note that the command is checked for in our SCB_array first
 *  before the sequencer is paused, so if k_absent is returned,
 *  then the sequencer is NOT paused.
 */

static
enum k_state aic7xxx_kill(Scsi_Cmnd *cmd, unsigned char message,
			  unsigned int result, int unpause)
{
	struct aic7xxx_host *p;
	int i, scb, found, queued;
	unsigned char scbsave[AIC7XXX_MAXSCB];

	p = (struct aic7xxx_host *)cmd->host->hostdata;

	/*
	 *  If we can't find the command, assume it just completed
	 *  and shrug it away.
	 */
	for (scb = 0; scb < p->maxscb; scb++)
		if (p->SCB_array[scb] == cmd)
			break;

	if (scb == p->maxscb)
		return(k_absent);

	PAUSE_SEQUENCER(p);

	/*
	 *  This is the best case, really.  Check to see if the
	 *  command is still in the sequencer's input queue.  If
	 *  so, simply remove it.  Reload the queue afterward.
	 */
	queued = inb(O_QINCNT(p->base));
	
	for (i = found = 0; i < queued; i++) {
		scbsave[i] = inb(O_QINFIFO(p->base));

		if (scbsave[i] == scb) {
			found = 1;
			i -= 1;
		}
	}

	queued -= found;
	for (i = 0; i < queued; i++)
		outb(scbsave[i], O_QINFIFO(p->base));

	if (found)
		goto complete;

	/*
	 *  Check the current SCB bank.  If it's not the one belonging
	 *  to the command we want to kill, assume that the command
	 *  is disconnected.  It's rather a pain to force a reconnect
	 *  and send a message to the target, so we abdicate responsibility
	 *  in this case.
	 */
	if (inb(O_SCBPTR(p->base)) != scb) {
		if (unpause)
			UNPAUSE_SEQUENCER(p);
		return(k_disconnect);
	}

	/*
	 *  Presumably at this point our target command is active.  Check
	 *  to see if there's a message already in effect.  If not, place
	 *  our message in and assert ATN so the target goes into MESSAGE
	 *  OUT phase.
	 */
	if (inb(HA_MSG_FLAGS(p->base)) & 0x80) {
		if (unpause)
			UNPAUSE_SEQUENCER(p);
		return(k_busy);
	}

	outb(0x80, HA_MSG_FLAGS(p->base));		/* active message */
	outb(1, HA_MSG_LEN(p->base));			/* length = 1 */
	outb(message, HA_MSG_START(p->base));		/* message body */

	/*
	 *  Assert ATN.  Use the value of SCSISIGO saved by the
	 *  sequencer code so we don't alter its contents radically
	 *  in the middle of something critical.
	 */
	outb(inb(HA_SIGSTATE(p->base)) | 0x10, O_SCSISIGO(p->base));

	/*
	 *  The command has been killed.  Do the bookkeeping, unpause
	 *  the sequencer, and notify the higher-level SCSI code.
	 */
complete:
	p->SCB_array[scb] = NULL;
	if (unpause)
		UNPAUSE_SEQUENCER(p);

	cmd->result = result << 16;
	cmd->scsi_done(cmd);
	return(k_ok);
}

int aic7xxx_abort(Scsi_Cmnd *cmd)
{
	int rv;
	long flags;

	save_flags(flags);
	cli();

	switch (aic7xxx_kill(cmd, ABORT, DID_ABORT, !0)) {
	    case k_ok:		rv = SCSI_ABORT_SUCCESS;	break;
	    case k_busy:	rv = SCSI_ABORT_BUSY;		break;
	    case k_absent:	rv = SCSI_ABORT_NOT_RUNNING;	break;
	    case k_disconnect:	rv = SCSI_ABORT_SNOOZE;		break;
	    default:
		panic("aic7xxx_do_abort: internal error\n");
	}

	restore_flags(flags);
	return(rv);
}

/*
 *  Resetting the bus always succeeds - is has to, otherwise the
 *  kernel will panic!  Try a surgical technique - sending a BUS
 *  DEVICE RESET message - on the offending target before pulling
 *  the SCSI bus reset line.
 */

int aic7xxx_reset(Scsi_Cmnd *cmd)
{
	int i;
	long flags;
	Scsi_Cmnd *reset;
	struct aic7xxx_host *p;

	p = (struct aic7xxx_host *)cmd->host->hostdata;
	save_flags(flags);
	cli();

	switch (aic7xxx_kill(cmd, BUS_DEVICE_RESET, DID_RESET, 0)) {

	    case k_ok:
		/*
		 *  The RESET message was sent to the target
		 *  with no problems.  Flag that target as
		 *  needing a SDTR negotiation on the next
		 *  connection and restart the sequencer.
		 */
		outb((1 << cmd->target), HA_NEEDSDTR(p->base));
		UNPAUSE_SEQUENCER(p);
		break;

	    case k_absent:
		/*
		 *  The sequencer will not be paused if aic7xxx_kill()
		 *  couldn't find the command.
		 */
		PAUSE_SEQUENCER(p);
		/* falls through */

	    case k_busy:
	    case k_disconnect:
		/*
		 *  Do a hard reset of the SCSI bus.  According to the
		 *  SCSI-2 draft specification, reset has to be asserted
		 *  for at least 25us.  I'm invoking the kernel delay
		 *  function for 30us since I'm not totally trusting of
		 *  the busy loop timing.
		 *
		 *  XXX - I'm not convinced this works.  I tried resetting
		 *	  the bus before, trying to get the devices on the
		 *	  bus to revert to asynchronous transfer, and it
		 *	  never seemed to work.
		 */
		debug("aic7xxx: attempting to reset scsi bus and card\n");

		outb(1, O_SCSISEQ(p->base));		/* SCSIRSTO */
		udelay(30);
		outb(0, O_SCSISEQ(p->base));		/* !SCSIRSTO */

		outb(0xff, HA_NEEDSDTR(p->base));
		UNPAUSE_SEQUENCER(p);

		/*
		 *  Locate the command and return a "reset" status
		 *  for it.  This is not completely correct and will
		 *  probably return to haunt me later.
		 */
		for (i = 0; i < p->maxscb; i++) {
			if (cmd == p->SCB_array[i]) {
				reset = (Scsi_Cmnd *)p->SCB_array[i];
				p->SCB_array[i] = NULL;
				reset->result = DID_RESET << 16;
				reset->scsi_done(reset);
				break;
			}
		}
		break;

	    default:
		panic("aic7xxx_reset: internal error\n");
	}

	restore_flags(flags);
	return(SCSI_RESET_SUCCESS);
}

int aic7xxx_biosparam(Disk *disk, int devno, int geom[])
{
	int heads, sectors, cylinders;
	struct aic7xxx_host *p;
	
	p = (struct aic7xxx_host *)disk->device->host->hostdata;

	/*
	 *  XXX - if I could portably find the card's configuration
	 *	  information, then this could be autodetected instead
	 *	  of left to a boot-time switch.
	 */
	heads = 64;
	sectors = 32;
	cylinders = disk->capacity / (heads * sectors);

	if (p->extended && cylinders > 1024) {
		heads = 255;
		sectors = 63;
		cylinders = disk->capacity / (255 * 63);
	}

	geom[0] = heads;
	geom[1] = sectors;
	geom[2] = cylinders;

	return(0);
}

