/*
 * drivers/sbus/audio/dbri.c
 *
 * Copyright (C) 1997 Rudolf Koenig (rfkoenig@immd4.informatik.uni-erlangen.de)
 * The SparcLinux interface was adopted from the CS4231 driver.
 *
 * This is the lowlevel driver for the DBRI & MMCODEC duo used for ISDN & AUDIO
 * on Sun SPARCstation 10, 20, LX and Voyager models.
 * NOTE: This driver only supports audio for now, there is NO SUPPORT for ISDN.
 *
 * - DBRI: AT&T T5900FX Dual Basic Rates ISDN Interface. It is a 32 channel
 *   data time multiplexer with ISDN support (aka T7259)
 *   Interfaces: SBus,ISDN NT & TE, CHI, 4 bits parallel.
 *   CHI: (spelled ki) Concentration Highway Interface (AT&T or Intel bus ?).
 *   Documentation:
 *   - "STP 4000SBus Dual Basic Rate ISDN (DBRI) Tranceiver" from
 *     Sparc Technology Business (courtesy of Sun Support)
 *   - Data sheet of the T7903, a newer but very similar ISA bus equivalent
 *     available from the Lucent (formarly AT&T microelectronics) home
 *     page.
 * - MMCODEC: Crystal Semiconductor CS4215 16 bit Multimedia Audio Codec
 *   Interfaces: CHI, Audio In & Out, 2 bits parallel
 *   Documentation: from the Crystal Semiconductor home page.
 *
 * The DBRI is a 32 pipe machine, each pipe can transfer some bits between
 * memory and a serial device (long pipes, nr 0-15) or between two serial
 * devices (short pipes, nr 16-31), or simply send a fixed data to a serial
 * device (short pipes).
 * A timeslot defines the bit-offset and nr of bits read from a serial device.
 * The timeslots are linked to 6 circular lists, one for each direction for
 * each serial device (NT,TE,CHI). A timeslot is associated to 1 or 2 pipes
 * (the second one is a monitor/tee pipe, valid only for serial input).
 *
 * The mmcodec is connected via the CHI bus and needs the data & some
 * parameters (volume, balance, output selection) timemultiplexed in 8 byte
 * chunks. It also has a control mode, which serves for audio format setting.
 *
 * Looking at the CS4215 data sheet it is easy to set up 2 or 4 codecs on
 * the same CHI bus, so I thought perhaps it is possible to use the onboard
 * & the speakerbox codec simultanously, giving 2 (not very independent :-)
 * audio devices. But the SUN HW group decided against it, at least on my
 * LX the speakerbox connector has at least 1 pin missing and 1 wrongly
 * connected.
 */



#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/sbus.h>

#include <asm/audioio.h>
#include "dbri.h"



#define DBRI_DEBUG

#ifdef DBRI_DEBUG

#define dprintk(a, x) if(dbri_debug & a) printk x
#define D_GEN	(1<<0)
#define D_INT	(1<<1)
#define D_CMD	(1<<2)
#define D_MM	(1<<3)
#define D_USR	(1<<4)

static int dbri_debug = D_GEN|D_INT|D_CMD|D_MM|D_USR;
static char *cmds[] = { 
  "WAIT", "PAUSE", "JUMP", "IIQ", "REX", "SDP", "CDP", "DTS",
  "SSP", "CHI", "NT", "TE", "CDEC", "TEST", "CDM", "RESRV"
};

/* Bit hunting */
#define dumpcmd {int i; for(i=0; i<n; i++) printk("DBRI: %x\n", dbri->cmd[i]); }

#define DBRI_CMD(cmd, intr, value) ((cmd << 28) | (1 << 27) | value)

#else

#define dprintk(a, x)
#define dumpcmd
#define DBRI_CMD(cmd, intr, value) ((cmd << 28) | (intr << 27) | value)

#endif	/* DBRI_DEBUG */



#define MAX_DRIVERS	2	/* Increase this if need more than 2 DBRI's */

#define WAIT_INTR1	0xbe
#define WAIT_INTR2	0xbf

static struct sparcaudio_driver drivers[MAX_DRIVERS];
static char drv_name[] = "DBRI/audio";
static int num_drivers;
static int dbri_cmdlocked = 0;

static void * output_callback_arg;

/*
 * Make sure, that we can send a command to the dbri
 */
static int dbri_cmdlock(struct dbri *dbri)
{
	unsigned long flags;
	int was_sleeping = 0;

	save_flags(flags);
	cli();

	if(dbri_cmdlocked) {
		interruptible_sleep_on(&dbri->wait);
		was_sleeping = 1;
	}
	if(dbri_cmdlocked)
		return -EINTR;
	dbri_cmdlocked = 1;

	restore_flags(flags);

	if(was_sleeping)
		dprintk(D_INT, ("DBRI: Just woke up\n"));
	return 0;
}

static void dbri_reset(struct sparcaudio_driver *drv)
{
	struct dbri *dbri = (struct dbri *)drv->private;
	int i;

	dprintk(D_GEN, ("DBRI: reset 0:%x 2:%x 8:%x 9:%x\n",
		dbri->regs->reg0, dbri->regs->reg2,
		dbri->regs->reg8, dbri->regs->reg9)); 

	dbri->regs->reg0 = D_R; /* Soft Reset */
	for(i = 0; (dbri->regs->reg0 & D_R) && i < 10; i++)
		udelay(10);
}

static void dbri_detach(struct sparcaudio_driver *drv)
{
        struct dbri *info = (struct dbri *)drv->private;

	dbri_reset(drv);
        unregister_sparcaudio_driver(drv);
        free_irq(info->irq, drv);
        sparc_free_io(info->regs, info->regs_size);
        kfree(drv->private);
}


static void dbri_initialize(struct sparcaudio_driver *drv)
{
	struct dbri *dbri = (struct dbri *)drv->private;
	int n;

        dbri_reset(drv);
	dbri->wait = NULL;

	dprintk(D_GEN, ("DBRI: init: cmd: %x, int: %x\n",
			(int)dbri->cmd, (int)dbri->intr));

	/*
	 * Initialize the interrupt ringbuffer.
	 */
	for(n = 0; n < DBRI_NO_INTS-1; n++)
		dbri->intr[n * DBRI_INT_BLK] = 
					(int)(&dbri->intr[(n+1)*DBRI_INT_BLK]);
	dbri->intr[n * DBRI_INT_BLK] = (int)(dbri->intr);
	dbri->dbri_irqp = 1;

#ifdef USE_SBUS_BURSTS
        /* Enable 4-word, 8-word, and 16-word SBus Bursts */
	dbri->regs->reg0 |= (D_G|D_S|D_E);
#else
        /* Disable 4-word, 8-word, and 16-word SBus Bursts */
        dbri->regs->reg0 &= ~(D_G|D_S|D_E);
#endif

	/*
	 * Set up the interrupt queue
	 */
	(void)dbri_cmdlock(dbri);

	n = 0;
	dbri->cmd[n++] = DBRI_CMD(D_IIQ, 0, 0);
	dbri->cmd[n++] = (int)(dbri->intr);
	dbri->cmd[n++] = DBRI_CMD(D_WAIT, 1, WAIT_INTR1);
	dbri->regs->reg8 = (int)dbri->cmd;
}



/*
 * Short data pipes transmit LSB first. The CS4215 receives MSB first. Grrr.
 * So we have to reverse the bits. Note: only 1, 2 or 4 bytes are supported.
 */
static __u32 reverse_bytes(__u32 b, int len)
{
	switch(len) {
		case 4: b = ((b & 0xffff0000) >> 16) | ((b & 0x0000ffff) << 16);
		case 2: b = ((b & 0xff00ff00) >>  8) | ((b & 0x00ff00ff) <<  8);
		case 1: b = ((b & 0xf0f0f0f0) >>  4) | ((b & 0x0f0f0f0f) <<  4);
			b = ((b & 0xcccccccc) >>  2) | ((b & 0x33333333) <<  2);
			b = ((b & 0xaaaaaaaa) >>  1) | ((b & 0x55555555) <<  1);
	}
	return b;
}



static void mmcodec_default(struct cs4215 *mm)
{
	/*
	 * No action, memory resetting only.
	 *
	 * Data Time Slot 5-8
	 * Speaker,Line and Headphone enable. Gain set to the half.
	 * Input is mike.
	 */
	mm->data[0] = CS4215_LO(0x20) | CS4215_HE|CS4215_LE;
	mm->data[1] = CS4215_RO(0x20) | CS4215_SE;
	mm->data[2] = CS4215_LG( 0x8) | CS4215_IS | CS4215_PIO0 | CS4215_PIO1;
	mm->data[3] = CS4215_RG( 0x8) | CS4215_MA(0xf);

	/*
	 * Control Time Slot 1-4
	 * 0: Default I/O voltage scale
	 * 1: 8 bit ulaw, 8kHz, mono, high pass filter disabled
	 * 2: Serial enable, CHI master, 1 CHI device (64bit), clock 1
	 * 3: Tests disabled
	 */
	mm->ctrl[0] = CS4215_RSRVD_1;
	mm->ctrl[1] = CS4215_DFR_ULAW | CS4215_FREQ[0].csval;
	mm->ctrl[2] = CS4215_XCLK |
			CS4215_BSEL_128 | CS4215_FREQ[0].xtal;
	mm->ctrl[3] = 0;
}

static void mmcodec_init_data(struct dbri *dbri)
{
	int val, n = 0;

	dbri_cmdlock(dbri);

	/*
	 * Data mode:
	 * Pipe  4: Send timeslots 1-4 (audio data)
	 * Pipe 17: Send timeslots 5-8 (part of ctrl data)
	 * Pipe  6: Receive timeslots 1-4 (audio data)
	 * Pipe 20: Receive timeslots 6-7. We can only receive 20 bits via
	 *          interrupt, and the rest of the data (slot 5 and 8) is
	 *	    not relevant for us (only for doublechecking).
         *
         * Just like in control mode, the time slots are all offset by eight
         * bits.  The CS4215, it seems, observes TSIN (the delayed signal)
         * even if it's the CHI master.  Don't ask me...
	 */
	

	/* Pipe 4: SDP */
	val = D_SDP_MEM|D_SDP_TO_SER|D_SDP_C|D_SDP_P|D_SDP_MSB|D_PIPE(D_P_4);
	dbri->cmd[n++] = DBRI_CMD(D_SDP, 0, val);
	dbri->cmd[n++] = 0;


	/* Pipe 17: SDP */
	val = D_SDP_FIXED|D_SDP_TO_SER|D_SDP_C|D_PIPE(D_P_17);
	dbri->cmd[n++] = DBRI_CMD(D_SDP, 0, val);
	dbri->cmd[n++] = 0;				/* Fixed data */

        /* Pipe 17: SSP */
	dbri->cmd[n++] = DBRI_CMD(D_SSP, 0, D_PIPE(D_P_17));
	dbri->cmd[n++] = reverse_bytes(*(int *)dbri->mm.data, 4);


	/* Pipe 6: SDP */
	val=D_SDP_MEM|D_SDP_FROM_SER|D_SDP_C|D_SDP_P|D_SDP_MSB|D_PIPE(D_P_6);
	dbri->cmd[n++] = DBRI_CMD(D_SDP, 0, val);
	dbri->cmd[n++] = 0;


	/* Pipe 20: SDP */
	val = D_SDP_FIXED|D_SDP_FROM_SER|D_SDP_CHANGE|D_SDP_C|D_PIPE(D_P_20);
	dbri->cmd[n++] = DBRI_CMD(D_SDP, 0, val);
	dbri->cmd[n++] = 0;				/* Fixed data */



	dbri->cmd[n++] = DBRI_CMD(D_PAUSE, 0, 0);


        /* Pipe 4: DTS */
	val = D_DTS_VO | D_DTS_INS | D_DTS_PRVOUT(D_P_16) | D_PIPE(D_P_4);
	dbri->cmd[n++] = DBRI_CMD(D_DTS, 0, val);
	dbri->cmd[n++] = 0;
#if 0
        /* Full blown, four time slots, 16 bit stereo */
	dbri->cmd[n++] = D_TS_LEN(32) | D_TS_CYCLE(8)| D_TS_NEXT(D_P_16);
#else
        /* Single time slot, 8 bit mono */
	dbri->cmd[n++] = D_TS_LEN(8) | D_TS_CYCLE(8)| D_TS_NEXT(D_P_16);
#endif

        /* Pipe 17: DTS */
	val = D_DTS_VO | D_DTS_INS | D_DTS_PRVOUT(D_P_4) | D_PIPE(D_P_17);
	dbri->cmd[n++] = DBRI_CMD(D_DTS, 0, val);
	dbri->cmd[n++] = 0;
	dbri->cmd[n++] = D_TS_LEN(32) | D_TS_CYCLE(40) | D_TS_NEXT(D_P_16);

        /* Pipe 6: DTS */
	val = D_DTS_VI | D_DTS_INS | D_DTS_PRVIN(D_P_16) | D_PIPE(D_P_6);
	dbri->cmd[n++] = DBRI_CMD(D_DTS, 0, val);
#if 0
        /* Full blown, four time slots, 16 bit stereo */
	dbri->cmd[n++] = D_TS_LEN(32) | D_TS_CYCLE(8)| D_TS_NEXT(D_P_16);
#else
        /* Single time slot, 8 bit mono */
	dbri->cmd[n++] = D_TS_LEN(8) | D_TS_CYCLE(8)| D_TS_NEXT(D_P_16);
#endif
	dbri->cmd[n++] = 0;

        /* Pipe 20: DTS */
	val = D_DTS_VI | D_DTS_INS | D_DTS_PRVIN(D_P_6) | D_PIPE(D_P_20);
	dbri->cmd[n++] = DBRI_CMD(D_DTS, 0, val);
	dbri->cmd[n++] = D_TS_LEN(16) | D_TS_CYCLE(48) | D_TS_NEXT(D_P_16); 
	dbri->cmd[n++] = 0;

        /* CHI: Slave mode; enable interrupts */
	dbri->cmd[n++] = DBRI_CMD(D_CHI, 0, D_CHI_CHICM(0) | D_CHI_IR | D_CHI_EN);

	dbri->cmd[n++] = DBRI_CMD(D_WAIT, 0, WAIT_INTR1);

	dbri->regs->reg8 = (int)dbri->cmd;
}


/*
 * Send the control information (i.e. audio format)
 */
static void mmcodec_setctrl(struct dbri *dbri)
{
	int n = 0, val;

	/*
	 * Enable Control mode: Set DBRI's PIO3 (4215's D/~C) to 0, then wait
	 * 12 cycles <= 12/(5512.5*64) sec = 34.01 usec
	 */
	val = D_ENPIO | D_PIO1 | (dbri->mm.onboard ? D_PIO0 : D_PIO2);
	dbri->regs->reg2 = val;
	udelay(34);

        /* In Control mode, the CS4215 is a slave device, so the DBRI must
         * operate as CHI master, supplying clocking and frame synchronization.
         *
         * In Data mode, however, the CS4215 must be CHI master to insure
         * that its data stream is synchronous with its codec.
         *
         * The upshot of all this?  We start by putting the DBRI into master
         * mode, program the CS4215 in Control mode, then switch the CS4215
         * into Data mode and put the DBRI into slave mode.  Various timing
         * requirements must be observed along the way.
         *
         * Oh, and one more thing - when the DBRI is master (and only when
         * the DBRI is master), the addressing of the CS4215's time slots
         * is offset by eight bits, so we add eight to all the "cycle"
         * values in the Define Time Slot (DTS) commands.  This is done in
         * hardware by a TI 248 that delays the DBRI->4215 frame sync signal
         * by eight clock cycles.  Anybody know why?
         */

	dbri_cmdlock(dbri);

	/*
	 * Control mode:
	 * Pipe 17: Send timeslots 1-4 (slots 5-8 are readonly)
	 * Pipe 18: Receive timeslot 1 (clb).
	 * Pipe 19: Receive timeslot 7 (version). 
	 */

	/* Set CHI Anchor: Pipe 16. This should take care of the rest. */
	val = D_DTS_VI | D_DTS_VO | D_DTS_INS |
	      D_DTS_PRVIN(D_P_16) | D_DTS_PRVOUT(D_P_16) | D_PIPE(D_P_16);
	dbri->cmd[n++] = DBRI_CMD(D_DTS, 0, val);
	dbri->cmd[n++] = D_TS_ANCHOR | D_TS_NEXT(D_P_16);
	dbri->cmd[n++] = D_TS_ANCHOR | D_TS_NEXT(D_P_16);


	/* Setup the pipes first */
	val = D_SDP_FIXED|D_SDP_TO_SER|D_SDP_P|D_SDP_C|D_PIPE(D_P_17);
	dbri->cmd[n++] = DBRI_CMD(D_SDP, 0, val);
	dbri->cmd[n++] = 0;

	val = D_SDP_FIXED|D_SDP_CHANGE|D_SDP_C|D_PIPE(D_P_18);
	dbri->cmd[n++] = DBRI_CMD(D_SDP, 0, val);
	dbri->cmd[n++] = 0;

	val = D_SDP_FIXED|D_SDP_CHANGE|D_SDP_C|D_PIPE(D_P_19);
	dbri->cmd[n++] = DBRI_CMD(D_SDP, 0, val);
	dbri->cmd[n++] = 0;

	/* Fill in the data to send */
	dbri->mm.ctrl[0] &= ~CS4215_CLB;
	dbri->cmd[n++] = DBRI_CMD(D_SSP, 0, D_PIPE(D_P_17));
	dbri->cmd[n++] = reverse_bytes(*(int *)dbri->mm.ctrl, 4);

	dbri->cmd[n++] = DBRI_CMD(D_PAUSE, 0, 0);



	/* Link the timeslots */

        /* Pipe 17 - CS4215 Status, Data Format, Serial Control, Test - output
         *           time slots 1, 2, 3 and 4 - 32 bits
         */

	val = D_DTS_VO | D_DTS_INS | D_DTS_PRVOUT(D_P_16) | D_PIPE(D_P_17);
	dbri->cmd[n++] = DBRI_CMD(D_DTS, 0, val);
	dbri->cmd[n++] = 0;
	dbri->cmd[n++] = D_TS_LEN(32) | D_TS_CYCLE(8) | D_TS_NEXT(D_P_16);

        /* Pipe 18 - CS4215 Status and Data Format - input
         *           time slots 1 & 2 - 16 bits
         */

	val = D_DTS_VI | D_DTS_INS | D_DTS_PRVIN(D_P_16) | D_PIPE(D_P_18);
	dbri->cmd[n++] = DBRI_CMD(D_DTS, 0, val);
	dbri->cmd[n++] = D_TS_LEN(16) | D_TS_CYCLE(8) | D_TS_NEXT(D_P_16);
	dbri->cmd[n++] = 0;

        /* Pipe 19 - CS4215 Revision - time slot 7, eight bits - input
         */

	val = D_DTS_VI | D_DTS_INS | D_DTS_PRVIN(D_P_18) | D_PIPE(D_P_19);
	dbri->cmd[n++] = DBRI_CMD(D_DTS, 0, val);
	dbri->cmd[n++] = D_TS_LEN(8) | D_TS_CYCLE(56) | D_TS_NEXT(D_P_16);
	dbri->cmd[n++] = 0;


	/* Setup DBRI for CHI Master
         *
         * BPF   =  128 (128 bits per 8 kHz frame = 1.024 MHz clock rate)
         * CHICM =  12 (12.288 MHz / 24 = 1.024 MHz clock rate)
         * FD    =  1 - drive CHIFS on rising edge of CHICK
         *
         * RCE   =  0 - receive on falling edge of CHICK
         * XCE   =  1 - transmit on rising edge of CHICK
         */
	dbri->cmd[n++] = DBRI_CMD(D_CHI, 0, D_CHI_CHICM(12) | D_CHI_FD |
					D_CHI_IR | D_CHI_EN | D_CHI_BPF(128));
	dbri->cmd[n++] = DBRI_CMD(D_CDM, 0, D_CDM_XCE|D_CDM_XEN|D_CDM_REN);
	dbri->cmd[n++] = DBRI_CMD(D_PAUSE, 0, 0);

	dbri->cmd[n++] = DBRI_CMD(D_WAIT, 1, WAIT_INTR1);
	dbri->regs->reg8 = (int)dbri->cmd;


	/* Wait for the data from the CS4215 */
        interruptible_sleep_on(&dbri->int_wait);

        /* Switch CS4215 to data mode - data sheet says
         * "Set CLB=1 and send two more frames of valid control info"
         */
	dbri_cmdlock(dbri);

        n = 0;
	dbri->mm.ctrl[0] |= CS4215_CLB;
	dbri->cmd[n++] = DBRI_CMD(D_SSP, 0, D_PIPE(D_P_17));
	dbri->cmd[n++] = reverse_bytes(*(int *)dbri->mm.ctrl, 4);

	dbri->cmd[n++] = DBRI_CMD(D_WAIT, 1, WAIT_INTR1);
	dbri->regs->reg8 = (int)dbri->cmd;

        dbri_cmdlock(dbri);

        /* Two frames of control info @ 8kHz frame rate = 250 us delay */
        udelay(250);

	n = 0;

	/* Now switch back to data mode */
	/* Reset CHI Anchor: Stop Send/Receive */
	val = D_DTS_VI | D_DTS_VO | D_DTS_INS |
	      D_DTS_PRVIN(D_P_16) | D_DTS_PRVOUT(D_P_16) | D_PIPE(D_P_16);
	dbri->cmd[n++] = DBRI_CMD(D_DTS, 0, val);
	dbri->cmd[n++] = D_TS_ANCHOR | D_TS_NEXT(D_P_16);
	dbri->cmd[n++] = D_TS_ANCHOR | D_TS_NEXT(D_P_16);


	/* Setup DBRI for CHI Slave */
	dbri->cmd[n++] = DBRI_CMD(D_CHI, 0, D_CHI_CHICM(0));
	/* dbri->cmd[n++] = DBRI_CMD(D_CHI, 0, D_CHI_CHICM(0) | D_CHI_IR | D_CHI_EN); */
	dbri->cmd[n++] = DBRI_CMD(D_PAUSE, 0, 0x16);


	dbri->cmd[n++] = DBRI_CMD(D_WAIT, 1, WAIT_INTR1);
	dbri->regs->reg8 = (int)dbri->cmd;

        /* Wait for command to complete */
        dbri_cmdlock(dbri);
        n = 0;
	dbri->cmd[n++] = DBRI_CMD(D_WAIT, 1, WAIT_INTR1);
	dbri->regs->reg8 = (int)dbri->cmd;


        /* Switch CS4215 to data mode - set PIO3 to 1 */
	dbri->regs->reg2 = D_ENPIO | D_PIO1 | D_PIO3 |
				(dbri->mm.onboard ? D_PIO0 : D_PIO2);
}

static int mmcodec_init(struct sparcaudio_driver *drv)
{
	struct dbri *dbri = (struct dbri *)drv->private;
	int reg2 = dbri->regs->reg2;


	/* Look for the cs4215 chips */
	if(reg2 & D_PIO2) {
		dprintk(D_MM, ("DBRI: Onboard CS4215 detected\n"));
		dbri->mm.onboard = 1;
	}
	if(reg2 & D_PIO0) {
		dprintk(D_MM, ("DBRI: Speakerbox detected\n"));
		dbri->mm.onboard = 0;
	}
	

	/* Using the Speakerbox, if both are attached.  */
	if((reg2 & D_PIO2) && (reg2 & D_PIO0)) {
		printk("DBRI: Using speakerbox / ignoring onboard mmcodec.\n");
		dbri->regs->reg2 = D_ENPIO2;
		dbri->mm.onboard = 0;
	}
	if( !(reg2 & (D_PIO0|D_PIO2)) ) {
		printk("DBRI: no mmcodec found.\n");
		return -EIO;
	}


	/* Now talk to our baby */
	dbri->regs->reg0 |= D_C;	/* Enable CHI */

	mmcodec_default(&dbri->mm);

	dbri->mm.version = 0xff;
	mmcodec_setctrl(dbri);
	if(dbri->mm.version == 0xff) 
		return -EIO;

	mmcodec_init_data(dbri);

	return 0;
}

void dbri_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	struct sparcaudio_driver *drv = (struct sparcaudio_driver *)dev_id;
	struct dbri *dbri = (struct dbri *)drv->private;
	int x, val;
	static int numint = 0;
	
	/*
	 * Read it, so the interrupt goes away.
	 */
	x = dbri->regs->reg1;
#if 0
	if(numint++ > 20) {
	    dbri->regs->reg0 = D_R; /* Soft Reset */
	    numint = 0;
	    printk("Soft reset\n");
	}
#endif

	if ( x & (D_MRR|D_MLE|D_LBG|D_MBE) ) {
		/*
		 * What should I do here ?
		 */
		if(x & D_MRR) printk("DBRI: Multiple Error Ack on SBus\n");
		if(x & D_MLE) printk("DBRI: Multiple Late Error on SBus\n");
		if(x & D_LBG) printk("DBRI: Lost Bus Grant on SBus\n");
		if(x & D_MBE) printk("DBRI: Burst Error on SBus\n");
	}

	if (!(x & D_IR))	/* Not for us */
		return;

	x = dbri->intr[dbri->dbri_irqp];
	while (x != 0) {
		dbri->intr[dbri->dbri_irqp] = 0;

		if(D_INTR_GETCHAN(x) == D_INTR_CMD) {
			dprintk(D_INT,("DBRI: INTR: Command: %-5s  Value:%d\n",
				cmds[D_INTR_GETCMD(x)], D_INTR_GETVAL(x)));
		} else {
			dprintk(D_INT,("DBRI: INTR: Chan:%d Code:%d Val:%#x\n",
				D_INTR_GETCHAN(x), D_INTR_GETCODE(x),
				D_INTR_GETRVAL(x)));
		}

		val = D_INTR_GETVAL(x);

		switch(D_INTR_GETCHAN(x)) {
			case D_INTR_CMD:
				if(D_INTR_GETCMD(x) == D_WAIT)
					if(val == WAIT_INTR1) {
						dbri_cmdlocked = 0;
						wake_up(&dbri->wait);
					}
					if(val == WAIT_INTR2)
						wake_up(&dbri->int_wait);
				break;
			case D_P_4:
				if (D_INTR_GETCODE(x) == D_INTR_XCMP) {
					sparcaudio_output_done(output_callback_arg, 1);
				}
				break;

			case D_P_18:
				if(val != 0) {
					x = reverse_bytes(val,2)&CS4215_12_MASK;
printk("Comparing int: %x with hi(%x)\n", x, *(int *)dbri->mm.ctrl);
					if(x == (*(int *)dbri->mm.ctrl >> 16))
{
printk("Comp ok\n");
						wake_up(&dbri->int_wait);
}
				}
				break;
			case D_P_19:
				if(val != 0) {
					dbri->mm.version = 
						reverse_bytes(val, 1) & 0xf;
				}
				break;
		}

		dbri->dbri_irqp++;
		if (dbri->dbri_irqp == (DBRI_NO_INTS * DBRI_INT_BLK))
			dbri->dbri_irqp = 1;
		else if ((dbri->dbri_irqp & (DBRI_INT_BLK-1)) == 0)
			dbri->dbri_irqp++;
		x = dbri->intr[dbri->dbri_irqp];
	}
}


/*
****************************************************************************
******************** Interface with sparcaudio midlevel ********************
****************************************************************************
*/


static void dummy()
{
}

static int dbri_open(struct inode * inode, struct file * file,
                     struct sparcaudio_driver *drv)
{
	struct dbri *dbri = (struct dbri *)drv->private;

#if 0
	/* Set the default audio parameters. */
	info->rgain = 128;
	info->pgain = 200;
	info->mgain = 0;
#endif

	MOD_INC_USE_COUNT;

	return 0;
}

static void dbri_release(struct inode * inode, struct file * file,
                         struct sparcaudio_driver *drv)
{
	MOD_DEC_USE_COUNT;
}

static void dbri_start_output(struct sparcaudio_driver *drv,
                              __u8 * buffer, unsigned long count)
{
	struct dbri *dbri = (struct dbri *)drv->private;
        int val, n = 0;

        /* XXX - This routine can be called via interrupt.  If DBRI
         * was cmdlocked, that would cause a sleep, which would be
         * scheduling in an interrupt, and that's not allowed
         *
         * Fortunately, there's nothing else talking to our DBRI (yet),
         * so this isn't a problem (yet)
         */

        dbri_cmdlock(dbri);

        dbri->mm.td.flags = DBRI_TD_F | DBRI_TD_B | DBRI_TD_D | DBRI_TD_CNT(count);
        dbri->mm.td.ba = (__u32) buffer;
        dbri->mm.td.nda = 0;
        dbri->mm.td.status = 0;

        /* Pipe 4 is audio transmit */
	val = D_SDP_MEM|D_SDP_TO_SER|D_SDP_P|D_SDP_MSB|D_PIPE(D_P_4);
	dbri->cmd[n++] = DBRI_CMD(D_SDP, 0, val);
	dbri->cmd[n++] = (__u32)&dbri->mm.td;

	dbri->cmd[n++] = DBRI_CMD(D_WAIT, 0, WAIT_INTR1);

	dbri->regs->reg8 = (int)dbri->cmd;

        output_callback_arg = drv;
}

static void dbri_stop_output(struct sparcaudio_driver *drv)
{
	struct dbri *dbri = (struct dbri *)drv->private;
}

static struct sparcaudio_operations dbri_ops = {
	dbri_open,
	dbri_release,
	dummy, /* dbri_ioctl, */
	dbri_start_output,
	dbri_stop_output,
	dummy, /* dbri_start_input, */
        dummy, /* dbri_stop_input, */
	dummy, /* dbri_audio_getdev, */
	dummy, /* dbri_set_output_volume, */
	dummy, /* dbri_get_output_volume, */
	dummy, /* dbri_set_input_volume, */
	dummy, /* dbri_get_input_volume, */
	dummy, /* dbri_set_monitor_volume, */
	dummy, /* dbri_get_monitor_volume, */
	dummy, /* dbri_set_output_balance */
	dummy, /* dbri_get_output_balance, */
	dummy, /* dbri_set_input_balance */
	dummy, /* dbri_get_input_balance, */
	dummy, /* dbri_set_output_channels */
	dummy, /* dbri_get_output_channels, */
	dummy, /* dbri_set_input_channels */
	dummy, /* dbri_get_input_channels, */
	dummy, /* dbri_set_output_precision */
	dummy, /* dbri_get_output_precision, */
	dummy, /* dbri_set_input_precision */
	dummy, /* dbri_get_input_precision, */
	dummy, /* dbri_set_output_port */
	dummy, /* dbri_get_output_port, */
	dummy, /* dbri_set_input_port */
	dummy, /* dbri_get_input_port, */
	dummy, /* dbri_set_output_encoding */
	dummy, /* dbri_get_output_encoding, */
	dummy, /* dbri_set_input_encoding */
	dummy, /* dbri_get_input_encoding, */
	dummy, /* dbri_set_output_rate */
	dummy, /* dbri_get_output_rate, */
	dummy, /* dbri_set_input_rate */
	dummy, /* dbri_get_input_rate, */
	dummy, /* dbri_sunaudio_getdev_sunos, */
	dummy, /* dbri_get_output_ports, */
	dummy, /* dbri_get_input_ports, */
	dummy, /* dbri_set_output_muted */
	dummy, /* dbri_get_output_muted, */
};


static int dbri_attach(struct sparcaudio_driver *drv, 
			 struct linux_sbus_device *sdev)
{
	struct dbri *dbri;
	struct linux_prom_irqs irq;
	int err;

	if (sdev->prom_name[9] < 'e') {
		printk(KERN_ERR "DBRI: unsupported chip version %c found.\n",
			sdev->prom_name[9]);
		return -EIO;
	}

	drv->ops = &dbri_ops;
	drv->private = kmalloc(sizeof(struct dbri), GFP_KERNEL);
	if (!drv->private)
		return -ENOMEM;
	dbri = (struct dbri *)drv->private;

        memset(dbri, 0, sizeof(*dbri));

	dbri->dbri_version = sdev->prom_name[9];

	/* Map the registers into memory. */
	prom_apply_sbus_ranges(sdev->my_bus, &sdev->reg_addrs[0], 
		sdev->num_registers, sdev);
	dbri->regs_size = sdev->reg_addrs[0].reg_size;
	dbri->regs = sparc_alloc_io(sdev->reg_addrs[0].phys_addr, 0,
		sdev->reg_addrs[0].reg_size, 
		drv_name, sdev->reg_addrs[0].which_io, 0);
	if (!dbri->regs) {
		printk(KERN_ERR "DBRI: could not allocate registers\n");
		kfree(drv->private);
		return -EIO;
	}

	prom_getproperty(sdev->prom_node, "intr", (char *)&irq, sizeof(irq));
	dbri->irq = irq.pri;

	err = request_irq(dbri->irq, dbri_intr, SA_SHIRQ, "DBRI/audio", drv);
	if (err) {
		printk(KERN_ERR "DBRI: Can't get irq %d\n", dbri->irq);
		sparc_free_io(dbri->regs, dbri->regs_size);
		kfree(drv->private);
		return err;
	}

	/* Register ourselves with the midlevel audio driver. */
	err = register_sparcaudio_driver(drv);
	if (err) {
		printk(KERN_ERR "DBRI: unable to register audio\n");
		free_irq(dbri->irq, drv);
		sparc_free_io(dbri->regs, dbri->regs_size);
		kfree(drv->private);
		return err;
	}

	dbri_initialize(drv);
	err = mmcodec_init(drv);
	if(err) {
		dbri_detach(drv);
		return err;
	}
	  

	dbri->perchip_info.play.active   = dbri->perchip_info.play.pause = 0;
	dbri->perchip_info.record.active = dbri->perchip_info.record.pause = 0;

	printk(KERN_INFO "audio%d at 0x%lx (irq %d) is DBRI(%c)+CS4215(%d)\n",
	       num_drivers, (unsigned long)dbri->regs,
	       dbri->irq, dbri->dbri_version, dbri->mm.version);
	
	return 0;
}

/* Probe for the dbri chip and then attach the driver. */
#ifdef MODULE
int init_module(void)
#else
__initfunc(int dbri_init(void))
#endif
{
	struct linux_sbus *bus;
	struct linux_sbus_device *sdev;
  
	num_drivers = 0;
  
	/* Probe each SBUS for the DBRI chip(s). */
	for_all_sbusdev(sdev,bus) {
		/*
		 * The version is coded in the last character
		 */
		if (!strncmp(sdev->prom_name, "SUNW,DBRI", 9)) {
      			dprintk(D_GEN, ("DBRI: Found %s in SBUS slot %d\n",
				sdev->prom_name, sdev->slot));
			if (num_drivers >= MAX_DRIVERS) {
				printk("DBRI: Ignoring slot %d\n", sdev->slot);
				continue;
			}
	      
			if (dbri_attach(&drivers[num_drivers], sdev) == 0)
				num_drivers++;
		}
	}
  
	return (num_drivers > 0) ? 0 : -EIO;
}

#ifdef MODULE
void cleanup_module(void)
{
        register int i;

        for (i = 0; i < num_drivers; i++) {
                dbri_detach(&drivers[i]);
                num_drivers--;
        }
}
#endif

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 4
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -4
 * c-argdecl-indent: 4
 * c-label-offset: -4
 * c-continued-statement-offset: 4
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
