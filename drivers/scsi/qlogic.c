/*----------------------------------------------------------------*/
/*
   Qlogic linux driver - work in progress. No Warranty express or implied.
   Use at your own risk.  Support Tort Reform so you won't have to read all
   these silly disclaimers.

   Copyright 1994, Tom Zerucha.
   zerucha@shell.portal.com

   Additional Code, and much appreciated help by
   Michael A. Griffith
   grif@cs.ucr.edu

   Reference Qlogic FAS408 Technical Manual, 53408-510-00A, May 10, 1994
   (you can reference it, but it is incomplete and inaccurate in places)

   Version 0.38b

   This also works with loadable SCSI as a module.  Check configuration
   options QL_INT_ACTIVE_HIGH and QL_TURBO_PDMA for PCMCIA usage (which
   also requires an enabler).

   Redistributable under terms of the GNU Public License

*/
/*----------------------------------------------------------------*/
/* Configuration */
/* Set this if you are using the PCMCIA adapter - it will automatically
   take care of several settings */
#define QL_PCMCIA 0

/* Set the following to 2 to use normal interrupt (active high/totempole-
   tristate), otherwise use 0 (REQUIRED FOR PCMCIA) for active low, open
   drain */
#define QL_INT_ACTIVE_HIGH 2

/* Set the following to 1 to enable the use of interrupts.  Note that 0 tends
   to be more stable, but slower (or ties up the system more) */
#define QL_USE_IRQ 1

/* Set the following to max out the speed of the PIO PseudoDMA transfers,
   again, 0 tends to be slower, but more stable.  THIS SHOULD BE ZERO FOR
   PCMCIA */
#define QL_TURBO_PDMA 1

/* This will reset all devices when the driver is initialized (during bootup).
   The other linux drivers don't do this, but the DOS drivers do, and after
   using DOS or some kind of crash or lockup this will bring things back */
#define QL_RESET_AT_START 1

/* This will set fast (10Mhz) synchronous timing, FASTCLK must also be 1*/
#define FASTSCSI  0

/* This will set a faster sync transfer rate */
#define FASTCLK   0

/* This bit needs to be set to 1 if your cabling is long or noisy */
#define SLOWCABLE 0

/* This is the sync transfer divisor, 40Mhz/X will be the data rate
	The power on default is 5, the maximum normal value is 5 */
#define SYNCXFRPD 4

/* This is the count of how many synchronous transfers can take place
	i.e. how many reqs can occur before an ack is given.
	The maximum value for this is 15, the upper bits can modify
	REQ/ACK assertion and deassertion during synchronous transfers
	If this is 0, the bus will only transfer asynchronously */
#define SYNCOFFST 0
/* for the curious, bits 7&6 control the deassertion delay in 1/2 cycles
	of the 40Mhz clock. If FASTCLK is 1, specifying 01 (1/2) will
	cause the deassertion to be early by 1/2 clock.  Bits 5&4 control
	the assertion delay, also in 1/2 clocks (FASTCLK is ignored here). */

/* Option Synchronization */
	
#if QL_PCMCIA
#undef QL_INT_ACTIVE_HIGH
#undef QL_TURBO_PDMA
#define QL_INT_ACTIVE_HIGH 0
#define QL_TURBO_PDMA 0
#endif

/*----------------------------------------------------------------*/

#ifdef MODULE
#include <linux/module.h>
#endif

#include "../block/blk.h"	/* to get disk capacity */
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <unistd.h>
#include <asm/io.h>
#include <asm/irq.h>
#include "sd.h"
#include "hosts.h"
#include "qlogic.h"

/*----------------------------------------------------------------*/
/* driver state info, local to driver */
static int	    qbase;	/* Port */
static int	    qinitid;	/* initiator ID */
static int	    qabort;	/* Flag to cause an abort */
static int	    qlirq;	/* IRQ being used */
static char	    qinfo[80];	/* description */
static Scsi_Cmnd   *qlcmd;	/* current command being processed */

/*----------------------------------------------------------------*/

#define REG0 ( outb( inb( qbase + 0xd ) & 0x7f , qbase + 0xd ), outb( 4 , qbase + 0xd ))
#define REG1 ( outb( inb( qbase + 0xd ) | 0x80 , qbase + 0xd ), outb( 0xb4 | QL_INT_ACTIVE_HIGH , qbase + 0xd ))

/* following is watchdog timeout */
#define WATCHDOG 5000000

/*----------------------------------------------------------------*/
/* the following will set the monitor border color (useful to find
   where something crashed or gets stuck at and as a simple profiler) */

#if 0
#define rtrc(i) {inb(0x3da);outb(0x31,0x3c0);outb((i),0x3c0);}
#else
#define rtrc(i) {}
#endif

/*----------------------------------------------------------------*/
/* local functions */
/*----------------------------------------------------------------*/
static void	ql_zap(void);
/* error recovery - reset everything */
void	ql_zap()
{
int	x;
unsigned long	flags;
	save_flags( flags );
	cli();
	x = inb(qbase + 0xd);
	REG0;
	outb(3, qbase + 3);				/* reset SCSI */
	outb(2, qbase + 3);				/* reset chip */
	if (x & 0x80)
		REG1;
	restore_flags( flags );
}

/*----------------------------------------------------------------*/
/* do pseudo-dma */
static int	ql_pdma(int phase, char *request, int reqlen)
{
int	j;
	j = 0;
	if (phase & 1) {	/* in */
#if QL_TURBO_PDMA
		/* empty fifo in large chunks */
		if( reqlen >= 128 && (inb( qbase + 8 ) & 2) ) { /* full */
			insl( qbase + 4, request, 32 );
			reqlen -= 128;
			request += 128;
		}
		while( reqlen >= 84 && !( j & 0xc0 ) ) /* 2/3 */
			if( (j=inb( qbase + 8 )) & 4 ) {
				insl( qbase + 4, request, 21 );
				reqlen -= 84;
				request += 84;
			}
		if( reqlen >= 44 && (inb( qbase + 8 ) & 8) ) {	/* 1/3 */
			insl( qbase + 4, request, 11 );
			reqlen -= 44;
			request += 44;
		}
#endif
		/* until both empty and int (or until reclen is 0) */
		j = 0;
		while( reqlen && !( (j & 0x10) && (j & 0xc0) ) ) {
			/* while bytes to receive and not empty */
			j &= 0xc0;
			while ( reqlen && !( (j=inb(qbase + 8)) & 0x10 ) ) {
				*request++ = inb(qbase + 4);
				reqlen--;
			}
			if( j & 0x10 )
				j = inb(qbase+8);

		}
	}
	else {	/* out */
#if QL_TURBO_PDMA
		if( reqlen >= 128 && inb( qbase + 8 ) & 0x10 ) { /* empty */
			outsl(qbase + 4, request, 32 );
			reqlen -= 128;
			request += 128;
		}
		while( reqlen >= 84 && !( j & 0xc0 ) ) /* 1/3 */
			if( !((j=inb( qbase + 8 )) & 8) ) {
				outsl( qbase + 4, request, 21 );
				reqlen -= 84;
				request += 84;
			}
		if( reqlen >= 40 && !(inb( qbase + 8 ) & 4 ) ) { /* 2/3 */
			outsl( qbase + 4, request, 10 );
			reqlen -= 40;
			request += 40;
		}
#endif
		/* until full and int (or until reclen is 0) */
		j = 0;
		while( reqlen && !( (j & 2) && (j & 0xc0) ) ) {
			/* while bytes to send and not full */
			while ( reqlen && !( (j=inb(qbase + 8)) & 2 ) ) {
				outb(*request++, qbase + 4);
				reqlen--;
			}
			if( j & 2 )
				j = inb(qbase+8);
		}
	}
/* maybe return reqlen */
	return inb( qbase + 8 ) & 0xc0;
}

/*----------------------------------------------------------------*/
/* wait for interrupt flag (polled - not real hardware interrupt) */
static int	ql_wai(void)
{
int	i,k;
	i = jiffies + WATCHDOG;
	while ( i > jiffies && !qabort && !((k = inb(qbase + 4)) & 0xe0));
	if (i <= jiffies)
		return (DID_TIME_OUT);
	if (qabort)
		return (qabort == 1 ? DID_ABORT : DID_RESET);
	if (k & 0x60)
		ql_zap();
	if (k & 0x20)
		return (DID_PARITY);
	if (k & 0x40)
		return (DID_ERROR);
	return 0;
}

/*----------------------------------------------------------------*/
/* initiate scsi command - queueing handler */
static void	ql_icmd(Scsi_Cmnd * cmd)
{
unsigned int	    i;
unsigned long	flags;

	qabort = 0;

	save_flags( flags );
	cli();
	REG0;
/* clearing of interrupts and the fifo is needed */
	inb(qbase + 5); 			/* clear interrupts */
	if (inb(qbase + 5))			/* if still interrupting */
		outb(2, qbase + 3);		/* reset chip */
	else if (inb(qbase + 7) & 0x1f)
		outb(1, qbase + 3);		/* clear fifo */
	while (inb(qbase + 5)); 		/* clear ints */
	REG1;
	outb(1, qbase + 8);			/* set for PIO pseudo DMA */
	outb(0, qbase + 0xb);			/* disable ints */
	inb(qbase + 8); 			/* clear int bits */
	REG0;
	outb(0x40, qbase + 0xb);		/* enable features */

/* configurables */
#if FASTSCSI
#if FASTCLK
	outb(0x18, qbase + 0xc);
#else
	outb(0x10, qbase + 0xc);
#endif
#else
#if FASTCLK
	outb(8, qbase + 0xc);
#endif
#endif

#if SLOWCABLE
	outb(0xd0 | qinitid, qbase + 8);	/* (initiator) bus id */
#else
	outb(0x50 | qinitid, qbase + 8);	/* (initiator) bus id */
#endif
	outb( SYNCOFFST , qbase + 7 );
	outb( SYNCXFRPD , qbase + 6 );
/**/
	outb(0x99, qbase + 5);	/* timer */
	outb(cmd->target, qbase + 4);

	for (i = 0; i < cmd->cmd_len; i++)
		outb(cmd->cmnd[i], qbase + 2);
	qlcmd = cmd;
	outb(0x41, qbase + 3);	/* select and send command */
	restore_flags( flags );
}
/*----------------------------------------------------------------*/
/* process scsi command - usually after interrupt */
static unsigned int	ql_pcmd(Scsi_Cmnd * cmd)
{
unsigned int	i, j, k;
unsigned int	result; 		/* ultimate return result */
unsigned int	status; 		/* scsi returned status */
unsigned int	message;		/* scsi returned message */
unsigned int	phase;			/* recorded scsi phase */
unsigned int	reqlen; 		/* total length of transfer */
struct scatterlist	*sglist;	/* scatter-gather list pointer */
unsigned int	sgcount;		/* sg counter */

	j = inb(qbase + 6);
	i = inb(qbase + 5);
	if (i == 0x20) {
		return (DID_NO_CONNECT << 16);
	}
	i |= inb(qbase + 5);	/* the 0x10 bit can be set after the 0x08 */
	if (i != 0x18) {
		printk("Ql:Bad Interrupt status:%02x\n", i);
		ql_zap();
		return (DID_BAD_INTR << 16);
	}
	j &= 7; /* j = inb( qbase + 7 ) >> 5; */
/* correct status is supposed to be step 4 */
/* it sometimes returns step 3 but with 0 bytes left to send */
/* We can try stuffing the FIFO with the max each time, but we will get a
   sequence of 3 if any bytes are left (but we do flush the FIFO anyway */
	if(j != 3 && j != 4) {
		printk("Ql:Bad sequence for command %d, int %02X, cmdleft = %d\n", j, i, inb( qbase+7 ) & 0x1f );
		ql_zap();
		return (DID_ERROR << 16);
	}
	result = DID_OK;
	if (inb(qbase + 7) & 0x1f)	/* if some bytes in fifo */
		outb(1, qbase + 3);		/* clear fifo */
/* note that request_bufflen is the total xfer size when sg is used */
	reqlen = cmd->request_bufflen;
/* note that it won't work if transfers > 16M are requested */
	if (reqlen && !((phase = inb(qbase + 4)) & 6)) {	/* data phase */
rtrc(1)
		outb(reqlen, qbase);			/* low-mid xfer cnt */
		outb(reqlen >> 8, qbase+1);			/* low-mid xfer cnt */
		outb(reqlen >> 16, qbase + 0xe);	/* high xfer cnt */
		outb(0x90, qbase + 3);			/* command do xfer */
/* PIO pseudo DMA to buffer or sglist */
		REG1;
		if (!cmd->use_sg)
			ql_pdma(phase, cmd->request_buffer, cmd->request_bufflen);
		else {
			sgcount = cmd->use_sg;
			sglist = cmd->request_buffer;
			while (sgcount--) {
				if (qabort) {
					REG0;
					return ((qabort == 1 ? DID_ABORT : DID_RESET) << 16);
				}
				if (ql_pdma(phase, sglist->address, sglist->length))
					break;
				sglist++;
			}
		}
		REG0;
rtrc(2)
/* wait for irq (split into second state of irq handler if this can take time) */
		if ((k = ql_wai()))
			return (k << 16);
		k = inb(qbase + 5);	/* should be 0x10, bus service */
	}
/*** Enter Status (and Message In) Phase ***/
	k = jiffies + WATCHDOG;
rtrc(4)
	while ( k > jiffies && !qabort && !(inb(qbase + 4) & 6));	/* wait for status phase */
	if ( k <= jiffies ) {
		ql_zap();
		return (DID_TIME_OUT << 16);
	}
	while (inb(qbase + 5)); 				/* clear pending ints */
	if (qabort)
		return ((qabort == 1 ? DID_ABORT : DID_RESET) << 16);
	outb(0x11, qbase + 3);					/* get status and message */
	if ((k = ql_wai()))
		return (k << 16);
	i = inb(qbase + 5);					/* get chip irq stat */
	j = inb(qbase + 7) & 0x1f;				/* and bytes rec'd */
	status = inb(qbase + 2);
	message = inb(qbase + 2);
/* should get function complete int if Status and message, else bus serv if only status */
	if (!((i == 8 && j == 2) || (i == 0x10 && j == 1))) {
		printk("Ql:Error during status phase, int=%02X, %d bytes recd\n", i, j);
		result = DID_ERROR;
	}
	outb(0x12, qbase + 3);	/* done, disconnect */
rtrc(3)
	if ((k = ql_wai()))
		return (k << 16);
/* should get bus service interrupt and disconnect interrupt */
	i = inb(qbase + 5);	/* should be bus service */
	while (!qabort && ((i & 0x20) != 0x20))
		i |= inb(qbase + 5);
rtrc(0)
	if (qabort)
		return ((qabort == 1 ? DID_ABORT : DID_RESET) << 16);
	return (result << 16) | (message << 8) | (status & STATUS_MASK);
}

#if QL_USE_IRQ
/*----------------------------------------------------------------*/
/* interrupt handler */
static void		    ql_ihandl(int irq, struct pt_regs * regs)
{
Scsi_Cmnd	   *icmd;
	REG0;
	if (!(inb(qbase + 4) & 0x80))	/* false alarm? */
		return;
	if (qlcmd == NULL) {		/* no command to process? */
		while (inb(qbase + 5)); /* maybe also ql_zap() */
		return;
	}
	icmd = qlcmd;
	icmd->result = ql_pcmd(icmd);
	qlcmd = NULL;
/* if result is CHECK CONDITION done calls qcommand to request sense */
	(icmd->scsi_done) (icmd);
}
#endif

/*----------------------------------------------------------------*/
/* global functions */
/*----------------------------------------------------------------*/
/* non queued command */
#if QL_USE_IRQ
static void	qlidone(Scsi_Cmnd * cmd) {};		/* null function */
#endif

/* command process */
int	qlogic_command(Scsi_Cmnd * cmd)
{
int	k;
#if QL_USE_IRQ
	if (qlirq >= 0) {
		qlogic_queuecommand(cmd, qlidone);
		while (qlcmd != NULL);
		return cmd->result;
	}
#endif
/* non-irq version */
	if (cmd->target == qinitid)
		return (DID_BAD_TARGET << 16);
	ql_icmd(cmd);
	if ((k = ql_wai()))
		return (k << 16);
	return ql_pcmd(cmd);

}

#if QL_USE_IRQ
/*----------------------------------------------------------------*/
/* queued command */
int	qlogic_queuecommand(Scsi_Cmnd * cmd, void (*done) (Scsi_Cmnd *))
{
	if(cmd->target == qinitid) {
		cmd->result = DID_BAD_TARGET << 16;
		done(cmd);
		return 0;
	}

	cmd->scsi_done = done;
/* wait for the last command's interrupt to finish */
	while (qlcmd != NULL);
	ql_icmd(cmd);
	return 0;
}
#else
int	qlogic_queuecommand(Scsi_Cmnd * cmd, void (*done) (Scsi_Cmnd *))
{
	return 1;
}
#endif

/*----------------------------------------------------------------*/
/* look for qlogic card and init if found */
int	qlogic_detect(Scsi_Host_Template * host)
{
int	i, j;			/* these are only used by IRQ detect */
int	qltyp;			/* type of chip */
struct	Scsi_Host	*hreg;	/* registered host structure */
unsigned long	flags;

/* Qlogic Cards only exist at 0x230 or 0x330 (the chip itself decodes the
   address - I check 230 first since MIDI cards are typically at 330
   Note that this will not work for 2 Qlogic cards in 1 system.  The
   easiest way to do that is to create 2 versions of this file, one for
   230 and one for 330.

   Alternately, the Scsi_Host structure now stores the i/o port and can
   be used to set the port (go through and replace qbase with
   (struct Scsi_Cmnd *) cmd->host->io_port, or for efficiency, set a local
   copy of qbase.  There will also need to be something similar within the
   IRQ handlers to sort out which board it came from and thus which port.
*/

	for (qbase = 0x230; qbase < 0x430; qbase += 0x100) {
#ifndef PCMCIA
		if( check_region( qbase , 0x10 ) )
			continue;
#endif			
		REG1;
		if ( ( (inb(qbase + 0xe) ^ inb(qbase + 0xe)) == 7 )
		  && ( (inb(qbase + 0xe) ^ inb(qbase + 0xe)) == 7 ) )
			break;
	}
	if (qbase == 0x430)
		return 0;

	qltyp = inb(qbase + 0xe) & 0xf8;
	qinitid = host->this_id;
	if (qinitid < 0)
		qinitid = 7;			/* if no ID, use 7 */
	outb(1, qbase + 8);			/* set for PIO pseudo DMA */
	REG0;
	outb(0xd0 | qinitid, qbase + 8);	/* (ini) bus id, disable scsi rst */
	outb(0x99, qbase + 5);			/* select timer */
	qlirq = -1;
#if QL_RESET_AT_START
	outb( 3 , qbase + 3 );
	REG1;
	while( inb( qbase + 0xf ) & 4 );
	REG0;
#endif
#if QL_USE_IRQ
/* IRQ probe - toggle pin and check request pending */
	save_flags( flags );
	cli();
	i = 0xffff;
	j = 3;
	outb(0x90, qbase + 3);	/* illegal command - cause interrupt */
	REG1;
	outb(10, 0x20); /* access pending interrupt map */
	outb(10, 0xa0);
	while (j--) {
		outb(0xb0 | QL_INT_ACTIVE_HIGH , qbase + 0xd);		/* int pin off */
		i &= ~(inb(0x20) | (inb(0xa0) << 8));	/* find IRQ off */
		outb(0xb4 | QL_INT_ACTIVE_HIGH , qbase + 0xd);		/* int pin on */
		i &= inb(0x20) | (inb(0xa0) << 8);	/* find IRQ on */
	}
	REG0;
	while (inb(qbase + 5)); 			/* purge int */
	while (i)					/* find on bit */
		i >>= 1, qlirq++;	/* should check for exactly 1 on */
	if (qlirq >= 0 && !request_irq(qlirq, ql_ihandl, SA_INTERRUPT, "qlogic"))
		host->can_queue = 1;
	restore_flags( flags );
#endif
#ifndef PCMCIA
	request_region( qbase , 0x10 ,"qlogic");
#endif
	hreg = scsi_register( host , 0 );	/* no host data */
	hreg->io_port = qbase;
	hreg->n_io_port = 16;
	if( qlirq != -1 )
		hreg->irq = qlirq;

	sprintf(qinfo, "Qlogic Driver version 0.38b, chip %02X at %03X, IRQ %d, Opts:%d%d",
	    qltyp, qbase, qlirq, QL_INT_ACTIVE_HIGH, QL_TURBO_PDMA );
	host->name = qinfo;

	return 1;
}

/*----------------------------------------------------------------*/
/* return bios parameters */
int	qlogic_biosparam(Disk * disk, int dev, int ip[])
{
/* This should mimic the DOS Qlogic driver's behavior exactly */
	ip[0] = 0x40;
	ip[1] = 0x20;
	ip[2] = disk->capacity / (ip[0] * ip[1]);
	if (ip[2] > 1024) {
		ip[0] = 0xff;
		ip[1] = 0x3f;
		ip[2] = disk->capacity / (ip[0] * ip[1]);
		if (ip[2] > 1023)
			ip[2] = 1023;
	}
	return 0;
}

/*----------------------------------------------------------------*/
/* abort command in progress */
int	qlogic_abort(Scsi_Cmnd * cmd)
{
	qabort = 1;
	ql_zap();
	return 0;
}

/*----------------------------------------------------------------*/
/* reset SCSI bus */
int	qlogic_reset(Scsi_Cmnd * cmd)
{
	qabort = 2;
	ql_zap();
	return 1;
}

/*----------------------------------------------------------------*/
/* return info string */
const char	*qlogic_info(struct Scsi_Host * host)
{
	return qinfo;
}

#ifdef MODULE
/* Eventually this will go into an include file, but this will be later */
Scsi_Host_Template driver_template = QLOGIC;

#include "scsi_module.c"
#endif
