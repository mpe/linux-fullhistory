/*  -*- linux-c -*-
 *
 * sound/wavfront.c
 *
 * A Linux driver for Turtle Beach WaveFront Series (Maui, Tropez, Tropez Plus)
 *
 * This driver supports the onboard wavetable synthesizer (an ICS2115),
 * including patch, sample and program loading and unloading, conversion
 * of GUS patches during loading, and full user-level access to all
 * WaveFront commands. It tries to provide semi-intelligent patch and
 * sample management as well.
 *
 * It also provides support for the ICS emulation of an MPU-401.  Full
 * support for the ICS emulation's "virtual MIDI mode" is provided in
 * wf_midi.c.
 *
 * Support is also provided for the Tropez Plus' onboard FX processor,
 * a Yamaha YSS225. Currently, code exists to configure the YSS225,
 * and there is an interface allowing tweaking of any of its memory
 * addresses. However, I have been unable to decipher the logical
 * positioning of the configuration info for various effects, so for
 * now, you just get the YSS225 in the same state as Turtle Beach's
 * "SETUPSND.EXE" utility leaves it.
 *
 * The boards' CODEC (a Crystal CS4232) is supported by cs4232.[co],
 * This chip also controls the configuration of the card: the wavefront
 * synth is logical unit 4.
 *
 **********************************************************************
 *
 * Copyright (C) by Paul Barton-Davis 1998
 *
 * Some portions of this file are taken from work that is
 * copyright (C) by Hannu Savolainen 1993-1996
 *
 * Although the relevant code here is all new, the handling of
 * sample/alias/multi- samples is entirely based on a driver by Matt
 * Martin and Rutger Nijlunsing which demonstrated how to get things
 * to most aspects of this to work correctly. The GUS patch loading
 * code has been almost unaltered by me, except to fit formatting and
 * function names in the rest of the file. Many thanks to them.
 *
 * Appreciation and thanks to Hannu Savolainen for his early work on the Maui
 * driver, and answering a few questions while this one was developed.
 *
 * Absolutely NO thanks to Turtle Beach/Voyetra and Yamaha for their
 * complete lack of help in developing this driver, and in particular
 * for their utter silence in response to questions about undocumented
 * aspects of configuring a WaveFront soundcard, particularly the
 * effects processor.
 *
 * $Id: wavfront.c,v 0.5 1998/07/22 16:16:41 pbd Exp $
 *
 * This program is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <asm/init.h>

#include "sound_config.h"
#include "soundmodule.h"

#include <linux/wavefront.h>

#define MIDI_SYNTH_NAME	"WaveFront MIDI"
#define MIDI_SYNTH_CAPS	SYNTH_CAP_INPUT
#include "midi_synth.h"

/* This thing is meant to work as a module */

#if defined(CONFIG_SOUND_WAVEFRONT_MODULE) && defined(MODULE)

/* if WF_DEBUG not defined, no run-time debugging messages will
   be available via the debug flag setting. Given the current
   beta state of the driver, this will remain set until a future 
   version.
*/

#define WF_DEBUG 1

/* bitmasks for WaveFront status port value */

#define STAT_INTR_WRITE		0x40
#define STAT_CAN_WRITE		0x20
#define STAT_WINTR_ENABLED	0x10
#define STAT_INTR_READ		0x04
#define STAT_CAN_READ		0x02
#define STAT_RINTR_ENABLED	0x01

/*** Module-accessible parameters ***************************************/

int wf_raw = 0; /* we normally check for "raw state" to firmware
		   loading. if set, then during driver loading, the
		   state of the board is ignored, and we reset the
		   board and load the firmware anyway.
		*/
		   
int fx_raw = 1; /* if this is zero, we'll leave the FX processor in
		   whatever state it is when the driver is loaded.
		   The default is to download the microprogram and
		   associated coefficients to set it up for "default"
		   operation, whatever that means.
		*/

int debug_default = 0;  /* you can set this to control debugging
			      during driver loading. it takes any combination
			      of the WF_DEBUG_* flags defined in
			      wavefront.h
			   */

/* XXX this needs to be made firmware and hardware version dependent */

char *ospath = "/etc/sound/wavefront.os"; /* where to find a processed
					     version of the WaveFront OS
					  */

int sleep_interval = 100;     /* HZ/sleep_interval seconds per sleep */
int sleep_tries = 50;       /* number of times we'll try to sleep */

int wait_usecs = 150; /* This magic number seems to give pretty optimal
			 throughput based on my limited experimentation.
			 If you want to play around with it and find a better
			 value, be my guest. Remember, the idea is to
			 get a number that causes us to just busy wait
			 for as many WaveFront commands as possible, without
			 coming up with a number so large that we hog the
			 whole CPU.

			 Specifically, with this number, out of about 134,000
			 status waits, only about 250 result in a sleep.
		      */

MODULE_PARM(wf_raw,"i");
MODULE_PARM(fx_raw,"i");
MODULE_PARM(debug_default,"i");
MODULE_PARM(sleep_interval,"i");
MODULE_PARM(sleep_tries,"i");
MODULE_PARM(wait_usecs,"i");
MODULE_PARM(ospath,"s");

/***************************************************************************/

static struct synth_info wavefront_info =
{"Turtle Beach WaveFront", 0, SYNTH_TYPE_SAMPLE, SAMPLE_TYPE_WAVEFRONT,
 0, 32, 0, 0, SYNTH_CAP_INPUT};

static int (*midi_load_patch) (int dev, int format, const char *addr,
			       int offs, int count, int pmgr_flag) = NULL;

typedef struct wf_config {
	int devno;            /* device number from kernel */
	int irq;              /* "you were one, one of the few ..." */
	int base;             /* low i/o port address */

#define mpu_data_port    base 
#define mpu_command_port base + 1 /* write semantics */
#define mpu_status_port  base + 1 /* read semantics */
#define data_port        base + 2 
#define status_port      base + 3 /* read semantics */
#define control_port     base + 3 /* write semantics  */
#define block_port       base + 4 /* 16 bit, writeonly */
#define last_block_port  base + 6 /* 16 bit, writeonly */

	/* FX ports. These are mapped through the ICS2115 to the YS225.
	   The ICS2115 takes care of flipping the relevant pins on the
	   YS225 so that access to each of these ports does the right
	   thing. Note: these are NOT documented by Turtle Beach.
	*/

#define fx_status       base + 8 
#define fx_op           base + 8 
#define fx_lcr          base + 9 
#define fx_dsp_addr     base + 0xa
#define fx_dsp_page     base + 0xb 
#define fx_dsp_lsb      base + 0xc 
#define fx_dsp_msb      base + 0xd 
#define fx_mod_addr     base + 0xe
#define fx_mod_data     base + 0xf 

	volatile int irq_ok;               /* set by interrupt handler */
	int opened;                        /* flag, holds open(1) mode */
	char debug;                        /* debugging flags */
	int freemem;                       /* installed RAM, in bytes */ 
	int synthdev;                      /* OSS minor devnum for synth */
	int mididev;                       /* OSS minor devno for internal MIDI */
	int ext_mididev;                   /* OSS minor devno for external MIDI */ 
	char fw_version[2];                /* major = [0], minor = [1] */
	char hw_version[2];                /* major = [0], minor = [1] */
	char israw;                        /* needs Motorola microcode */
	char prog_status[WF_MAX_PROGRAM];  /* WF_SLOT_* */
	char patch_status[WF_MAX_PATCH];   /* WF_SLOT_* */
	char sample_status[WF_MAX_SAMPLE]; /* WF_ST_* | WF_SLOT_* */
	int samples_used;                  /* how many */
	char interrupts_on;                /* h/w MPU interrupts enabled ? */
	char rom_samples_rdonly;           /* can we write on ROM samples */
	struct wait_queue *interrupt_sleeper; 
#ifdef  WF_STATS
	unsigned long status_found_during_loop;
	unsigned long status_found_during_sleep[4];
#endif  WF_STATS

} wf_config;

/* Note: because this module doesn't export any symbols, this really isn't
   a global variable, even if it looks like one. I was quite confused by
   this when I started writing this as a (newer) module -- pbd.
*/

static wf_config wavefront_configuration;

#define wavefront_status(hw) (inb (hw->status_port))

/* forward references */

static int wffx_ioctl (struct wf_config *, wavefront_fx_info *);
static int wffx_init (struct wf_config *hw);
static int wavefront_delete_sample (struct wf_config *hw, int sampnum);

typedef struct {
	int cmd;
	char *action;
	unsigned int read_cnt;
	unsigned int write_cnt;
	int need_ack;
} wavefront_command;

static struct {
	int errno;
	const char *errstr;
} wavefront_errors[] = {
	{ 0x01, "Bad sample number" },
	{ 0x02, "Out of sample memory" },
	{ 0x03, "Bad patch number" },
	{ 0x04, "Error in number of voices" },
	{ 0x06, "Sample load already in progress" },
	{ 0x0B, "No sample load request pending" },
	{ 0x0E, "Bad MIDI channel number" },
	{ 0x10, "Download Record Error" },
	{ 0x80, "Success" },
	{ 0x0, 0x0 }
};

#define NEEDS_ACK 1

static wavefront_command wavefront_commands[] = {
	{ WFC_SET_SYNTHVOL, "set synthesizer volume", 0, 1, NEEDS_ACK },
	{ WFC_GET_SYNTHVOL, "get synthesizer volume", 1, 0, 0},
	{ WFC_SET_NVOICES, "set number of voices", 0, 1, NEEDS_ACK },
	{ WFC_GET_NVOICES, "get number of voices", 1, 0, 0 },
	{ WFC_SET_TUNING, "set synthesizer tuning", 0, 2, NEEDS_ACK },
	{ WFC_GET_TUNING, "get synthesizer tuning", 2, 0, 0 },
	{ WFC_DISABLE_CHANNEL, "disable synth channel", 0, 1, NEEDS_ACK },
	{ WFC_ENABLE_CHANNEL, "enable synth channel", 0, 1, NEEDS_ACK },
	{ WFC_GET_CHANNEL_STATUS, "get synth channel status", 3, 0, 0 },
	{ WFC_MISYNTH_OFF, "disable midi-in to synth", 0, 0, NEEDS_ACK },
	{ WFC_MISYNTH_ON, "enable midi-in to synth", 0, 0, NEEDS_ACK },
	{ WFC_VMIDI_ON, "enable virtual midi mode", 0, 0, NEEDS_ACK },
	{ WFC_VMIDI_OFF, "disable virtual midi mode", 0, 0, NEEDS_ACK },
	{ WFC_MIDI_STATUS, "report midi status", 1, 0, 0 },
	{ WFC_FIRMWARE_VERSION, "report firmware version", 2, 0, 0 },
	{ WFC_HARDWARE_VERSION, "report hardware version", 2, 0, 0 },
	{ WFC_GET_NSAMPLES, "report number of samples", 2, 0, 0 },
	{ WFC_INSTOUT_LEVELS, "report instantaneous output levels", 7, 0, 0 },
	{ WFC_PEAKOUT_LEVELS, "report peak output levels", 7, 0, 0 },
	{ WFC_DOWNLOAD_SAMPLE, "download sample",
	  0, WF_SAMPLE_BYTES, NEEDS_ACK },
	{ WFC_DOWNLOAD_BLOCK, "download block", 0, 0, NEEDS_ACK},
	{ WFC_DOWNLOAD_SAMPLE_HEADER, "download sample header",
	  0, WF_SAMPLE_HDR_BYTES, NEEDS_ACK },
	{ WFC_UPLOAD_SAMPLE_HEADER, "upload sample header", 13, 2, 0 },

	/* This command requires a variable number of bytes to be written.
	   There is a hack in wavefront_cmd() to support this. The actual
	   count is passed in as the read buffer ptr, cast appropriately.
	   Ugh.
	*/

	{ WFC_DOWNLOAD_MULTISAMPLE, "download multisample", 0, 0, NEEDS_ACK },

	/* This one is a hack as well. We just read the first byte of the
	   response, don't fetch an ACK, and leave the rest to the 
	   calling function. Ugly, ugly, ugly.
	*/

	{ WFC_UPLOAD_MULTISAMPLE, "upload multisample", 2, 1, 0 },
	{ WFC_DOWNLOAD_SAMPLE_ALIAS, "download sample alias",
	  0, WF_ALIAS_BYTES, NEEDS_ACK },
	{ WFC_UPLOAD_SAMPLE_ALIAS, "upload sample alias", WF_ALIAS_BYTES, 2, 0},
	{ WFC_DELETE_SAMPLE, "delete sample", 0, 2, NEEDS_ACK },
	{ WFC_IDENTIFY_SAMPLE_TYPE, "identify sample type", 5, 2, 0 },
	{ WFC_UPLOAD_SAMPLE_PARAMS, "upload sample parameters" },
	{ WFC_REPORT_FREE_MEMORY, "report free memory", 4, 0, 0 },
	{ WFC_DOWNLOAD_PATCH, "download patch", 0, 134, NEEDS_ACK },
	{ WFC_UPLOAD_PATCH, "upload patch", 132, 2, 0 },
	{ WFC_DOWNLOAD_PROGRAM, "download program", 0, 33, NEEDS_ACK },
	{ WFC_UPLOAD_PROGRAM, "upload program", 32, 1, 0 },
	{ WFC_DOWNLOAD_EDRUM_PROGRAM, "download enhanced drum program", 0, 9,
	  NEEDS_ACK},
	{ WFC_UPLOAD_EDRUM_PROGRAM, "upload enhanced drum program", 8, 1, 0},
	{ WFC_SET_EDRUM_CHANNEL, "set enhanced drum program channel",
	  0, 1, NEEDS_ACK },
	{ WFC_DISABLE_DRUM_PROGRAM, "disable drum program", 0, 1, NEEDS_ACK },
	{ WFC_REPORT_CHANNEL_PROGRAMS, "report channel program numbers",
	  32, 0, 0 },
	{ WFC_NOOP, "the no-op command", 0, 0, NEEDS_ACK },
	{ 0x00 }
};

static const char *
wavefront_errorstr (int errnum)

{
	int i;

	for (i = 0; wavefront_errors[i].errstr; i++) {
		if (wavefront_errors[i].errno == errnum) {
			return wavefront_errors[i].errstr;
		}
	}

	return "Unknown WaveFront error";
}

static wavefront_command *
wavefront_get_command (int cmd) 

{
	int i;

	for (i = 0; wavefront_commands[i].cmd != 0; i++) {
		if (cmd == wavefront_commands[i].cmd) {
			return &wavefront_commands[i];
		}
	}

	return (wavefront_command *) 0;
}

static int
wavefront_sleep (wf_config *hw, int limit)

{
	current->timeout = jiffies + limit;
	current->state = TASK_INTERRUPTIBLE;
	schedule();
	current->timeout = 0;

	return signal_pending(current);
}
    
static int
wavefront_wait (wf_config *hw, int mask)

{
	int             i;
	static int      short_loop_cnt = 0;

	if (short_loop_cnt == 0) {
	    short_loop_cnt = (int) (((double) wait_usecs / 1000000.0) *
		(double) current_cpu_data.loops_per_sec);
	}

	for (i = 0; i < short_loop_cnt; i++) {
		if (wavefront_status(hw) & mask) {
#ifdef WF_STATS
		        hw->status_found_during_loop++;
#endif WF_STATS
			return 1;
		}
	}

	for (i = 0; i < sleep_tries; i++) {

		if (wavefront_status(hw) & mask) {
#ifdef WF_STATS
    		        if (i < 4) {
				hw->status_found_during_sleep[i]++;
			}
#endif WF_STATS
			return 1;
		}

		if (wavefront_sleep (hw, HZ/sleep_interval)) {
			return (0);
		}
	}

	return 0;
}

static int
wavefront_read (wf_config *hw)
{
	if (wavefront_wait (hw, STAT_CAN_READ))
		return inb (hw->data_port);

#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_DATA) {
		printk (KERN_DEBUG "WaveFront: read timeout.\n");
	}
#endif WF_DEBUG

	return -1;
}

static int
wavefront_write (wf_config *hw, unsigned char data)

{
	if (wavefront_wait (hw, STAT_CAN_WRITE)) {
		outb (data, hw->data_port);
		return 1;
	}

#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_DATA) {
		printk (KERN_DEBUG "WaveFront: write timeout.\n");
	}
#endif WF_DEBUG

	return 0;
}

static int
wavefront_cmd (wf_config *hw, int cmd,
	       unsigned char *rbuf,
	       unsigned char *wbuf)

{
	int ack;
	int i;
	int c;
	wavefront_command *wfcmd;

	if ((wfcmd = wavefront_get_command (cmd)) == (wavefront_command *) 0) {
		printk (KERN_WARNING "WaveFront: command 0x%x not supported.\n",
			cmd);
		return 1;
	}

	/* Hack to handle the one variable-size write command. See
	   wavefront_send_multisample() for the other half of this
	   gross and ugly strategy.
	*/

	if (cmd == WFC_DOWNLOAD_MULTISAMPLE) {
		wfcmd->write_cnt = (unsigned int) rbuf;
		rbuf = 0;
	}

#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_CMD) {
		printk (KERN_DEBUG "Wavefront: 0x%x [%s] (%d,%d,%d)\n",
			cmd, wfcmd->action, wfcmd->read_cnt, wfcmd->write_cnt,
			wfcmd->need_ack);
	}
#endif WF_DEBUG
    
	if (!wavefront_write (hw, cmd)) { 
#ifdef WF_DEBUG
		if (hw->debug & (WF_DEBUG_IO|WF_DEBUG_CMD)) {
			printk (KERN_DEBUG "WaveFront: cannot request "
				"0x%x [%s].\n",
				cmd, wfcmd->action);
		}
#endif WF_DEBUG
		return 1;
	} 

	if (wfcmd->write_cnt > 0) {
#ifdef WF_DEBUG
		if (hw->debug & WF_DEBUG_DATA) {
			printk (KERN_DEBUG "WaveFront: writing %d bytes "
				"for 0x%x\n",
				wfcmd->write_cnt, cmd);
		}
#endif WF_DEBUG

		for (i = 0; i < wfcmd->write_cnt; i++) {
			if (!wavefront_write (hw, wbuf[i])) {
#ifdef WF_DEBUG
				if (hw->debug & WF_DEBUG_IO) {
					printk (KERN_DEBUG
                           "WaveFront: bad write for byte %d of 0x%x [%s].\n",
						i, cmd, wfcmd->action);
				}
#endif WF_DEBUG
				return 1;
			}
#ifdef WF_DEBUG
			if (hw->debug & WF_DEBUG_DATA) {
				printk (KERN_DEBUG
                                        "WaveFront: write[%d] = 0x%x\n",
					i, wbuf[i]);
#endif WF_DEBUG
			}
		}
	}

	if (wfcmd->read_cnt > 0) {
#ifdef WF_DEBUG
		if (hw->debug & WF_DEBUG_DATA) {
			printk (KERN_DEBUG "WaveFront: reading %d ints "
				"for 0x%x\n",
				wfcmd->read_cnt, cmd);
		}
#endif WF_DEBUG

		for (i = 0; i < wfcmd->read_cnt; i++) {

			if ((c = wavefront_read(hw)) == -1) {
#ifdef WF_DEBUG
				if (hw->debug & WF_DEBUG_IO) {
					printk (KERN_DEBUG
                               "WaveFront: bad read for byte %d of 0x%x [%s].\n",
						i, cmd, wfcmd->action);
				}
#endif WF_DEBUG
				return 1;
			}

			/* Now handle errors. Lots of special cases here */
	    
			if (c == 0xff) { 
				if ((c = wavefront_read (hw)) == -1) {
#ifdef WF_DEBUG
					if (hw->debug & WF_DEBUG_IO) {
						printk (KERN_DEBUG
                                 "WaveFront: bad read for error byte at "
                                  "read byte %d of 0x%x [%s].\n",
							i, cmd, wfcmd->action);
					}
#endif WF_DEBUG
					return 1;
				}

				/* Can you believe this madness ? */

				if (c == 1 &&
				    wfcmd->cmd == WFC_IDENTIFY_SAMPLE_TYPE) {
					rbuf[0] = WF_ST_EMPTY;
					return (0);

				} else if (c == 3 &&
					   wfcmd->cmd == WFC_UPLOAD_PATCH) {

					return 3;

				} else if (c == 1 &&
					   wfcmd->cmd == WFC_UPLOAD_PROGRAM) {

					return 1;

				} else {

#ifdef WF_DEBUG
					if (hw->debug & WF_DEBUG_IO) {
						printk (KERN_DEBUG
                                            "WaveFront: error %d (%s) during "
                                                "read for byte "
					          "%d of 0x%x [%s].\n",
							c,
							wavefront_errorstr (c),
							i, cmd, wfcmd->action);
					}
#endif WF_DEBUG
					return 1;

				}
			} else {
				rbuf[i] = c;
			}

#ifdef WF_DEBUG
			if (hw->debug & WF_DEBUG_DATA) {
				printk (KERN_DEBUG
					"WaveFront: read[%d] = 0x%x\n",
					i, rbuf[i]);
			}
#endif WF_DEBUG
		}
	}

	if ((wfcmd->read_cnt == 0 && wfcmd->write_cnt == 0) || wfcmd->need_ack) {

#ifdef WF_DEBUG
		if (hw->debug & WF_DEBUG_CMD) {
			printk (KERN_DEBUG "WaveFront: reading ACK for 0x%x\n",
				cmd);
		}
#endif WF_DEBUG

		/* Some commands need an ACK, but return zero instead
		   of the standard value.
		*/
	    
		if ((ack = wavefront_read(hw)) == 0) {
			ack = WF_ACK;
		}
	
		if (ack != WF_ACK) {
			if (ack == -1) {
#ifdef WF_DEBUG
				if (hw->debug & WF_DEBUG_IO) {
					printk (KERN_DEBUG
                               "WaveFront: cannot read ack for 0x%x [%s].\n",
						cmd, wfcmd->action);
				}
#endif WF_DEBUG
				return 1;
		
			} else {
				int err = -1; /* something unknown */

				if (ack == 0xff) { /* explicit error */
		    
					if ((err = wavefront_read (hw)) == -1) {
#ifdef WF_DEBUG
						if (hw->debug & WF_DEBUG_DATA) {
							printk (KERN_DEBUG
                               "WaveFront: cannot read err for 0x%x [%s].\n",
                                                            cmd, wfcmd->action);
						}
#endif WF_DEBUG
					}
				}

#ifdef WF_DEBUG
				if (hw->debug & WF_DEBUG_IO) {
					printk (KERN_DEBUG
						"WaveFront: 0x%x [%s] "
						"failed (0x%x, 0x%x, %s)\n",
						cmd, wfcmd->action, ack, err,
						wavefront_errorstr (err));
				}
#endif WF_DEBUG
				return -err;
			} 
		}

#ifdef WF_DEBUG
		if (hw->debug & WF_DEBUG_DATA) {
			printk (KERN_DEBUG "WaveFront: ack received "
				"for 0x%x [%s]\n",
				cmd, wfcmd->action);
		}
#endif WF_DEBUG
	} else {
#ifdef WF_DEBUG
		if (hw->debug & WF_DEBUG_CMD) {
			printk (KERN_DEBUG 
				"Wavefront: 0x%x [%s] does not need "
				"ACK (%d,%d,%d)\n",
				cmd, wfcmd->action, wfcmd->read_cnt,
				wfcmd->write_cnt, wfcmd->need_ack);
#endif WF_DEBUG
		}
	}

	return 0;
	
}

/***********************************************************************
WaveFront: data munging   

Things here are wierd. All data written to the board cannot 
have its most significant bit set. Any data item with values 
potentially > 0x7F (127) must be split across multiple bytes.

Sometimes, we need to munge numeric values that are represented on
the x86 side as 8-32 bit values. Sometimes, we need to munge data
that is represented on the x86 side as an array of bytes. The most
efficient approach to handling both cases seems to be to use 2
different functions for munging and 2 for de-munging. This avoids
wierd casting and worrying about bit-level offsets.

**********************************************************************/

static 
unsigned char *
munge_int32 (unsigned int src,
	     unsigned char *dst,
	     unsigned int dst_size)
{
	int i;

	for (i = 0;i < dst_size; i++) {
		*dst = src & 0x7F;  /* Mask high bit of LSB */
		src = src >> 7;     /* Rotate Right 7 bits  */
	                            /* Note: we leave the upper bits in place */ 

		dst++;
 	};
	return dst;
};

static int 
demunge_int32 (unsigned char* src, int src_size)

{
	int i;
 	int outval = 0;
	
 	for (i = src_size - 1; i >= 0; i--) {
		outval=(outval<<7)+src[i];
	}

	return outval;
};

static 
unsigned char *
munge_buf (unsigned char *src, unsigned char *dst, unsigned int dst_size)

{
	int i;
	unsigned int last = dst_size / 2;

	for (i = 0; i < last; i++) {
		*dst++ = src[i] & 0x7f;
		*dst++ = src[i] >> 7;
	}
	return dst;
}

static 
unsigned char *
demunge_buf (unsigned char *src, unsigned char *dst, unsigned int src_bytes)

{
	int i;
	unsigned char *end = src + src_bytes;
    
	end = src + src_bytes;

	/* NOTE: src and dst *CAN* point to the same address */

	for (i = 0; src != end; i++) {
		dst[i] = *src++;
		dst[i] |= (*src++)<<7;
	}

	return dst;
}

/***********************************************************************
WaveFront: sample, patch and program management.
***********************************************************************/

static int
wavefront_delete_sample (wf_config *hw, int sample_num)

{
	unsigned char wbuf[2];
	int x;

	wbuf[0] = sample_num & 0x7f;
	wbuf[1] = sample_num >> 7;

	if ((x = wavefront_cmd (hw, WFC_DELETE_SAMPLE, 0, wbuf)) == 0) {
		hw->sample_status[sample_num] = WF_ST_EMPTY;
	}

	return x;
}

static int
wavefront_get_sample_status (struct wf_config *hw, int assume_rom)

{
	int i;
	unsigned char rbuf[32], wbuf[32];
	unsigned int    sc_real, sc_alias, sc_multi;

	/* check sample status */
    
	if (wavefront_cmd (hw, WFC_GET_NSAMPLES, rbuf, wbuf)) {
		printk ("WaveFront: cannot request sample count.\n");
	} 
    
	sc_real = sc_alias = sc_multi = hw->samples_used = 0;
    
	for (i = 0; i < WF_MAX_SAMPLE; i++) {
	
		wbuf[0] = i & 0x7f;
		wbuf[1] = i >> 7;

		if (wavefront_cmd (hw, WFC_IDENTIFY_SAMPLE_TYPE, rbuf, wbuf)) {
			printk (KERN_WARNING
				"WaveFront: cannot identify sample "
				"type of slot %d\n", i);
			hw->sample_status[i] = WF_ST_EMPTY;
			continue;
		}

		hw->sample_status[i] = (WF_SLOT_FILLED|rbuf[0]);

		if (assume_rom) {
			hw->sample_status[i] |= WF_SLOT_ROM;
		}

		switch (rbuf[0] & WF_ST_MASK) {
		case WF_ST_SAMPLE:
			sc_real++;
			break;
		case WF_ST_MULTISAMPLE:
			sc_multi++;
			break;
		case WF_ST_ALIAS:
			sc_alias++;
			break;
		case WF_ST_EMPTY:
			break;

		default:
			printk (KERN_WARNING
				"WaveFront: unknown sample type for "
				"slot %d (0x%x)\n", 
				i, rbuf[0]);
		}

		if (rbuf[0] != WF_ST_EMPTY) {
			hw->samples_used++;
		} 
	}

	printk (KERN_INFO
		"WaveFront: %d samples used (%d real, %d aliases, %d multi), "
		"%d empty\n", hw->samples_used, sc_real, sc_alias, sc_multi,
		WF_MAX_SAMPLE - hw->samples_used);


	return (0);

}

static int
wavefront_get_patch_status (struct wf_config *hw)
{
	unsigned char patchbuf[WF_PATCH_BYTES];
	unsigned char patchnum[2];
	wavefront_patch *p;
	int i, x, cnt, cnt2;

	for (i = 0; i < WF_MAX_PATCH; i++) {
		patchnum[0] = i & 0x7f;
		patchnum[1] = i >> 7;

		if ((x = wavefront_cmd (hw, WFC_UPLOAD_PATCH, patchbuf,
					patchnum)) == 0) {

			hw->patch_status[i] |= WF_SLOT_FILLED;
			p = (wavefront_patch *) patchbuf;
			hw->sample_status
				[p->sample_number|(p->sample_msb<<7)] |=
				WF_SLOT_USED;
	    
		} else if (x == 3) { /* Bad patch number */
			hw->patch_status[i] = 0;
		} else {
			printk (KERN_ERR "WaveFront: upload patch "
				"error 0x%x\n", x);
			hw->patch_status[i] = 0;
			return 1;
		}
	}

	/* program status has already filled in slot_used bits */

	for (i = 0, cnt = 0, cnt2 = 0; i < WF_MAX_PATCH; i++) {
		if (hw->patch_status[i] & WF_SLOT_FILLED) {
			cnt++;
		}
		if (hw->patch_status[i] & WF_SLOT_USED) {
			cnt2++;
		}
	
	}
	printk (KERN_INFO
		"WaveFront: %d patch slots filled, %d in use\n", cnt, cnt2);

	return (0);
}

static int
wavefront_get_program_status (struct wf_config *hw)
{
	unsigned char progbuf[WF_PROGRAM_BYTES];
	wavefront_program prog;
	unsigned char prognum;
	int i, x, l, cnt;

	for (i = 0; i < WF_MAX_PROGRAM; i++) {
		prognum = i;

		if ((x = wavefront_cmd (hw, WFC_UPLOAD_PROGRAM, progbuf,
					&prognum)) == 0) {

			hw->prog_status[i] |= WF_SLOT_USED;

			demunge_buf (progbuf, (unsigned char *) &prog,
				     WF_PROGRAM_BYTES);

			for (l = 0; l < WF_NUM_LAYERS; l++) {
				if (prog.layer[l].mute) {
					hw->patch_status
						[prog.layer[l].patch_number] |=
						WF_SLOT_USED;
				}
			}
		} else if (x == 1) { /* Bad program number */
			hw->prog_status[i] = 0;
		} else {
			printk (KERN_ERR "WaveFront: upload program "
				"error 0x%x\n", x);
			hw->prog_status[i] = 0;
		}
	}

	for (i = 0, cnt = 0; i < WF_MAX_PROGRAM; i++) {
		if (hw->prog_status[i]) {
			cnt++;
		}
	}

	printk (KERN_INFO "WaveFront: %d programs slots in use\n", cnt);

	return (0);
}

static int
wavefront_send_patch (wf_config *hw, 
		      wavefront_patch_info *header)

{
	unsigned char buf[WF_PATCH_BYTES+2];
	unsigned char *bptr;

#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_LOAD_PATCH) {
		printk (KERN_DEBUG "WaveFront: downloading patch %d\n",
			header->number);
	}
#endif WF_DEBUG

	hw->patch_status[header->number] |= WF_SLOT_FILLED;

	bptr = buf;
	bptr = munge_int32 (header->number, buf, 2);
	munge_buf ((unsigned char *)&header->hdr.p, bptr, WF_PATCH_BYTES);
    
	if (wavefront_cmd (hw, WFC_DOWNLOAD_PATCH, 0, buf)) {
		printk (KERN_ERR "WaveFront: download patch failed\n");
		return -(EIO);
	}

	return (0);
}

static int
wavefront_send_program (wf_config *hw, 
			wavefront_patch_info *header)

{
	unsigned char buf[WF_PROGRAM_BYTES+1];
	int i;

#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_LOAD_PATCH) {
		printk (KERN_DEBUG
			"WaveFront: downloading program %d\n", header->number);
	}
#endif WF_DEBUG

	hw->prog_status[header->number] = WF_SLOT_USED;

	/* XXX need to zero existing SLOT_USED bit for program_status[i]
	   where `i' is the program that's being (potentially) overwritten.
	*/
    
	for (i = 0; i < WF_NUM_LAYERS; i++) {
		if (header->hdr.pr.layer[i].mute) {
			hw->patch_status[header->hdr.pr.layer[i].patch_number] |=
				WF_SLOT_USED;

			/* XXX need to mark SLOT_USED for sample used by
			   patch_number, but this means we have to load it. Ick.
			*/
		}
	}

	buf[0] = header->number;
	munge_buf ((unsigned char *)&header->hdr.pr, &buf[1], WF_PROGRAM_BYTES);
    
	if (wavefront_cmd (hw, WFC_DOWNLOAD_PROGRAM, 0, buf)) {
		printk (KERN_WARNING "WaveFront: download patch failed\n");	
		return -(EIO);
	}

	return (0);
}

static int
wavefront_freemem (wf_config *hw)

{
	char rbuf[8];

	if (wavefront_cmd (hw, WFC_REPORT_FREE_MEMORY, rbuf, 0)) {
		printk (KERN_WARNING "WaveFront: can't get memory stats.\n");
		return -1;
	} else {
		return demunge_int32 (rbuf, 4);
	}
}

static int
wavefront_send_sample (wf_config      *hw,
		       wavefront_patch_info *header,
		       UINT16 *dataptr,
		       int data_is_unsigned)

{
	/* samples are downloaded via a 16-bit wide i/o port
	   (you could think of it as 2 adjacent 8-bit wide ports
	   but its less efficient that way). therefore, all
	   the blocksizes and so forth listed in the documentation,
	   and used conventionally to refer to sample sizes,
	   which are given in 8-bit units (bytes), need to be
	   divided by 2.
        */

	UINT16 sample_short;
	UINT32 length;
	UINT16 *data_end = 0;
	unsigned int i;
	const int max_blksize = 4096/2;
	unsigned int written;
	unsigned int blocksize;
	int dma_ack;
	int blocknum;
	unsigned char sample_hdr[WF_SAMPLE_HDR_BYTES];
	unsigned char *shptr;
	int skip = 0;
	int initial_skip = 0;

#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_LOAD_PATCH) {
		printk (KERN_DEBUG "WaveFront: sample %sdownload for slot %d, "
			"type %d, %d bytes from 0x%x\n",
			header->size ? "" : "header ", 
			header->number, header->subkey, header->size,
			(int) header->dataptr);
	}
#endif WF_DEBUG

	if (header->size) {

		/* XXX its a debatable point whether or not RDONLY semantics
		   on the ROM samples should cover just the sample data or
		   the sample header. For now, it only covers the sample data,
		   so anyone is free at all times to rewrite sample headers.

		   My reason for this is that we have the sample headers
		   available in the WFB file for General MIDI, and so these
		   can always be reset if needed. The sample data, however,
		   cannot be recovered without a complete reset and firmware
		   reload of the ICS2115, which is a very expensive operation.

		   So, doing things this way allows us to honor the notion of
		   "RESETSAMPLES" reasonably cheaply. Note however, that this
		   is done purely at user level: there is no WFB parser in
		   this driver, and so a complete reset (back to General MIDI,
		   or theoretically some other configuration) is the
		   responsibility of the user level library. 

		   To try to do this in the kernel would be a little
		   crazy: we'd need 158K of kernel space just to hold
		   a copy of the patch/program/sample header data.
		*/

		if (hw->rom_samples_rdonly) {
			if (hw->sample_status[header->number] & WF_SLOT_ROM) {
				printk (KERN_ERR "WaveFront: sample slot %d "
					"write protected\n",
					header->number);
				return -EACCES;
			}
		}

		wavefront_delete_sample (hw, header->number);
	}

	if (header->size) {
		hw->freemem = wavefront_freemem (hw);

		if (hw->freemem < header->size) {
			printk (KERN_ERR
				"WaveFront: insufficient memory to "
				"load %d byte sample.\n",
				header->size);
			return -ENOMEM;
		}
	
	}

	skip = WF_GET_CHANNEL(&header->hdr.s);

	if (skip > 0) {
		switch (header->hdr.s.SampleResolution) {
		case LINEAR_16BIT:
			break;
		default:
			printk (KERN_ERR
				"WaveFront: channel selection only possible "
				"on 16-bit samples");
			return -(EINVAL);
		}
	}

	switch (skip) {
	case 0:
		initial_skip = 0;
		skip = 1;
		break;
	case 1:
		initial_skip = 0;
		skip = 2;
		break;
	case 2:
		initial_skip = 1;
		skip = 2;
		break;
	case 3:
		initial_skip = 2;
		skip = 3;
		break;
	case 4:
		initial_skip = 3;
		skip = 4;
		break;
	case 5:
		initial_skip = 4;
		skip = 5;
		break;
	case 6:
		initial_skip = 5;
		skip = 6;
		break;
	}

#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_LOAD_PATCH) {
		printk (KERN_DEBUG "WaveFront: channel selection: %d => "
			"initial skip = %d, skip = %d\n",
			WF_GET_CHANNEL (&header->hdr.s), initial_skip, skip);
	}
#endif WF_DEBUG
    
	/* Be safe, and zero the "Unused" bits ... */

	WF_SET_CHANNEL(&header->hdr.s, 0);

	/* adjust size for 16 bit samples by dividing by two.  We always
	   send 16 bits per write, even for 8 bit samples, so the length
	   is always half the size of the sample data in bytes.
	*/

	length = header->size / 2;

	/* the data we're sent has not been munged, and in fact, the
	   header we have to send isn't just a munged copy either.
	   so, build the sample header right here.
	*/

	shptr = &sample_hdr[0];

	shptr = munge_int32 (header->number, shptr, 2);

	if (header->size) {
		shptr = munge_int32 (length, shptr, 4);
	}

	/* Yes, a 4 byte result doesn't contain all of the offset bits,
	   but the offset only uses 24 bits.
	*/

	shptr = munge_int32 (*((UINT32 *) &header->hdr.s.sampleStartOffset),
			     shptr, 4);
	shptr = munge_int32 (*((UINT32 *) &header->hdr.s.loopStartOffset),
			     shptr, 4);
	shptr = munge_int32 (*((UINT32 *) &header->hdr.s.loopEndOffset),
			     shptr, 4);
	shptr = munge_int32 (*((UINT32 *) &header->hdr.s.sampleEndOffset),
			     shptr, 4);
	
	/* This one is truly wierd. What kind of wierdo decided that in
	   a system dominated by 16 and 32 bit integers, they would use
	   a just 12 bits ?
	*/
	
	shptr = munge_int32 (header->hdr.s.FrequencyBias, shptr, 3);
	
	/* Why is this nybblified, when the MSB is *always* zero ? 
	   Anyway, we can't take address of bitfield, so make a
	   good-faith guess at where it starts.
	*/
	
	shptr = munge_int32 (*(&header->hdr.s.FrequencyBias+1),
			     shptr, 2);

	if (wavefront_cmd (hw, header->size ?
			   WFC_DOWNLOAD_SAMPLE : WFC_DOWNLOAD_SAMPLE_HEADER,
			   0, sample_hdr)) {
		printk (KERN_WARNING "WaveFront: sample %sdownload refused.\n",
			header->size ? "" : "header ");
		return -(EIO);
	}

	if (header->size == 0) {
		goto sent; /* Sorry. Just had to have one somewhere */
	}
    
	data_end = dataptr + length;

	/* Do any initial skip over an unused channel's data */

	dataptr += initial_skip;
    
	for (written = 0, blocknum = 0;
	     written < length; written += max_blksize, blocknum++) {
	
		if ((length - written) > max_blksize) {
			blocksize = max_blksize;
		} else {
			/* round to nearest 16-byte value */
			blocksize = ((length-written+7)&~0x7);
		}

		if (wavefront_cmd (hw, WFC_DOWNLOAD_BLOCK, 0, 0)) {
			printk (KERN_WARNING "WaveFront: download block "
				"request refused.\n");
			return -(EIO);
		}

		for (i = 0; i < blocksize; i++) {

			if (dataptr < data_end) {
		
				get_user (sample_short, dataptr);
				dataptr += skip;
		
				if (data_is_unsigned) { /* GUS ? */

					if (WF_SAMPLE_IS_8BIT(&header->hdr.s)) {
			
						/* 8 bit sample
						 resolution, sign
						 extend both bytes.
						*/
			
						((unsigned char*)
						 &sample_short)[0] += 0x7f;
						((unsigned char*)
						 &sample_short)[1] += 0x7f;
			
					} else {
			
						/* 16 bit sample
						 resolution, sign
						 extend the MSB.
						*/
			
						sample_short += 0x7fff;
					}
				}

			} else {

				/* In padding section of final block:

				   Don't fetch unsupplied data from
				   user space, just continue with
				   whatever the final value was.
				*/
			}
	    
			if (i < blocksize - 1) {
				outw (sample_short, hw->block_port);
			} else {
				outw (sample_short, hw->last_block_port);
			}
		}

		/* Get "DMA page acknowledge" */
	
		if ((dma_ack = wavefront_read (hw)) != WF_DMA_ACK) {
			if (dma_ack == -1) {
				printk (KERN_ERR "WaveFront: upload sample "
					"DMA ack timeout\n");
				return -(EIO);
			} else {
				printk (KERN_ERR "WaveFront: upload sample "
					"DMA ack error 0x%x\n",
					dma_ack);
				return -(EIO);
			}
		}
	}

	hw->sample_status[header->number] = (WF_SLOT_FILLED|WF_ST_SAMPLE);

	/* Note, label is here because sending the sample header shouldn't
	   alter the sample_status info at all.
	*/

 sent:
	return (0);
}

static int
wavefront_send_alias (struct wf_config *hw, 
		      wavefront_patch_info *header)

{
	unsigned char alias_hdr[WF_ALIAS_BYTES];

#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_LOAD_PATCH) {
		printk (KERN_DEBUG "WaveFront: download alias, %d is "
			"alias for %d\n",
			header->number,
			header->hdr.a.OriginalSample);
	}
#endif WF_DEBUG
    
	munge_int32 (header->number, &alias_hdr[0], 2);
	munge_int32 (header->hdr.a.OriginalSample, &alias_hdr[2], 2);
	munge_int32 (*((unsigned int *)&header->hdr.a.sampleStartOffset),
		     &alias_hdr[4], 4);
	munge_int32 (*((unsigned int *)&header->hdr.a.loopStartOffset),
		     &alias_hdr[8], 4);
	munge_int32 (*((unsigned int *)&header->hdr.a.loopEndOffset),
		     &alias_hdr[12], 4);
	munge_int32 (*((unsigned int *)&header->hdr.a.sampleEndOffset),
		     &alias_hdr[16], 4);
	munge_int32 (header->hdr.a.FrequencyBias, &alias_hdr[20], 3);
	munge_int32 (*(&header->hdr.a.FrequencyBias+1), &alias_hdr[23], 2);

	if (wavefront_cmd (hw, WFC_DOWNLOAD_SAMPLE_ALIAS, 0, alias_hdr)) {
		printk (KERN_ERR "WaveFront: download alias failed.\n");
		return -(EIO);
	}

	hw->sample_status[header->number] = (WF_SLOT_FILLED|WF_ST_ALIAS);

	return (0);
}

static int
wavefront_send_multisample (struct wf_config *hw,
			    wavefront_patch_info *header)
{
	int i;
	int num_samples;
	unsigned char msample_hdr[WF_MSAMPLE_BYTES];

	munge_int32 (header->number, &msample_hdr[0], 2);

	/* You'll recall at this point that the "number of samples" value
	   in a wavefront_multisample struct is actually the log2 of the
	   real number of samples.
	*/

	num_samples = (1<<(header->hdr.ms.NumberOfSamples&7));
	msample_hdr[2] = (unsigned char) header->hdr.ms.NumberOfSamples;

#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_LOAD_PATCH) {
		printk (KERN_DEBUG "WaveFront: multi %d with %d=%d samples\n",
			header->number, header->hdr.ms.NumberOfSamples, num_samples);
	}
#endif WF_DEBUG

	for (i = 0; i < num_samples; i++) {
#ifdef WF_DEBUG
		if ((hw->debug & (WF_DEBUG_LOAD_PATCH|WF_DEBUG_DATA)) ==
		    (WF_DEBUG_LOAD_PATCH|WF_DEBUG_DATA)) {
			printk (KERN_DEBUG "WaveFront: sample[%d] = %d\n",
				i, header->hdr.ms.SampleNumber[i]);
		}
#endif WF_DEBUG
		munge_int32 (header->hdr.ms.SampleNumber[i],
			     &msample_hdr[3+(i*2)], 2);
	}
    
	/* Need a hack here to pass in the number of bytes
	   to be written to the synth. This is ugly, and perhaps
	   one day, I'll fix it.
	*/

	if (wavefront_cmd (hw, WFC_DOWNLOAD_MULTISAMPLE, 
			   (unsigned char *) ((num_samples*2)+3),
			   msample_hdr)) {
		printk (KERN_ERR "WaveFront: download of multisample failed.\n");
		return -(EIO);
	}

	hw->sample_status[header->number] = (WF_SLOT_FILLED|WF_ST_MULTISAMPLE);

	return (0);
}

static int
wavefront_fetch_multisample (struct wf_config *hw,
			     wavefront_patch_info *header)
{
	int i;
	unsigned char log_ns[1];
	unsigned char number[2];
	int num_samples;

	munge_int32 (header->number, number, 2);
    
	if (wavefront_cmd (hw, WFC_UPLOAD_MULTISAMPLE, log_ns, number)) {
		printk (KERN_ERR "WaveFront: upload multisample failed.\n");
		return -(EIO);
	}
    
#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_DATA) {
		printk (KERN_DEBUG "WaveFront: msample %d has %d samples\n",
			header->number, log_ns[0]);
	}
#endif WF_DEBUG

	header->hdr.ms.NumberOfSamples = log_ns[0];

	/* get the number of samples ... */

	num_samples = (1 << log_ns[0]);
    
	for (i = 0; i < num_samples; i++) {
		char d[2];
	
		if ((d[0] = wavefront_read (hw)) == -1) {
			printk (KERN_ERR "WaveFront: upload multisample failed "
				"during sample loop.\n");
			return -(EIO);
		}

		if ((d[1] = wavefront_read (hw)) == -1) {
			printk (KERN_ERR "WaveFront: upload multisample failed "
				"during sample loop.\n");
			return -(EIO);
		}
	
		header->hdr.ms.SampleNumber[i] =
			demunge_int32 ((unsigned char *) d, 2);
	
#ifdef WF_DEBUG
		if (hw->debug & WF_DEBUG_DATA) {
			printk (KERN_DEBUG "WaveFront: msample "
				"sample[%d] = %d\n",
				i, header->hdr.ms.SampleNumber[i]);
		}
#endif WF_DEBUG
	}

	return (0);
}


static int
wavefront_send_drum (struct wf_config *hw, wavefront_patch_info *header)

{
	unsigned char drumbuf[WF_DRUM_BYTES];
	wavefront_drum *drum = &header->hdr.d;
	int i;

#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_LOAD_PATCH) {
		printk (KERN_DEBUG
			"WaveFront: downloading edrum for MIDI "
			"note %d, patch = %d\n", 
			header->number, drum->PatchNumber);
	}
#endif WF_DEBUG

	drumbuf[0] = header->number & 0x7f;

	for (i = 0; i < 4; i++) {
		munge_int32 (((unsigned char *)drum)[i], &drumbuf[1+(i*2)], 2);
	}

	if (wavefront_cmd (hw, WFC_DOWNLOAD_EDRUM_PROGRAM, 0, drumbuf)) {
		printk (KERN_ERR "WaveFront: download drum failed.\n");
		return -(EIO);
	}

	return (0);
}

static int 
wavefront_find_free_sample (struct wf_config *hw)

{
	int i;

	for (i = 0; i < WF_MAX_SAMPLE; i++) {
		if (!(hw->sample_status[i] & WF_SLOT_FILLED)) {
			return i;
		}
	}
	printk (KERN_WARNING "WaveFront: no free sample slots!\n");
	return -1;
}

static int 
wavefront_find_free_patch (struct wf_config *hw)

{
	int i;

	for (i = 0; i < WF_MAX_PATCH; i++) {
		if (!(hw->patch_status[i] & WF_SLOT_FILLED)) {
			return i;
		}
	}
	printk (KERN_WARNING "WaveFront: no free patch slots!\n");
	return -1;
}

static int 
log2_2048(int n)

{
	int tbl[]={0, 0, 2048, 3246, 4096, 4755, 5294, 5749, 6143,
		   6492, 6803, 7084, 7342, 7578, 7797, 8001, 8192,
		   8371, 8540, 8699, 8851, 8995, 9132, 9264, 9390,
		   9510, 9626, 9738, 9845, 9949, 10049, 10146};
	int i;

	/* Returns 2048*log2(n) */

	/* FIXME: this is like doing integer math
	   on quantum particles (RuN) */

	i=0;
	while(n>=32*256) {
		n>>=8;
		i+=2048*8;
	}
	while(n>=32) {
		n>>=1;
		i+=2048;
	}
	i+=tbl[n];
	return(i);
}

static int
wavefront_load_gus_patch (struct wf_config *hw,
			  int dev, int format, const char *addr,
			  int offs, int count, int pmgr_flag)
{
	struct patch_info guspatch;
	wavefront_patch_info samp, pat, prog;
	wavefront_patch *patp;
	wavefront_sample *sampp;
	wavefront_program *progp;

	int i,base_note;
	long sizeof_patch;

	/* Copy in the header of the GUS patch */

	sizeof_patch = (long) &guspatch.data[0] - (long) &guspatch; 
	copy_from_user (&((char *) &guspatch)[offs],
			&(addr)[offs], sizeof_patch - offs);

	if ((i = wavefront_find_free_patch (hw)) == -1) {
		return -EBUSY;
	}
	pat.number = i;
	pat.subkey = WF_ST_PATCH;
	patp = &pat.hdr.p;

	if ((i = wavefront_find_free_sample (hw)) == -1) {
		return -EBUSY;
	}
	samp.number = i;
	samp.subkey = WF_ST_SAMPLE;
	samp.size = guspatch.len;
	sampp = &samp.hdr.s;

	prog.number = guspatch.instr_no;
	progp = &prog.hdr.pr;

	/* Setup the patch structure */

	patp->amplitude_bias=guspatch.volume;
	patp->portamento=0;
	patp->sample_number= samp.number & 0xff;
	patp->sample_msb= samp.number>>8;
	patp->pitch_bend= /*12*/ 0;
	patp->mono=1;
	patp->retrigger=1;
	patp->nohold=(guspatch.mode & WAVE_SUSTAIN_ON) ? 0:1;
	patp->frequency_bias=0;
	patp->restart=0;
	patp->reuse=0;
	patp->reset_lfo=1;
	patp->fm_src2=0;
	patp->fm_src1=WF_MOD_MOD_WHEEL;
	patp->am_src=WF_MOD_PRESSURE;
	patp->am_amount=127;
	patp->fc1_mod_amount=0;
	patp->fc2_mod_amount=0; 
	patp->fm_amount1=0;
	patp->fm_amount2=0;
	patp->envelope1.attack_level=127;
	patp->envelope1.decay1_level=127;
	patp->envelope1.decay2_level=127;
	patp->envelope1.sustain_level=127;
	patp->envelope1.release_level=0;
	patp->envelope2.attack_velocity=127;
	patp->envelope2.attack_level=127;
	patp->envelope2.decay1_level=127;
	patp->envelope2.decay2_level=127;
	patp->envelope2.sustain_level=127;
	patp->envelope2.release_level=0;
	patp->envelope2.attack_velocity=127;
	patp->randomizer=0;

	/* Program for this patch */

	progp->layer[0].patch_number= pat.number; /* XXX is this right ? */
	progp->layer[0].mute=1;
	progp->layer[0].pan_or_mod=1;
	progp->layer[0].pan=7;
	progp->layer[0].mix_level=127  /* guspatch.volume */;
	progp->layer[0].split_type=0;
	progp->layer[0].split_point=0;
	progp->layer[0].updown=0;

	for (i = 1; i < 4; i++) {
		progp->layer[i].mute=0;
	}

	/* Sample data */

	sampp->SampleResolution=((~guspatch.mode & WAVE_16_BITS)<<1);

	for (base_note=0;
	     note_to_freq (base_note) < guspatch.base_note;
	     base_note++);

	if ((guspatch.base_note-note_to_freq(base_note))
	    >(note_to_freq(base_note)-guspatch.base_note))
		base_note++;

	printk(KERN_DEBUG "ref freq=%d,base note=%d\n",
	       guspatch.base_freq,
	       base_note);

	sampp->FrequencyBias = (29550 - log2_2048(guspatch.base_freq)
				+ base_note*171);
	printk(KERN_DEBUG "Freq Bias is %d\n", sampp->FrequencyBias);
	sampp->Loop=(guspatch.mode & WAVE_LOOPING) ? 1:0;
	sampp->sampleStartOffset.Fraction=0;
	sampp->sampleStartOffset.Integer=0;
	sampp->loopStartOffset.Fraction=0;
	sampp->loopStartOffset.Integer=guspatch.loop_start
		>>((guspatch.mode&WAVE_16_BITS) ? 1:0);
	sampp->loopEndOffset.Fraction=0;
	sampp->loopEndOffset.Integer=guspatch.loop_end
		>>((guspatch.mode&WAVE_16_BITS) ? 1:0);
	sampp->sampleEndOffset.Fraction=0;
	sampp->sampleEndOffset.Integer=guspatch.len >> (guspatch.mode&1);
	sampp->Bidirectional=(guspatch.mode&WAVE_BIDIR_LOOP) ? 1:0;
	sampp->Reverse=(guspatch.mode&WAVE_LOOP_BACK) ? 1:0;

	/* Now ship it down */

	wavefront_send_sample (hw, &samp, 
			       (unsigned short *) &(addr)[sizeof_patch],
			       (guspatch.mode & WAVE_UNSIGNED) ? 1:0);
	wavefront_send_patch (hw, &pat);
	wavefront_send_program (hw, &prog);

	/* Now pan as best we can ... use the slave/internal MIDI device
	   number if it exists (since it talks to the WaveFront), or the
	   master otherwise.
	*/

#ifdef CONFIG_MIDI
	if (hw->mididev > 0) {
		midi_synth_controller (hw->mididev, guspatch.instr_no, 10,
				       ((guspatch.panning << 4) > 127) ?
				       127 : (guspatch.panning << 4));
	}
#endif CONFIG_MIDI

	return(0);
}

int
wavefront_load_patch (int dev, int format, const char *addr,
		      int offs, int count, int pmgr_flag)
{

	struct wf_config *hw = &wavefront_configuration;
	wavefront_patch_info header;

	if (format == SYSEX_PATCH) {	/* Handled by midi_synth.c */
		if (midi_load_patch == NULL) {
			printk (KERN_ERR
				"WaveFront: SYSEX not loadable: "
				"no midi patch loader!\n");
			return -(EINVAL);
		}
		return midi_load_patch (dev, format, addr,
					offs, count, pmgr_flag);

	} else if (format == GUS_PATCH) {
		return wavefront_load_gus_patch (hw, dev, format,
						 addr, offs, count, pmgr_flag);

	} else if (format != WAVEFRONT_PATCH) {
		printk (KERN_ERR "WaveFront: unknown patch format %d\n", format);
		return -(EINVAL);
	}

	if (count < sizeof (wavefront_patch_info)) {
		printk (KERN_ERR "WaveFront: sample header too short\n");
		return -(EINVAL);
	}

	/* copied in so far: `offs' bytes from `addr'. We shouldn't copy
	   them in again, and they correspond to header->key and header->devno.
	   So now, copy the rest of the wavefront_patch_info struct, except
	   for the 'hdr' field, since this is handled via indirection
	   through the 'hdrptr' field.
	*/

	copy_from_user (&((char *) &header)[offs], &(addr)[offs],
			sizeof(wavefront_patch_info) -
			sizeof(wavefront_any) - offs);

#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_LOAD_PATCH) {
		printk (KERN_DEBUG "WaveFront: download "
			"Sample type: %d "
			"Sample number: %d "
			"Sample size: %d\n",
			header.subkey,
			header.number,
			header.size);
	}
#endif WF_DEBUG

	switch (header.subkey) {
	case WF_ST_SAMPLE:  /* sample or sample_header, based on patch->size */

		copy_from_user ((unsigned char *) &header.hdr.s,
				(unsigned char *) header.hdrptr,
				sizeof (wavefront_sample));

		return wavefront_send_sample (hw, &header, header.dataptr, 0);

	case WF_ST_MULTISAMPLE:

		copy_from_user ((unsigned char *) &header.hdr.s,
				(unsigned char *) header.hdrptr,
				sizeof (wavefront_multisample));

		return wavefront_send_multisample (hw, &header);


	case WF_ST_ALIAS:

		copy_from_user ((unsigned char *) &header.hdr.a,
				(unsigned char *) header.hdrptr,
				sizeof (wavefront_alias));

		return wavefront_send_alias (hw, &header);

	case WF_ST_DRUM:
		copy_from_user ((unsigned char *) &header.hdr.d, 
				(unsigned char *) header.hdrptr,
				sizeof (wavefront_drum));

		return wavefront_send_drum (hw, &header);

	case WF_ST_PATCH:
		copy_from_user ((unsigned char *) &header.hdr.p, 
				(unsigned char *) header.hdrptr,
				sizeof (wavefront_patch));

		return wavefront_send_patch (hw, &header);

	case WF_ST_PROGRAM:
		copy_from_user ((unsigned char *) &header.hdr.pr, 
				(unsigned char *) header.hdrptr,
				sizeof (wavefront_program));

		return wavefront_send_program (hw, &header);

	default:
		printk (KERN_ERR "WaveFront: unknown patch type %d.\n",
			header.subkey);
		return -(EINVAL);
	}

	return 0;
}

/***********************************************************************
WaveFront: /dev/sequencer{,2} and other hardware-dependent interfaces
***********************************************************************/

static void
process_sample_hdr (UCHAR8 *buf)

{
	wavefront_sample s;
	UCHAR8 *ptr;

	ptr = buf;

	/* The board doesn't send us an exact copy of a "wavefront_sample"
	   in response to an Upload Sample Header command. Instead, we 
	   have to convert the data format back into our data structure,
	   just as in the Download Sample command, where we have to do
	   something very similar in the reverse direction.
	*/

	*((UINT32 *) &s.sampleStartOffset) = demunge_int32 (ptr, 4); ptr += 4;
	*((UINT32 *) &s.loopStartOffset) = demunge_int32 (ptr, 4); ptr += 4;
	*((UINT32 *) &s.loopEndOffset) = demunge_int32 (ptr, 4); ptr += 4;
	*((UINT32 *) &s.sampleEndOffset) = demunge_int32 (ptr, 4); ptr += 4;
	*((UINT32 *) &s.FrequencyBias) = demunge_int32 (ptr, 3); ptr += 3;

	s.SampleResolution = *ptr & 0x3;
	s.Loop = *ptr & 0x8;
	s.Bidirectional = *ptr & 0x10;
	s.Reverse = *ptr & 0x40;

	/* Now copy it back to where it came from */

	memcpy (buf, (unsigned char *) &s, sizeof (wavefront_sample));
}

static int
wavefront_synth_control (int dev, int cmd, caddr_t arg)

{
	struct wf_config *hw = &wavefront_configuration;
	wavefront_control wc;
	unsigned char patchnumbuf[2];
	int i;

	copy_from_user (&wc, arg, sizeof (wc));

#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_CMD) {
		printk (KERN_DEBUG "WaveFront: synth control with "
			"cmd 0x%x\n", wc.cmd);
	}
#endif WF_DEBUG

	/* special case handling of or for various commands */

	switch (wc.cmd) {
	case WFC_DISABLE_INTERRUPTS:
		printk (KERN_INFO "WaveFront: interrupts disabled.\n");
		outb (0x80|0x20, hw->control_port);
		hw->interrupts_on = 0;
		return 0;

	case WFC_ENABLE_INTERRUPTS:
		printk (KERN_INFO "WaveFront: interrupts enabled.\n");
		outb (0x80|0x20|0x40, hw->control_port);
		hw->interrupts_on = 1;
		return 0;

	case WFC_INTERRUPT_STATUS:
		wc.rbuf[0] = hw->interrupts_on;
		return 0;

	case WFC_ROMSAMPLES_RDONLY:
		hw->rom_samples_rdonly = wc.wbuf[0];
		wc.status = 0;
		return 0;

	case WFC_IDENTIFY_SLOT_TYPE:
		i = wc.wbuf[0] | (wc.wbuf[1] << 7);
		if (i <0 || i >= WF_MAX_SAMPLE) {
			printk (KERN_WARNING "WaveFront: invalid slot ID %d\n",
				i);
			wc.status = EINVAL;
			return 0;
		}
		wc.rbuf[0] = hw->sample_status[i];
		wc.status = 0;
		return 0;

	case WFC_DEBUG_DRIVER:
		hw->debug = wc.wbuf[0];
		printk (KERN_INFO "WaveFront: debug = 0x%x\n", hw->debug);
		return 0;

	case WFC_FX_IOCTL:
		wffx_ioctl (hw, (wavefront_fx_info *) &wc.wbuf[0]);
		return 0;

	case WFC_UPLOAD_PATCH:
		munge_int32 (*((UINT32 *) wc.wbuf), patchnumbuf, 2);
		memcpy (wc.wbuf, patchnumbuf, 2);
		break;

	case WFC_UPLOAD_MULTISAMPLE:
	case WFC_UPLOAD_SAMPLE_ALIAS:
		printk (KERN_INFO "WaveFront: support for various uploads "
			"being considered.\n");
		wc.status = EINVAL;
		return -EINVAL;
	}

	wc.status = wavefront_cmd (hw, wc.cmd, wc.rbuf, wc.wbuf);

	/* Special case handling of certain commands.

	   In particular, if the command was an upload, demunge the data
	   so that the user-level doesn't have to think about it.
	*/

	if (wc.status == 0) {
		switch (wc.cmd) {
			/* intercept any freemem requests so that we know
			   we are always current with the user-level view
			   of things.
			*/

		case WFC_REPORT_FREE_MEMORY:
			hw->freemem = demunge_int32 (wc.rbuf, 4);
			break;

		case WFC_UPLOAD_PATCH:
			demunge_buf (wc.rbuf, wc.rbuf, WF_PATCH_BYTES);
			break;

		case WFC_UPLOAD_PROGRAM:
			demunge_buf (wc.rbuf, wc.rbuf, WF_PROGRAM_BYTES);
			break;

		case WFC_UPLOAD_EDRUM_PROGRAM:
			demunge_buf (wc.rbuf, wc.rbuf, WF_DRUM_BYTES - 1);
			break;

		case WFC_UPLOAD_SAMPLE_HEADER:
			process_sample_hdr (wc.rbuf);
			break;

		case WFC_UPLOAD_MULTISAMPLE:
		case WFC_UPLOAD_SAMPLE_ALIAS:
			printk (KERN_INFO "WaveFront: support for "
				"various uploads "
				"being considered.\n");
			break;

		case WFC_VMIDI_OFF:
			virtual_midi_disable (hw->mididev);
			break;

		case WFC_VMIDI_ON:
			virtual_midi_enable (hw->mididev, 0);
			break;

			break;
		}
	}

	/* XXX It would be nice to avoid a complete copy of the whole
	   struct sometimes. But I think its fast enough that this
	   is a low priority fix.
	*/

	copy_to_user (arg, &wc, sizeof (wc));
	return 0;
}


/***********************************************************************
WaveFront: MIDI synth interface
***********************************************************************/


static int
wavefront_ioctl (int dev, unsigned int cmd, caddr_t arg)
{
	wf_config *hw = &wavefront_configuration;
	unsigned char rbuf[4];

	switch (cmd) {
	case SNDCTL_SYNTH_INFO:
		memcpy (&((char *) arg)[0], &wavefront_info,
			sizeof (wavefront_info));
		return 0;
		break;

	case SNDCTL_SEQ_RESETSAMPLES:
		printk (KERN_WARNING
			"WaveFront: cannot reset sample status in kernel.\n");
		return 0; /* don't force an error */
		break;

	case SNDCTL_SEQ_PERCMODE:
		/* XXX does this correspond to anything obvious ?*/
		return 0; /* don't force an error */
		break;

	case SNDCTL_SYNTH_MEMAVL:
		if (wavefront_cmd (hw, WFC_REPORT_FREE_MEMORY, rbuf, 0) != 0) {
			printk (KERN_ERR
				"WaveFront: cannot get free memory size\n");
			return 0;
		} else {
			hw->freemem = demunge_int32 (rbuf, 4);
			return hw->freemem;
		}

	case SNDCTL_SYNTH_CONTROL:
		return wavefront_synth_control (dev, cmd, arg);

	default:
		return -(EINVAL);
	}
}

static int
wavefront_open (int dev, int mode)

{
	struct wf_config *hw = &wavefront_configuration;

	if (hw->opened) {
		printk (KERN_WARNING "WaveFront: warning: device in use\n");
	}

	hw->opened = mode;
	return (0);
}

static void
wavefront_close (int dev)

{
	struct wf_config *hw = &wavefront_configuration;
	int i;

#ifdef WF_STATS
	printk ("Status during loop: %ld\n", hw->status_found_during_loop);
	for (i = 0; i < 4; i++) {
		printk ("Status during sleep[%d]: %ld\n",
			i, hw->status_found_during_sleep[i]);
	}
#endif WF_STATS
	hw->opened = 0;
	hw->debug = 0;

	return;
}

static void
wavefront_aftertouch (int dev, int channel, int pressure)
{
	midi_synth_aftertouch (wavefront_configuration.mididev,channel,pressure);
};

static void
wavefront_bender (int dev, int chn, int value)
{
	midi_synth_bender (wavefront_configuration.mididev, chn, value);
};

static void
wavefront_controller (int dev, int channel, int ctrl_num, int value)
{
	if(ctrl_num==CTRL_PITCH_BENDER) wavefront_bender(0,channel,value);
	midi_synth_controller (wavefront_configuration.mididev,
			       channel,ctrl_num,value);
};

static void
wavefront_panning(int dev, int channel, int pressure)
{
	midi_synth_controller (wavefront_configuration.mididev,
			       channel,CTL_PAN,pressure);
};

static int
wavefront_set_instr (int dev, int channel, int instr_no)
{
	return(midi_synth_set_instr (wavefront_configuration.mididev,
				     channel,instr_no));
};

static int
wavefront_kill_note (int dev, int channel, int note, int volume)
{
	if (note==255)
		return (midi_synth_start_note (wavefront_configuration.mididev,
					       channel, 0, 0));
	return(midi_synth_kill_note (wavefront_configuration.mididev,
				     channel, note, volume));
};

static int
wavefront_start_note (int dev, int channel, int note, int volume)
{
	if (note==255) {
		midi_synth_aftertouch (wavefront_configuration.mididev,
				       channel,volume); 
		return(0);
	};

	if (volume==0) {
		volume=127;
		midi_synth_aftertouch
			(wavefront_configuration.mididev,
			 channel,0);
	};

	midi_synth_start_note (wavefront_configuration.mididev,
			       channel, note, volume);
	return(0);
};

static void
wavefront_setup_voice (int dev, int voice, int chn)
{
};

static void wavefront_reset (int dev)

{
	int i;

	for (i = 0; i < 16; i++) {
		midi_synth_kill_note (dev,i,0,0);
	};
};

static struct synth_operations wavefront_operations =
{
	"WaveFront",
	&wavefront_info,
	0,
	SYNTH_TYPE_SAMPLE,
	SAMPLE_TYPE_WAVEFRONT,
	wavefront_open,
	wavefront_close,
	wavefront_ioctl,
	wavefront_kill_note,
	wavefront_start_note,
	wavefront_set_instr,
	wavefront_reset,
	NULL,
	wavefront_load_patch,
	wavefront_aftertouch,
	wavefront_controller,
	wavefront_panning,
	NULL,
	wavefront_bender,
	NULL,
	wavefront_setup_voice
};


/***********************************************************************
WaveFront: OSS/Free and/or Linux kernel installation interface
***********************************************************************/

void
wavefrontintr (int irq, void *dev_id, struct pt_regs *dummy)
{
        /* We don't use this handler except during device
	   configuration. While the module is installed, the 
	   interrupt is used to signal MIDI interrupts, and is 
	   handled by the interrupt routine in wf_midi.c
	 */
	   
	wf_config *hw = (wf_config *) dev_id;
	hw->irq_ok = 1;

	if ((wavefront_status(hw) & STAT_INTR_WRITE) ||
	    (wavefront_status(hw) & STAT_INTR_READ)) {
		wake_up (&hw->interrupt_sleeper);
	}
}

/* STATUS REGISTER 

0 Host Rx Interrupt Enable (1=Enabled)
1 Host Rx Register Full (1=Full)
2 Host Rx Interrupt Pending (1=Interrupt)
3 Unused
4 Host Tx Interrupt (1=Enabled)
5 Host Tx Register empty (1=Empty)
6 Host Tx Interrupt Pending (1=Interrupt)
7 Unused

11111001 
  Rx Intr enable
  nothing to read from board
  no rx interrupt pending
  unused
  tx interrupt enabled
  space to transmit
  tx interrupt pending

*/

int
wavefront_interrupt_bits (int irq)

{
	int bits;

	switch (irq) {
	case 9:
		bits = 0x00;
		break;
	case 5:
		bits = 0x08;
		break;
	case 12:
		bits = 0x10;
		break;
	case 15:
		bits = 0x18;
		break;
	
	default:
		printk (KERN_WARNING "WaveFront: invalid IRQ %d\n", irq);
		bits = -1;
	}

	return bits;
}

void
wavefront_should_cause_interrupt (wf_config *hw, int val, int port, int timeout)

{
	unsigned long flags;

	save_flags (flags);
	cli();
	hw->irq_ok = 0;
	outb (val,port);
	current->timeout = jiffies + timeout;
	interruptible_sleep_on (&hw->interrupt_sleeper);
	restore_flags (flags);
}

static int
wavefront_hw_reset (wf_config *hw)

{
	int bits;
	int hwv[2];

	/* Check IRQ is legal */

	if ((bits = wavefront_interrupt_bits (hw->irq)) < 0) {
		return 1;
	}

	if (request_irq (hw->irq, wavefrontintr,
			 0, "WaveFront", (void *) hw) < 0) {
		printk (KERN_WARNING "WaveFront: IRQ %d not available!\n",
			hw->irq);
		return 1;
	}

	/* try reset of port */
      
	outb (0x0, hw->control_port); 
  
	/* At this point, the board is in reset, and the H/W initialization
	   register is accessed at the same address as the data port.
     
	   Bit 7 - Enable IRQ Driver	
	   0 - Tri-state the Wave-Board drivers for the PC Bus IRQs
	   1 - Enable IRQ selected by bits 5:3 to be driven onto the PC Bus.
     
	   Bit 6 - MIDI Interface Select

	   0 - Use the MIDI Input from the 26-pin WaveBlaster
	   compatible header as the serial MIDI source
	   1 - Use the MIDI Input from the 9-pin D connector as the serial MIDI 
	   source.
     
	   Bits 5:3 - IRQ Selection
	   0 0 0 - IRQ 2/9
	   0 0 1 - IRQ 5
	   0 1 0 - IRQ 12
	   0 1 1 - IRQ 15
	   1 0 0 - Reserved
	   1 0 1 - Reserved
	   1 1 0 - Reserved
	   1 1 1 - Reserved
     
	   Bits 2:1 - Reserved
	   Bit 0 - Disable Boot ROM
	   0 - memory accesses to 03FC30-03FFFFH utilize the internal Boot ROM
	   1 - memory accesses to 03FC30-03FFFFH are directed to external 
	   storage.
     
	*/

	/* configure hardware: IRQ, enable interrupts, 
	   plus external 9-pin MIDI interface selected
	*/

	outb (0x80 | 0x40 | bits, hw->data_port);	
  
	/* CONTROL REGISTER

	   0 Host Rx Interrupt Enable (1=Enabled)      0x1
	   1 Unused                                    0x2
	   2 Unused                                    0x4
	   3 Unused                                    0x8
	   4 Host Tx Interrupt Enable                 0x10
	   5 Mute (0=Mute; 1=Play)                    0x20
	   6 Master Interrupt Enable (1=Enabled)      0x40
	   7 Master Reset (0=Reset; 1=Run)            0x80

	   Take us out of reset, unmute, master + TX + RX interrupts on.
	   
	   We'll get an interrupt presumably to tell us that the TX
	   register is clear. However, this doesn't mean that the
	   board is ready. We actually have to send it a command, and
	   wait till it gets back to use. After a cold boot, this can
	   take some time.
	   
	   I think this is because its only after a cold boot that the
	   onboard ROM does its memory check, which can take "up to 4
	   seconds" according to the WaveFront SDK. So, since sleeping
	   doesn't cost us much, we'll give it *plenty* of time. It
	   turns out that with 12MB of RAM, it can take up to 16
	   seconds or so!! See the code after "ABOUT INTERRUPTS"
	*/

	wavefront_should_cause_interrupt(hw,
					 0x80|0x40|0x10|0x1,
					 hw->control_port,
					 (2*HZ)/100);

	/* Note: data port is now the data port, not the h/w initialization
	   port.
	 */

	if (!hw->irq_ok) {
		printk (KERN_WARNING
			"WaveFront: intr not received after h/w un-reset.\n");
		goto gone_bad;
	} 

	hw->interrupts_on = 1;
	
	/* ABOUT INTERRUPTS:
	   -----------------
	   
	   When we talk about interrupts, there are two kinds
	   generated by the ICS2115. The first is to signal MPU data
	   ready to read, and the second is to signal RX or TX status
	   changes. We *always* want interrupts for MPU stuff but we 
	   generally avoid using RX/TX interrupts.

	   In theory, we could use the TX and RX interrupts for all
	   communication with the card. However, there are 2 good
	   reasons not to do this.

	   First of all, the MIDI interface is going to use the same
	   interrupt. This presents no practical problem since Linux
	   allows us to share IRQ's. However, there are times when it
	   makes sense for a user to ask the driver to disable
	   interrupts, to avoid bothering Linux with a stream of MIDI
	   interrupts that aren't going to be used because nothing
	   cares about them. If we rely on them for communication with
	   the WaveFront synth as well, this disabling would be
	   crippling. Since being able to disable them can save quite
	   a bit of overhead (consider the interrupt frequency of a
	   physical MIDI controller like a modwheel being shunted back
	   and forth - its higher than the mouse, and much of
	   the time is of absolutely no interest to the kernel or any
	   user space processes whatsoever), we don't want to do this.

	   Secondly, much of the time, there's no reason to go to
	   sleep on a TX or RX status: the WaveFront gets back to us
	   quickly enough that its a lot more efficient to just busy
	   wait on the relevant status. Once we go to sleep, all is
	   lost anyway, and so interrupts don't really help us much anyway.

	   Therefore, we don't use interrupts for communication with
	   the WaveFront synth. We just poll the relevant RX/TX status.

	   However, there is one broad exception to this. During module
	   loading, to deal with several situations where timing would
	   be an issue, we use TX/RX interrupts to help us avoid busy
	   waiting for indeterminate and hard to manage periods of
	   time. So, TX/RX interrupts are enabled until the end of 
	   wavefront_init(), and not used again after that.

	 */

	/* Note: data port is now the data port, not the h/w initialization
	   port.

	   At this point, only "HW VERSION" or "DOWNLOAD OS" commands
	   will work. So, issue one of them, and wait for TX
	   interrupt. This can take a *long* time after a cold boot,
	   while the ISC ROM does its RAM test. The SDK says up to 4
	   seconds - with 12MB of RAM on a Tropez+, it takes a lot
	   longer than that (~16secs). Note that the card understands
	   the difference between a warm and a cold boot, so
	   subsequent ISC2115 reboots (say, caused by module
	   reloading) will get through this much faster.

	   Interesting question: why is no RX interrupt received first ?
	*/
	
	wavefront_should_cause_interrupt(hw, WFC_HARDWARE_VERSION, 
					 hw->data_port, 20*HZ);

	if (!hw->irq_ok) {
		printk (KERN_WARNING
			"WaveFront: post-RAM-check interrupt not received.\n");
		goto gone_bad;
	} 

	if (!(wavefront_status(hw) & STAT_CAN_READ)) {
		printk (KERN_WARNING
			"WaveFront: no response to HW version cmd.\n");
		goto gone_bad;
	}
	
	if ((hwv[0] = wavefront_read (hw)) == -1) {
		printk (KERN_WARNING
			"WaveFront: board not responding correctly.\n");
		goto gone_bad;
	}

	if (hwv[0] == 0xFF) { /* NAK */

		/* Board's RAM test failed. Try to read error code,
		   and tell us about it either way.
		*/
		
		if ((hwv[0] = wavefront_read (hw)) == -1) {
			printk (KERN_WARNING 
				"WaveFront: on-board RAM test failed "
				"(bad error code).\n");
		} else {
			printk (KERN_WARNING 
				"WaveFront: on-board RAM test failed "
				"(error code: 0x%x).\n",
				hwv[0]);
		}
		goto gone_bad;
	}

	/* We're OK, just get the next byte of the HW version response */

	if ((hwv[1] = wavefront_read (hw)) == -1) {
		printk (KERN_WARNING
			"WaveFront: board not responding correctly(2).\n");
		goto gone_bad;
	}

	printk (KERN_INFO "WaveFront: hardware version %d.%d\n",
		hwv[0], hwv[1]);

	return 0;


     gone_bad:
	free_irq (hw->irq, hw);
	return (1);
	}

int
probe_wavefront (struct address_info *hw_config)

{
	unsigned char   rbuf[4], wbuf[4];
	wf_config       *hw;

	if (hw_config->irq < 0 || hw_config->irq > 16) {
	    printk (KERN_WARNING "WaveFront: impossible IRQ suggested(%d)\n", 
		    hw_config->irq);
	    return 0;
	}
  
	/* Yeah yeah, TB docs say 8, but the FX device on the Tropez Plus
	   takes up another 8 ...
	*/

	if (check_region (hw_config->io_base, 16)) {
		printk (KERN_ERR "WaveFront: IO address range 0x%x - 0x%x "
			"already in use - ignored\n", hw_config->io_base,
			hw_config->io_base+15);
		return 0;
	}
  
	hw = &wavefront_configuration;

	hw->irq = hw_config->irq;
	hw->base = hw_config->io_base;

	hw->israw = 0;
	hw->debug = debug_default;
	hw->interrupts_on = 0;
	hw->rom_samples_rdonly = 1; /* XXX default lock on ROM sample slots */

#ifdef WF_STATS
	hw->status_found_during_sleep[0] = 0;
	hw->status_found_during_sleep[1] = 0;
	hw->status_found_during_sleep[2] = 0;
	hw->status_found_during_sleep[3] = 0;
	hw->status_found_during_loop = 0;
#endif WF_STATS

	hw_config->slots[WF_SYNTH_SLOT] = hw->synthdev = -1;
	hw_config->slots[WF_INTERNAL_MIDI_SLOT] = hw->mididev = -1;
	hw_config->slots[WF_EXTERNAL_MIDI_SLOT] = hw->ext_mididev = -1;

	if (wavefront_cmd (hw, WFC_FIRMWARE_VERSION, rbuf, wbuf) == 0) {

		hw->fw_version[0] = rbuf[0];
		hw->fw_version[1] = rbuf[1];
		printk (KERN_INFO
			"WaveFront: firmware %d.%d already loaded.\n",
			rbuf[0], rbuf[1]);

		/* check that a command actually works */
      
		if (wavefront_cmd (hw, WFC_HARDWARE_VERSION,
				   rbuf, wbuf) == 0) {
			hw->hw_version[0] = rbuf[0];
			hw->hw_version[1] = rbuf[1];
		} else {
			printk (KERN_INFO "WaveFront: not raw, but no "
				"hardware version!\n");
			return 0;
		}

		if (!wf_raw) {
			return 1;
		} else {
			printk (KERN_INFO
				"WaveFront: reloading firmware anyway.\n");
		}

	} else {

		hw->israw = 1;
		printk (KERN_INFO "WaveFront: no response to firmware probe, "
			"assume raw.\n");

	}

	init_waitqueue (&hw->interrupt_sleeper);

	if (wavefront_hw_reset (hw)) {
		printk (KERN_WARNING "WaveFront: hardware reset failed\n");
		return 0;
	}

	return 1;
}

#include "os.h"
#define __KERNEL_SYSCALLS__
#include <linux/fs.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/unistd.h>
#include <asm/uaccess.h>

static int errno; 

static int
wavefront_download_firmware (wf_config *hw, char *path)

{
	unsigned char section[WF_SECTION_MAX];
	char section_length; /* yes, just a char; max value is WF_SECTION_MAX */
	int section_cnt_downloaded = 0;
	int fd;
	int c;
	int i;
	mm_segment_t fs;

	/* This tries to be a bit cleverer than the stuff Alan Cox did for
	   the generic sound firmware, in that it actually knows
	   something about the structure of the Motorola firmware. In
	   particular, it uses a version that has been stripped of the
	   20K of useless header information, and had section lengths
	   added, making it possible to load the entire OS without any
	   [kv]malloc() activity, since the longest entity we ever read is
	   42 bytes (well, WF_SECTION_MAX) long.
	*/

	fs = get_fs();
	set_fs (get_ds());

	if ((fd = open (path, 0, 0)) < 0) {
		printk (KERN_WARNING "WaveFront: Unable to load \"%s\".\n",
			path);
		return 1;
	}

	while (1) {
		int x;

		if ((x = read (fd, &section_length, sizeof (section_length))) !=
		    sizeof (section_length)) {
			printk (KERN_ERR "WaveFront: firmware read error.\n");
			goto failure;
		}

		if (section_length == 0) {
			break;
		}

		if (read (fd, section, section_length) != section_length) {
			printk (KERN_ERR "WaveFront: firmware section "
				"read error.\n");
			goto failure;
		}

		/* Send command */
	
		if (!wavefront_write (hw, WFC_DOWNLOAD_OS)) {
			goto failure;
		}
	
		for (i = 0; i < section_length; i++) {
			if (!wavefront_write (hw, section[i])) {
				goto failure;
			}
		}
	
		/* get ACK */
	
		if (wavefront_wait (hw, STAT_CAN_READ)) {

			if ((c = inb (hw->data_port)) != WF_ACK) {

				printk (KERN_ERR "WaveFront: download "
					"of section #%d not "
					"acknowledged, ack = 0x%x\n",
					section_cnt_downloaded + 1, c);
				goto failure;
		
			} else {
#ifdef WF_DEBUG
			    if ((hw->debug & WF_DEBUG_IO) &&
				   !(++section_cnt_downloaded % 10)) {
				printk (KERN_DEBUG ".");
			    }
#endif WF_DEBUG
			}

		} else {
			printk (KERN_ERR "WaveFront: timed out "
				"for download ACK.\n");
		}

	}

	close (fd);
	set_fs (fs);
#ifdef WF_DEBUG
	if (hw->debug & WF_DEBUG_IO) {
		printk (KERN_DEBUG "\n");
	}
#endif WF_DEBUG
	return 0;

 failure:
	close (fd);
	set_fs (fs);
	printk (KERN_ERR "\nWaveFront: firmware download failed!!!\n");
	return 1;
}

static int
wavefront_config_midi (wf_config *hw, struct address_info *hw_config)

{
	unsigned char rbuf[4], wbuf[4];
    
	if (!probe_wf_mpu (hw_config)) {
		printk (KERN_WARNING "WaveFront: could not install "
			"MPU-401 device.\n");
		return 1;
	} 

	/* Attach an modified MPU-401 driver to the master MIDI interface */

	hw_config->name = "WaveFront Internal MIDI";
	attach_wf_mpu (hw_config);

	if (hw_config->slots[WF_INTERNAL_MIDI_SLOT] == -1) {
		printk (KERN_WARNING "WaveFront: MPU-401 not configured.\n");
		return 1;
	}

	hw->mididev = hw_config->slots[WF_INTERNAL_MIDI_SLOT];

	/* Route external MIDI to WaveFront synth (by default) */
    
	if (wavefront_cmd (hw, WFC_MISYNTH_ON, rbuf, wbuf)) {
		printk (KERN_WARNING
			"WaveFront: cannot enable MIDI-IN to synth routing.\n");
		/* XXX error ? */
	}

	/* Get the regular MIDI patch loading function, so we can
	   use it if we ever get handed a SYSEX patch. This is
	   unlikely, because its so damn slow, but we may as well
	   leave this functionality from maui.c behind, since it
	   could be useful for sequencer applications that can
	   only use MIDI to do patch loading.
	*/

	if (midi_devs[hw->mididev]->converter != NULL) {
		midi_load_patch = midi_devs[hw->mididev]->converter->load_patch;
		midi_devs[hw->mididev]->converter->load_patch =
		    &wavefront_load_patch;
	}

	/* Turn on Virtual MIDI, but first *always* turn it off,
	   since otherwise consectutive reloads of the driver will
	   never cause the hardware to generate the initial "internal" or 
	   "external" source bytes in the MIDI data stream. This
	   is pretty important, since the internal hardware generally will
	   be used to generate none or very little MIDI output, and
	   thus the only source of MIDI data is actually external. Without
	   the switch bytes, the driver will think it all comes from
	   the internal interface. Duh.
	*/

	if (wavefront_cmd (hw, WFC_VMIDI_OFF, rbuf, wbuf)) { 
		printk (KERN_WARNING "WaveFront: cannot disable "
			"virtual MIDI mode\n");
		/* XXX go ahead and try anyway ? */
	}

	hw_config->name = "WaveFront External MIDI";
    
	if (virtual_midi_enable (hw->mididev, hw_config)) {
		printk (KERN_WARNING "WaveFront: no virtual MIDI access.\n");
	} else {
		hw->ext_mididev = hw_config->slots[WF_EXTERNAL_MIDI_SLOT];
		if (wavefront_cmd (hw, WFC_VMIDI_ON, rbuf, wbuf)) {
			printk (KERN_WARNING
				"WaveFront: cannot enable virtual MIDI mode.\n");
			virtual_midi_disable (hw->mididev);
		} 
	}
    
	return 0;
}

static int
wavefront_do_reset (wf_config *hw, int atboot)

{
	char voices[1];

	if (!atboot && wavefront_hw_reset (hw)) {
		printk (KERN_WARNING "WaveFront: hw reset failed.\n");
		goto gone_bad;
	}

	if (hw->israw || wf_raw) {
		if (wavefront_download_firmware (hw, ospath)) {
			goto gone_bad;
		}

		/* Wait for the OS to get running. The protocol for
		   this is non-obvious, and was determined by
		   using port-IO tracing in DOSemu and some
		   experimentation here.
		   
		   Rather than busy-wait, use interrupts creatively.
		*/

		wavefront_should_cause_interrupt (hw, WFC_NOOP,
					  hw->data_port, (10*HZ));
		
		if (!hw->irq_ok) {
			printk (KERN_WARNING
				"WaveFront: no post-OS interrupt.\n");
			goto gone_bad;
		}
		
		/* Now, do it again ! */
		
		wavefront_should_cause_interrupt (hw, WFC_NOOP,
						  hw->data_port, (10*HZ));
		
		if (!hw->irq_ok) {
			printk (KERN_WARNING
				"WaveFront: no post-OS interrupt(2).\n");
			goto gone_bad;
		}

		/* OK, no (RX/TX) interrupts any more, but leave mute
		   on. Master interrupts get enabled when we're done here.
		*/
		
		outb (0x80, hw->control_port); 
		
		/* No need for the IRQ anymore */
		
		free_irq (hw->irq, hw);
	}

	if (/*XXX has_fx_device() && */ fx_raw) {
		wffx_init (hw);
	}

	/* SETUPSND.EXE asks for sample memory config here, but since i
	   have no idea how to interpret the result, we'll forget
	   about it.
	*/
	
	if ((hw->freemem = wavefront_freemem (hw)) < 0) {
		goto gone_bad;
	}
		
	printk (KERN_INFO "WaveFront: available DRAM %dk\n", hw->freemem / 1024);

	if (!wavefront_write (hw, 0xf0) ||
	    !wavefront_write (hw, 1) ||
	    (wavefront_read (hw) < 0)) {
		hw->debug = 0;
		printk (KERN_WARNING "WaveFront: MPU emulation mode not set.\n");
		goto gone_bad;
	}

	voices[0] = 32;

	if (wavefront_cmd (hw, WFC_SET_NVOICES, 0, voices)) {
		printk (KERN_WARNING
			"WaveFront: cannot set number of voices to 32.\n");
	}

	return 0;

 gone_bad:
	/* reset that sucker so that it doesn't bother us. */

	outb (0x0, hw->control_port);
	free_irq (hw->irq, hw);
	return 1;
}

static int
wavefront_init (wf_config *hw, int atboot)

{
	int samples_are_from_rom;

	if (hw->israw || wf_raw) {
		samples_are_from_rom = 1;
	} else {
		samples_are_from_rom = 0;
	}

	if (hw->israw || wf_raw || fx_raw) {
		if (wavefront_do_reset (hw, atboot)) {
			return 1;
		}
	}

	wavefront_get_sample_status (hw, samples_are_from_rom);
	wavefront_get_program_status (hw);
	wavefront_get_patch_status (hw);

	/* Start normal operation: unreset, master interrupt enable
	   (for MPU interrupts) no mute
	*/

	outb (0x80|0x40|0x20, hw->control_port); 

	return (0);
}

void
attach_wavefront (struct address_info *hw_config)
{
	int i;
	struct wf_config *hw = &wavefront_configuration;

	if ((i = sound_alloc_synthdev()) == -1) {
		printk (KERN_ERR "WaveFront: Too many synthesizers\n");
		return;
	} else {
		hw_config->slots[WF_SYNTH_SLOT] = i;
		hw->synthdev = i;
		synth_devs[hw->synthdev] = &wavefront_operations;
	}

	if (wavefront_init (hw, 1)) {
		printk (KERN_WARNING "WaveFront: board could not "
			"be initialized.\n");
		sound_unload_synthdev (i);
		return;
	}
    
	request_region (hw_config->io_base+2, 6, "WaveFront synth");
	request_region (hw_config->io_base+8, 8, "WaveFront FX");

	conf_printf2 ("WaveFront Synth", hw_config->io_base, 0, -1, -1);

#if defined(CONFIG_MIDI)    
	if (wavefront_config_midi (hw, hw_config)) {
		printk (KERN_WARNING "WaveFront: could not initialize MIDI.\n");
	}
#else
	printk (KERN_WARNING
		"WaveFront: MIDI not configured at kernel-config time.\n");
#endif CONFIG_MIDI

	return;
}
void
unload_wavefront (struct address_info *hw_config)
{
	struct wf_config *hw = &wavefront_configuration;

	/* the first two are freed by the wf_mpu code */
	release_region (hw->base+2, 6);
	release_region (hw->base+8, 8);
	sound_unload_synthdev (hw->synthdev);
#if defined(CONFIG_MIDI)
	unload_wf_mpu (hw_config);
#endif
}

/***********************************************************************/
/*   WaveFront FX control                                              */
/***********************************************************************/

#include "yss225.h"

/* Control bits for the Load Control Register
 */

#define FX_LSB_TRANSFER 0x01    /* transfer after DSP LSB byte written */
#define FX_MSB_TRANSFER 0x02    /* transfer after DSP MSB byte written */
#define FX_AUTO_INCR    0x04    /* auto-increment DSP address after transfer */

static int
wffx_idle (struct wf_config *hw) 
    
{
	int i;
	unsigned int x = 0x80;
    
	for (i = 0; i < 1000; i++) {
		x = inb (hw->fx_status);
		if ((x & 0x80) == 0) {
			break;
		}
	}
    
	if (x & 0x80) {
		printk (KERN_ERR "WaveFront: FX device never idle.\n");
		return 0;
	}
    
	return (1);
}

static void
wffx_mute (struct wf_config *hw, int onoff)
    
{
	if (!wffx_idle(hw)) {
		return;
	}
    
	outb (onoff ? 0x02 : 0x00, hw->fx_op);
}

static int
wffx_memset (struct wf_config *hw, int page,
	     int addr, int cnt, unsigned short *data)
{
	if (page < 0 || page > 7) {
		printk (KERN_ERR "WaveFront: FX memset: "
			"page must be >= 0 and <= 7\n");
		return -(EINVAL);
	}

	if (addr < 0 || addr > 0x7f) {
		printk (KERN_ERR "WaveFront: FX memset: "
			"addr must be >= 0 and <= 7f\n");
		return -(EINVAL);
	}

	if (cnt == 1) {

		outb (FX_LSB_TRANSFER, hw->fx_lcr);
		outb (page, hw->fx_dsp_page);
		outb (addr, hw->fx_dsp_addr);
		outb ((data[0] >> 8), hw->fx_dsp_msb);
		outb ((data[0] & 0xff), hw->fx_dsp_lsb);

		printk (KERN_INFO "WaveFront: FX: addr %d:%x set to 0x%x\n",
			page, addr, data[0]);
	
	} else {
		int i;

		outb (FX_AUTO_INCR|FX_LSB_TRANSFER, hw->fx_lcr);
		outb (page, hw->fx_dsp_page);
		outb (addr, hw->fx_dsp_addr);

		for (i = 0; i < cnt; i++) {
			outb ((data[i] >> 8), hw->fx_dsp_msb);
			outb ((data[i] & 0xff), hw->fx_dsp_lsb);
			if (!wffx_idle (hw)) {
				break;
			}
		}

		if (i != cnt) {
			printk (KERN_WARNING
				"WaveFront: FX memset "
				"(0x%x, 0x%x, 0x%x, %d) incomplete\n",
				page, addr, (int) data, cnt);
			return -(EIO);
		}
	}

	return 0;
}

static int
wffx_ioctl (struct wf_config *hw, wavefront_fx_info *r)

{
	unsigned short page_data[256];
	unsigned short *pd;

	switch (r->request) {
	case WFFX_MUTE:
		wffx_mute (hw, r->data[0]);
		return 0;

	case WFFX_MEMSET:

		if (r->data[2] <= 0) {
			printk (KERN_ERR "WaveFront: cannot write "
				"<= 0 bytes to FX\n");
			return -(EINVAL);
		} else if (r->data[2] == 1) {
			pd = (unsigned short *) &r->data[3];
		} else {
			if (r->data[2] > sizeof (page_data)) {
				printk (KERN_ERR "WaveFront: cannot write "
					"> 255 bytes to FX\n");
				return -(EINVAL);
			}
			copy_from_user (page_data, (unsigned char *) r->data[3],
					r->data[2]);
			pd = page_data;
		}

		return wffx_memset (hw,
				    r->data[0], /* page */
				    r->data[1], /* addr */
				    r->data[2], /* cnt */
				    pd);

	default:
		printk (KERN_WARNING
			"WaveFront: FX: ioctl %d not yet supported\n",
			r->request);
		return -(EINVAL);
	}
}

/* YSS225 initialization.

   This code was developed using DOSEMU. The Turtle Beach SETUPSND
   utility was run with I/O tracing in DOSEMU enabled, and a reconstruction
   of the port I/O done, using the Yamaha faxback document as a guide
   to add more logic to the code. Its really pretty wierd.

   There was an alternative approach of just dumping the whole I/O
   sequence as a series of port/value pairs and a simple loop
   that output it. However, I hope that eventually I'll get more
   control over what this code does, and so I tried to stick with
   a somewhat "algorithmic" approach.
*/

static int
wffx_init (struct wf_config *hw)

{
	int i;
	int j;

	/* Set all bits for all channels on the MOD unit to zero */
	/* XXX But why do this twice ? */

	for (j = 0; j < 2; j++) {
		for (i = 0x10; i <= 0xff; i++) {
	    
			if (!wffx_idle (hw)) {
				return (-1);
			}
	    
			outb (i, hw->fx_mod_addr);
			outb (0x0, hw->fx_mod_data);
		}
	}

	if (!wffx_idle(hw)) return (-1);
	outb (0x02, hw->fx_op);                        /* mute on */

	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x44, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x00, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x42, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x00, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x43, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x00, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x7c, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x00, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x7e, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x00, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x46, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x00, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x49, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x00, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x47, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x00, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x4a, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x00, hw->fx_dsp_lsb);

	/* either because of stupidity by TB's programmers, or because it
	   actually does something, rezero the MOD page.
	*/
	for (i = 0x10; i <= 0xff; i++) {
	
		if (!wffx_idle (hw)) {
			return (-1);
		}
	
		outb (i, hw->fx_mod_addr);
		outb (0x0, hw->fx_mod_data);
	}
	/* load page zero */

	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, hw->fx_lcr);
	outb (0x00, hw->fx_dsp_page);
	outb (0x00, hw->fx_dsp_addr);

	for (i = 0; i < sizeof (page_zero); i += 2) {
		outb (page_zero[i], hw->fx_dsp_msb);
		outb (page_zero[i+1], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}

	/* Now load page one */

	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, hw->fx_lcr);
	outb (0x01, hw->fx_dsp_page);
	outb (0x00, hw->fx_dsp_addr);

	for (i = 0; i < sizeof (page_one); i += 2) {
		outb (page_one[i], hw->fx_dsp_msb);
		outb (page_one[i+1], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, hw->fx_lcr);
	outb (0x02, hw->fx_dsp_page);
	outb (0x00, hw->fx_dsp_addr);

	for (i = 0; i < sizeof (page_two); i++) {
		outb (page_two[i], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, hw->fx_lcr);
	outb (0x03, hw->fx_dsp_page);
	outb (0x00, hw->fx_dsp_addr);

	for (i = 0; i < sizeof (page_three); i++) {
		outb (page_three[i], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, hw->fx_lcr);
	outb (0x04, hw->fx_dsp_page);
	outb (0x00, hw->fx_dsp_addr);

	for (i = 0; i < sizeof (page_four); i++) {
		outb (page_four[i], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}

	/* Load memory area (page six) */
    
	outb (FX_LSB_TRANSFER, hw->fx_lcr); 
	outb (0x06, hw->fx_dsp_page); 

	for (i = 0; i < sizeof (page_six); i += 3) {
		outb (page_six[i], hw->fx_dsp_addr);
		outb (page_six[i+1], hw->fx_dsp_msb);
		outb (page_six[i+2], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, hw->fx_lcr);
	outb (0x07, hw->fx_dsp_page);
	outb (0x00, hw->fx_dsp_addr);

	for (i = 0; i < sizeof (page_seven); i += 2) {
		outb (page_seven[i], hw->fx_dsp_msb);
		outb (page_seven[i+1], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}

	/* Now setup the MOD area. We do this algorithmically in order to
	   save a little data space. It could be done in the same fashion
	   as the "pages".
	*/

	for (i = 0x00; i <= 0x0f; i++) {
		outb (0x01, hw->fx_mod_addr);
		outb (i, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
		outb (0x02, hw->fx_mod_addr);
		outb (0x00, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}

	for (i = 0xb0; i <= 0xbf; i++) {
		outb (i, hw->fx_mod_addr);
		outb (0x20, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}

	for (i = 0xf0; i <= 0xff; i++) {
		outb (i, hw->fx_mod_addr);
		outb (0x20, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}

	for (i = 0x10; i <= 0x1d; i++) {
		outb (i, hw->fx_mod_addr);
		outb (0xff, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}

	outb (0x1e, hw->fx_mod_addr);
	outb (0x40, hw->fx_mod_data);
	if (!wffx_idle(hw)) return (-1);

	for (i = 0x1f; i <= 0x2d; i++) {
		outb (i, hw->fx_mod_addr);
		outb (0xff, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}

	outb (0x2e, hw->fx_mod_addr);
	outb (0x00, hw->fx_mod_data);
	if (!wffx_idle(hw)) return (-1);

	for (i = 0x2f; i <= 0x3e; i++) {
		outb (i, hw->fx_mod_addr);
		outb (0x00, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}

	outb (0x3f, hw->fx_mod_addr);
	outb (0x20, hw->fx_mod_data);
	if (!wffx_idle(hw)) return (-1);

	for (i = 0x40; i <= 0x4d; i++) {
		outb (i, hw->fx_mod_addr);
		outb (0x00, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}

	outb (0x4e, hw->fx_mod_addr);
	outb (0x0e, hw->fx_mod_data);
	if (!wffx_idle(hw)) return (-1);
	outb (0x4f, hw->fx_mod_addr);
	outb (0x0e, hw->fx_mod_data);
	if (!wffx_idle(hw)) return (-1);


	for (i = 0x50; i <= 0x6b; i++) {
		outb (i, hw->fx_mod_addr);
		outb (0x00, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}

	outb (0x6c, hw->fx_mod_addr);
	outb (0x40, hw->fx_mod_data);
	if (!wffx_idle(hw)) return (-1);

	outb (0x6d, hw->fx_mod_addr);
	outb (0x00, hw->fx_mod_data);
	if (!wffx_idle(hw)) return (-1);

	outb (0x6e, hw->fx_mod_addr);
	outb (0x40, hw->fx_mod_data);
	if (!wffx_idle(hw)) return (-1);

	outb (0x6f, hw->fx_mod_addr);
	outb (0x40, hw->fx_mod_data);
	if (!wffx_idle(hw)) return (-1);

	for (i = 0x70; i <= 0x7f; i++) {
		outb (i, hw->fx_mod_addr);
		outb (0xc0, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}
    
	for (i = 0x80; i <= 0xaf; i++) {
		outb (i, hw->fx_mod_addr);
		outb (0x00, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}

	for (i = 0xc0; i <= 0xdd; i++) {
		outb (i, hw->fx_mod_addr);
		outb (0x00, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}

	outb (0xde, hw->fx_mod_addr);
	outb (0x10, hw->fx_mod_data);
	if (!wffx_idle(hw)) return (-1);
	outb (0xdf, hw->fx_mod_addr);
	outb (0x10, hw->fx_mod_data);
	if (!wffx_idle(hw)) return (-1);

	for (i = 0xe0; i <= 0xef; i++) {
		outb (i, hw->fx_mod_addr);
		outb (0x00, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}

	for (i = 0x00; i <= 0x0f; i++) {
		outb (0x01, hw->fx_mod_addr);
		outb (i, hw->fx_mod_data);
		outb (0x02, hw->fx_mod_addr);
		outb (0x01, hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}

	outb (0x02, hw->fx_op); /* mute on */

	/* Now set the coefficients and so forth for the programs above */

	for (i = 0; i < sizeof (coefficients); i += 4) {
		outb (coefficients[i], hw->fx_dsp_page);
		outb (coefficients[i+1], hw->fx_dsp_addr);
		outb (coefficients[i+2], hw->fx_dsp_msb);
		outb (coefficients[i+3], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}

	/* Some settings (?) that are too small to bundle into loops */

	if (!wffx_idle(hw)) return (-1);
	outb (0x1e, hw->fx_mod_addr);
	outb (0x14, hw->fx_mod_data);
	if (!wffx_idle(hw)) return (-1);
	outb (0xde, hw->fx_mod_addr);
	outb (0x20, hw->fx_mod_data);
	if (!wffx_idle(hw)) return (-1);
	outb (0xdf, hw->fx_mod_addr);
	outb (0x20, hw->fx_mod_data);
    
	/* some more coefficients */

	if (!wffx_idle(hw)) return (-1);
	outb (0x06, hw->fx_dsp_page);
	outb (0x78, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x40, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x03, hw->fx_dsp_addr);
	outb (0x0f, hw->fx_dsp_msb);
	outb (0xff, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x0b, hw->fx_dsp_addr);
	outb (0x0f, hw->fx_dsp_msb);
	outb (0xff, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x02, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x00, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x0a, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x00, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x46, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x00, hw->fx_dsp_lsb);
	if (!wffx_idle(hw)) return (-1);
	outb (0x07, hw->fx_dsp_page);
	outb (0x49, hw->fx_dsp_addr);
	outb (0x00, hw->fx_dsp_msb);
	outb (0x00, hw->fx_dsp_lsb);
    
	/* Now, for some strange reason, lets reload every page
	   and all the coefficients over again. I have *NO* idea
	   why this is done. I do know that no sound is produced
	   is this phase is omitted.
	*/

	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, hw->fx_lcr);
	outb (0x00, hw->fx_dsp_page);  
	outb (0x10, hw->fx_dsp_addr);

	for (i = 0; i < sizeof (page_zero_v2); i += 2) {
		outb (page_zero_v2[i], hw->fx_dsp_msb);
		outb (page_zero_v2[i+1], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, hw->fx_lcr);
	outb (0x01, hw->fx_dsp_page);
	outb (0x10, hw->fx_dsp_addr);

	for (i = 0; i < sizeof (page_one_v2); i += 2) {
		outb (page_one_v2[i], hw->fx_dsp_msb);
		outb (page_one_v2[i+1], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}
    
	if (!wffx_idle(hw)) return (-1);
	if (!wffx_idle(hw)) return (-1);
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, hw->fx_lcr);
	outb (0x02, hw->fx_dsp_page);
	outb (0x10, hw->fx_dsp_addr);

	for (i = 0; i < sizeof (page_two_v2); i++) {
		outb (page_two_v2[i], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, hw->fx_lcr);
	outb (0x03, hw->fx_dsp_page);
	outb (0x10, hw->fx_dsp_addr);

	for (i = 0; i < sizeof (page_three_v2); i++) {
		outb (page_three_v2[i], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}
    
	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, hw->fx_lcr);
	outb (0x04, hw->fx_dsp_page);
	outb (0x10, hw->fx_dsp_addr);

	for (i = 0; i < sizeof (page_four_v2); i++) {
		outb (page_four_v2[i], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}
    
	outb (FX_LSB_TRANSFER, hw->fx_lcr);
	outb (0x06, hw->fx_dsp_page);

	/* Page six v.2 is algorithmic */
    
	for (i = 0x10; i <= 0x3e; i += 2) {
		outb (i, hw->fx_dsp_addr);
		outb (0x00, hw->fx_dsp_msb);
		outb (0x00, hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}

	outb (FX_AUTO_INCR|FX_LSB_TRANSFER, hw->fx_lcr);
	outb (0x07, hw->fx_dsp_page);
	outb (0x10, hw->fx_dsp_addr);

	for (i = 0; i < sizeof (page_seven_v2); i += 2) {
		outb (page_seven_v2[i], hw->fx_dsp_msb);
		outb (page_seven_v2[i+1], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}

	for (i = 0x00; i < sizeof(mod_v2); i += 2) {
		outb (mod_v2[i], hw->fx_mod_addr);
		outb (mod_v2[i+1], hw->fx_mod_data);
		if (!wffx_idle(hw)) return (-1);
	}

	for (i = 0; i < sizeof (coefficients2); i += 4) {
		outb (coefficients2[i], hw->fx_dsp_page);
		outb (coefficients2[i+1], hw->fx_dsp_addr);
		outb (coefficients2[i+2], hw->fx_dsp_msb);
		outb (coefficients2[i+3], hw->fx_dsp_lsb);
		if (!wffx_idle(hw)) return (-1);
	}

	outb (0x00, hw->fx_op);
	if (!wffx_idle(hw)) return (-1);
    
	for (i = 0; i < sizeof (coefficients3); i += 2) {
		int x;

		outb (0x07, hw->fx_dsp_page);
		x = (i % 4) ? 0x4e : 0x4c;
		outb (x, hw->fx_dsp_addr);
		outb (coefficients3[i], hw->fx_dsp_msb);
		outb (coefficients3[i+1], hw->fx_dsp_lsb);
	}

	outb (0x00, hw->fx_op); /* mute off */

	return (0);
}

EXPORT_NO_SYMBOLS;
struct address_info cfg;

int io = -1;
int irq = -1;

MODULE_PARM(io,"i");
MODULE_PARM(irq,"i");

int init_module (void)

{
	printk ("Turtle Beach WaveFront Driver\n"
		"Copyright (C) by Hannu Solvainen, "
		"Paul Barton-Davis 1993-1998.\n");

	if (io == -1 || irq == -1) {
		printk (KERN_INFO "WaveFront: irq and io "
			"options must be set.\n");
		return -EINVAL;
	}

	cfg.io_base = io;
	cfg.irq = irq;

	if (probe_wavefront (&cfg) == 0) {
		return -ENODEV;
	} 
	attach_wavefront (&cfg);
	SOUND_LOCK;
	return 0;
}

void cleanup_module (void)

{
	unload_wavefront (&cfg);
	SOUND_LOCK_END;
}

#endif CONFIG_SOUND_WAVEFRONT_MODULE_AND_MODULE


