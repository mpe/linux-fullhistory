/*
 * drivers/sbus/audio/dbri.c
 *
 * Copyright (C) 1997 Rudolf Koenig (rfkoenig@immd4.informatik.uni-erlangen.de)
 * Copyright (C) 1998, 1999 Brent Baccala (baccala@freesoft.org)
 *
 * This is the lowlevel driver for the DBRI & MMCODEC duo used for ISDN & AUDIO
 * on Sun SPARCstation 10, 20, LX and Voyager models.
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
#include <linux/version.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/sbus.h>
#include <asm/pgtable.h>

#include <asm/audioio.h>
#include "dbri.h"

#if defined(DBRI_ISDN) || defined (LINUX_VERSION_CODE) && LINUX_VERSION_CODE > 0x200ff
#include "../../isdn/hisax/hisax.h"
#include "../../isdn/hisax/isdnl1.h"
#include "../../isdn/hisax/foreign.h"
#endif

#define DBRI_DEBUG

#ifdef DBRI_DEBUG

#define dprintk(a, x) if(dbri_debug & a) printk x
#define D_GEN	(1<<0)
#define D_INT	(1<<1)
#define D_CMD	(1<<2)
#define D_MM	(1<<3)
#define D_USR	(1<<4)

/* static int dbri_debug = D_GEN|D_INT|D_CMD|D_MM|D_USR; */
static int dbri_debug = 0;
MODULE_PARM(dbri_debug, "i");

static char *cmds[] = { 
  "WAIT", "PAUSE", "JUMP", "IIQ", "REX", "SDP", "CDP", "DTS",
  "SSP", "CHI", "NT", "TE", "CDEC", "TEST", "CDM", "RESRV"
};

/* Bit hunting */
#define dumpcmd {int i; for(i=0; i<n; i++) printk("DBRI: %x\n", dbri->dma->cmd[i]); }

#define DBRI_CMD(cmd, intr, value) ((cmd << 28) | (1 << 27) | value)

#else

#define dprintk(a, x)
#define dumpcmd
#define DBRI_CMD(cmd, intr, value) ((cmd << 28) | (intr << 27) | value)

#endif	/* DBRI_DEBUG */



#define MAX_DRIVERS	2	/* Increase this if need more than 2 DBRI's */

static struct sparcaudio_driver drivers[MAX_DRIVERS];
static int num_drivers = 0;


/*
****************************************************************************
************** DBRI initialization and command synchronization *************
****************************************************************************
*/


/*
 * Commands are sent to the DBRI by building a list of them in memory,
 * then writing the address of the first list item to DBRI register 8.
 * The list is terminated with a WAIT command, which can generate a
 * CPU interrupt if required.
 *
 * Since the DBRI can run asynchronously to the CPU, several means of
 * synchronization present themselves.  The original scheme (Rudolf's)
 * was to set a flag when we "cmdlock"ed the DBRI, clear the flag when
 * an interrupt signaled completion, and wait on a wait_queue if a routine
 * attempted to cmdlock while the flag was set.  The problems arose when
 * we tried to cmdlock from inside an interrupt handler, which might
 * cause scheduling in an interrupt (if we waited), etc, etc
 *
 * A more sophisticated scheme might involve a circular command buffer
 * or an array of command buffers.  A routine could fill one with
 * commands and link it onto a list.  When a interrupt signaled
 * completion of the current command buffer, look on the list for
 * the next one.
 *
 * I've decided to implement something much simpler - after each command,
 * the CPU waits for the DBRI to finish the command by polling the P bit
 * in DBRI register 0.  I've tried to implement this in such a way
 * that might make implementing a more sophisticated scheme easier.
 *
 * Every time a routine wants to write commands to the DBRI, it must
 * first call dbri_cmdlock() and get an initial pointer into dbri->dma->cmd
 * in return.  After the commands have been writen, dbri_cmdsend() is
 * called with the final pointer value.
 */

static int dbri_locked = 0;			/* XXX not SMP safe! XXX */

static volatile int * dbri_cmdlock(struct dbri *dbri)
{
        if (dbri_locked) {
                printk("DBRI: Command buffer locked! (bug in driver)\n");
        }
        dbri_locked ++;
        return dbri->dma->cmd;
}

static void dbri_cmdsend(struct dbri *dbri, volatile int * cmd)
{
        int maxloops = 1000000;

        dbri_locked --;
        if (dbri_locked != 0) {
                printk("DBRI: Command buffer improperly locked! (bug in driver)\n");
        } else if ((cmd - dbri->dma->cmd) >= DBRI_NO_CMDS-1) {
                printk("DBRI: Command buffer overflow! (bug in driver)\n");
        } else {
                *(cmd++) = DBRI_CMD(D_PAUSE, 0, 0);
                *(cmd++) = DBRI_CMD(D_WAIT, 0, 0);
                dbri->regs->reg8 = (int)dbri->dma_dvma->cmd;
                while ((maxloops--) > 0 && (dbri->regs->reg0 & D_P));
        }

        if (maxloops == 0) {
                printk("DBRI: Chip never completed command buffer\n");
        }
}

static void dbri_reset(struct dbri *dbri)
{
	int i;

	dprintk(D_GEN, ("DBRI: reset 0:%x 2:%x 8:%x 9:%x\n",
		dbri->regs->reg0, dbri->regs->reg2,
		dbri->regs->reg8, dbri->regs->reg9)); 

	dbri->regs->reg0 = D_R; /* Soft Reset */
	for(i = 0; (dbri->regs->reg0 & D_R) && i < 10; i++)
		udelay(10);
}

static void dbri_detach(struct dbri *dbri)
{
	dbri_reset(dbri);
        free_irq(dbri->irq, dbri);
        sparc_free_io(dbri->regs, dbri->regs_size);
        /* Should we release the DMA structure dbri->dma here? */
        kfree(dbri);
}


static void dbri_initialize(struct dbri *dbri)
{
        int n;
	volatile int *cmd;

        dbri_reset(dbri);

	dprintk(D_GEN, ("DBRI: init: cmd: %x, int: %x\n",
			(int)dbri->dma->cmd, (int)dbri->dma->intr));

	/*
	 * Initialize the interrupt ringbuffer.
	 */
	for(n = 0; n < DBRI_NO_INTS-1; n++)
		dbri->dma->intr[n * DBRI_INT_BLK] = 
                        (int)(&dbri->dma_dvma->intr[(n+1)*DBRI_INT_BLK]);
	dbri->dma->intr[n * DBRI_INT_BLK] = (int)(dbri->dma_dvma->intr);
	dbri->dbri_irqp = 1;

        /* We should query the openprom to see what burst sizes this
         * SBus supports.  For now, just disable all SBus bursts */
        dbri->regs->reg0 &= ~(D_G|D_S|D_E);

	/*
	 * Set up the interrupt queue
	 */
	cmd = dbri_cmdlock(dbri);

	*(cmd++) = DBRI_CMD(D_IIQ, 0, 0);
	*(cmd++) = (int)(dbri->dma_dvma->intr);

        dbri_cmdsend(dbri, cmd);
}


/*
****************************************************************************
*************************** DBRI interrupt handler *************************
****************************************************************************
*/


/*
 * Short data pipes transmit LSB first. The CS4215 receives MSB first. Grrr.
 * So we have to reverse the bits. Note: not all bit lengths are supported
 */
static __u32 reverse_bytes(__u32 b, int len)
{
	switch(len) {
        case 32:
                b = ((b & 0xffff0000) >> 16) | ((b & 0x0000ffff) << 16);
        case 16:
                b = ((b & 0xff00ff00) >>  8) | ((b & 0x00ff00ff) <<  8);
        case 8:
                b = ((b & 0xf0f0f0f0) >>  4) | ((b & 0x0f0f0f0f) <<  4);
        case 4:
                b = ((b & 0xcccccccc) >>  2) | ((b & 0x33333333) <<  2);
        case 2:
                b = ((b & 0xaaaaaaaa) >>  1) | ((b & 0x55555555) <<  1);
        case 1:
                break;
        default:
                printk("DBRI reverse_bytes: unsupported length\n");
	}
	return b;
}

/* transmission_complete_intr()
 *
 * Called by main interrupt handler when DBRI signals transmission complete
 * on a pipe.
 *
 * Walks through the pipe's list of transmit buffer descriptors, releasing
 * each one's DMA buffer (if present) and signaling its callback routine
 * (if present), before flaging the descriptor available and proceeding
 * to the next one.
 *
 * Assumes that only the last in a chain of descriptors will have FINT
 * sent to signal an interrupt, so that the chain will be completely
 * transmitted by the time we get here, and there's no need to save
 * any of the descriptors.  In particular, use of the DBRI's CDP command
 * is precluded, but I've not been able to get CDP working reliably anyway.
 */

static void transmission_complete_intr(struct dbri *dbri, int pipe)
{
        int td = dbri->pipes[pipe].desc;
        int status;
        void *buffer;
        void (*callback)(void *, int);

        dbri->pipes[pipe].desc = -1;

        for (; td >= 0; td = dbri->descs[td].next) {

                if (td >= DBRI_NO_DESCS) {
                        printk("DBRI: invalid td on pipe %d\n", pipe);
                        return;
                }

                status = dbri->dma->desc[td].word4;

                buffer = dbri->descs[td].buffer;
                if (buffer) {
                        mmu_release_scsi_one(sbus_dvma_addr(buffer),
                                             dbri->descs[td].len,
                                             dbri->sdev->my_bus);
                }

                callback = dbri->descs[td].output_callback;
                if (callback != NULL) {
                        callback(dbri->descs[td].output_callback_arg,
                                 DBRI_TD_STATUS(status) & 0xe);
                }

                dbri->descs[td].inuse = 0;
        }
}

static void reception_complete_intr(struct dbri *dbri, int pipe)
{
        int rd = dbri->pipes[pipe].desc;
        int status;
        void *buffer;
        void (*callback)(void *, int, unsigned int);

        if (rd < 0 || rd >= DBRI_NO_DESCS) {
                printk("DBRI: invalid rd on pipe %d\n", pipe);
                return;
        }

        dbri->descs[rd].inuse = 0;
        dbri->pipes[pipe].desc = -1;
        status = dbri->dma->desc[rd].word1;

        buffer = dbri->descs[rd].buffer;
        if (buffer) {
                mmu_release_scsi_one(sbus_dvma_addr(buffer),
                                     dbri->descs[rd].len,
                                     dbri->sdev->my_bus);
        }

        callback = dbri->descs[rd].input_callback;
        if (callback != NULL) {
                callback(dbri->descs[rd].input_callback_arg,
                         DBRI_RD_STATUS(status),
                         DBRI_RD_CNT(status)-2);
        }
}

static void dbri_intr(int irq, void *opaque, struct pt_regs *regs)
{
	struct dbri *dbri = (struct dbri *)opaque;
	int x;
	
	/*
	 * Read it, so the interrupt goes away.
	 */
	x = dbri->regs->reg1;

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

	x = dbri->dma->intr[dbri->dbri_irqp];
	while (x != 0) {
                int val = D_INTR_GETVAL(x);
                int channel = D_INTR_GETCHAN(x);

		dbri->dma->intr[dbri->dbri_irqp] = 0;

		if(D_INTR_GETCHAN(x) == D_INTR_CMD) {
			dprintk(D_INT,("DBRI: INTR: Command: %-5s  Value:%d\n",
				cmds[D_INTR_GETCMD(x)], D_INTR_GETVAL(x)));
		} else {
			dprintk(D_INT,("DBRI: INTR: Chan:%d Code:%d Val:%#x\n",
				D_INTR_GETCHAN(x), D_INTR_GETCODE(x),
				D_INTR_GETRVAL(x)));
		}

                if (D_INTR_GETCODE(x) == D_INTR_SBRI) {

                        /* SBRI - BRI status change */

                        int liu_states[] = {1, 0, 8, 3, 4, 5, 6, 7};
                        dbri->liu_state = liu_states[val & 0x7];
                        if (dbri->liu_callback)
                                dbri->liu_callback(dbri->liu_callback_arg);
                }

                if (D_INTR_GETCODE(x) == D_INTR_BRDY) {
                        reception_complete_intr(dbri, channel);
                }

                if (D_INTR_GETCODE(x) == D_INTR_XCMP) {
                        transmission_complete_intr(dbri, channel);
                }

                if (D_INTR_GETCODE(x) == D_INTR_FXDT) {

                        /* FXDT - Fixed data change */

                        if (dbri->pipes[D_INTR_GETCHAN(x)].sdp & D_SDP_MSB) {
                                val = reverse_bytes(val, dbri->pipes[channel].length);
                        }

                        if (dbri->pipes[D_INTR_GETCHAN(x)].recv_fixed_ptr) {
                                * dbri->pipes[channel].recv_fixed_ptr = val;
                        }
                }


		dbri->dbri_irqp++;
		if (dbri->dbri_irqp == (DBRI_NO_INTS * DBRI_INT_BLK))
			dbri->dbri_irqp = 1;
		else if ((dbri->dbri_irqp & (DBRI_INT_BLK-1)) == 0)
			dbri->dbri_irqp++;
		x = dbri->dma->intr[dbri->dbri_irqp];
	}
}


/*
****************************************************************************
************************** DBRI data pipe management ***********************
****************************************************************************
*/


/* reset_pipe(dbri, pipe)
 *
 * Called on an in-use pipe to clear anything being transmitted or received
 */

static void reset_pipe(struct dbri *dbri, int pipe)
{
        int sdp;
        volatile int *cmd;

        if (pipe < 0 || pipe > 31) {
                printk("DBRI: reset_pipe called with illegal pipe number\n");
                return;
        }

        sdp = dbri->pipes[pipe].sdp;
        if (sdp == 0) {
                printk("DBRI: reset_pipe called on uninitialized pipe\n");
                return;
        }

        cmd = dbri_cmdlock(dbri);
        *(cmd++) = DBRI_CMD(D_SDP, 0, sdp | D_SDP_C | D_SDP_P);
        *(cmd++) = 0;
        dbri_cmdsend(dbri, cmd);

        dbri->pipes[pipe].desc = -1;
}

static void setup_pipe(struct dbri *dbri, int pipe, int sdp)
{
        if (pipe < 0 || pipe > 31) {
                printk("DBRI: setup_pipe called with illegal pipe number\n");
                return;
        }

        if ((sdp & 0xf800) != sdp) {
                printk("DBRI: setup_pipe called with strange SDP value\n");
                /* sdp &= 0xf800; */
        }

        sdp |= D_PIPE(pipe);
        dbri->pipes[pipe].sdp = sdp;

        reset_pipe(dbri, pipe);
}

enum master_or_slave { CHImaster, CHIslave };

static void reset_chi(struct dbri *dbri, enum master_or_slave master_or_slave,
                      int bits_per_frame)
{
        volatile int *cmd;
        int val;

        cmd = dbri_cmdlock(dbri);

	/* Set CHI Anchor: Pipe 16 */

        val = D_DTS_VI | D_DTS_VO | D_DTS_INS |
                D_DTS_PRVIN(D_P_16) | D_DTS_PRVOUT(D_P_16) | D_PIPE(D_P_16);
	*(cmd++) = DBRI_CMD(D_DTS, 0, val);
	*(cmd++) = D_TS_ANCHOR | D_TS_NEXT(D_P_16);
	*(cmd++) = D_TS_ANCHOR | D_TS_NEXT(D_P_16);

        dbri->pipes[16].sdp = 1;
        dbri->pipes[16].nextpipe = 16;

        if (master_or_slave == CHIslave) {
                /* Setup DBRI for CHI Slave - receive clock, frame sync (FS)
                 *
                 * CHICM  = 0 (slave mode, 8 kHz frame rate)
                 * IR     = give immediate CHI status interrupt
                 * EN     = give CHI status interrupt upon change
                 */
                *(cmd++) = DBRI_CMD(D_CHI, 0, D_CHI_CHICM(0)
                                    | D_CHI_IR | D_CHI_EN);
        } else {
                /* Setup DBRI for CHI Master - generate clock, FS
                 *
                 * BPF				=  bits per 8 kHz frame
                 * 12.288 MHz / CHICM_divisor	= clock rate
                 * FD  =  1 - drive CHIFS on rising edge of CHICK
                 */

                int clockrate = bits_per_frame * 8;
                int divisor   = 12288 / clockrate;

                if (divisor > 255 || divisor * clockrate != 12288) {
                        printk("DBRI: illegal bits_per_frame in setup_chi\n");
                }

                *(cmd++) = DBRI_CMD(D_CHI, 0, D_CHI_CHICM(divisor) | D_CHI_FD
                                    | D_CHI_IR | D_CHI_EN
                                    | D_CHI_BPF(bits_per_frame));
        }

        /* CHI Data Mode
         *
         * RCE   =  0 - receive on falling edge of CHICK
         * XCE   =  1 - transmit on rising edge of CHICK
         * XEN   =  1 - enable transmitter
         * REN   =  1 - enable receiver
         */

        *(cmd++) = DBRI_CMD(D_PAUSE, 0, 0);

        *(cmd++) = DBRI_CMD(D_CDM, 0, D_CDM_XCE|D_CDM_XEN|D_CDM_REN);

        dbri_cmdsend(dbri, cmd);
}

enum in_or_out { PIPEinput, PIPEoutput };

static void link_time_slot(struct dbri *dbri, int pipe,
                           enum in_or_out direction, int prevpipe,
                           int length, int cycle)
{
        volatile int *cmd;
        int val;
        int nextpipe;

        if (pipe < 0 || pipe > 31 || prevpipe < 0 || prevpipe > 31) {
                printk("DBRI: link_time_slot called with illegal pipe number\n");
                return;
        }

        if (dbri->pipes[pipe].sdp == 0 || dbri->pipes[prevpipe].sdp == 0) {
                printk("DBRI: link_time_slot called on uninitialized pipe\n");
                return;
        }

        if (pipe == prevpipe) {
                nextpipe = pipe;
        } else {
                nextpipe = dbri->pipes[prevpipe].nextpipe;
        }

        dbri->pipes[pipe].nextpipe = nextpipe;
        dbri->pipes[pipe].cycle = cycle;
        dbri->pipes[pipe].length = length;

        cmd = dbri_cmdlock(dbri);

        if (direction == PIPEinput) {
                val = D_DTS_VI | D_DTS_INS | D_DTS_PRVIN(prevpipe) | pipe;
                *(cmd++) = DBRI_CMD(D_DTS, 0, val);
                *(cmd++) = D_TS_LEN(length) | D_TS_CYCLE(cycle) | D_TS_NEXT(nextpipe);
                *(cmd++) = 0;
        } else {
                val = D_DTS_VO | D_DTS_INS | D_DTS_PRVOUT(prevpipe) | pipe;
                *(cmd++) = DBRI_CMD(D_DTS, 0, val);
                *(cmd++) = 0;
                *(cmd++) = D_TS_LEN(length) | D_TS_CYCLE(cycle) | D_TS_NEXT(nextpipe);
        }

        dbri_cmdsend(dbri, cmd);
}

static void xmit_fixed(struct dbri *dbri, int pipe, unsigned int data)
{
        volatile int *cmd;

        if (pipe < 16 || pipe > 31) {
                printk("DBRI: xmit_fixed called with illegal pipe number\n");
                return;
        }

        if (D_SDP_MODE(dbri->pipes[pipe].sdp) != D_SDP_FIXED) {
                printk("DBRI: xmit_fixed called on non-fixed pipe\n");
                return;
        }

        if (! dbri->pipes[pipe].sdp & D_SDP_TO_SER) {
                printk("DBRI: xmit_fixed called on receive pipe\n");
                return;
        }

        /* DBRI short pipes always transmit LSB first */

        if (dbri->pipes[pipe].sdp & D_SDP_MSB) {
                data = reverse_bytes(data, dbri->pipes[pipe].length);
        }

        cmd = dbri_cmdlock(dbri);

        *(cmd++) = DBRI_CMD(D_SSP, 0, pipe);
        *(cmd++) = data;

        dbri_cmdsend(dbri, cmd);
}

/* recv_fixed()
 *
 * Receive data on a "fixed" pipe - i.e, one whose contents are not
 * expected to change much, and which we don't need to read constantly
 * into a buffer.  The DBRI only interrupts us when the data changes.
 * Only short pipes (numbers 16-31) can be used in fixed data mode.
 *
 * Pass this function a pointer to a 32-bit field, no matter how large
 * the actual time slot is.  The interrupt handler takes care of bit
 * ordering and alignment.  An 8-bit time slot will always end up
 * in the low-order 8 bits, filled either MSB-first or LSB-first,
 * depending on the settings passed to setup_pipe()
 */

static void recv_fixed(struct dbri *dbri, int pipe, __u32 *ptr)
{
        if (pipe < 16 || pipe > 31) {
                printk("DBRI: recv_fixed called with illegal pipe number\n");
                return;
        }

        if (D_SDP_MODE(dbri->pipes[pipe].sdp) != D_SDP_FIXED) {
                printk("DBRI: recv_fixed called on non-fixed pipe\n");
                return;
        }

        if (dbri->pipes[pipe].sdp & D_SDP_TO_SER) {
                printk("DBRI: recv_fixed called on transmit pipe\n");
                return;
        }

        dbri->pipes[pipe].recv_fixed_ptr = ptr;
}


static void xmit_on_pipe(struct dbri *dbri, int pipe,
                         void * buffer, unsigned int len,
                         void (*callback)(void *, int), void * callback_arg)
{
        volatile int *cmd;
        int td = 0;
        int first_td = -1;
        int last_td;
        __u32 dvma_buffer;

        if (pipe < 0 || pipe > 15) {
                printk("DBRI: xmit_on_pipe called with illegal pipe number\n");
                return;
        }

        if (dbri->pipes[pipe].sdp == 0) {
                printk("DBRI: xmit_on_pipe called on uninitialized pipe\n");
                return;
        }

        if (! dbri->pipes[pipe].sdp & D_SDP_TO_SER) {
                printk("DBRI: xmit_on_pipe called on receive pipe\n");
                return;
        }

        /* XXX Fix this XXX
         * Should be able to queue multiple buffers to send on a pipe
         */

        if (dbri->pipes[pipe].desc != -1) {
                printk("DBRI: xmit_on_pipe called on active pipe\n");
                return;
        }

        dvma_buffer = mmu_get_scsi_one(buffer, len, dbri->sdev->my_bus);

        while (len > 0) {
                int mylen;

                for (td; td < DBRI_NO_DESCS; td ++) {
                        if (! dbri->descs[td].inuse) break;
                }
                if (td == DBRI_NO_DESCS) {
                        break;
                }

                if (len > (1 << 13) - 1) {
                        mylen = (1 << 13) - 1;
                } else {
                        mylen = len;
                }

                dbri->descs[td].inuse = 1;
                dbri->descs[td].next = -1;
                dbri->descs[td].buffer = NULL;
                dbri->descs[td].output_callback = NULL;
                dbri->descs[td].input_callback = NULL;

                dbri->dma->desc[td].word1 = DBRI_TD_CNT(mylen);
                dbri->dma->desc[td].ba = dvma_buffer;
                dbri->dma->desc[td].nda = 0;
                dbri->dma->desc[td].word4 = 0;

                if (first_td == -1) {
                        first_td = td;
                } else {
                        dbri->descs[last_td].next = td;
                        dbri->dma->desc[last_td].nda =
                                (int) & dbri->dma_dvma->desc[td];
                }

                last_td = td;
                dvma_buffer += mylen;
                len -= mylen;
        }

        if (first_td == -1) {
                printk("xmit_on_pipe: No descriptors available\n");
                return;
        }

        if (len > 0) {
                printk("xmit_on_pipe: Insufficient descriptors; data truncated\n");
        }

        dbri->dma->desc[last_td].word1 |= DBRI_TD_I | DBRI_TD_F | DBRI_TD_B;

        dbri->descs[last_td].buffer = buffer;
        dbri->descs[last_td].len = len;
        dbri->descs[last_td].output_callback = callback;
        dbri->descs[last_td].output_callback_arg = callback_arg;

        dbri->pipes[pipe].desc = first_td;

        cmd = dbri_cmdlock(dbri);

        *(cmd++) = DBRI_CMD(D_SDP, 0, dbri->pipes[pipe].sdp | D_SDP_P | D_SDP_C);
        *(cmd++) = (int) & dbri->dma_dvma->desc[first_td];

        dbri_cmdsend(dbri, cmd);
}

static void recv_on_pipe(struct dbri *dbri, int pipe,
                         void * buffer, unsigned int len,
                         void (*callback)(void *, int, unsigned int),
                         void * callback_arg)
{
        volatile int *cmd;
        int rd;

        if (pipe < 0 || pipe > 15) {
                printk("DBRI: recv_on_pipe called with illegal pipe number\n");
                return;
        }

        if (dbri->pipes[pipe].sdp == 0) {
                printk("DBRI: recv_on_pipe called on uninitialized pipe\n");
                return;
        }

        if (dbri->pipes[pipe].sdp & D_SDP_TO_SER) {
                printk("DBRI: recv_on_pipe called on transmit pipe\n");
                return;
        }

        /* XXX Fix this XXX
         * Should be able to queue multiple buffers to send on a pipe
         */

        if (dbri->pipes[pipe].desc != -1) {
                printk("DBRI: recv_on_pipe called on active pipe\n");
                return;
        }

        /* XXX Fix this XXX
         * Use multiple descriptors, if needed, to fit in all the data
         */

        if (len > (1 << 13) - 1) {
                printk("recv_on_pipe called with len=%d; truncated\n", len);
                len = (1 << 13) - 1;
        }

        /* Make sure buffer size is multiple of four */
        len &= ~3;

        for (rd = 0; rd < DBRI_NO_DESCS; rd ++) {
                if (! dbri->descs[rd].inuse) break;
        }
        if (rd == DBRI_NO_DESCS) {
                printk("DBRI xmit_on_pipe: No descriptors available\n");
                return;
        }

        dbri->dma->desc[rd].word1 = 0;
        dbri->dma->desc[rd].ba = mmu_get_scsi_one(buffer, len,
                                                  dbri->sdev->my_bus);
        dbri->dma->desc[rd].nda = 0;
        dbri->dma->desc[rd].word4 = DBRI_RD_B | DBRI_RD_BCNT(len);

        dbri->descs[rd].buffer = buffer;
        dbri->descs[rd].len = len;
        dbri->descs[rd].input_callback = callback;
        dbri->descs[rd].input_callback_arg = callback_arg;

        dbri->pipes[pipe].desc = rd;

        cmd = dbri_cmdlock(dbri);

        *(cmd++) = DBRI_CMD(D_SDP, 0, dbri->pipes[pipe].sdp | D_SDP_P);
        *(cmd++) = (int) & dbri->dma_dvma->desc[rd];

        dbri_cmdsend(dbri, cmd);
}


/*
****************************************************************************
*********************** CS4215 audio codec management **********************
****************************************************************************
*/


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
	 * 2: Serial enable, CHI master, 128 bits per frame, clock 1
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


        /* Switch CS4215 to data mode - set PIO3 to 1 */
	dbri->regs->reg2 = D_ENPIO | D_PIO1 | D_PIO3 |
				(dbri->mm.onboard ? D_PIO0 : D_PIO2);

	reset_chi(dbri, CHIslave, 0);

        setup_pipe(dbri,  4, D_SDP_MEM   | D_SDP_TO_SER | D_SDP_MSB);
        setup_pipe(dbri, 17, D_SDP_FIXED | D_SDP_TO_SER | D_SDP_MSB);
        setup_pipe(dbri,  6, D_SDP_MEM   | D_SDP_FROM_SER | D_SDP_MSB);
        setup_pipe(dbri, 20, D_SDP_FIXED | D_SDP_FROM_SER | D_SDP_MSB);

        /* Pipes 4 and 6 - Single time slot, 8 bit mono */

        link_time_slot(dbri, 17, PIPEoutput, 16, 32, 32);
        link_time_slot(dbri,  4, PIPEoutput, 17, 8, 128);
        link_time_slot(dbri,  6, PIPEinput, 16, 8, 0);
        link_time_slot(dbri, 20, PIPEinput, 6, 16, 40);

        xmit_fixed(dbri, 17, *(int *)dbri->mm.data);
}


/*
 * Send the control information (i.e. audio format)
 */
static void mmcodec_setctrl(struct dbri *dbri)
{
	int i, val;

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
         * Oh, and one more thing, on a SPARCStation 20 (and maybe
         * others?), the addressing of the CS4215's time slots is
         * offset by eight bits, so we add eight to all the "cycle"
         * values in the Define Time Slot (DTS) commands.  This is
         * done in hardware by a TI 248 that delays the DBRI->4215
         * frame sync signal by eight clock cycles.  Anybody know why?
         */

        reset_chi(dbri, CHImaster, 128);

	/*
	 * Control mode:
	 * Pipe 17: Send timeslots 1-4 (slots 5-8 are readonly)
	 * Pipe 18: Receive timeslot 1 (clb).
	 * Pipe 19: Receive timeslot 7 (version). 
	 */

        setup_pipe(dbri, 17, D_SDP_FIXED | D_SDP_TO_SER | D_SDP_MSB);
        setup_pipe(dbri, 18, D_SDP_FIXED | D_SDP_CHANGE | D_SDP_MSB);
        setup_pipe(dbri, 19, D_SDP_FIXED | D_SDP_CHANGE | D_SDP_MSB);

        link_time_slot(dbri, 17, PIPEoutput, 16, 32, 128);
        link_time_slot(dbri, 18, PIPEinput, 16, 8, 0);
        link_time_slot(dbri, 19, PIPEinput, 18, 8, 48);

        recv_fixed(dbri, 18, & dbri->mm.status);
        recv_fixed(dbri, 19, & dbri->mm.version);

        /* Wait for the chip to echo back CLB (Control Latch Bit) as zero */

	dbri->mm.ctrl[0] &= ~CS4215_CLB;
        xmit_fixed(dbri, 17, *(int *)dbri->mm.ctrl);

        i = 1000000;
        while ((! dbri->mm.status & CS4215_CLB) && i--);
        if (i == 0) {
                printk("CS4215 didn't respond to CLB\n");
		return;
        }

        /* Terminate CS4215 control mode - data sheet says
         * "Set CLB=1 and send two more frames of valid control info"
         */

	dbri->mm.ctrl[0] |= CS4215_CLB;
        xmit_fixed(dbri, 17, *(int *)dbri->mm.ctrl);

        /* Two frames of control info @ 8kHz frame rate = 250 us delay */
        udelay(250);
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


/*
****************************************************************************
******************** Interface with sparcaudio midlevel ********************
****************************************************************************
*/


static int dbri_open(struct inode * inode, struct file * file,
                     struct sparcaudio_driver *drv)
{
	struct dbri *dbri = (struct dbri *)drv->private;

	MOD_INC_USE_COUNT;

	return 0;
}

static void dbri_release(struct inode * inode, struct file * file,
                         struct sparcaudio_driver *drv)
{
	MOD_DEC_USE_COUNT;
}

static int dbri_ioctl(struct inode * inode, struct file * file,
                      unsigned int x, unsigned long y,
                      struct sparcaudio_driver *drv)
{
        return 0;
}

static void dbri_audio_output_callback(void * callback_arg, int status)
{
        struct sparcaudio_driver *drv = callback_arg;

        sparcaudio_output_done(drv, 1);
}

static void dbri_start_output(struct sparcaudio_driver *drv,
                              __u8 * buffer, unsigned long count)
{
	struct dbri *dbri = (struct dbri *)drv->private;

        /* Pipe 4 is audio transmit */
        xmit_on_pipe(dbri, 4, buffer, count, &dbri_audio_output_callback, drv);
}

static void dbri_stop_output(struct sparcaudio_driver *drv)
{
	struct dbri *dbri = (struct dbri *)drv->private;

        reset_pipe(dbri, 4);
}

static void dbri_start_input(struct sparcaudio_driver *drv,
                             __u8 * buffer, unsigned long len)
{
}

static void dbri_stop_input(struct sparcaudio_driver *drv)
{
}

static void dbri_audio_getdev(struct sparcaudio_driver *drv,
                              audio_device_t *devptr)
{
}

static int dbri_set_output_volume(struct sparcaudio_driver *drv, int volume)
{
        return 0;
}

static int dbri_get_output_volume(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_set_input_volume(struct sparcaudio_driver *drv, int volume)
{
        return 0;
}

static int dbri_get_input_volume(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_set_monitor_volume(struct sparcaudio_driver *drv, int volume)
{
        return 0;
}

static int dbri_get_monitor_volume(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_set_output_balance(struct sparcaudio_driver *drv, int balance)
{
        return 0;
}

static int dbri_get_output_balance(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_set_input_balance(struct sparcaudio_driver *drv, int balance)
{
        return 0;
}

static int dbri_get_input_balance(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_set_output_channels(struct sparcaudio_driver *drv, int chan)
{
        return 0;
}

static int dbri_get_output_channels(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_set_input_channels(struct sparcaudio_driver *drv, int chan)
{
        return 0;
}

static int dbri_get_input_channels(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_set_output_precision(struct sparcaudio_driver *drv, int prec)
{
        return 8;
}

static int dbri_get_output_precision(struct sparcaudio_driver *drv)
{
        return 8;
}

static int dbri_set_input_precision(struct sparcaudio_driver *drv, int prec)
{
        return 8;
}

static int dbri_get_input_precision(struct sparcaudio_driver *drv)
{
        return 8;
}

static int dbri_set_output_port(struct sparcaudio_driver *drv, int port)
{
        return 0;
}

static int dbri_get_output_port(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_set_input_port(struct sparcaudio_driver *drv, int port)
{
        return 0;
}

static int dbri_get_input_port(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_set_output_encoding(struct sparcaudio_driver *drv, int enc)
{
        return 0;
}

static int dbri_get_output_encoding(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_set_input_encoding(struct sparcaudio_driver *drv, int enc)
{
        return 0;
}

static int dbri_get_input_encoding(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_set_output_rate(struct sparcaudio_driver *drv, int rate)
{
        return 0;
}

static int dbri_get_output_rate(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_set_input_rate(struct sparcaudio_driver *drv, int rate)
{
        return 0;
}

static int dbri_get_input_rate(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_sunaudio_getdev_sunos(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_get_output_ports(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_get_input_ports(struct sparcaudio_driver *drv)
{
        return 0;
}

static int dbri_set_output_muted(struct sparcaudio_driver *drv, int mute)
{
        return 0;
}

static int dbri_get_output_muted(struct sparcaudio_driver *drv)
{
        return 0;
}



static struct sparcaudio_operations dbri_ops = {
	dbri_open,
	dbri_release,
	dbri_ioctl,
	dbri_start_output,
	dbri_stop_output,
	dbri_start_input,
        dbri_stop_input,
	dbri_audio_getdev,
	dbri_set_output_volume,
	dbri_get_output_volume,
	dbri_set_input_volume,
	dbri_get_input_volume,
	dbri_set_monitor_volume,
	dbri_get_monitor_volume,
	dbri_set_output_balance,
	dbri_get_output_balance,
	dbri_set_input_balance,
	dbri_get_input_balance,
	dbri_set_output_channels,
	dbri_get_output_channels,
	dbri_set_input_channels,
	dbri_get_input_channels,
	dbri_set_output_precision,
	dbri_get_output_precision,
	dbri_set_input_precision,
	dbri_get_input_precision,
	dbri_set_output_port,
	dbri_get_output_port,
	dbri_set_input_port,
	dbri_get_input_port,
	dbri_set_output_encoding,
	dbri_get_output_encoding,
	dbri_set_input_encoding,
	dbri_get_input_encoding,
	dbri_set_output_rate,
	dbri_get_output_rate,
	dbri_set_input_rate,
	dbri_get_input_rate,
	dbri_sunaudio_getdev_sunos,
	dbri_get_output_ports,
	dbri_get_input_ports,
	dbri_set_output_muted,
	dbri_get_output_muted,
};


/*
****************************************************************************
************************** ISDN (Hisax) Interface **************************
****************************************************************************
*/


void dbri_isdn_init(struct dbri *dbri)
{
        /* Pipe  0: Receive D channel
         * Pipe  8: Receive B1 channel
         * Pipe  9: Receive B2 channel
         * Pipe  1: Transmit D channel
         * Pipe 10: Transmit B1 channel
         * Pipe 11: Transmit B2 channel
         */

        setup_pipe(dbri, 0, D_SDP_HDLC | D_SDP_FROM_SER | D_SDP_LSB);
        setup_pipe(dbri, 8, D_SDP_HDLC | D_SDP_FROM_SER | D_SDP_LSB);
        setup_pipe(dbri, 9, D_SDP_HDLC | D_SDP_FROM_SER | D_SDP_LSB);

        setup_pipe(dbri, 1, D_SDP_HDLC_D | D_SDP_TO_SER | D_SDP_LSB);
        setup_pipe(dbri,10, D_SDP_HDLC | D_SDP_TO_SER | D_SDP_LSB);
        setup_pipe(dbri,11, D_SDP_HDLC | D_SDP_TO_SER | D_SDP_LSB);

        link_time_slot(dbri, 0, PIPEinput, 0, 2, 17);
        link_time_slot(dbri, 8, PIPEinput, 8, 8, 0);
        link_time_slot(dbri, 9, PIPEinput, 9, 8, 8);

        link_time_slot(dbri,  1, PIPEoutput,  1, 2, 17);
        link_time_slot(dbri, 10, PIPEoutput,  1, 8, 0);
        link_time_slot(dbri, 11, PIPEoutput, 10, 8, 8);
}

int dbri_get_irqnum(int dev)
{
       struct dbri *dbri;

       if (dev >= num_drivers) {
               return(0);
       }

       dbri = (struct dbri *) drivers[dev].private;

        /* On the sparc, the cpu's irq number is only part of the "irq" */
       return (dbri->irq & NR_IRQS);
}

int dbri_get_liu_state(int dev)
{
       struct dbri *dbri;

       if (dev >= num_drivers) {
               return(0);
       }

       dbri = (struct dbri *) drivers[dev].private;

       return dbri->liu_state;
}

void dbri_liu_activate(int dev, int priority);

void dbri_liu_init(int dev, void (*callback)(void *), void *callback_arg)
{
       struct dbri *dbri;

       if (dev >= num_drivers) {
               return;
       }

       dbri = (struct dbri *) drivers[dev].private;

       /* Set callback for LIU state change */
        dbri->liu_callback = callback;
       dbri->liu_callback_arg = callback_arg;

        dbri_isdn_init(dbri);
        dbri_liu_activate(dev, 0);
}

void dbri_liu_activate(int dev, int priority)
{
       struct dbri *dbri;
       int val;
       volatile int *cmd;

       if (dev >= num_drivers) {
               return;
       }

       dbri = (struct dbri *) drivers[dev].private;

        cmd = dbri_cmdlock(dbri);

        /* Turn on the ISDN TE interface and request activation */
        val = D_NT_IRM_IMM | D_NT_IRM_EN | D_NT_ACT;
        *(cmd++) = DBRI_CMD(D_TE, 0, val);

       dbri_cmdsend(dbri, cmd);

        /* Activate the interface */
        dbri->regs->reg0 |= D_T;
}

void dbri_liu_deactivate(int dev)
{
       struct dbri *dbri;

       if (dev >= num_drivers) {
               return;
       }

       dbri = (struct dbri *) drivers[dev].private;

        /* Turn off the ISDN TE interface */
        dbri->regs->reg0 &= ~D_T;
}

void dbri_dxmit(int dev, __u8 *buffer, unsigned int count,
                       void (*callback)(void *, int), void *callback_arg)
{
       struct dbri *dbri;

       if (dev >= num_drivers) {
               return;
       }

       dbri = (struct dbri *) drivers[dev].private;

       /* Pipe 1 is D channel transmit */
       xmit_on_pipe(dbri, 1, buffer, count, callback, callback_arg);
}

void dbri_drecv(int dev, __u8 *buffer, unsigned int size,
                       void (*callback)(void *, int, unsigned int),
                       void *callback_arg)
{
       struct dbri *dbri;

       if (dev >= num_drivers) {
               return;
       }

       dbri = (struct dbri *) drivers[dev].private;

       /* Pipe 0 is D channel receive */
       recv_on_pipe(dbri, 0, buffer, size, callback, callback_arg);
}

int dbri_bopen(int dev, unsigned int chan,
               int hdlcmode, u_char xmit_idle_char)
{
       struct dbri *dbri;
       int val;

       if (dev >= num_drivers || chan > 1) {
               return -1;
       }

       dbri = (struct dbri *) drivers[dev].private;

       if (hdlcmode) {

               /* return -1; */

               /* Pipe 8/9: receive B1/B2 channel */
               setup_pipe(dbri, 8+chan, D_SDP_HDLC | D_SDP_FROM_SER|D_SDP_LSB);

               /* Pipe 10/11: transmit B1/B2 channel */
               setup_pipe(dbri,10+chan, D_SDP_HDLC | D_SDP_TO_SER | D_SDP_LSB);

       } else {        /* !hdlcmode means transparent */

               /* Pipe 8/9: receive B1/B2 channel */
               setup_pipe(dbri, 8+chan, D_SDP_MEM | D_SDP_FROM_SER|D_SDP_LSB);

               /* Pipe 10/11: transmit B1/B2 channel */
               setup_pipe(dbri,10+chan, D_SDP_MEM | D_SDP_TO_SER | D_SDP_LSB);

       }
       return 0;
}

void dbri_bclose(int dev, unsigned int chan)
{
       struct dbri *dbri;

       if (dev >= num_drivers || chan > 1) {
               return;
       }

       dbri = (struct dbri *) drivers[dev].private;

       reset_pipe(dbri, 8+chan);
       reset_pipe(dbri, 10+chan);
}

void dbri_bxmit(int dev, unsigned int chan,
                       __u8 *buffer, unsigned long count,
                       void (*callback)(void *, int),
                       void *callback_arg)
{
       struct dbri *dbri;

       if (dev >= num_drivers || chan > 1) {
               return;
       }

       dbri = (struct dbri *) drivers[dev].private;

       /* Pipe 10/11 is B1/B2 channel transmit */
       xmit_on_pipe(dbri, 10+chan, buffer, count, callback, callback_arg);
}

void dbri_brecv(int dev, unsigned int chan,
                       __u8 *buffer, unsigned long size,
                       void (*callback)(void *, int, unsigned int),
                       void *callback_arg)
{
       struct dbri *dbri;

       if (dev >= num_drivers || chan > 1) {
               return;
       }

       dbri = (struct dbri *) drivers[dev].private;

       /* Pipe 8/9 is B1/B2 channel receive */
       recv_on_pipe(dbri, 8+chan, buffer, size, callback, callback_arg);
}

#if defined(DBRI_ISDN) || defined (LINUX_VERSION_CODE) && LINUX_VERSION_CODE > 0x200ff
struct foreign_interface dbri_foreign_interface = {
        dbri_get_irqnum,
        dbri_get_liu_state,
        dbri_liu_init,
        dbri_liu_activate,
        dbri_liu_deactivate,
        dbri_dxmit,
        dbri_drecv,
        dbri_bopen,
        dbri_bclose,
        dbri_bxmit,
        dbri_brecv
};
EXPORT_SYMBOL(dbri_foreign_interface);
#endif

/*
****************************************************************************
**************************** Initialization ********************************
****************************************************************************
*/

static int dbri_attach(struct sparcaudio_driver *drv, 
			 struct linux_sbus_device *sdev)
{
	struct dbri *dbri;
	struct linux_prom_irqs irq;
        __u32 dma_dvma;
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

        /* sparc_dvma_malloc() will halt the kernel if the malloc fails */
        dbri->dma = sparc_dvma_malloc (sizeof (struct dbri_dma),
                                       "DBRI DMA Cmd Block", &dma_dvma);
        dbri->dma_dvma = (struct dbri_dma *) dma_dvma;

	dbri->dbri_version = sdev->prom_name[9];
        dbri->sdev = sdev;

	/* Map the registers into memory. */
	prom_apply_sbus_ranges(sdev->my_bus, &sdev->reg_addrs[0], 
		sdev->num_registers, sdev);
	dbri->regs_size = sdev->reg_addrs[0].reg_size;
	dbri->regs = sparc_alloc_io(sdev->reg_addrs[0].phys_addr, 0,
		sdev->reg_addrs[0].reg_size, 
		"DBRI Registers", sdev->reg_addrs[0].which_io, 0);
	if (!dbri->regs) {
		printk(KERN_ERR "DBRI: could not allocate registers\n");
		kfree(drv->private);
		return -EIO;
	}

	prom_getproperty(sdev->prom_node, "intr", (char *)&irq, sizeof(irq));
	dbri->irq = irq.pri;

	err = request_irq(dbri->irq, dbri_intr, SA_SHIRQ,
                          "DBRI audio/ISDN", dbri);
	if (err) {
		printk(KERN_ERR "DBRI: Can't get irq %d\n", dbri->irq);
		sparc_free_io(dbri->regs, dbri->regs_size);
		kfree(drv->private);
		return err;
	}

	dbri_initialize(dbri);
	err = mmcodec_init(drv);
	if(err) {
		dbri_detach(dbri);
		return err;
	}
	  
	/* Register ourselves with the midlevel audio driver. */
	err = register_sparcaudio_driver(drv,1);
	if (err) {
		printk(KERN_ERR "DBRI: unable to register audio\n");
                dbri_detach(dbri);
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
                dbri_detach((struct dbri *) drivers[i].private);
                unregister_sparcaudio_driver(& drivers[i], 1);
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
 * Local Variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
