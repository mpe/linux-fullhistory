/*
 *    ixj.c
 *
 *    Device Driver for the Internet PhoneJACK and
 *    Internet LineJACK Telephony Cards.
 *
 *    (c) Copyright 1999 Quicknet Technologies, Inc.
 *
 *    This program is free software; you can redistribute it and/or
 *    modify it under the terms of the GNU General Public License
 *    as published by the Free Software Foundation; either version
 *    2 of the License, or (at your option) any later version.
 *
 * Author:          Ed Okerson, <eokerson@quicknet.net>
 *    
 * Contributors:    Greg Herlein, <gherlein@quicknet.net>
 *                  David W. Erhart, <derhart@quicknet.net>
 *                  John Sellers, <jsellers@quicknet.net>
 *                  Mike Preston, <mpreston@quicknet.net>
 *
 * Fixes:
 *
 *	2.3.x port	:		Alan Cox
 *
 * More information about the hardware related to this driver can be found
 * at our website:    http://www.quicknet.net
 *
 */

static char ixj_c_rcsid[] = "$Id: ixj.c,v 3.4 1999/12/16 22:18:36 root Exp root $";

//#define PERFMON_STATS
#define IXJDEBUG 0
#define MAXRINGS 5

#include <linux/module.h>
#include <linux/config.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/tqueue.h>
#include <linux/proc_fs.h>
#include <linux/poll.h>
#include <linux/timer.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/smp_lock.h>

#include <asm/io.h>
#include <asm/segment.h>
#include <asm/uaccess.h>

#ifdef CONFIG_ISAPNP
#include <linux/isapnp.h>
#endif

#include "ixj.h"

#define TYPE(dev) (MINOR(dev) >> 4)
#define NUM(dev) (MINOR(dev) & 0xf)

static int ixjdebug = 0;
static int hertz = HZ;
static int samplerate = 100;

MODULE_PARM(ixjdebug, "i");

static IXJ ixj[IXJMAX];

static struct timer_list ixj_timer;

int ixj_convert_loaded = 0;

/************************************************************************
*
* These are function definitions to allow external modules to register
* enhanced functionality call backs.
*
************************************************************************/

static int Stub(IXJ * J, unsigned long arg)
{
	return 0;
}

static IXJ_REGFUNC ixj_DownloadG729 = &Stub;
static IXJ_REGFUNC ixj_DownloadTS85 = &Stub;
static IXJ_REGFUNC ixj_PreRead = &Stub;
static IXJ_REGFUNC ixj_PostRead = &Stub;
static IXJ_REGFUNC ixj_PreWrite = &Stub;
static IXJ_REGFUNC ixj_PostWrite = &Stub;
static IXJ_REGFUNC ixj_PreIoctl = &Stub;
static IXJ_REGFUNC ixj_PostIoctl = &Stub;

static void ixj_read_frame(int board);
static void ixj_write_frame(int board);
static void ixj_init_timer(void);
static void ixj_add_timer(void);
static void ixj_del_timer(void);
static void ixj_timeout(unsigned long ptr);
static int read_filters(int board);
static int LineMonitor(int board);
static int ixj_fasync(int fd, struct file *, int mode);
static int ixj_hookstate(int board);
static int ixj_record_start(int board);
static void ixj_record_stop(int board);
static int ixj_play_start(int board);
static void ixj_play_stop(int board);
static int ixj_set_tone_on(unsigned short arg, int board);
static int ixj_set_tone_off(unsigned short, int board);
static int ixj_play_tone(int board, char tone);
static int idle(int board);
static void ixj_ring_on(int board);
static void ixj_ring_off(int board);
static void aec_stop(int board);
static void ixj_ringback(int board);
static void ixj_busytone(int board);
static void ixj_dialtone(int board);
static void ixj_cpt_stop(int board);
static char daa_int_read(int board);
static int daa_set_mode(int board, int mode);
static int ixj_linetest(int board);
static int ixj_daa_cid_read(int board);
static void DAA_Coeff_US(int board);
static void DAA_Coeff_UK(int board);
static void DAA_Coeff_France(int board);
static void DAA_Coeff_Germany(int board);
static void DAA_Coeff_Australia(int board);
static void DAA_Coeff_Japan(int board);
static int ixj_init_filter(int board, IXJ_FILTER * jf);
static int ixj_init_tone(int board, IXJ_TONE * ti);
static int ixj_build_cadence(int board, IXJ_CADENCE * cp);
// Serial Control Interface funtions
static int SCI_Control(int board, int control);
static int SCI_Prepare(int board);
static int SCI_WaitHighSCI(int board);
static int SCI_WaitLowSCI(int board);
static DWORD PCIEE_GetSerialNumber(WORD wAddress);

/************************************************************************
CT8020/CT8021 Host Programmers Model
Host address	Function					Access
DSPbase +
0-1		Aux Software Status Register (reserved)		Read Only
2-3		Software Status Register			Read Only
4-5		Aux Software Control Register (reserved)	Read Write
6-7		Software Control Register			Read Write
8-9		Hardware Status Register			Read Only
A-B		Hardware Control Register			Read Write
C-D Host Transmit (Write) Data Buffer Access Port (buffer input)Write Only
E-F Host Recieve (Read) Data Buffer Access Port (buffer input)	Read Only
************************************************************************/

extern __inline__ void ixj_read_HSR(int board)
{
	ixj[board].hsr.bytes.low = inb_p(ixj[board].DSPbase + 8);
	ixj[board].hsr.bytes.high = inb_p(ixj[board].DSPbase + 9);
}
extern __inline__ int IsControlReady(int board)
{
	ixj_read_HSR(board);
	return ixj[board].hsr.bits.controlrdy ? 1 : 0;
}

extern __inline__ int IsStatusReady(int board)
{
	ixj_read_HSR(board);
	return ixj[board].hsr.bits.statusrdy ? 1 : 0;
}

extern __inline__ int IsRxReady(int board)
{
	ixj_read_HSR(board);
	return ixj[board].hsr.bits.rxrdy ? 1 : 0;
}

extern __inline__ int IsTxReady(int board)
{
	ixj_read_HSR(board);
	return ixj[board].hsr.bits.txrdy ? 1 : 0;
}

extern __inline__ BYTE SLIC_GetState(int board)
{
	IXJ *j = &ixj[board];

	j->pld_slicr.byte = inb_p(j->XILINXbase + 0x01);

	return j->pld_slicr.bits.state;
}

static BOOL SLIC_SetState(BYTE byState, int board)
{
	BOOL fRetVal = FALSE;
	IXJ *j = &ixj[board];

	// Set the C1, C2, C3 & B2EN signals.
	switch (byState) {
	case PLD_SLIC_STATE_OC:
		j->pld_slicw.bits.c1 = 0;
		j->pld_slicw.bits.c2 = 0;
		j->pld_slicw.bits.c3 = 0;
		j->pld_slicw.bits.b2en = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		fRetVal = TRUE;
		break;
	case PLD_SLIC_STATE_RINGING:
		j->pld_slicw.bits.c1 = 1;
		j->pld_slicw.bits.c2 = 0;
		j->pld_slicw.bits.c3 = 0;
		j->pld_slicw.bits.b2en = 1;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		fRetVal = TRUE;
		break;
	case PLD_SLIC_STATE_ACTIVE:
		j->pld_slicw.bits.c1 = 0;
		j->pld_slicw.bits.c2 = 1;
		j->pld_slicw.bits.c3 = 0;
		j->pld_slicw.bits.b2en = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		fRetVal = TRUE;
		break;
	case PLD_SLIC_STATE_OHT:	// On-hook transmit

		j->pld_slicw.bits.c1 = 1;
		j->pld_slicw.bits.c2 = 1;
		j->pld_slicw.bits.c3 = 0;
		j->pld_slicw.bits.b2en = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		fRetVal = TRUE;
		break;
	case PLD_SLIC_STATE_TIPOPEN:
		j->pld_slicw.bits.c1 = 0;
		j->pld_slicw.bits.c2 = 0;
		j->pld_slicw.bits.c3 = 1;
		j->pld_slicw.bits.b2en = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		fRetVal = TRUE;
		break;
	case PLD_SLIC_STATE_STANDBY:
		j->pld_slicw.bits.c1 = 1;
		j->pld_slicw.bits.c2 = 0;
		j->pld_slicw.bits.c3 = 1;
		j->pld_slicw.bits.b2en = 1;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		fRetVal = TRUE;
		break;
	case PLD_SLIC_STATE_APR:	// Active polarity reversal

		j->pld_slicw.bits.c1 = 0;
		j->pld_slicw.bits.c2 = 1;
		j->pld_slicw.bits.c3 = 1;
		j->pld_slicw.bits.b2en = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		fRetVal = TRUE;
		break;
	case PLD_SLIC_STATE_OHTPR:	// OHT polarity reversal

		j->pld_slicw.bits.c1 = 1;
		j->pld_slicw.bits.c2 = 1;
		j->pld_slicw.bits.c3 = 1;
		j->pld_slicw.bits.b2en = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		fRetVal = TRUE;
		break;
	default:
		fRetVal = FALSE;
		break;
	}

	return fRetVal;
}

int ixj_register(int index, IXJ_REGFUNC regfunc)
{
	int cnt;
	int retval = 0;
	switch (index) {
	case G729LOADER:
		ixj_DownloadG729 = regfunc;
		for (cnt = 0; cnt < IXJMAX; cnt++)
			ixj_DownloadG729(&ixj[cnt], 0L);
		break;
	case TS85LOADER:
		ixj_DownloadTS85 = regfunc;
		for (cnt = 0; cnt < IXJMAX; cnt++)
			ixj_DownloadTS85(&ixj[cnt], 0L);
		break;
	case PRE_READ:
		ixj_PreRead = regfunc;
		break;
	case POST_READ:
		ixj_PostRead = regfunc;
		break;
	case PRE_WRITE:
		ixj_PreWrite = regfunc;
		break;
	case POST_WRITE:
		ixj_PostWrite = regfunc;
		break;
	case PRE_IOCTL:
		ixj_PreIoctl = regfunc;
		break;
	case POST_IOCTL:
		ixj_PostIoctl = regfunc;
		break;
	default:
		retval = 1;
	}
	return retval;
}

int ixj_unregister(int index)
{
	int retval = 0;
	switch (index) {
	case G729LOADER:
		ixj_DownloadG729 = &Stub;
		break;
	case TS85LOADER:
		ixj_DownloadTS85 = &Stub;
		break;
	case PRE_READ:
		ixj_PreRead = &Stub;
		break;
	case POST_READ:
		ixj_PostRead = &Stub;
		break;
	case PRE_WRITE:
		ixj_PreWrite = &Stub;
		break;
	case POST_WRITE:
		ixj_PostWrite = &Stub;
		break;
	case PRE_IOCTL:
		ixj_PreIoctl = &Stub;
		break;
	case POST_IOCTL:
		ixj_PostIoctl = &Stub;
		break;
	default:
		retval = 1;
	}
	return retval;
}

static void ixj_init_timer(void)
{
	init_timer(&ixj_timer);
	ixj_timer.function = ixj_timeout;
	ixj_timer.data = (int) NULL;
}

static void ixj_add_timer(void)
{
	ixj_timer.expires = jiffies + (hertz / samplerate);
	add_timer(&ixj_timer);
}

static void ixj_del_timer(void)
{
	del_timer(&ixj_timer);
}

static void ixj_tone_timeout(int board)
{
	IXJ *j = &ixj[board];
	IXJ_TONE ti;
	
	j->tone_state++;
	if (j->tone_state == 3) {
		j->tone_state = 0;
		if (j->cadence_t) {
			j->tone_cadence_state++;
			if (j->tone_cadence_state >= j->cadence_t->elements_used) 
			{
				switch (j->cadence_t->termination) 
				{
					case PLAY_ONCE:
						ixj_cpt_stop(board);
							break;
					case REPEAT_LAST_ELEMENT:
						j->tone_cadence_state--;
						ixj_play_tone(board, j->cadence_t->ce[j->tone_cadence_state].index);
						break;
					case REPEAT_ALL:
						j->tone_cadence_state = 0;
						if (j->cadence_t->ce[j->tone_cadence_state].freq0) 
						{
							ti.tone_index = j->cadence_t->ce[j->tone_cadence_state].index;
							ti.freq0 = j->cadence_t->ce[j->tone_cadence_state].freq0;
							ti.gain0 = j->cadence_t->ce[j->tone_cadence_state].gain0;
							ti.freq1 = j->cadence_t->ce[j->tone_cadence_state].freq1;
							ti.gain1 = j->cadence_t->ce[j->tone_cadence_state].gain1;
							ixj_init_tone(board, &ti);
						}
						ixj_set_tone_on(j->cadence_t->ce[0].tone_on_time, board);
						ixj_set_tone_off(j->cadence_t->ce[0].tone_off_time, board);
						ixj_play_tone(board, j->cadence_t->ce[0].index);
						break;
				}
			} else {
				if (j->cadence_t->ce[j->tone_cadence_state].gain0) 
				{
					ti.tone_index = j->cadence_t->ce[j->tone_cadence_state].index;
					ti.freq0 = j->cadence_t->ce[j->tone_cadence_state].freq0;
					ti.gain0 = j->cadence_t->ce[j->tone_cadence_state].gain0;
					ti.freq1 = j->cadence_t->ce[j->tone_cadence_state].freq1;
					ti.gain1 = j->cadence_t->ce[j->tone_cadence_state].gain1;
					ixj_init_tone(board, &ti);
				}
				ixj_set_tone_on(j->cadence_t->ce[j->tone_cadence_state].tone_on_time, board);
				ixj_set_tone_off(j->cadence_t->ce[j->tone_cadence_state].tone_off_time, board);
				ixj_play_tone(board, j->cadence_t->ce[j->tone_cadence_state].index);
			}
		}
	}
}

static void ixj_timeout(unsigned long ptr)
{
	int board;
	unsigned long jifon;
	IXJ *j;

	for (board = 0; board < IXJMAX; board++) 
	{
		j = &ixj[board];

		if (j->DSPbase) 
		{
#ifdef PERFMON_STATS
			j->timerchecks++;
#endif
			if (j->tone_state) 
			{
				if (!ixj_hookstate(board)) 
				{
					ixj_cpt_stop(board);
					if (j->m_hook) 
					{
						j->m_hook = 0;
						j->ex.bits.hookstate = 1;
						kill_fasync(&j->async_queue, SIGIO, POLL_IN);	// Send apps notice of change
					}
					goto timer_end;
				}
				if (j->tone_state == 1)
					jifon = (hertz * j->tone_on_time * 25 / 100000);
				else
					jifon = (hertz * j->tone_on_time * 25 / 100000) +
					    (hertz * j->tone_off_time * 25 / 100000);
				if (jiffies < j->tone_start_jif + jifon) {
					if (j->tone_state == 1) {
						ixj_play_tone(board, j->tone_index);
						if (j->dsp.low == 0x20) {
							goto timer_end;
						}
					} else {
						ixj_play_tone(board, 0);
						if (j->dsp.low == 0x20) {
							goto timer_end;
						}
					}
				} else {
					ixj_tone_timeout(board);
					if (j->flags.dialtone) {
						ixj_dialtone(board);
					}
					if (j->flags.busytone) {
						ixj_busytone(board);
						if (j->dsp.low == 0x20) {
							goto timer_end;
						}
					}
					if (j->flags.ringback) {
						ixj_ringback(board);
						if (j->dsp.low == 0x20) {
							goto timer_end;
						}
					}
					if (!j->tone_state) {
						if (j->dsp.low == 0x20 || (j->play_mode == -1 && j->rec_mode == -1))
							idle(board);
						if (j->dsp.low == 0x20 && j->play_mode != -1)
							ixj_play_start(board);
						if (j->dsp.low == 0x20 && j->rec_mode != -1)
							ixj_record_start(board);
					}
				}
			}
			if (!j->tone_state || j->dsp.low != 0x20) {
				if (IsRxReady(board)) {
					ixj_read_frame(board);
				}
				if (IsTxReady(board)) {
					ixj_write_frame(board);
				}
			}
			if (j->flags.cringing) {
				if (ixj_hookstate(board) & 1) {
					j->flags.cringing = 0;
					ixj_ring_off(board);
				} else {
					if (jiffies - j->ring_cadence_jif >= (hertz/2)) {
						j->ring_cadence_t--;
						if (j->ring_cadence_t == -1)
							j->ring_cadence_t = 15;
						j->ring_cadence_jif = jiffies;
					}
					if (j->ring_cadence & 1 << j->ring_cadence_t) {
						ixj_ring_on(board);
					} else {
						ixj_ring_off(board);
					}
					goto timer_end;
				}
			}
			if (!j->flags.ringing) {
				if (ixj_hookstate(board)) {
					if (j->dsp.low == 0x21 &&
					    j->pld_slicr.bits.state != PLD_SLIC_STATE_ACTIVE)
               // Internet LineJACK
					{
						SLIC_SetState(PLD_SLIC_STATE_ACTIVE, board);
					}
					LineMonitor(board);
					read_filters(board);
					ixj_WriteDSPCommand(0x511B, board);
					j->proc_load = j->ssr.high << 8 | j->ssr.low;
					if (!j->m_hook) {
						j->m_hook = j->ex.bits.hookstate = 1;
						kill_fasync(&j->async_queue, SIGIO, POLL_IN);	// Send apps notice of change
					}
				} else {
					if (j->dsp.low == 0x21 &&
					    j->pld_slicr.bits.state == PLD_SLIC_STATE_ACTIVE)
               // Internet LineJACK
					{
						SLIC_SetState(PLD_SLIC_STATE_STANDBY, board);
					}
					if (j->ex.bits.dtmf_ready) {
						j->dtmf_wp = j->dtmf_rp = j->ex.bits.dtmf_ready = 0;
					}
					if (j->m_hook) {
						j->m_hook = 0;
						j->ex.bits.hookstate = 1;
						kill_fasync(&j->async_queue, SIGIO, POLL_IN);	// Send apps notice of change
					}
				}
			}
			if (j->cardtype == 300) {
				if (j->flags.pstn_present) {
					j->pld_scrr.byte = inb_p(j->XILINXbase);
					if (jiffies >= j->pstn_sleeptil && j->pld_scrr.bits.daaflag) {
						daa_int_read(board);
						if (j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.RING) {
							if (!j->flags.pstn_ringing) {
								j->flags.pstn_ringing = 1;
								if (j->daa_mode != SOP_PU_RINGING)
									daa_set_mode(board, SOP_PU_RINGING);
							}
						}
						if (j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.VDD_OK) {
							j->pstn_winkstart = 0;
							if (j->flags.pstn_ringing && !j->pstn_envelope) {
								j->ex.bits.pstn_ring = 0;
								j->pstn_envelope = 1;
								j->pstn_ring_start = jiffies;
							}
						} else {
							if (j->flags.pstn_ringing && j->pstn_envelope &&
							    jiffies > j->pstn_ring_start + ((hertz * 15) / 10)) {
								j->ex.bits.pstn_ring = 1;
								j->pstn_envelope = 0;
							} else if (j->daa_mode == SOP_PU_CONVERSATION) {
								if (!j->pstn_winkstart) {
									j->pstn_winkstart = jiffies;
								} else if (jiffies > j->pstn_winkstart + (hertz * j->winktime / 1000)) {
									daa_set_mode(board, SOP_PU_SLEEP);
									j->pstn_winkstart = 0;
									j->ex.bits.pstn_wink = 1;
								}
							} else {
								j->ex.bits.pstn_ring = 0;
							}
						}
						if (j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.Cadence) {
							if (j->daa_mode == SOP_PU_RINGING) {
								daa_set_mode(board, SOP_PU_SLEEP);
								j->flags.pstn_ringing = 0;
								j->ex.bits.pstn_ring = 0;
							}
						}
						if (j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.Caller_ID) {
							if (j->daa_mode == SOP_PU_RINGING && j->flags.pstn_ringing) {
								j->pstn_cid_intr = 1;
								j->pstn_cid_recieved = jiffies;
							}
						}
					} else {
						if (j->pld_scrr.bits.daaflag) {
							daa_int_read(board);
						}
						j->ex.bits.pstn_ring = 0;
						if (j->pstn_cid_intr && jiffies > j->pstn_cid_recieved + (hertz * 3)) {
							if (j->daa_mode == SOP_PU_RINGING) {
								ixj_daa_cid_read(board);
								j->ex.bits.caller_id = 1;
							}
							j->pstn_cid_intr = 0;
						} else {
							j->ex.bits.caller_id = 0;
						}
						if (!j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.VDD_OK) {
							if (j->flags.pstn_ringing && j->pstn_envelope) {
								j->ex.bits.pstn_ring = 1;
								j->pstn_envelope = 0;
							} else if (j->daa_mode == SOP_PU_CONVERSATION) {
								if (!j->pstn_winkstart) {
									j->pstn_winkstart = jiffies;
								} else if (jiffies > j->pstn_winkstart + (hertz * 320 / 1000)) {
									daa_set_mode(board, SOP_PU_SLEEP);
									j->pstn_winkstart = 0;
									j->ex.bits.pstn_wink = 1;
								}
							}
						}
					}
				}
			}
			if ((j->ex.bits.f0 || j->ex.bits.f1 || j->ex.bits.f2 || j->ex.bits.f3)
			    && j->filter_cadence) {
			}
			if (j->ex.bytes) {
				wake_up_interruptible(&j->poll_q);	// Wake any blocked selects
				kill_fasync(&j->async_queue, SIGIO, POLL_IN);	// Send apps notice of change
			}
		} else {
			break;
		}
	}
timer_end:
	ixj_add_timer();
}

static int ixj_status_wait(int board)
{
	unsigned long jif;

	jif = jiffies;
	while (!IsStatusReady(board)) {
		if (jiffies - jif > (60 * (hertz / 100))) {
			return -1;
		}
	}
	return 0;
}

int ixj_WriteDSPCommand(unsigned short cmd, int board)
{
	BYTES bytes;
	unsigned long jif;

	bytes.high = (cmd & 0xFF00) >> 8;
	bytes.low = cmd & 0x00FF;
	jif = jiffies;
	while (!IsControlReady(board)) {
		if (jiffies - jif > (60 * (hertz / 100))) {
			return -1;
		}
	}
	outb_p(bytes.low, ixj[board].DSPbase + 6);
	outb_p(bytes.high, ixj[board].DSPbase + 7);

	if (ixj_status_wait(board)) {
		ixj[board].ssr.low = 0xFF;
		ixj[board].ssr.high = 0xFF;
		return -1;
	}
/* Read Software Status Register */
	ixj[board].ssr.low = inb_p(ixj[board].DSPbase + 2);
	ixj[board].ssr.high = inb_p(ixj[board].DSPbase + 3);
	return 0;
}

/***************************************************************************
*
*  General Purpose IO Register read routine
*
***************************************************************************/
extern __inline__ int ixj_gpio_read(int board)
{
	if (ixj_WriteDSPCommand(0x5143, board))
		return -1;

	ixj[board].gpio.bytes.low = ixj[board].ssr.low;
	ixj[board].gpio.bytes.high = ixj[board].ssr.high;

	return 0;
}

extern __inline__ void LED_SetState(int state, int board)
{
	if (ixj[board].dsp.low == 0x21) {
		ixj[board].pld_scrw.bits.led1 = state & 0x1 ? 1 : 0;
		ixj[board].pld_scrw.bits.led2 = state & 0x2 ? 1 : 0;
		ixj[board].pld_scrw.bits.led3 = state & 0x4 ? 1 : 0;
		ixj[board].pld_scrw.bits.led4 = state & 0x8 ? 1 : 0;

		outb_p(ixj[board].pld_scrw.byte, ixj[board].XILINXbase);
	}
}

/*********************************************************************
*  GPIO Pins are configured as follows on the Quicknet Internet
*  PhoneJACK Telephony Cards
* 
* POTS Select        GPIO_6=0 GPIO_7=0
* Mic/Speaker Select GPIO_6=0 GPIO_7=1
* Handset Select     GPIO_6=1 GPIO_7=0
*
* SLIC Active        GPIO_1=0 GPIO_2=1 GPIO_5=0
* SLIC Ringing       GPIO_1=1 GPIO_2=1 GPIO_5=0
* SLIC Open Circuit  GPIO_1=0 GPIO_2=0 GPIO_5=0
*
* Hook Switch changes reported on GPIO_3
*********************************************************************/
static int ixj_set_port(int board, int arg)
{
	IXJ *j = &ixj[board];

	if (j->cardtype == 400) {
		if (arg != PORT_POTS)
			return 10;
		else
			return 0;
	}
	switch (arg) {
	case PORT_POTS:
		j->port = PORT_POTS;
		switch (j->cardtype) {
		case 500:
			j->pld_slicw.pcib.mic = 0;
			j->pld_slicw.pcib.spk = 0;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			break;
		case 300:
			if (ixj_WriteDSPCommand(0xC528, board))		/* Write CODEC config to
									   Software Control Register */
				return 2;
			j->pld_scrw.bits.daafsyncen = 0;	// Turn off DAA Frame Sync

			outb_p(j->pld_scrw.byte, j->XILINXbase);
			j->pld_clock.byte = 0;
			outb_p(j->pld_clock.byte, j->XILINXbase + 0x04);
			j->pld_slicw.bits.rly1 = 1;
			j->pld_slicw.bits.spken = 0;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			SLIC_SetState(PLD_SLIC_STATE_STANDBY, board);
			break;
		case 100:
			j->gpio.bytes.high = 0x0B;
			j->gpio.bits.gpio6 = 0;
			j->gpio.bits.gpio7 = 0;
			ixj_WriteDSPCommand(j->gpio.word, board);
			break;
		}
		break;
	case PORT_PSTN:
		if (j->cardtype == 300) {
			ixj_WriteDSPCommand(0xC534, board);	/* Write CODEC config to Software Control Register */

			j->pld_slicw.bits.rly3 = 0;
			j->pld_slicw.bits.rly1 = 1;
			j->pld_slicw.bits.spken = 0;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			j->port = PORT_PSTN;
		} else {
			return 4;
		}
		break;
	case PORT_SPEAKER:
		j->port = PORT_SPEAKER;
		switch (j->cardtype) {
		case 500:
			j->pld_slicw.pcib.mic = 1;
			j->pld_slicw.pcib.spk = 1;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			break;
		case 300:
			break;
		case 100:
			j->gpio.bytes.high = 0x0B;
			j->gpio.bits.gpio6 = 0;
			j->gpio.bits.gpio7 = 1;
			ixj_WriteDSPCommand(j->gpio.word, board);
			break;
		}
		break;
	case PORT_HANDSET:
		if (j->cardtype == 300 || j->cardtype == 500) {
			return 5;
		} else {
			j->gpio.bytes.high = 0x0B;
			j->gpio.bits.gpio6 = 1;
			j->gpio.bits.gpio7 = 0;
			ixj_WriteDSPCommand(j->gpio.word, board);
			j->port = PORT_HANDSET;
		}
		break;
	default:
		return 6;
		break;
	}
	return 0;
}

static int ixj_set_pots(int board, int arg)
{
	IXJ *j = &ixj[board];

	if (j->cardtype == 300) {
		if (arg) {
			if (j->port == PORT_PSTN) {
				j->pld_slicw.bits.rly1 = 0;
				outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
				return 1;
			} else {
				return 0;
			}
		} else {
			j->pld_slicw.bits.rly1 = 1;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			return 1;
		}
	} else {
		return 0;
	}
}

static void ixj_ring_on(int board)
{
	IXJ *j = &ixj[board];
	if (j->dsp.low == 0x20)	// Internet PhoneJACK
	 {
		if (ixjdebug > 0)
			printk(KERN_INFO "IXJ Ring On /dev/phone%d\n", board);

		j->gpio.bytes.high = 0x0B;
		j->gpio.bytes.low = 0x00;
		j->gpio.bits.gpio1 = 1;
		j->gpio.bits.gpio2 = 1;
		j->gpio.bits.gpio5 = 0;
		ixj_WriteDSPCommand(j->gpio.word, board);	/* send the ring signal */
	} else			//  Internet LineJACK, Internet PhoneJACK Lite or
              //  Internet PhoneJACK PCI
	 {
		if (ixjdebug > 0)
			printk(KERN_INFO "IXJ Ring On /dev/phone%d\n", board);

		SLIC_SetState(PLD_SLIC_STATE_RINGING, board);
	}
}

static int ixj_hookstate(int board)
{
	unsigned long det;
	IXJ *j = &ixj[board];
	int fOffHook = 0;

	switch (j->cardtype) {
	case 100:
		ixj_gpio_read(board);
		fOffHook = j->gpio.bits.gpio3read ? 1 : 0;
		break;
	case 300:
	case 400:
	case 500:
		SLIC_GetState(board);
		if (j->pld_slicr.bits.state == PLD_SLIC_STATE_ACTIVE ||
		    j->pld_slicr.bits.state == PLD_SLIC_STATE_STANDBY) 
		{
			if (j->flags.ringing) 
			{
				if(!in_interrupt())
				{
					det = jiffies + (hertz / 50);
					while (time_before(jiffies, det)) {
						current->state = TASK_INTERRUPTIBLE;
						schedule_timeout(1);
					}
				}
				SLIC_GetState(board);
				if (j->pld_slicr.bits.state == PLD_SLIC_STATE_RINGING) {
					ixj_ring_on(board);
				}
			}
			if (j->cardtype == 500) {
				j->pld_scrr.byte = inb_p(j->XILINXbase);
				fOffHook = j->pld_scrr.pcib.det ? 1 : 0;
			} else
				fOffHook = j->pld_slicr.bits.det ? 1 : 0;
		}
		break;
	}
	if (j->r_hook != fOffHook) {
		j->r_hook = fOffHook;
		if (j->port != PORT_POTS) {
			j->ex.bits.hookstate = 1;
			kill_fasync(&j->async_queue, SIGIO, POLL_IN);	// Send apps notice of change

		}
	}
	if (j->port == PORT_PSTN && j->daa_mode == SOP_PU_CONVERSATION)
		fOffHook |= 2;

	if (j->port == PORT_SPEAKER)
		fOffHook |= 2;

	if (j->port == PORT_HANDSET)
		fOffHook |= 2;

	return fOffHook;
}

static void ixj_ring_off(board)
{
	IXJ *j = &ixj[board];

	if (j->dsp.low == 0x20)	// Internet PhoneJACK
	 {
		if (ixjdebug > 0)
			printk(KERN_INFO "IXJ Ring Off\n");
		j->gpio.bytes.high = 0x0B;
		j->gpio.bytes.low = 0x00;
		j->gpio.bits.gpio1 = 0;
		j->gpio.bits.gpio2 = 1;
		j->gpio.bits.gpio5 = 0;
		ixj_WriteDSPCommand(j->gpio.word, board);
	} else			// Internet LineJACK
	 {
		if (ixjdebug > 0)
			printk(KERN_INFO "IXJ Ring Off\n");

		SLIC_SetState(PLD_SLIC_STATE_STANDBY, board);	

		SLIC_GetState(board);
	}
}

static void ixj_ring_start(int board)
{
	IXJ *j = &ixj[board];

	j->flags.cringing = 1;
	if (ixj_hookstate(board) & 1) {
		if (j->port == PORT_POTS)
			ixj_ring_off(board);
		j->flags.cringing = 0;
	} else {
		j->ring_cadence_jif = jiffies;
		j->ring_cadence_t = 15;
		if (j->ring_cadence & 1 << j->ring_cadence_t) {
			ixj_ring_on(board);
		} else {
			ixj_ring_off(board);
		}
	}
}

static int ixj_ring(int board)
{
	char cntr;
	unsigned long jif, det;
	IXJ *j = &ixj[board];

	j->flags.ringing = 1;
	if (ixj_hookstate(board) & 1) {
		ixj_ring_off(board);
		j->flags.ringing = 0;
		return 1;
	}
	det = 0;
	for (cntr = 0; cntr < j->maxrings; cntr++) {
		jif = jiffies + (1 * hertz);
		ixj_ring_on(board);
		while (time_before(jiffies, jif)) {
			if (ixj_hookstate(board) & 1) {
				ixj_ring_off(board);
				j->flags.ringing = 0;
				return 1;
			}
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
			if(signal_pending(current))
				break;
		}
		jif = jiffies + (3 * hertz);
		ixj_ring_off(board);
		while (time_before(jiffies, jif)) {
			if (ixj_hookstate(board) & 1) {
				det = jiffies + (hertz / 100);
				while (time_before(jiffies, det)) {
					current->state = TASK_INTERRUPTIBLE;
					schedule_timeout(1);
					if(signal_pending(current))
						break;
				}
				if (ixj_hookstate(board) & 1) {
					j->flags.ringing = 0;
					return 1;
				}
			}
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
			if(signal_pending(current))
				break;
		}
	}
	ixj_ring_off(board);
	j->flags.ringing = 0;
	return 0;
}

int ixj_open(struct phone_device *p, struct file *file_p)
{
	IXJ *j = &ixj[p->board];

	if (!j->DSPbase)
		return -ENODEV;

	if (file_p->f_mode & FMODE_READ)
		j->readers++;
	if (file_p->f_mode & FMODE_WRITE)
		j->writers++;

	if (ixjdebug > 0)
//    printk(KERN_INFO "Opening board %d\n", NUM(inode->i_rdev));
		printk(KERN_INFO "Opening board %d\n", p->board);

	return 0;
}

int ixj_release(struct inode *inode, struct file *file_p)
{
	IXJ_TONE ti;
	int board = NUM(inode->i_rdev);
	IXJ *j = &ixj[board];

	if (ixjdebug > 0)
		printk(KERN_INFO "Closing board %d\n", NUM(inode->i_rdev));

	lock_kernel();
	daa_set_mode(board, SOP_PU_SLEEP);
	ixj_set_port(board, PORT_POTS);
	aec_stop(board);
	ixj_play_stop(board);
	ixj_record_stop(board);

	ti.tone_index = 10;
	ti.gain0 = 1;
	ti.freq0 = hz941;
	ti.gain1 = 0;
	ti.freq1 = hz1209;
	ti.tone_index = 11;
	ti.gain0 = 1;
	ti.freq0 = hz941;
	ti.gain1 = 0;
	ti.freq1 = hz1336;
	ti.tone_index = 12;
	ti.gain0 = 1;
	ti.freq0 = hz941;
	ti.gain1 = 0;
	ti.freq1 = hz1477;
	ti.tone_index = 13;
	ti.gain0 = 1;
	ti.freq0 = hz800;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(board, &ti);
	ti.tone_index = 14;
	ti.gain0 = 1;
	ti.freq0 = hz1000;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(board, &ti);
	ti.tone_index = 15;
	ti.gain0 = 1;
	ti.freq0 = hz1250;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(board, &ti);
	ti.tone_index = 16;
	ti.gain0 = 1;
	ti.freq0 = hz950;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(board, &ti);
	ti.tone_index = 17;
	ti.gain0 = 1;
	ti.freq0 = hz1100;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(board, &ti);
	ti.tone_index = 18;
	ti.gain0 = 1;
	ti.freq0 = hz1400;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(board, &ti);
	ti.tone_index = 19;
	ti.gain0 = 1;
	ti.freq0 = hz1500;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(board, &ti);
	ti.tone_index = 20;
	ti.gain0 = 1;
	ti.freq0 = hz1600;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(board, &ti);
	ti.tone_index = 21;
	ti.gain0 = 1;
	ti.freq0 = hz1800;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(board, &ti);
	ti.tone_index = 22;
	ti.gain0 = 1;
	ti.freq0 = hz2100;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(board, &ti);
	ti.tone_index = 23;
	ti.gain0 = 1;
	ti.freq0 = hz1300;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(board, &ti);
	ti.tone_index = 24;
	ti.gain0 = 1;
	ti.freq0 = hz2450;
	ti.gain1 = 0;
	ti.freq1 = 0;
	ixj_init_tone(board, &ti);
	ti.tone_index = 25;
	ti.gain0 = 1;
	ti.freq0 = hz350;
	ti.gain1 = 0;
	ti.freq1 = hz440;
	ixj_init_tone(board, &ti);
	ti.tone_index = 26;
	ti.gain0 = 1;
	ti.freq0 = hz440;
	ti.gain1 = 0;
	ti.freq1 = hz480;
	ixj_init_tone(board, &ti);
	ti.tone_index = 27;
	ti.gain0 = 1;
	ti.freq0 = hz480;
	ti.gain1 = 0;
	ti.freq1 = hz620;
	ixj_init_tone(board, &ti);

	idle(board);

	if (file_p->f_mode & FMODE_READ)
		j->readers--;
	if (file_p->f_mode & FMODE_WRITE)
		j->writers--;

	if (j->read_buffer && !j->readers) {
		kfree(j->read_buffer);
		j->read_buffer = NULL;
		j->read_buffer_size = 0;
	}
	if (j->write_buffer && !j->writers) {
		kfree(j->write_buffer);
		j->write_buffer = NULL;
		j->write_buffer_size = 0;
	}
	j->rec_codec = j->play_codec = 0;
	j->rec_frame_size = j->play_frame_size = 0;
	ixj_fasync(-1, file_p, 0);	// remove from list of async notification

	unlock_kernel();
	return 0;
}

static int read_filters(int board)
{
	unsigned short fc, cnt;
	IXJ *j = &ixj[board];

	if (ixj_WriteDSPCommand(0x5144, board))
		return -1;

	fc = j->ssr.high << 8 | j->ssr.low;
	if (fc == j->frame_count)
		return 1;

	j->frame_count = fc;

	for (cnt = 0; cnt < 4; cnt++) {
		if (ixj_WriteDSPCommand(0x5154 + cnt, board))
			return -1;

		if (ixj_WriteDSPCommand(0x515C, board))
			return -1;

		j->filter_hist[cnt] = j->ssr.high << 8 | j->ssr.low;
		if ((j->filter_hist[cnt] & 1 && !(j->filter_hist[cnt] & 2)) ||
		(j->filter_hist[cnt] & 2 && !(j->filter_hist[cnt] & 1))) {
			switch (cnt) {
			case 0:
				j->ex.bits.f0 = 1;
				break;
			case 1:
				j->ex.bits.f1 = 1;
				break;
			case 2:
				j->ex.bits.f2 = 1;
				break;
			case 3:
				j->ex.bits.f3 = 1;
				break;
			}
		}
	}
	return 0;
}

static int LineMonitor(int board)
{
	IXJ *j = &ixj[board];

	if (j->dtmf_proc) {
		return -1;
	}
	j->dtmf_proc = 1;

	if (ixj_WriteDSPCommand(0x7000, board))		// Line Monitor

		return -1;

	j->dtmf.bytes.high = j->ssr.high;
	j->dtmf.bytes.low = j->ssr.low;
	if (!j->dtmf_state && j->dtmf.bits.dtmf_valid) {
		j->dtmf_state = 1;
		j->dtmf_current = j->dtmf.bits.digit;
	}
	if (j->dtmf_state && !j->dtmf.bits.dtmf_valid)	// && j->dtmf_wp != j->dtmf_rp)
	 {
		j->dtmfbuffer[j->dtmf_wp] = j->dtmf_current;
		j->dtmf_wp++;
		if (j->dtmf_wp == 79)
			j->dtmf_wp = 0;
		j->ex.bits.dtmf_ready = 1;
		j->dtmf_state = 0;
	}
	j->dtmf_proc = 0;

	return 0;
}

ssize_t ixj_read(struct file * file_p, char *buf, size_t length, loff_t * ppos)
{
	unsigned long i = *ppos;
	IXJ *j = &ixj[NUM(file_p->f_dentry->d_inode->i_rdev)];
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&j->read_q, &wait);
	current->state = TASK_INTERRUPTIBLE;
	mb();

	while (!j->read_buffer_ready || (j->dtmf_state && j->flags.dtmf_oob)) {
		++j->read_wait;
		if (file_p->f_flags & O_NONBLOCK) {
			current->state = TASK_RUNNING;
			remove_wait_queue(&j->read_q, &wait);
			return -EAGAIN;
		}
		if (!ixj_hookstate(NUM(file_p->f_dentry->d_inode->i_rdev))) {
			current->state = TASK_RUNNING;
			remove_wait_queue(&j->read_q, &wait);
			return 0;
		}
		interruptible_sleep_on(&j->read_q);
		if (signal_pending(current)) {
			current->state = TASK_RUNNING;
			remove_wait_queue(&j->read_q, &wait);
			return -EINTR;
		}
	}

	remove_wait_queue(&j->read_q, &wait);
	current->state = TASK_RUNNING;
	/* Don't ever copy more than the user asks */
	i = copy_to_user(buf, j->read_buffer, min(length, j->read_buffer_size));
	j->read_buffer_ready = 0;
	if (i)
		return -EFAULT;
	else
		return min(length, j->read_buffer_size);
}

ssize_t ixj_enhanced_read(struct file * file_p, char *buf, size_t length,
			  loff_t * ppos)
{
	int pre_retval;
	ssize_t read_retval = 0;
	IXJ *j = &ixj[NUM(file_p->f_dentry->d_inode->i_rdev)];

	pre_retval = ixj_PreRead(j, 0L);
	switch (pre_retval) {
	case NORMAL:
		read_retval = ixj_read(file_p, buf, length, ppos);
		ixj_PostRead(j, 0L);
		break;
	case NOPOST:
		read_retval = ixj_read(file_p, buf, length, ppos);
		break;
	case POSTONLY:
		ixj_PostRead(j, 0L);
		break;
	default:
		read_retval = pre_retval;
	}
	return read_retval;
}

ssize_t ixj_write(struct file * file_p, const char *buf, size_t count, loff_t * ppos)
{
	unsigned long i = *ppos;
	int board = NUM(file_p->f_dentry->d_inode->i_rdev);
	IXJ *j = &ixj[board];
	DECLARE_WAITQUEUE(wait, current);

	add_wait_queue(&j->read_q, &wait);
	current->state = TASK_INTERRUPTIBLE;
	mb();


	while (!j->write_buffers_empty) {
		++j->write_wait;
		if (file_p->f_flags & O_NONBLOCK) {
			current->state = TASK_RUNNING;
			remove_wait_queue(&j->read_q, &wait);
			return -EAGAIN;
		}
		if (!ixj_hookstate(NUM(file_p->f_dentry->d_inode->i_rdev))) {
			current->state = TASK_RUNNING;
			remove_wait_queue(&j->read_q, &wait);
			return 0;
		}
		interruptible_sleep_on(&j->write_q);
		if (signal_pending(current)) {
			current->state = TASK_RUNNING;
			remove_wait_queue(&j->read_q, &wait);
			return -EINTR;
		}
	}
	current->state = TASK_RUNNING;
	remove_wait_queue(&j->read_q, &wait);
	if (j->write_buffer_wp + count >= j->write_buffer_end)
		j->write_buffer_wp = j->write_buffer;
	i = copy_from_user(j->write_buffer_wp, buf, min(count, j->write_buffer_size));
	if (i)
		return -EFAULT;

	return min(count, j->write_buffer_size);
}

ssize_t ixj_enhanced_write(struct file * file_p, const char *buf, size_t count,
			   loff_t * ppos)
{
	int pre_retval;
	ssize_t write_retval = 0;
	IXJ *j = &ixj[NUM(file_p->f_dentry->d_inode->i_rdev)];

	pre_retval = ixj_PreWrite(j, 0L);
	switch (pre_retval) {
	case NORMAL:
		write_retval = ixj_write(file_p, buf, count, ppos);
		if (write_retval != -EFAULT) {
			ixj_PostWrite(j, 0L);
			j->write_buffer_wp += count;
			j->write_buffers_empty--;
		}
		break;
	case NOPOST:
		write_retval = ixj_write(file_p, buf, count, ppos);
		if (write_retval != -EFAULT) {
			j->write_buffer_wp += count;
			j->write_buffers_empty--;
		}
		break;
	case POSTONLY:
		ixj_PostWrite(j, 0L);
		break;
	default:
		write_retval = pre_retval;
	}
	return write_retval;
}

static void ixj_read_frame(int board)
{
	int cnt, dly;
	IXJ *j = &ixj[board];

	if (j->read_buffer) {
		for (cnt = 0; cnt < j->rec_frame_size * 2; cnt += 2) {
			if (!(cnt % 16) && !IsRxReady(board)) {
				dly = 0;
				while (!IsRxReady(board)) {
					if (dly++ > 5) {
						dly = 0;
						break;
					}
					udelay(10);
				}
			}
			// Throw away word 0 of the 8021 compressed format to get standard G.729.
			if (j->rec_codec == G729 && (cnt == 0 || cnt == 5 || cnt == 10)) {
				inb_p(j->DSPbase + 0x0E);
				inb_p(j->DSPbase + 0x0F);
			}
			*(j->read_buffer + cnt) = inb_p(j->DSPbase + 0x0E);
			*(j->read_buffer + cnt + 1) = inb_p(j->DSPbase + 0x0F);
		}
#ifdef PERFMON_STATS
		++j->framesread;
#endif
		if (j->intercom != -1) {
			if (IsTxReady(j->intercom)) {
				for (cnt = 0; cnt < j->rec_frame_size * 2; cnt += 2) {
					if (!(cnt % 16) && !IsTxReady(board)) {
						dly = 0;
						while (!IsTxReady(board)) {
							if (dly++ > 5) {
								dly = 0;
								break;
							}
							udelay(10);
						}
					}
					outb_p(*(j->read_buffer + cnt), ixj[j->intercom].DSPbase + 0x0C);
					outb_p(*(j->read_buffer + cnt + 1), ixj[j->intercom].DSPbase + 0x0D);
				}
#ifdef PERFMON_STATS
				++ixj[j->intercom].frameswritten;
#endif
			}
		} else {
			j->read_buffer_ready = 1;
			wake_up_interruptible(&j->read_q);	// Wake any blocked readers

			wake_up_interruptible(&j->poll_q);	// Wake any blocked selects

			kill_fasync(&j->async_queue, SIGIO, POLL_IN);	// Send apps notice of frame

		}
	}
}

static void ixj_write_frame(int board)
{
	int cnt, frame_count, dly;
	BYTES blankword;
	IXJ *j = &ixj[board];

	frame_count = 0;
	if (j->write_buffer && j->write_buffers_empty < 2) {
		if (j->write_buffer_wp > j->write_buffer_rp) {
			frame_count =
			    (j->write_buffer_wp - j->write_buffer_rp) / (j->play_frame_size * 2);
		}
		if (j->write_buffer_rp > j->write_buffer_wp) {
			frame_count =
			    (j->write_buffer_wp - j->write_buffer) / (j->play_frame_size * 2) +
			    (j->write_buffer_end - j->write_buffer_rp) / (j->play_frame_size * 2);
		}
		if (frame_count >= 1) {
			if (j->ver.low == 0x12 && j->play_mode && j->flags.play_first_frame) {
				switch (j->play_mode) {
				case PLAYBACK_MODE_ULAW:
				case PLAYBACK_MODE_ALAW:
					blankword.low = blankword.high = 0xFF;
					break;
				case PLAYBACK_MODE_8LINEAR:
				case PLAYBACK_MODE_16LINEAR:
					blankword.low = blankword.high = 0x00;
					break;
				case PLAYBACK_MODE_8LINEAR_WSS:
					blankword.low = blankword.high = 0x80;
					break;
				}
				for (cnt = 0; cnt < 16; cnt++) {
					if (!(cnt % 16) && !IsTxReady(board)) {
						dly = 0;
						while (!IsTxReady(board)) {
							if (dly++ > 5) {
								dly = 0;
								break;
							}
							udelay(10);
						}
					}
					outb_p((blankword.low), j->DSPbase + 0x0C);
					outb_p((blankword.high), j->DSPbase + 0x0D);
				}
				j->flags.play_first_frame = 0;
			}
			for (cnt = 0; cnt < j->play_frame_size * 2; cnt += 2) {
				if (!(cnt % 16) && !IsTxReady(board)) {
					dly = 0;
					while (!IsTxReady(board)) {
						if (dly++ > 5) {
							dly = 0;
							break;
						}
						udelay(10);
					}
				}
// Add word 0 to G.729 frames for the 8021.  Right now we don't do VAD/CNG 
				// so all frames are type 1.
				if (j->play_codec == G729 && (cnt == 0 || cnt == 5 || cnt == 10)) {
					outb_p(0x01, j->DSPbase + 0x0C);
					outb_p(0x00, j->DSPbase + 0x0D);
				}
				outb_p(*(j->write_buffer_rp + cnt), j->DSPbase + 0x0C);
				outb_p(*(j->write_buffer_rp + cnt + 1), j->DSPbase + 0x0D);
				*(j->write_buffer_rp + cnt) = 0;
				*(j->write_buffer_rp + cnt + 1) = 0;
			}
			j->write_buffer_rp += j->play_frame_size * 2;
			if (j->write_buffer_rp >= j->write_buffer_end) {
				j->write_buffer_rp = j->write_buffer;
			}
			j->write_buffers_empty++;
			wake_up_interruptible(&(j->write_q));	// Wake any blocked writers

			wake_up_interruptible(&j->poll_q);	// Wake any blocked selects

			kill_fasync(&j->async_queue, SIGIO, POLL_IN);	// Send apps notice of empty buffer
#ifdef PERFMON_STATS
			++j->frameswritten;
#endif
		}
	} else {
		j->drybuffer++;
	}
}

static int idle(int board)
{
	IXJ *j = &ixj[board];

	if (ixj_WriteDSPCommand(0x0000, board))		// DSP Idle

		return 0;
	if (j->ssr.high || j->ssr.low)
		return 0;
	else
		return 1;
}

static int set_base_frame(int board, int size)
{
	unsigned short cmd;
	int cnt;
	IXJ *j = &ixj[board];

	aec_stop(board);
	for (cnt = 0; cnt < 10; cnt++) {
		if (idle(board))
			break;
	}
	if (j->ssr.high || j->ssr.low)
		return -1;
	if (j->dsp.low != 0x20) {
		switch (size) {
		case 30:
			cmd = 0x07F0;
			/* Set Base Frame Size to 240 pg9-10 8021 */
			break;
		case 20:
			cmd = 0x07A0;
			/* Set Base Frame Size to 160 pg9-10 8021 */
			break;
		case 10:
			cmd = 0x0750;
			/* Set Base Frame Size to 80 pg9-10 8021 */
			break;
		default:
			return -1;
		}
	} else {
		if (size == 30)
			return size;
		else
			return -1;
	}
	if (ixj_WriteDSPCommand(cmd, board)) {
		j->baseframe.high = j->baseframe.low = 0xFF;
		return -1;
	} else {
		j->baseframe.high = j->ssr.high;
		j->baseframe.low = j->ssr.low;
	}
	return size;
}

static int set_rec_codec(int board, int rate)
{
	int retval = 0;
	IXJ *j = &ixj[board];

	j->rec_codec = rate;

	switch (rate) {
	case G723_63:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->rec_frame_size = 12;
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G723_53:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->rec_frame_size = 10;
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case TS85:
		if (j->dsp.low == 0x20 || j->flags.ts85_loaded) {
			j->rec_frame_size = 16;
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case TS48:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->rec_frame_size = 9;
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case TS41:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->rec_frame_size = 8;
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G728:
		if (j->dsp.low != 0x20) {
			j->rec_frame_size = 48;
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G729:
		if (j->dsp.low != 0x20) {
			if (!j->flags.g729_loaded) {
				retval = 1;
				break;
			}
			switch (j->baseframe.low) {
			case 0xA0:
				j->rec_frame_size = 10;
				break;
			case 0x50:
				j->rec_frame_size = 5;
				break;
			default:
				j->rec_frame_size = 15;
				break;
			}
			j->rec_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case ULAW:
		switch (j->baseframe.low) {
		case 0xA0:
			j->rec_frame_size = 80;
			break;
		case 0x50:
			j->rec_frame_size = 40;
			break;
		default:
			j->rec_frame_size = 120;
			break;
		}
		j->rec_mode = 4;
		break;
	case ALAW:
		switch (j->baseframe.low) {
		case 0xA0:
			j->rec_frame_size = 80;
			break;
		case 0x50:
			j->rec_frame_size = 40;
			break;
		default:
			j->rec_frame_size = 120;
			break;
		}
		j->rec_mode = 4;
		break;
	case LINEAR16:
		switch (j->baseframe.low) {
		case 0xA0:
			j->rec_frame_size = 160;
			break;
		case 0x50:
			j->rec_frame_size = 80;
			break;
		default:
			j->rec_frame_size = 240;
			break;
		}
		j->rec_mode = 5;
		break;
	case LINEAR8:
		switch (j->baseframe.low) {
		case 0xA0:
			j->rec_frame_size = 80;
			break;
		case 0x50:
			j->rec_frame_size = 40;
			break;
		default:
			j->rec_frame_size = 120;
			break;
		}
		j->rec_mode = 6;
		break;
	case WSS:
		switch (j->baseframe.low) {
		case 0xA0:
			j->rec_frame_size = 80;
			break;
		case 0x50:
			j->rec_frame_size = 40;
			break;
		default:
			j->rec_frame_size = 120;
			break;
		}
		j->rec_mode = 7;
		break;
	default:
		j->rec_frame_size = 0;
		j->rec_mode = -1;
		if (j->read_buffer) {
			kfree(j->read_buffer);
			j->read_buffer = NULL;
			j->read_buffer_size = 0;
		}
		retval = 1;
		break;
	}
	return retval;
}

static int ixj_record_start(int board)
{
	unsigned short cmd = 0x0000;
	IXJ *j = &ixj[board];

	if (!j->rec_mode) {
		switch (j->rec_codec) {
		case G723_63:
			cmd = 0x5131;
			break;
		case G723_53:
			cmd = 0x5132;
			break;
		case TS85:
			cmd = 0x5130;	// TrueSpeech 8.5

			break;
		case TS48:
			cmd = 0x5133;	// TrueSpeech 4.8

			break;
		case TS41:
			cmd = 0x5134;	// TrueSpeech 4.1

			break;
		case G728:
			cmd = 0x5135;
			break;
		case G729:
			cmd = 0x5136;
			break;
		default:
			return 1;
		}
		if (ixj_WriteDSPCommand(cmd, board))
			return -1;
	}
	if (!j->read_buffer) {
		if (!j->read_buffer)
			j->read_buffer = kmalloc(j->rec_frame_size * 2, GFP_ATOMIC);
		if (!j->read_buffer) {
			printk("Read buffer allocation for ixj board %d failed!\n", board);
			return -ENOMEM;
		}
	}
	j->read_buffer_size = j->rec_frame_size * 2;

	if (ixj_WriteDSPCommand(0x5102, board))		// Set Poll sync mode

		return -1;

	switch (j->rec_mode) {
	case 0:
		cmd = 0x1C03;	// Record C1

		break;
	case 4:
		if (j->ver.low == 0x12) {
			cmd = 0x1E03;	// Record C1

		} else {
			cmd = 0x1E01;	// Record C1

		}
		break;
	case 5:
		if (j->ver.low == 0x12) {
			cmd = 0x1E83;	// Record C1

		} else {
			cmd = 0x1E81;	// Record C1

		}
		break;
	case 6:
		if (j->ver.low == 0x12) {
			cmd = 0x1F03;	// Record C1

		} else {
			cmd = 0x1F01;	// Record C1

		}
		break;
	case 7:
		if (j->ver.low == 0x12) {
			cmd = 0x1F83;	// Record C1

		} else {
			cmd = 0x1F81;	// Record C1

		}
		break;
	}
	if (ixj_WriteDSPCommand(cmd, board))
		return -1;

	return 0;
}

static void ixj_record_stop(int board)
{
	IXJ *j = &ixj[board];

	if (j->rec_mode > -1) {
		ixj_WriteDSPCommand(0x5120, board);
		j->rec_mode = -1;
	}
}

static void set_rec_depth(int board, int depth)
{
	if (depth > 60)
		depth = 60;
	if (depth < 0)
		depth = 0;
	ixj_WriteDSPCommand(0x5180 + depth, board);
}

static void set_rec_volume(int board, int volume)
{
	ixj_WriteDSPCommand(0xCF03, board);
	ixj_WriteDSPCommand(volume, board);
}

static int get_rec_level(int board)
{
	IXJ *j = &ixj[board];

	ixj_WriteDSPCommand(0xCF88, board);

	return j->ssr.high << 8 | j->ssr.low;
}

static void ixj_aec_start(int board, int level)
{
	IXJ *j = &ixj[board];

	j->aec_level = level;
	if (!level) {
		ixj_WriteDSPCommand(0xB002, board);
	} else {
		if (j->rec_codec == G729 || j->play_codec == G729) {
			ixj_WriteDSPCommand(0xE022, board);	// Move AEC filter buffer

			ixj_WriteDSPCommand(0x0300, board);
		}
		ixj_WriteDSPCommand(0xB001, board);	// AEC On

		ixj_WriteDSPCommand(0xE013, board);	// Advanced AEC C1

		switch (level) {
		case 1:
			ixj_WriteDSPCommand(0x0000, board);	// Advanced AEC C2 = off

			ixj_WriteDSPCommand(0xE011, board);
			ixj_WriteDSPCommand(0xFFFF, board);
			break;

		case 2:
			ixj_WriteDSPCommand(0x0600, board);	// Advanced AEC C2 = on medium

			ixj_WriteDSPCommand(0xE011, board);
			ixj_WriteDSPCommand(0x0080, board);
			break;

		case 3:
			ixj_WriteDSPCommand(0x0C00, board);	// Advanced AEC C2 = on high

			ixj_WriteDSPCommand(0xE011, board);
			ixj_WriteDSPCommand(0x0080, board);
			break;
		}
	}
}

static void aec_stop(int board)
{
	IXJ *j = &ixj[board];

	if (j->rec_codec == G729 || j->play_codec == G729) {
		ixj_WriteDSPCommand(0xE022, board);	// Move AEC filter buffer back

		ixj_WriteDSPCommand(0x0700, board);
	}
	if (ixj[board].play_mode != -1 && ixj[board].rec_mode != -1)
	{
		ixj_WriteDSPCommand(0xB002, board);	// AEC Stop

	}
}

static int set_play_codec(int board, int rate)
{
	int retval = 0;
	IXJ *j = &ixj[board];

	j->play_codec = rate;

	switch (rate) {
	case G723_63:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->play_frame_size = 12;
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G723_53:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->play_frame_size = 10;
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case TS85:
		if (j->dsp.low == 0x20 || j->flags.ts85_loaded) {
			j->play_frame_size = 16;
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case TS48:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->play_frame_size = 9;
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case TS41:
		if (j->ver.low != 0x12 || ixj_convert_loaded) {
			j->play_frame_size = 8;
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G728:
		if (j->dsp.low != 0x20) {
			j->play_frame_size = 48;
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case G729:
		if (j->dsp.low != 0x20) {
			if (!j->flags.g729_loaded) {
				retval = 1;
				break;
			}
			switch (j->baseframe.low) {
			case 0xA0:
				j->play_frame_size = 10;
				break;
			case 0x50:
				j->play_frame_size = 5;
				break;
			default:
				j->play_frame_size = 15;
				break;
			}
			j->play_mode = 0;
		} else {
			retval = 1;
		}
		break;
	case ULAW:
		switch (j->baseframe.low) {
		case 0xA0:
			j->play_frame_size = 80;
			break;
		case 0x50:
			j->play_frame_size = 40;
			break;
		default:
			j->play_frame_size = 120;
			break;
		}
		j->play_mode = 2;
		break;
	case ALAW:
		switch (j->baseframe.low) {
		case 0xA0:
			j->play_frame_size = 80;
			break;
		case 0x50:
			j->play_frame_size = 40;
			break;
		default:
			j->play_frame_size = 120;
			break;
		}
		j->play_mode = 2;
		break;
	case LINEAR16:
		switch (j->baseframe.low) {
		case 0xA0:
			j->play_frame_size = 160;
			break;
		case 0x50:
			j->play_frame_size = 80;
			break;
		default:
			j->play_frame_size = 240;
			break;
		}
		j->play_mode = 6;
		break;
	case LINEAR8:
		switch (j->baseframe.low) {
		case 0xA0:
			j->play_frame_size = 80;
			break;
		case 0x50:
			j->play_frame_size = 40;
			break;
		default:
			j->play_frame_size = 120;
			break;
		}
		j->play_mode = 4;
		break;
	case WSS:
		switch (j->baseframe.low) {
		case 0xA0:
			j->play_frame_size = 80;
			break;
		case 0x50:
			j->play_frame_size = 40;
			break;
		default:
			j->play_frame_size = 120;
			break;
		}
		j->play_mode = 5;
		break;
	default:
		j->play_frame_size = 0;
		j->play_mode = -1;
		if (j->write_buffer) {
			kfree(j->write_buffer);
			j->write_buffer = NULL;
			j->write_buffer_size = 0;
		}
		retval = 1;
		break;
	}
	return retval;
}

static int ixj_play_start(int board)
{
	unsigned short cmd = 0x0000;
	IXJ *j = &ixj[board];

	j->flags.play_first_frame = 1;
	j->drybuffer = 0;

	if (!j->play_mode) {
		switch (j->play_codec) {
		case G723_63:
			cmd = 0x5231;
			break;
		case G723_53:
			cmd = 0x5232;
			break;
		case TS85:
			cmd = 0x5230;	// TrueSpeech 8.5

			break;
		case TS48:
			cmd = 0x5233;	// TrueSpeech 4.8

			break;
		case TS41:
			cmd = 0x5234;	// TrueSpeech 4.1

			break;
		case G728:
			cmd = 0x5235;
			break;
		case G729:
			cmd = 0x5236;
			break;
		default:
			return 1;
		}
		if (ixj_WriteDSPCommand(cmd, board))
			return -1;
	}
	if (!j->write_buffer) {
		j->write_buffer = kmalloc(j->play_frame_size * 2, GFP_ATOMIC);
		if (!j->write_buffer) {
			printk("Write buffer allocation for ixj board %d failed!\n", board);
			return -ENOMEM;
		}
	}
	j->write_buffers_empty = 2;
	j->write_buffer_size = j->play_frame_size * 2;
	j->write_buffer_end = j->write_buffer + j->play_frame_size * 2;
	j->write_buffer_rp = j->write_buffer_wp = j->write_buffer;

	if (ixj_WriteDSPCommand(0x5202, board))		// Set Poll sync mode

		return -1;

	switch (j->play_mode) {
	case 0:
		cmd = 0x2C03;
		break;
	case 2:
		if (j->ver.low == 0x12) {
			cmd = 0x2C23;
		} else {
			cmd = 0x2C21;
		}
		break;
	case 4:
		if (j->ver.low == 0x12) {
			cmd = 0x2C43;
		} else {
			cmd = 0x2C41;
		}
		break;
	case 5:
		if (j->ver.low == 0x12) {
			cmd = 0x2C53;
		} else {
			cmd = 0x2C51;
		}
		break;
	case 6:
		if (j->ver.low == 0x12) {
			cmd = 0x2C63;
		} else {
			cmd = 0x2C61;
		}
		break;
	}
	if (ixj_WriteDSPCommand(cmd, board))
		return -1;

	if (ixj_WriteDSPCommand(0x2000, board))		// Playback C2

		return -1;

	if (ixj_WriteDSPCommand(0x2000 + ixj[board].play_frame_size, board))	// Playback C3

		return -1;

	return 0;
}

static void ixj_play_stop(int board)
{
	IXJ *j = &ixj[board];

	if (j->play_mode > -1) {
		ixj_WriteDSPCommand(0x5221, board);	// Stop playback

		j->play_mode = -1;
	}
}

extern __inline__ void set_play_depth(int board, int depth)
{
	if (depth > 60)
		depth = 60;
	if (depth < 0)
		depth = 0;
	ixj_WriteDSPCommand(0x5280 + depth, board);
}

extern __inline__ void set_play_volume(int board, int volume)
{
	ixj_WriteDSPCommand(0xCF02, board);
	ixj_WriteDSPCommand(volume, board);
}

extern __inline__ int get_play_level(int board)
{
	ixj_WriteDSPCommand(0xCF8F, board);
	return ixj[board].ssr.high << 8 | ixj[board].ssr.low;
}

static unsigned int ixj_poll(struct file *file_p, poll_table * wait)
{
	unsigned int mask = 0;
	IXJ *j = &ixj[NUM(file_p->f_dentry->d_inode->i_rdev)];

	poll_wait(file_p, &(j->poll_q), wait);
	if (j->read_buffer_ready > 0)
		mask |= POLLIN | POLLRDNORM;	/* readable */
	if (j->write_buffers_empty > 0)
		mask |= POLLOUT | POLLWRNORM;	/* writable */
	if (j->ex.bytes)
		mask |= POLLPRI;
	return mask;
}

static int ixj_play_tone(int board, char tone)
{
	IXJ *j = &ixj[board];

	if (!j->tone_state)
		idle(board);

	j->tone_index = tone;
	if (ixj_WriteDSPCommand(0x6000 + j->tone_index, board))
		return -1;

	if (!j->tone_state) {
		j->tone_start_jif = jiffies;

		j->tone_state = 1;
	}
	return 0;
}

static int ixj_set_tone_on(unsigned short arg, int board)
{
	IXJ *j = &ixj[board];

	j->tone_on_time = arg;

	if (ixj_WriteDSPCommand(0x6E04, board))		// Set Tone On Period

		return -1;

	if (ixj_WriteDSPCommand(arg, board))
		return -1;

	return 0;
}

static int SCI_WaitHighSCI(int board)
{
	int cnt;
	IXJ *j = &ixj[board];

	j->pld_scrr.byte = inb_p(j->XILINXbase);
	if (!j->pld_scrr.bits.sci) {
		for (cnt = 0; cnt < 10; cnt++) {
			udelay(32);
			j->pld_scrr.byte = inb_p(j->XILINXbase);

			if ((j->pld_scrr.bits.sci))
				return 1;
		}
		if (ixjdebug > 1)
			printk(KERN_INFO "SCI Wait High failed %x\n", j->pld_scrr.byte);
		return 0;
	} else
		return 1;
}

static int SCI_WaitLowSCI(int board)
{
	int cnt;
	IXJ *j = &ixj[board];

	j->pld_scrr.byte = inb_p(j->XILINXbase);
	if (j->pld_scrr.bits.sci) {
		for (cnt = 0; cnt < 10; cnt++) {
			udelay(32);
			j->pld_scrr.byte = inb_p(j->XILINXbase);

			if (!(j->pld_scrr.bits.sci))
				return 1;
		}
		if (ixjdebug > 1)
			printk(KERN_INFO "SCI Wait Low failed %x\n", j->pld_scrr.byte);
		return 0;
	} else
		return 1;
}

static int SCI_Control(int board, int control)
{
	IXJ *j = &ixj[board];

	switch (control) {
	case SCI_End:
		j->pld_scrw.bits.c0 = 0;	// Set PLD Serial control interface

		j->pld_scrw.bits.c1 = 0;	// to no selection 

		break;
	case SCI_Enable_DAA:
		j->pld_scrw.bits.c0 = 1;	// Set PLD Serial control interface

		j->pld_scrw.bits.c1 = 0;	// to write to DAA

		break;
	case SCI_Enable_Mixer:
		j->pld_scrw.bits.c0 = 0;	// Set PLD Serial control interface

		j->pld_scrw.bits.c1 = 1;	// to write to mixer 

		break;
	case SCI_Enable_EEPROM:
		j->pld_scrw.bits.c0 = 1;	// Set PLD Serial control interface

		j->pld_scrw.bits.c1 = 1;	// to write to EEPROM 

		break;
	default:
		return 0;
		break;
	}
	outb_p(j->pld_scrw.byte, j->XILINXbase);

	switch (control) {
	case SCI_End:
		return 1;
		break;
	case SCI_Enable_DAA:
	case SCI_Enable_Mixer:
	case SCI_Enable_EEPROM:
		if (!SCI_WaitHighSCI(board))
			return 0;
		break;
	default:
		return 0;
		break;
	}
	return 1;
}

static int SCI_Prepare(int board)
{
	if (!SCI_Control(board, SCI_End))
		return 0;

	if (!SCI_WaitLowSCI(board))
		return 0;

	return 1;
}

static int ixj_mixer(long val, int board)
{
	BYTES bytes;
	IXJ *j = &ixj[board];

	bytes.high = (val & 0xFF00) >> 8;
	bytes.low = val & 0x00FF;

	outb_p(bytes.high & 0x1F, j->XILINXbase + 0x03);	// Load Mixer Address

	outb_p(bytes.low, j->XILINXbase + 0x02);	// Load Mixer Data

	SCI_Control(board, SCI_Enable_Mixer);

	SCI_Control(board, SCI_End);

	return 0;
}

static int daa_load(BYTES * p_bytes, int board)
{
	IXJ *j = &ixj[board];

	outb_p(p_bytes->high, j->XILINXbase + 0x03);
	outb_p(p_bytes->low, j->XILINXbase + 0x02);
	if (!SCI_Control(board, SCI_Enable_DAA))
		return 0;
	else
		return 1;
}

static int ixj_daa_cr4(int board, char reg)
{
	IXJ *j = &ixj[board];
	BYTES bytes;

	switch (j->daa_mode) {
	case SOP_PU_SLEEP:
		bytes.high = 0x14;
		break;
	case SOP_PU_RINGING:
		bytes.high = 0x54;
		break;
	case SOP_PU_CONVERSATION:
		bytes.high = 0x94;
		break;
	case SOP_PU_PULSEDIALING:
		bytes.high = 0xD4;
		break;
	}

	switch (j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.bitreg.AGX) {
	case 0:
		j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.bitreg.AGR_Z = 0;
		break;
	case 1:
		j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.bitreg.AGR_Z = 2;
		break;
	case 2:
		j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.bitreg.AGR_Z = 1;
		break;
	case 3:
		j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.bitreg.AGR_Z = 3;
		break;
	}

	bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg;

	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Prepare(board))
		return 0;

	return 1;
}

static char daa_int_read(int board)
{
	BYTES bytes;
	IXJ *j = &ixj[board];

	if (!SCI_Prepare(board))
		return 0;

	bytes.high = 0x38;
	bytes.low = 0x00;
	outb_p(bytes.high, j->XILINXbase + 0x03);
	outb_p(bytes.low, j->XILINXbase + 0x02);

	if (!SCI_Control(board, SCI_Enable_DAA))
		return 0;

	bytes.high = inb_p(j->XILINXbase + 0x03);
	bytes.low = inb_p(j->XILINXbase + 0x02);
	if (bytes.low != ALISDAA_ID_BYTE) {
		if (ixjdebug > 0)
			printk("Cannot read DAA ID Byte high = %d low = %d\n", bytes.high, bytes.low);
		return 0;
	}
	if (!SCI_Control(board, SCI_Enable_DAA))
		return 0;
	if (!SCI_Control(board, SCI_End))
		return 0;

	bytes.high = inb_p(j->XILINXbase + 0x03);
	bytes.low = inb_p(j->XILINXbase + 0x02);

	j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.reg = bytes.high;
	return 1;
}

static int ixj_daa_cid_reset(int board)
{
	int i;
	BYTES bytes;
	IXJ *j = &ixj[board];

	if (!SCI_Prepare(board))
		return 0;

	bytes.high = 0x58;
	bytes.low = 0x00;
	outb_p(bytes.high, j->XILINXbase + 0x03);
	outb_p(bytes.low, j->XILINXbase + 0x02);

	if (!SCI_Control(board, SCI_Enable_DAA))
		return 0;

	if (!SCI_WaitHighSCI(board))
		return 0;

	for (i = 0; i < ALISDAA_CALLERID_SIZE - 1; i += 2) {
		bytes.high = bytes.low = 0x00;
		outb_p(bytes.high, j->XILINXbase + 0x03);

		if (i < ALISDAA_CALLERID_SIZE - 1)
			outb_p(bytes.low, j->XILINXbase + 0x02);

		if (!SCI_Control(board, SCI_Enable_DAA))
			return 0;

		if (!SCI_WaitHighSCI(board))
			return 0;

	}

	if (!SCI_Control(board, SCI_End))
		return 0;

	return 1;
}

static int ixj_daa_cid_read(int board)
{
	int i;
	BYTES bytes;
	char CID[ALISDAA_CALLERID_SIZE], mContinue;
	char *pIn, *pOut;
	IXJ *j = &ixj[board];

	if (!SCI_Prepare(board))
		return 0;

	bytes.high = 0x78;
	bytes.low = 0x00;
	outb_p(bytes.high, j->XILINXbase + 0x03);
	outb_p(bytes.low, j->XILINXbase + 0x02);

	if (!SCI_Control(board, SCI_Enable_DAA))
		return 0;

	if (!SCI_WaitHighSCI(board))
		return 0;

	bytes.high = inb_p(j->XILINXbase + 0x03);
	bytes.low = inb_p(j->XILINXbase + 0x02);
	if (bytes.low != ALISDAA_ID_BYTE) {
		if (ixjdebug > 0)
			printk("DAA Get Version Cannot read DAA ID Byte high = %d low = %d\n", bytes.high, bytes.low);
		return 0;
	}
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i += 2) {
		bytes.high = bytes.low = 0x00;
		outb_p(bytes.high, j->XILINXbase + 0x03);
		outb_p(bytes.low, j->XILINXbase + 0x02);

		if (!SCI_Control(board, SCI_Enable_DAA))
			return 0;

		if (!SCI_WaitHighSCI(board))
			return 0;

		CID[i + 0] = inb_p(j->XILINXbase + 0x03);
		CID[i + 1] = inb_p(j->XILINXbase + 0x02);
	}

	if (!SCI_Control(board, SCI_End))
		return 0;

	pIn = CID;
	pOut = j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID;
	mContinue = 1;
	while (mContinue) {
		if ((pIn[1] & 0x03) == 0x01) {
			pOut[0] = pIn[0];
		}
		if ((pIn[2] & 0x0c) == 0x04) {
			pOut[1] = ((pIn[2] & 0x03) << 6) | ((pIn[1] & 0xfc) >> 2);
		}
		if ((pIn[3] & 0x30) == 0x10) {
			pOut[2] = ((pIn[3] & 0x0f) << 4) | ((pIn[2] & 0xf0) >> 4);
		}
		if ((pIn[4] & 0xc0) == 0x40) {
			pOut[3] = ((pIn[4] & 0x3f) << 2) | ((pIn[3] & 0xc0) >> 6);
		} else {
			mContinue = FALSE;
		}
		pIn += 5, pOut += 4;
	}
	memset(&j->cid, 0, sizeof(IXJ_CID));
	pOut = j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID;
	pOut += 4;
	strncpy(j->cid.month, pOut, 2);
	pOut += 2;
	strncpy(j->cid.day, pOut, 2);
	pOut += 2;
	strncpy(j->cid.hour, pOut, 2);
	pOut += 2;
	strncpy(j->cid.min, pOut, 2);
	pOut += 3;
	j->cid.numlen = *pOut;
	pOut += 1;
	strncpy(j->cid.number, pOut, j->cid.numlen);
	pOut += j->cid.numlen + 1;
	j->cid.namelen = *pOut;
	pOut += 1;
	strncpy(j->cid.name, pOut, j->cid.namelen);

	ixj_daa_cid_reset(board);
	return 1;
}

static char daa_get_version(int board)
{
	BYTES bytes;
	IXJ *j = &ixj[board];

	if (!SCI_Prepare(board))
		return 0;

	bytes.high = 0x35;
	bytes.low = 0x00;
	outb_p(bytes.high, j->XILINXbase + 0x03);
	outb_p(bytes.low, j->XILINXbase + 0x02);

	if (!SCI_Control(board, SCI_Enable_DAA))
		return 0;

	bytes.high = inb_p(j->XILINXbase + 0x03);
	bytes.low = inb_p(j->XILINXbase + 0x02);
	if (bytes.low != ALISDAA_ID_BYTE) {
		if (ixjdebug > 0)
			printk("DAA Get Version Cannot read DAA ID Byte high = %d low = %d\n", bytes.high, bytes.low);
		return 0;
	}
	if (!SCI_Control(board, SCI_Enable_DAA))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;

	bytes.high = inb_p(j->XILINXbase + 0x03);
	bytes.low = inb_p(j->XILINXbase + 0x02);
	if (ixjdebug > 0)
		printk("DAA CR5 Byte high = 0x%x low = 0x%x\n", bytes.high, bytes.low);
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr5.reg = bytes.high;
	return bytes.high;
}

static int daa_set_mode(int board, int mode)
{
	// NOTE:
	//      The DAA *MUST* be in the conversation mode if the
	//      PSTN line is to be seized (PSTN line off-hook).
	//      Taking the PSTN line off-hook while the DAA is in
	//      a mode other than conversation mode will cause a
	//      hardware failure of the ALIS-A part.

	// NOTE:
	//      The DAA can only go to SLEEP, RINGING or PULSEDIALING modes
	//      if the PSTN line is on-hook.  Failure to have the PSTN line
	//      in the on-hook state WILL CAUSE A HARDWARE FAILURE OF THE
	//      ALIS-A part.
	//


	BYTES bytes;
	IXJ *j = &ixj[board];

	if (!SCI_Prepare(board))
		return 0;

	switch (mode) {
	case SOP_PU_SLEEP:
		j->pld_scrw.bits.daafsyncen = 0;	// Turn off DAA Frame Sync

		outb_p(j->pld_scrw.byte, j->XILINXbase);
		j->pld_slicw.bits.rly2 = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		bytes.high = 0x10;
		bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg;
		daa_load(&bytes, board);
		if (!SCI_Prepare(board))
			return 0;
		j->daa_mode = SOP_PU_SLEEP;
		j->flags.pstn_ringing = 0;
		j->pstn_sleeptil = jiffies + (hertz * 3);
		break;
	case SOP_PU_RINGING:
		j->pld_scrw.bits.daafsyncen = 0;	// Turn off DAA Frame Sync

		outb_p(j->pld_scrw.byte, j->XILINXbase);
		j->pld_slicw.bits.rly2 = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		bytes.high = 0x50;
		bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg;
		daa_load(&bytes, board);
		if (!SCI_Prepare(board))
			return 0;
		j->daa_mode = SOP_PU_RINGING;
		break;
	case SOP_PU_CONVERSATION:
		bytes.high = 0x90;
		bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg;
		daa_load(&bytes, board);
		if (!SCI_Prepare(board))
			return 0;
		j->pld_slicw.bits.rly2 = 1;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		j->pld_scrw.bits.daafsyncen = 1;	// Turn on DAA Frame Sync

		outb_p(j->pld_scrw.byte, j->XILINXbase);
		j->daa_mode = SOP_PU_CONVERSATION;
		j->flags.pstn_ringing = 0;
		j->ex.bits.pstn_ring = 0;
		break;
	case SOP_PU_PULSEDIALING:
		j->pld_scrw.bits.daafsyncen = 0;	// Turn off DAA Frame Sync

		outb_p(j->pld_scrw.byte, j->XILINXbase);
		j->pld_slicw.bits.rly2 = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		bytes.high = 0xD0;
		bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg;
		daa_load(&bytes, board);
		if (!SCI_Prepare(board))
			return 0;
		j->daa_mode = SOP_PU_PULSEDIALING;
		break;
	default:
		break;
	}
	return 1;
}

static int ixj_daa_write(int board)
{
	BYTES bytes;
	IXJ *j = &ixj[board];

	if (!SCI_Prepare(board))
		return 0;

	bytes.high = 0x14;
	bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg;
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg;
	bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg;
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg;
	bytes.low = j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Prepare(board))
		return 0;

	bytes.high = 0x1F;
	bytes.low = j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg;
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.XOP_xr6_W.reg;
	bytes.low = j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg;
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg;
	bytes.low = j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg;
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg;
	bytes.low = j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg;
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.XOP_xr0_W.reg;
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Prepare(board))
		return 0;

	bytes.high = 0x00;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x01;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x02;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x03;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x04;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x05;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x06;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x07;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x08;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x09;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x0A;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x0B;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x0C;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x0D;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x0E;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	if (!SCI_Control(board, SCI_End))
		return 0;
	if (!SCI_WaitLowSCI(board))
		return 0;

	bytes.high = 0x0F;
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2];
	bytes.low = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1];
	if (!daa_load(&bytes, board))
		return 0;

	bytes.high = j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0];
	bytes.low = 0x00;
	if (!daa_load(&bytes, board))
		return 0;

	udelay(32);
	j->pld_scrr.byte = inb_p(j->XILINXbase);
	if (!SCI_Control(board, SCI_End))
		return 0;

	return 1;
}

int ixj_set_tone_off(unsigned short arg, int board)
{
	ixj[board].tone_off_time = arg;

	if (ixj_WriteDSPCommand(0x6E05, board))		// Set Tone Off Period

		return -1;

	if (ixj_WriteDSPCommand(arg, board))
		return -1;

	return 0;
}

static int ixj_get_tone_on(int board)
{
	if (ixj_WriteDSPCommand(0x6E06, board))		// Get Tone On Period

		return -1;

	return 0;
}

static int ixj_get_tone_off(int board)
{
	if (ixj_WriteDSPCommand(0x6E07, board))		// Get Tone Off Period

		return -1;

	return 0;
}

static void ixj_busytone(int board)
{
	ixj[board].flags.ringback = 0;
	ixj[board].flags.dialtone = 0;
	ixj[board].flags.busytone = 1;

	ixj_set_tone_on(0x07D0, board);
	ixj_set_tone_off(0x07D0, board);
	ixj_play_tone(board, 27);
}

static void ixj_dialtone(int board)
{
	ixj[board].flags.ringback = 0;
	ixj[board].flags.dialtone = 1;
	ixj[board].flags.busytone = 0;

	if (ixj[board].dsp.low == 0x20) {
		return;
	} else {
		ixj_set_tone_on(0xFFFF, board);
		ixj_set_tone_off(0x0000, board);

		ixj_play_tone(board, 25);
	}
}

static void ixj_cpt_stop(board)
{
	IXJ *j = &ixj[board];

	j->flags.dialtone = 0;
	j->flags.busytone = 0;
	j->flags.ringback = 0;

	ixj_set_tone_on(0x0001, board);
	ixj_set_tone_off(0x0000, board);

	ixj_play_tone(board, 0);

	j->tone_state = 0;

	ixj_del_timer();
	if (j->cadence_t) {
		if (j->cadence_t->ce) {
			kfree(j->cadence_t->ce);
		}
		kfree(j->cadence_t);
		j->cadence_t = NULL;
	}
	ixj_add_timer();
	if (j->dsp.low == 0x20 || (j->play_mode == -1 && j->rec_mode == -1))
		idle(board);
	if (j->play_mode != -1)
		ixj_play_start(board);
	if (j->rec_mode != -1)
		ixj_record_start(board);
}

static void ixj_ringback(int board)
{
	ixj[board].flags.busytone = 0;
	ixj[board].flags.dialtone = 0;
	ixj[board].flags.ringback = 1;

	ixj_set_tone_on(0x0FA0, board);
	ixj_set_tone_off(0x2EE0, board);
	ixj_play_tone(board, 26);
}

static void ixj_testram(int board)
{
	ixj_WriteDSPCommand(0x3001, board);	/* Test External SRAM */
}

static int ixj_build_cadence(int board, IXJ_CADENCE * cp)
{
	IXJ_CADENCE *lcp;
	IXJ_CADENCE_ELEMENT *lcep;
	IXJ_TONE ti;
	IXJ *j = &ixj[board];

	lcp = kmalloc(sizeof(IXJ_CADENCE), GFP_KERNEL);
	if (lcp == NULL)
		return -ENOMEM;

	if (copy_from_user(lcp, (char *) cp, sizeof(IXJ_CADENCE)))
		return -EFAULT;

	lcep = kmalloc(sizeof(IXJ_CADENCE_ELEMENT) * lcp->elements_used, GFP_KERNEL);
	if (lcep == NULL) {
		kfree(lcp);
		return -ENOMEM;
	}
	if (copy_from_user(lcep, lcp->ce, sizeof(IXJ_CADENCE_ELEMENT) * lcp->elements_used))
		return -EFAULT;

	if(j->cadence_t)
	{
		kfree(j->cadence_t->ce);
		kfree(j->cadence_t);
	}
	
	lcp->ce = (void *) lcep;
	j->cadence_t = lcp;
	j->tone_cadence_state = 0;
	ixj_set_tone_on(lcp->ce[0].tone_on_time, board);
	ixj_set_tone_off(lcp->ce[0].tone_off_time, board);
	if (j->cadence_t->ce[j->tone_cadence_state].freq0) {
		ti.tone_index = j->cadence_t->ce[j->tone_cadence_state].index;
		ti.freq0 = j->cadence_t->ce[j->tone_cadence_state].freq0;
		ti.gain0 = j->cadence_t->ce[j->tone_cadence_state].gain0;
		ti.freq1 = j->cadence_t->ce[j->tone_cadence_state].freq1;
		ti.gain1 = j->cadence_t->ce[j->tone_cadence_state].gain1;
		ixj_init_tone(board, &ti);
	}
	ixj_play_tone(board, lcp->ce[0].index);

	return 1;
}

static void add_caps(int board)
{
	IXJ *j = &ixj[board];
	j->caps = 0;

	j->caplist[j->caps].cap = vendor;
	strcpy(j->caplist[j->caps].desc, "Quicknet Technologies, Inc. (www.quicknet.net)");
	j->caplist[j->caps].captype = vendor;
	j->caplist[j->caps].handle = j->caps++;
	j->caplist[j->caps].captype = device;
	switch (j->cardtype) {
	case 100:
		strcpy(j->caplist[j->caps].desc, "Quicknet Internet PhoneJACK");
		j->caplist[j->caps].cap = 100;
		break;
	case 300:
		strcpy(j->caplist[j->caps].desc, "Quicknet Internet LineJACK");
		j->caplist[j->caps].cap = 300;
		break;
	case 400:
		strcpy(j->caplist[j->caps].desc, "Quicknet Internet PhoneJACK Lite");
		j->caplist[j->caps].cap = 400;
		break;
	case 500:
		strcpy(j->caplist[j->caps].desc, "Quicknet Internet PhoneJACK PCI");
		j->caplist[j->caps].cap = 500;
		break;
	}
	j->caplist[j->caps].handle = j->caps++;
	strcpy(j->caplist[j->caps].desc, "POTS");
	j->caplist[j->caps].captype = port;
	j->caplist[j->caps].cap = pots;
	j->caplist[j->caps].handle = j->caps++;
	switch (ixj[board].cardtype) {
	case 100:
		strcpy(j->caplist[j->caps].desc, "SPEAKER");
		j->caplist[j->caps].captype = port;
		j->caplist[j->caps].cap = speaker;
		j->caplist[j->caps].handle = j->caps++;
		strcpy(j->caplist[j->caps].desc, "HANDSET");
		j->caplist[j->caps].captype = port;
		j->caplist[j->caps].cap = handset;
		j->caplist[j->caps].handle = j->caps++;
		break;
	case 300:
		strcpy(j->caplist[j->caps].desc, "SPEAKER");
		j->caplist[j->caps].captype = port;
		j->caplist[j->caps].cap = speaker;
		j->caplist[j->caps].handle = j->caps++;
		strcpy(j->caplist[j->caps].desc, "PSTN");
		j->caplist[j->caps].captype = port;
		j->caplist[j->caps].cap = pstn;
		j->caplist[j->caps].handle = j->caps++;
		break;
	}
	strcpy(j->caplist[j->caps].desc, "ULAW");
	j->caplist[j->caps].captype = codec;
	j->caplist[j->caps].cap = ULAW;
	j->caplist[j->caps].handle = j->caps++;
	strcpy(j->caplist[j->caps].desc, "LINEAR 16 bit");
	j->caplist[j->caps].captype = codec;
	j->caplist[j->caps].cap = LINEAR16;
	j->caplist[j->caps].handle = j->caps++;
	strcpy(j->caplist[j->caps].desc, "LINEAR 8 bit");
	j->caplist[j->caps].captype = codec;
	j->caplist[j->caps].cap = LINEAR8;
	j->caplist[j->caps].handle = j->caps++;
	strcpy(j->caplist[j->caps].desc, "Windows Sound System");
	j->caplist[j->caps].captype = codec;
	j->caplist[j->caps].cap = WSS;
	j->caplist[j->caps].handle = j->caps++;
	if (j->ver.low != 0x12) {
		strcpy(j->caplist[j->caps].desc, "G.723.1 6.3Kbps");
		j->caplist[j->caps].captype = codec;
		j->caplist[j->caps].cap = G723_63;
		j->caplist[j->caps].handle = j->caps++;
		strcpy(j->caplist[j->caps].desc, "G.723.1 5.3Kbps");
		j->caplist[j->caps].captype = codec;
		j->caplist[j->caps].cap = G723_53;
		j->caplist[j->caps].handle = j->caps++;
		strcpy(j->caplist[j->caps].desc, "TrueSpeech 4.8Kbps");
		j->caplist[j->caps].captype = codec;
		j->caplist[j->caps].cap = TS48;
		j->caplist[j->caps].handle = j->caps++;
		strcpy(j->caplist[j->caps].desc, "TrueSpeech 4.1Kbps");
		j->caplist[j->caps].captype = codec;
		j->caplist[j->caps].cap = TS41;
		j->caplist[j->caps].handle = j->caps++;
	}
	if (j->cardtype == 100) {
		strcpy(j->caplist[j->caps].desc, "TrueSpeech 8.5Kbps");
		j->caplist[j->caps].captype = codec;
		j->caplist[j->caps].cap = TS85;
		j->caplist[j->caps].handle = j->caps++;
	}
}
static int capabilities_check(int board, struct phone_capability *pcreq)
{
	int cnt;
	IXJ *j = &ixj[board];
	int retval = 0;

	for (cnt = 0; cnt < j->caps; cnt++) {
		if (pcreq->captype == j->caplist[cnt].captype &&
		    pcreq->cap == j->caplist[cnt].cap) {
			retval = 1;
			break;
		}
	}
	return retval;
}

int ixj_ioctl(struct inode *inode, struct file *file_p,
	      unsigned int cmd, unsigned long arg)
{
	IXJ_TONE ti;
	IXJ_FILTER jf;
	unsigned int minor = MINOR(inode->i_rdev);
	int board = NUM(inode->i_rdev);
	IXJ *j = &ixj[NUM(inode->i_rdev)];
	int retval = 0;

	if (ixjdebug > 1)
		printk(KERN_DEBUG "phone%d ioctl, cmd: 0x%x, arg: 0x%lx\n", minor, cmd, arg);
	if (minor >= IXJMAX)
		return -ENODEV;

	/*
	 *    Check ioctls only root can use.
	 */

	if (!capable(CAP_SYS_ADMIN)) {
		switch (cmd) {
		case IXJCTL_TESTRAM:
		case IXJCTL_HZ:
			return -EPERM;
		}
	}
	switch (cmd) {
	case IXJCTL_TESTRAM:
		ixj_testram(board);
		retval = (j->ssr.high << 8) + j->ssr.low;
		break;
	case IXJCTL_CARDTYPE:
		retval = j->cardtype;
		break;
	case IXJCTL_SERIAL:
		retval = j->serial;
		break;
	case PHONE_RING_CADENCE:
		j->ring_cadence = arg;
		break;
	case PHONE_RING_START:
		ixj_ring_start(board);
		break;
	case PHONE_RING_STOP:
		j->flags.cringing = 0;
		ixj_ring_off(board);
		break;
	case PHONE_RING:
		retval = ixj_ring(board);
		break;
	case PHONE_EXCEPTION:
		retval = j->ex.bytes;
		j->ex.bytes &= 0x03;
		break;
	case PHONE_HOOKSTATE:
		j->ex.bits.hookstate = 0;
		retval = j->r_hook;
		break;
	case IXJCTL_SET_LED:
		LED_SetState(arg, board);
		break;
	case PHONE_FRAME:
		retval = set_base_frame(board, arg);
		break;
	case PHONE_REC_CODEC:
		retval = set_rec_codec(board, arg);
		break;
	case PHONE_REC_START:
		ixj_record_start(board);
		break;
	case PHONE_REC_STOP:
		ixj_record_stop(board);
		break;
	case PHONE_REC_DEPTH:
		set_rec_depth(board, arg);
		break;
	case PHONE_REC_VOLUME:
		set_rec_volume(board, arg);
		break;
	case PHONE_REC_LEVEL:
		retval = get_rec_level(board);
		break;
	case IXJCTL_AEC_START:
		ixj_aec_start(board, arg);
		break;
	case IXJCTL_AEC_STOP:
		aec_stop(board);
		break;
	case IXJCTL_AEC_GET_LEVEL:
		retval = j->aec_level;
		break;
	case PHONE_PLAY_CODEC:
		retval = set_play_codec(board, arg);
		break;
	case PHONE_PLAY_START:
		ixj_play_start(board);
		break;
	case PHONE_PLAY_STOP:
		ixj_play_stop(board);
		break;
	case PHONE_PLAY_DEPTH:
		set_play_depth(board, arg);
		break;
	case PHONE_PLAY_VOLUME:
		set_play_volume(board, arg);
		break;
	case PHONE_PLAY_LEVEL:
		retval = get_play_level(board);
		break;
	case IXJCTL_DSP_TYPE:
		retval = (j->dsp.high << 8) + j->dsp.low;
		break;
	case IXJCTL_DSP_VERSION:
		retval = (j->ver.high << 8) + j->ver.low;
		break;
	case IXJCTL_HZ:
		hertz = arg;
		break;
	case IXJCTL_RATE:
		if (arg > hertz)
			retval = -1;
		else
			samplerate = arg;
		break;
	case IXJCTL_DRYBUFFER_READ:
		put_user(j->drybuffer, (unsigned long *) arg);
		break;
	case IXJCTL_DRYBUFFER_CLEAR:
		j->drybuffer = 0;
		break;
	case IXJCTL_FRAMES_READ:
		put_user(j->framesread, (unsigned long *) arg);
		break;
	case IXJCTL_FRAMES_WRITTEN:
		put_user(j->frameswritten, (unsigned long *) arg);
		break;
	case IXJCTL_READ_WAIT:
		put_user(j->read_wait, (unsigned long *) arg);
		break;
	case IXJCTL_WRITE_WAIT:
		put_user(j->write_wait, (unsigned long *) arg);
		break;
	case PHONE_MAXRINGS:
		j->maxrings = arg;
		break;
	case PHONE_SET_TONE_ON_TIME:
		ixj_set_tone_on(arg, board);
		break;
	case PHONE_SET_TONE_OFF_TIME:
		ixj_set_tone_off(arg, board);
		break;
	case PHONE_GET_TONE_ON_TIME:
		if (ixj_get_tone_on(board)) {
			retval = -1;
		} else {
			retval = (j->ssr.high << 8) + j->ssr.low;
		}
		break;
	case PHONE_GET_TONE_OFF_TIME:
		if (ixj_get_tone_off(board)) {
			retval = -1;
		} else {
			retval = (j->ssr.high << 8) + j->ssr.low;
		}
		break;
	case PHONE_PLAY_TONE:
		if (!j->tone_state)
			ixj_play_tone(board, arg);
		break;
	case PHONE_GET_TONE_STATE:
		retval = j->tone_state;
		break;
	case PHONE_DTMF_READY:
		retval = j->ex.bits.dtmf_ready;
		break;
	case PHONE_GET_DTMF:
		if (ixj_hookstate(board)) {
			if (j->dtmf_rp != j->dtmf_wp) {
				retval = j->dtmfbuffer[j->dtmf_rp];
				j->dtmf_rp++;
				if (j->dtmf_rp == 79)
					j->dtmf_rp = 0;
				if (j->dtmf_rp == j->dtmf_wp) {
					j->ex.bits.dtmf_ready = j->dtmf_rp = j->dtmf_wp = 0;
				}
			}
		}
		break;
	case PHONE_GET_DTMF_ASCII:
		if (ixj_hookstate(board)) {
			if (j->dtmf_rp != j->dtmf_wp) {
				switch (j->dtmfbuffer[j->dtmf_rp]) {
				case 10:
					retval = 42;	//'*';

					break;
				case 11:
					retval = 48;	//'0';

					break;
				case 12:
					retval = 35;	//'#';

					break;
				case 28:
					retval = 65;	//'A';

					break;
				case 29:
					retval = 66;	//'B';

					break;
				case 30:
					retval = 67;	//'C';

					break;
				case 31:
					retval = 68;	//'D';

					break;
				default:
					retval = 48 + j->dtmfbuffer[j->dtmf_rp];
					break;
				}
				j->dtmf_rp++;
				if (j->dtmf_rp == 79)
					j->dtmf_rp = 0;
//          if(j->dtmf_rp == j->dtmf_wp)
				{
					j->ex.bits.dtmf_ready = j->dtmf_rp = j->dtmf_wp = 0;
				}
			}
		}
		break;
	case PHONE_DTMF_OOB:
		j->flags.dtmf_oob = arg;
		break;
	case PHONE_DIALTONE:
		ixj_dialtone(board);
		break;
	case PHONE_BUSY:
		ixj_busytone(board);
		break;
	case PHONE_RINGBACK:
		ixj_ringback(board);
		break;
	case PHONE_CPT_STOP:
		ixj_cpt_stop(board);
		break;
	case PHONE_QUERY_CODEC:
	{
		struct phone_codec_data pd;
		int val;
		int proto_size[] = {
			-1, 
			12, 10, 16, 9, 8, 48, 5,
			40, 40, 80, 40, 40
		};
		if(copy_from_user(&pd, (void *)arg, sizeof(pd)))
			return -EFAULT;
		if(pd.type<1 || pd.type>12)
			return -EPROTONOSUPPORT;
		if(pd.type<G729)
			val=proto_size[pd.type];
		else switch(j->baseframe.low)
		{
			case 0xA0:val=2*proto_size[pd.type];break;
			case 0x50:val=proto_size[pd.type];break;
			default:val=proto_size[pd.type]*3;break;
		}
		pd.buf_min=pd.buf_max=pd.buf_opt=val;
		if(copy_to_user((void *)arg, &pd, sizeof(pd)))
			return -EFAULT;
		return 0;
	}
	case IXJCTL_DSP_IDLE:
		idle(board);
		break;
	case IXJCTL_MIXER:
		ixj_mixer(arg, board);
		break;
	case IXJCTL_DAA_COEFF_SET:
		switch (arg) {
		case DAA_US:
			DAA_Coeff_US(board);
			ixj_daa_write(board);
			break;
		case DAA_UK:
			DAA_Coeff_UK(board);
			ixj_daa_write(board);
			break;
		case DAA_FRANCE:
			DAA_Coeff_France(board);
			ixj_daa_write(board);
			break;
		case DAA_GERMANY:
			DAA_Coeff_Germany(board);
			ixj_daa_write(board);
			break;
		case DAA_AUSTRALIA:
			DAA_Coeff_Australia(board);
			ixj_daa_write(board);
			break;
		case DAA_JAPAN:
			DAA_Coeff_Japan(board);
			ixj_daa_write(board);
			break;
		default:
			break;
		}
		break;
	case IXJCTL_DAA_AGAIN:
		ixj_daa_cr4(board, arg | 0x02);
		break;
	case IXJCTL_PSTN_LINETEST:
	case PHONE_PSTN_LINETEST:
		retval = ixj_linetest(board);
		break;
	case IXJCTL_CID:
		if (copy_to_user((char *) arg, &j->cid, sizeof(IXJ_CID)))
			return -EFAULT;
		j->ex.bits.caller_id = 0;
		break;
	case IXJCTL_WINK_DURATION:
		j->winktime = arg;
		break;
	case IXJCTL_PORT:
		if (arg)
			retval = ixj_set_port(board, arg);
		else
			retval = j->port;
		break;
	case IXJCTL_POTS_PSTN:
		retval = ixj_set_pots(board, arg);
		break;
	case PHONE_CAPABILITIES:
		retval = j->caps;
		break;
	case PHONE_CAPABILITIES_LIST:
		if (copy_to_user((char *) arg, j->caplist, sizeof(struct phone_capability) * j->caps))
			return -EFAULT;
		break;
	case PHONE_CAPABILITIES_CHECK:
		retval = capabilities_check(board, (struct phone_capability *) arg);
		break;
	case PHONE_PSTN_SET_STATE:
		daa_set_mode(board, arg);
		break;
	case PHONE_PSTN_GET_STATE:
		retval = j->daa_mode;
		j->ex.bits.pstn_ring = 0;
		break;
	case IXJCTL_SET_FILTER:
		if (copy_from_user(&jf, (char *) arg, sizeof(ti)))
			return -EFAULT;
		retval = ixj_init_filter(board, &jf);
		break;
	case IXJCTL_GET_FILTER_HIST:
		retval = j->filter_hist[arg];
		break;
	case IXJCTL_INIT_TONE:
		copy_from_user(&ti, (char *) arg, sizeof(ti));
		retval = ixj_init_tone(board, &ti);
		break;
	case IXJCTL_TONE_CADENCE:
		retval = ixj_build_cadence(board, (IXJ_CADENCE *) arg);
		break;
	case IXJCTL_INTERCOM_STOP:
		ixj[board].intercom = -1;
		ixj[arg].intercom = -1;
		ixj_record_stop(board);
		ixj_record_stop(arg);
		ixj_play_stop(board);
		ixj_play_stop(arg);
		idle(board);
		idle(arg);
		break;
	case IXJCTL_INTERCOM_START:
		ixj[board].intercom = arg;
		ixj[arg].intercom = board;
		ixj_play_start(arg);
		ixj_record_start(board);
		ixj_play_start(board);
		ixj_record_start(arg);
		idle(board);
		idle(arg);
		break;
	}
	return retval;
}

static int ixj_fasync(int fd, struct file *file_p, int mode)
{
	IXJ *j = &ixj[NUM(file_p->f_dentry->d_inode->i_rdev)];
	return fasync_helper(fd, file_p, mode, &j->async_queue);
}

struct file_operations ixj_fops =
{
	owner:		THIS_MODULE,
	read:		ixj_enhanced_read,
	write:		ixj_enhanced_write,
	poll:		ixj_poll,
	ioctl:		ixj_ioctl,
	release:	ixj_release,
	fasync:		ixj_fasync,
};

static int ixj_linetest(int board)
{
	unsigned long jifwait;
	IXJ *j = &ixj[board];

	if (!j->flags.pots_correct) {
		j->flags.pots_correct = 1;	// Testing

		daa_int_read(board);	//Clear DAA Interrupt flags
		//
		// Hold all relays in the normally de-energized position.
		//

		j->pld_slicw.bits.rly1 = 0;
		j->pld_slicw.bits.rly2 = 0;
		j->pld_slicw.bits.rly3 = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		j->pld_scrw.bits.daafsyncen = 0;	// Turn off DAA Frame Sync

		outb_p(j->pld_scrw.byte, j->XILINXbase);
		j->pld_slicr.byte = inb_p(j->XILINXbase + 0x01);
		if (j->pld_slicr.bits.potspstn) {
			j->flags.pots_pstn = 1;
			j->flags.pots_correct = 0;
			LED_SetState(0x4, board);
		} else {
			j->flags.pots_pstn = 0;
			j->pld_slicw.bits.rly1 = 0;
			j->pld_slicw.bits.rly2 = 0;
			j->pld_slicw.bits.rly3 = 1;
			outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			j->pld_scrw.bits.daafsyncen = 0;	// Turn off DAA Frame Sync

			outb_p(j->pld_scrw.byte, j->XILINXbase);
			daa_set_mode(board, SOP_PU_CONVERSATION);
			jifwait = jiffies + hertz;
			while (time_before(jiffies, jifwait)) {
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(1);
			}
			daa_int_read(board);
			daa_set_mode(board, SOP_PU_SLEEP);
			if (j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.VDD_OK) {
				j->flags.pots_correct = 0;	// Should not be line voltage on POTS port.

				LED_SetState(0x4, board);
				j->pld_slicw.bits.rly3 = 0;
				outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			} else {
				j->flags.pots_correct = 1;
				LED_SetState(0x8, board);
				j->pld_slicw.bits.rly1 = 1;
				j->pld_slicw.bits.rly2 = 0;
				j->pld_slicw.bits.rly3 = 0;
				outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
			}
		}
	}
	if (!j->flags.pstn_present) {
		j->pld_slicw.bits.rly3 = 0;
		outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
		daa_set_mode(board, SOP_PU_CONVERSATION);
		jifwait = jiffies + hertz;
		while (time_before(jiffies, jifwait)) {
			current->state = TASK_INTERRUPTIBLE;
			schedule_timeout(1);
		}
		daa_int_read(board);
		daa_set_mode(board, SOP_PU_SLEEP);
		if (j->m_DAAShadowRegs.XOP_REGS.XOP.xr0.bitreg.VDD_OK) {
			j->flags.pstn_present = 1;
		} else {
			j->flags.pstn_present = 0;
		}
	}
	if (j->flags.pstn_present) {
		if (j->flags.pots_correct) {
			LED_SetState(0xA, board);
		} else {
			LED_SetState(0x6, board);
		}
	} else {
		if (j->flags.pots_correct) {
			LED_SetState(0x9, board);
		} else {
			LED_SetState(0x5, board);
		}
	}
	return j->flags.pstn_present;
}

static int ixj_selfprobe(int board)
{
	unsigned short cmd;
	unsigned long jif;
	BYTES bytes;
	IXJ *j = &ixj[board];
	
	/*
	 *	First initialise the queues
	 */

	init_waitqueue_head(&j->read_q);
	init_waitqueue_head(&j->write_q);
	init_waitqueue_head(&j->poll_q);

	/*
	 *	Now we can probe
	 */
	 
	if (ixjdebug > 0)
		printk(KERN_INFO "Write IDLE to Software Control Register\n");

	if (ixj_WriteDSPCommand(0x0000, board))		/* Write IDLE to Software Control Register */
		return -1;

// The read values of the SSR should be 0x00 for the IDLE command
	if (j->ssr.low || j->ssr.high)
		return -1;

	if (ixjdebug > 0)
		printk(KERN_INFO "Get Device ID Code\n");

	if (ixj_WriteDSPCommand(0x3400, board))		/* Get Device ID Code */
		return -1;

	j->dsp.low = j->ssr.low;
	j->dsp.high = j->ssr.high;

	if (ixjdebug > 0)
		printk(KERN_INFO "Get Device Version Code\n");

	if (ixj_WriteDSPCommand(0x3800, board))		/* Get Device Version Code */
		return -1;

	j->ver.low = j->ssr.low;
	j->ver.high = j->ssr.high;

	if (!j->cardtype) {
		if (j->dsp.low == 0x21) {
			j->XILINXbase = j->DSPbase + 0x10;
			bytes.high = bytes.low = inb_p(j->XILINXbase + 0x02);
			outb_p(bytes.low ^ 0xFF, j->XILINXbase + 0x02);
		        // Test for Internet LineJACK or Internet PhoneJACK Lite
			bytes.low = inb_p(j->XILINXbase + 0x02);
			if (bytes.low == bytes.high)	//  Register is read only on
                                    //  Internet PhoneJack Lite
			{
				j->cardtype = 400;	// Internet PhoneJACK Lite

				if (check_region(j->XILINXbase, 4)) {
					printk(KERN_INFO "ixj: can't get I/O address 0x%x\n", j->XILINXbase);
					return -1;
				}
				request_region(j->XILINXbase, 4, "ixj control");
				j->pld_slicw.pcib.e1 = 1;
				outb_p(j->pld_slicw.byte, j->XILINXbase);
			} else {
				j->cardtype = 300;	// Internet LineJACK

				if (check_region(j->XILINXbase, 8)) {
					printk(KERN_INFO "ixj: can't get I/O address 0x%x\n", j->XILINXbase);
					return -1;
				}
				request_region(j->XILINXbase, 8, "ixj control");
			}
		} else if (j->dsp.low == 0x22) {
			j->cardtype = 500;	// Internet PhoneJACK PCI

			request_region(j->XILINXbase, 4, "ixj control");
			j->pld_slicw.pcib.e1 = 1;
			outb_p(j->pld_slicw.byte, j->XILINXbase);
		} else
			j->cardtype = 100;	// Internet PhoneJACK

	} else {
		switch (j->cardtype) {
		case 100:	// Internet PhoneJACK

			if (!j->dsp.low != 0x20) {
				j->dsp.high = 0x80;
				j->dsp.low = 0x20;
				ixj_WriteDSPCommand(0x3800, board);
				j->ver.low = j->ssr.low;
				j->ver.high = j->ssr.high;
			}
			break;
		case 300:	// Internet LineJACK

			if (check_region(j->XILINXbase, 8)) {
				printk(KERN_INFO "ixj: can't get I/O address 0x%x\n", j->XILINXbase);
				return -1;
			}
			request_region(j->XILINXbase, 8, "ixj control");
			break;
		case 400:	//Internet PhoneJACK Lite

		case 500:	//Internet PhoneJACK PCI

			if (check_region(j->XILINXbase, 4)) {
				printk(KERN_INFO "ixj: can't get I/O address 0x%x\n", j->XILINXbase);
				return -1;
			}
			request_region(j->XILINXbase, 4, "ixj control");
			j->pld_slicw.pcib.e1 = 1;
			outb_p(j->pld_slicw.byte, j->XILINXbase);
			break;
		}
	}
	if (j->dsp.low == 0x20 || j->cardtype == 400 || j->cardtype == 500) {
		if (ixjdebug > 0)
			printk(KERN_INFO "Write CODEC config to Software Control Register\n");

		if (ixj_WriteDSPCommand(0xC462, board))		/* Write CODEC config to Software Control Register */
			return -1;

		if (ixjdebug > 0)
			printk(KERN_INFO "Write CODEC timing to Software Control Register\n");

		if (j->cardtype == 100) {
			cmd = 0x9FF2;
		} else {
			cmd = 0x9FF5;
		}
		if (ixj_WriteDSPCommand(cmd, board))	/* Write CODEC timing to Software Control Register */
			return -1;
	} else {
		if (set_base_frame(board, 30) != 30)
			return -1;

		if (j->cardtype == 300) {
			if (ixjdebug > 0)
				printk(KERN_INFO "Write CODEC config to Software Control Register\n");

			if (ixj_WriteDSPCommand(0xC528, board))		/* Write CODEC config to Software Control Register */
				return -1;

			if (ixjdebug > 0)
				printk(KERN_INFO "Turn on the PLD Clock at 8Khz\n");

			j->pld_clock.byte = 0;
			outb_p(j->pld_clock.byte, j->XILINXbase + 0x04);
		}
	}

	if (j->dsp.low == 0x20) {
		if (ixjdebug > 0)
			printk(KERN_INFO "Configure GPIO pins\n");

		j->gpio.bytes.high = 0x09;
		/*  bytes.low = 0xEF;  0xF7 */
		j->gpio.bits.gpio1 = 1;
		j->gpio.bits.gpio2 = 1;
		j->gpio.bits.gpio3 = 0;
		j->gpio.bits.gpio4 = 1;
		j->gpio.bits.gpio5 = 1;
		j->gpio.bits.gpio6 = 1;
		j->gpio.bits.gpio7 = 1;
		ixj_WriteDSPCommand(ixj[board].gpio.word, board);	/* Set GPIO pin directions */

		if (ixjdebug > 0)
			printk(KERN_INFO "Enable SLIC\n");

		j->gpio.bytes.high = 0x0B;
		j->gpio.bytes.low = 0x00;
		j->gpio.bits.gpio1 = 0;
		j->gpio.bits.gpio2 = 1;
		j->gpio.bits.gpio5 = 0;
		ixj_WriteDSPCommand(ixj[board].gpio.word, board);	/* send the ring stop signal */
		j->port = PORT_POTS;
	} else {
		if (j->cardtype == 300) {
			LED_SetState(0x1, board);
			jif = jiffies + (hertz / 10);
			while (time_before(jiffies, jif)) {
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(1);
			}
			LED_SetState(0x2, board);
			jif = jiffies + (hertz / 10);
			while (time_before(jiffies, jif)) {
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(1);
			}
			LED_SetState(0x4, board);
			jif = jiffies + (hertz / 10);
			while (time_before(jiffies, jif)) {
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(1);
			}
			LED_SetState(0x8, board);
			jif = jiffies + (hertz / 10);
			while (time_before(jiffies, jif)) {
				current->state = TASK_INTERRUPTIBLE;
				schedule_timeout(1);
			}
			LED_SetState(0x0, board);

			daa_get_version(board);

			if (ixjdebug > 0)
				printk("Loading DAA Coefficients\n");

			DAA_Coeff_US(board);
			if (!ixj_daa_write(board))
				printk("DAA write failed on board %d\n", board);

			ixj_daa_cid_reset(board);

			j->flags.pots_correct = 0;
			j->flags.pstn_present = 0;

			ixj_linetest(board);

			if (j->flags.pots_correct) {
				j->pld_scrw.bits.daafsyncen = 0;	// Turn off DAA Frame Sync

				outb_p(j->pld_scrw.byte, j->XILINXbase);
				j->pld_slicw.bits.rly1 = 1;
				j->pld_slicw.bits.spken = 1;
				outb_p(j->pld_slicw.byte, j->XILINXbase + 0x01);
				SLIC_SetState(PLD_SLIC_STATE_STANDBY, board);
				j->port = PORT_POTS;
			}
			if (ixjdebug > 0)
				printk(KERN_INFO "Enable Mixer\n");

			ixj_mixer(0x0000, board);	//Master Volume Left unmute 0db

			ixj_mixer(0x0100, board);	//Master Volume Right unmute 0db

			ixj_mixer(0x0F00, board);	//Mono Out Volume unmute 0db

			ixj_mixer(0x0C00, board);	//Mono1 Volume unmute 0db

			ixj_mixer(0x0200, board);	//Voice Left Volume unmute 0db

			ixj_mixer(0x0300, board);	//Voice Right Volume unmute 0db

			ixj_mixer(0x110C, board);	//Voice Left and Right out

			ixj_mixer(0x1401, board);	//Mono1 switch on mixer left

			ixj_mixer(0x1501, board);	//Mono1 switch on mixer right

			ixj_mixer(0x1700, board);	//Clock select

			ixj_mixer(0x1800, board);	//ADC Source select

		} else {
			j->port = PORT_POTS;
			SLIC_SetState(PLD_SLIC_STATE_STANDBY, board);
		}
	}

	j->intercom = -1;
	j->framesread = j->frameswritten = 0;
	j->rxreadycheck = j->txreadycheck = 0;

	if (ixj_WriteDSPCommand(0x0000, board))		/* Write IDLE to Software Control Register */
		return -1;

	// The read values of the SSR should be 0x00 for the IDLE command
	if (j->ssr.low || j->ssr.high)
		return -1;

	if (ixjdebug > 0)
		printk(KERN_INFO "Enable Line Monitor\n");

	if (ixjdebug > 0)
		printk(KERN_INFO "Set Line Monitor to Asyncronous Mode\n");

	if (ixj_WriteDSPCommand(0x7E01, board))		// Asynchronous Line Monitor

		return -1;

	if (ixjdebug > 0)
		printk(KERN_INFO "Enable DTMF Detectors\n");

	if (ixj_WriteDSPCommand(0x5151, board))		// Enable DTMF detection

		return -1;

	if (ixj_WriteDSPCommand(0x6E01, board))		// Set Asyncronous Tone Generation

		return -1;

	set_rec_depth(board, 2);	// Set Record Channel Limit to 2 frames

	set_play_depth(board, 2);	// Set Playback Channel Limit to 2 frames

	j->ex.bits.dtmf_ready = 0;
	j->dtmf_state = 0;
	j->dtmf_wp = ixj[board].dtmf_rp = 0;

	j->rec_mode = ixj[board].play_mode = -1;
	j->flags.ringing = 0;
	j->maxrings = MAXRINGS;
	j->ring_cadence = USA_RING_CADENCE;
	j->drybuffer = 0;
	j->winktime = 320;
	j->flags.dtmf_oob = 0;

	/* must be a device on the specified address */
	/* Register with the Telephony for Linux subsystem */
	j->p.f_op = &ixj_fops;
	j->p.open = ixj_open;
	phone_register_device(&j->p, PHONE_UNIT_ANY);

	add_caps(board);

	return 0;
}

static void cleanup(void)
{
	int cnt;

	del_timer(&ixj_timer);
//  if (ixj_major)
	//    unregister_chrdev(ixj_major, "ixj");
	for (cnt = 0; cnt < IXJMAX; cnt++) {
		if (ixj[cnt].cardtype == 300) {
			ixj[cnt].pld_scrw.bits.daafsyncen = 0;	// Turn off DAA Frame Sync

			outb_p(ixj[cnt].pld_scrw.byte, ixj[cnt].XILINXbase);
			ixj[cnt].pld_slicw.bits.rly1 = 0;
			ixj[cnt].pld_slicw.bits.rly2 = 0;
			ixj[cnt].pld_slicw.bits.rly3 = 0;
			outb_p(ixj[cnt].pld_slicw.byte, ixj[cnt].XILINXbase + 0x01);
			LED_SetState(0x0, cnt);

			release_region(ixj[cnt].XILINXbase, 8);
		}
		if (ixj[cnt].cardtype == 400 || ixj[cnt].cardtype == 500) {
			release_region(ixj[cnt].XILINXbase, 4);
		}
		if (ixj[cnt].DSPbase) {
			release_region(ixj[cnt].DSPbase, 16);
			phone_unregister_device(&ixj[cnt].p);
		}
		if (ixj[cnt].read_buffer)
			kfree(ixj[cnt].read_buffer);
		if (ixj[cnt].write_buffer)
			kfree(ixj[cnt].write_buffer);
#ifdef CONFIG_ISAPNP
		if (ixj[cnt].dev)
			ixj[cnt].dev->deactivate(ixj[cnt].dev);
#endif
	}
}


// Typedefs
typedef struct {
	BYTE length;
	DWORD bits;
} DATABLOCK;

static void PCIEE_WriteBit(WORD wEEPROMAddress, BYTE lastLCC, BYTE byData)
{
	lastLCC = lastLCC & 0xfb;
	lastLCC = lastLCC | (byData ? 4 : 0);
	outb(lastLCC, wEEPROMAddress);	//set data out bit as appropriate

	udelay(1000);
	lastLCC = lastLCC | 0x01;
	outb(lastLCC, wEEPROMAddress);	//SK rising edge

	byData = byData << 1;
	lastLCC = lastLCC & 0xfe;

	udelay(1000);
	outb(lastLCC, wEEPROMAddress);	//after delay, SK falling edge

}

static BYTE PCIEE_ReadBit(WORD wEEPROMAddress, BYTE lastLCC)
{
	udelay(1000);
	lastLCC = lastLCC | 0x01;
	outb(lastLCC, wEEPROMAddress);	//SK rising edge

	lastLCC = lastLCC & 0xfe;
	udelay(1000);
	outb(lastLCC, wEEPROMAddress);	//after delay, SK falling edge

	return ((inb(wEEPROMAddress) >> 3) & 1);
}

static BOOL PCIEE_ReadWord(WORD wAddress, WORD wLoc, WORD * pwResult)
{
	BYTE lastLCC;
	WORD wEEPROMAddress = wAddress + 3;
	DWORD i;
	BYTE byResult;

	*pwResult = 0;

	lastLCC = inb(wEEPROMAddress);

	lastLCC = lastLCC | 0x02;
	lastLCC = lastLCC & 0xfe;
	outb(lastLCC, wEEPROMAddress);	// CS hi, SK lo

	udelay(1000);		// delay

	PCIEE_WriteBit(wEEPROMAddress, lastLCC, 1);
	PCIEE_WriteBit(wEEPROMAddress, lastLCC, 1);
	PCIEE_WriteBit(wEEPROMAddress, lastLCC, 0);

	for (i = 0; i < 8; i++) {
		PCIEE_WriteBit(wEEPROMAddress, lastLCC, wLoc & 0x80 ? 1 : 0);
		wLoc <<= 1;
	}

	for (i = 0; i < 16; i++) {
		byResult = PCIEE_ReadBit(wEEPROMAddress, lastLCC);
		*pwResult = (*pwResult << 1) | byResult;
	}

	udelay(1000);		// another delay

	lastLCC = lastLCC & 0xfd;
	outb(lastLCC, wEEPROMAddress);	// negate CS

	return 0;
}

static DWORD PCIEE_GetSerialNumber(WORD wAddress)
{
	WORD wLo, wHi;

	if (PCIEE_ReadWord(wAddress, 62, &wLo))
		return 0;

	if (PCIEE_ReadWord(wAddress, 63, &wHi))
		return 0;

	return (((DWORD) wHi << 16) | wLo);
}

static int dspio[IXJMAX + 1];
static int xio[IXJMAX + 1];

MODULE_DESCRIPTION("Internet PhoneJACK/Internet LineJACK module - www.quicknet.net");
MODULE_AUTHOR("Ed Okerson <eokerson@quicknet.net>");

MODULE_PARM(dspio, "1-" __MODULE_STRING(IXJMAX) "i");
MODULE_PARM(xio, "1-" __MODULE_STRING(IXJMAX) "i");

static void __exit ixj_exit(void)
{
	cleanup();
}

static int __init ixj_init(void)
{
	int result;

	int i = 0;
	int cnt = 0;
	int probe = 0;
	struct pci_dev *pci = NULL;

#ifdef CONFIG_ISAPNP
	struct pci_dev *dev = NULL, *old_dev = NULL;
	int func = 0x110;

	while (1) {
		do {
			old_dev = dev;
			dev = isapnp_find_dev(NULL, ISAPNP_VENDOR('Q', 'T', 'I'),
					 ISAPNP_FUNCTION(func), old_dev);
			if (!dev)
				break;
			printk("preparing %x\n", func);
			result = dev->prepare(dev);
			if (result < 0) {
				printk("preparing failed %d \n", result);
				break;
			}
			if (!(dev->resource[0].flags & IORESOURCE_IO))
				return -ENODEV;
			dev->resource[0].flags |= IORESOURCE_AUTO;
			if (func != 0x110)
				dev->resource[1].flags |= IORESOURCE_AUTO;
			if (dev->activate(dev) < 0) {
				printk("isapnp configure failed (out of resources?)\n");
				return -ENOMEM;
			}
			ixj[cnt].DSPbase = dev->resource[0].start;	/* get real port */
			if (func != 0x110)
				ixj[cnt].XILINXbase = dev->resource[1].start;	/* get real port */

			result = check_region(ixj[cnt].DSPbase, 16);
			if (result) {
				printk(KERN_INFO "ixj: can't get I/O address 0x%x\n", ixj[cnt].DSPbase);
				cleanup();
				return result;
			}
			request_region(ixj[cnt].DSPbase, 16, "ixj DSP");
			switch (func) {
			case (0x110):
				ixj[cnt].cardtype = 100;
				break;
			case (0x310):
				ixj[cnt].cardtype = 300;
				break;
			case (0x410):
				ixj[cnt].cardtype = 400;
				break;
			}
			probe = ixj_selfprobe(cnt);

			ixj[cnt].serial = dev->bus->serial;
			ixj[cnt].dev = dev;
			printk(KERN_INFO "ixj: found card at 0x%x\n", ixj[cnt].DSPbase);
			cnt++;
		} while (dev);

		if (func == 0x410)
			break;
		if (func == 0x310)
			func = 0x410;
		if (func == 0x110)
			func = 0x310;
		dev = NULL;
	}
#else				//CONFIG_ISAPNP
	/* Use passed parameters for older kernels without PnP */

	for (cnt = 0; cnt < IXJMAX; cnt++) {
		if (dspio[cnt]) {
			ixj[cnt].DSPbase = dspio[cnt];
			ixj[cnt].XILINXbase = xio[cnt];
			ixj[cnt].cardtype = 0;
			result = check_region(ixj[cnt].DSPbase, 16);
			if (result) {
				printk(KERN_INFO "ixj: can't get I/O address 0x%x\n", ixj[cnt].DSPbase);
				cleanup();
				return result;
			}
			request_region(ixj[cnt].DSPbase, 16, "ixj DSP");
			probe = ixj_selfprobe(cnt);
			ixj[cnt].dev = NULL;
		}
	}
#endif
#ifdef CONFIG_PCI
	if (pci_present()) {
		for (i = 0; i < IXJMAX - cnt; i++) {
			pci = pci_find_device(0x15E2, 0x0500, pci);
			if (!pci)
				break;
			if (pci_enable_device(pci))
				break;
			{
				ixj[cnt].DSPbase = pci_resource_start(pci, 0);
				ixj[cnt].XILINXbase = ixj[cnt].DSPbase + 0x10;
				ixj[cnt].serial = (PCIEE_GetSerialNumber)pci_resource_start(pci, 2);

				result = check_region(ixj[cnt].DSPbase, 16);
				if (result) {
					printk(KERN_INFO "ixj: can't get I/O address 0x%x\n", ixj[cnt].DSPbase);
					cleanup();
					return result;
				}
				request_region(ixj[cnt].DSPbase, 16, "ixj DSP");
				ixj[cnt].cardtype = 500;
				probe = ixj_selfprobe(cnt);
				cnt++;
			}
		}
	}
#endif
	printk("%s\n", ixj_c_rcsid);

	ixj_init_timer();
	ixj_add_timer();	
	return probe;
}

module_init(ixj_init);
module_exit(ixj_exit);


static void DAA_Coeff_US(int board)
{
	IXJ *j = &ixj[board];

	int i;

	//-----------------------------------------------
	// CAO
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i++) {
		j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID[i] = 0;
	}

	// Bytes for IM-filter part 1 (04): 0E,32,E2,2F,C2,5A,C0,00
	    j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5] = 0xE2;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4] = 0x2F;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3] = 0xC2;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1] = 0xC0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0] = 0x00;

// Bytes for IM-filter part 2 (05): 72,85,00,0E,2B,3A,D0,08
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7] = 0x72;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6] = 0x85;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3] = 0x2B;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2] = 0x3A;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1] = 0xD0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0] = 0x08;

// Bytes for FRX-filter       (08): 03,8F,48,F2,8F,48,70,08
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7] = 0x03;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6] = 0x8F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5] = 0x48;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4] = 0xF2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3] = 0x8F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2] = 0x48;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1] = 0x70;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0] = 0x08;

// Bytes for FRR-filter       (07): 04,8F,38,7F,9B,EA,B0,08
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7] = 0x04;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6] = 0x8F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5] = 0x38;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4] = 0x7F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3] = 0x9B;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2] = 0xEA;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1] = 0xB0;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0] = 0x08;

// Bytes for AX-filter        (0A): 16,55,DD,CA
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3] = 0x16;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2] = 0x55;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1] = 0xDD;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0] = 0xCA;

// Bytes for AR-filter        (09): 52,D3,11,42
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2] = 0xD3;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1] = 0x11;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0] = 0x42;

// Bytes for TH-filter part 1 (00): 00,42,48,81,B3,80,00,98
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6] = 0x42;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5] = 0x48;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0] = 0x98;

// Bytes for TH-filter part 2 (01): 02,F2,33,A0,68,AB,8A,AD
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6] = 0xF2;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5] = 0x33;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4] = 0xA0;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3] = 0x68;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2] = 0xAB;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1] = 0x8A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0] = 0xAD;

// Bytes for TH-filter part 3 (02): 00,88,DA,54,A4,BA,2D,BB
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6] = 0x88;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4] = 0x54;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3] = 0xA4;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1] = 0x2D;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0] = 0xBB;

// ;  (10K, 0.68uF)
	// 
	// Bytes for Ringing part 1 (03):1B,3B,9B,BA,D4,1C,B3,23
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6] = 0x3B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5] = 0x9B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3] = 0xD4;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2] = 0x1C;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0] = 0x23;

// Bytes for Ringing part 2 (06):13,42,A6,BA,D4,73,CA,D5
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7] = 0x13;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6] = 0x42;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3] = 0xD4;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2] = 0x73;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0] = 0xD5;

// 
	// Levelmetering Ringing        (0D):B2,45,0F,8E      
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3] = 0xB2;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2] = 0x45;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1] = 0x0F;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0] = 0x8E;

// Caller ID 1st Tone           (0E):CA,0E,CA,09,99,99,99,99
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0] = 0x99;

// Caller ID 2nd Tone           (0F):FD,B5,BA,07,DA,00,00,00
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7] = 0xFD;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0] = 0x00;

// 
	// ;CR Registers
	// Config. Reg. 0 (filters)       (cr0):FE ; CLK gen. by crystal
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg = 0xFE;

// Config. Reg. 1 (dialing)       (cr1):05
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg = 0x05;

// Config. Reg. 2 (caller ID)     (cr2):04
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg = 0x04;

// Config. Reg. 3 (testloops)     (cr3):03 ; SEL Bit==0, HP-disabled
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg = 0x03;

// Config. Reg. 4 (analog gain)   (cr4):01
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = 0x02;		//0x01;

// Config. Reg. 5 (Version)       (cr5):02
	// Config. Reg. 6 (Reserved)      (cr6):00
	// Config. Reg. 7 (Reserved)      (cr7):00
	// 

// ;xr Registers
	// Ext. Reg. 0 (Interrupt Reg.)   (xr0):02
	j->m_DAAShadowRegs.XOP_xr0_W.reg = 0x02;	// SO_1 set to '1' because it is inverted.

// Ext. Reg. 1 (Interrupt enable) (xr1):1C // Cadence, RING, Caller ID, VDD_OK
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg = 0x3C;

// Ext. Reg. 2 (Cadence Time Out) (xr2):7D
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg = 0x7D;

// Ext. Reg. 3 (DC Char)          (xr3):32 ; B-Filter Off == 1
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg = 0x12;		//0x32;

// Ext. Reg. 4 (Cadence)          (xr4):00
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg = 0x00;

// Ext. Reg. 5 (Ring timer)       (xr5):22
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg = 0x22;

// Ext. Reg. 6 (Power State)      (xr6):00
	j->m_DAAShadowRegs.XOP_xr6_W.reg = 0x00;

// Ext. Reg. 7 (Vdd)              (xr7):40
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg = 0x40;		// 0x40 ??? Should it be 0x00?

// 
	// DTMF Tone 1                     (0B): 11,B3,5A,2C ;   697 Hz  
	//                                       12,33,5A,C3 ;  770 Hz  
	//                                       13,3C,5B,32 ;  852 Hz  
	//                                       1D,1B,5C,CC ;  941 Hz  
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3] = 0x11;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0] = 0x2C;

// DTMF Tone 2                     (0C): 32,32,52,B3 ;  1209 Hz  
	//                                       EC,1D,52,22 ;  1336 Hz  
	//                                       AA,AC,51,D2 ;  1477 Hz  
	//                                       9B,3B,51,25 ;  1633 Hz  
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0] = 0xB3;

}

static void DAA_Coeff_UK(int board)
{
	IXJ *j = &ixj[board];

	int i;

	//-----------------------------------------------
	// CAO
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i++) {
		j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID[i] = 0;
	}

	//  Bytes for IM-filter part 1 (04): 00,C2,BB,A8,CB,81,A0,00
	    j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6] = 0xC2;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5] = 0xBB;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4] = 0xA8;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3] = 0xCB;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1] = 0xA0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0] = 0x00;

	// Bytes for IM-filter part 2 (05): 40,00,00,0A,A4,33,E0,08
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7] = 0x40;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4] = 0x0A;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3] = 0xA4;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2] = 0x33;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1] = 0xE0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0] = 0x08;

// Bytes for FRX-filter       (08): 07,9B,ED,24,B2,A2,A0,08
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6] = 0x9B;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5] = 0xED;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4] = 0x24;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3] = 0xB2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1] = 0xA0;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0] = 0x08;

// Bytes for FRR-filter       (07): 0F,92,F2,B2,87,D2,30,08
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7] = 0x0F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6] = 0x92;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5] = 0xF2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4] = 0xB2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3] = 0x87;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2] = 0xD2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1] = 0x30;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0] = 0x08;

// Bytes for AX-filter        (0A): 1B,A5,DD,CA
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2] = 0xA5;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1] = 0xDD;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0] = 0xCA;

// Bytes for AR-filter        (09): E2,27,10,D6
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3] = 0xE2;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2] = 0x27;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1] = 0x10;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0] = 0xD6;

// Bytes for TH-filter part 1 (00): 80,2D,38,8B,D0,00,00,98
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6] = 0x2D;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5] = 0x38;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4] = 0x8B;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3] = 0xD0;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0] = 0x98;

// Bytes for TH-filter part 2 (01): 02,5A,53,F0,0B,5F,84,D4
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5] = 0x53;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4] = 0xF0;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3] = 0x0B;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2] = 0x5F;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1] = 0x84;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0] = 0xD4;

// Bytes for TH-filter part 3 (02): 00,88,6A,A4,8F,52,F5,32
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6] = 0x88;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5] = 0x6A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4] = 0xA4;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3] = 0x8F;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1] = 0xF5;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0] = 0x32;

// ; idle

// Bytes for Ringing part 1 (03):1B,3C,93,3A,22,12,A3,23
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6] = 0x3C;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5] = 0x93;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4] = 0x3A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0] = 0x23;

// Bytes for Ringing part 2 (06):12,A2,A6,BA,22,7A,0A,D5
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2] = 0x7A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1] = 0x0A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0] = 0xD5;

// Levelmetering Ringing           (0D):AA,35,0F,8E     ; 25Hz 30V less possible?
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3] = 0xAA;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2] = 0x35;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1] = 0x0F;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0] = 0x8E;

// Caller ID 1st Tone              (0E):CA,0E,CA,09,99,99,99,99
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0] = 0x99;

// Caller ID 2nd Tone              (0F):FD,B5,BA,07,DA,00,00,00
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7] = 0xFD;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0] = 0x00;

// ;CR Registers
	// Config. Reg. 0 (filters)        (cr0):FF
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg = 0xFF;		//0xFE;

// Config. Reg. 1 (dialing)        (cr1):05
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg = 0x05;

// Config. Reg. 2 (caller ID)      (cr2):04
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg = 0x04;

// Config. Reg. 3 (testloops)      (cr3):00        ; 
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg = 0x00;

// Config. Reg. 4 (analog gain)    (cr4):01
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = 0x02;		//0x01;

// Config. Reg. 5 (Version)        (cr5):02
	// Config. Reg. 6 (Reserved)       (cr6):00
	// Config. Reg. 7 (Reserved)       (cr7):00

// ;xr Registers
	// Ext. Reg. 0 (Interrupt Reg.)    (xr0):02
	j->m_DAAShadowRegs.XOP_xr0_W.reg = 0x02;	// SO_1 set to '1' because it is inverted.

// Ext. Reg. 1 (Interrupt enable)  (xr1):1C
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg = 0x1C;		// RING, Caller ID, VDD_OK

// Ext. Reg. 2 (Cadence Time Out)  (xr2):7D
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg = 0x7D;

// Ext. Reg. 3 (DC Char)           (xr3):36        ; 
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg = 0x36;

// Ext. Reg. 4 (Cadence)           (xr4):00
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg = 0x00;

// Ext. Reg. 5 (Ring timer)        (xr5):22
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg = 0x22;

// Ext. Reg. 6 (Power State)       (xr6):00
	j->m_DAAShadowRegs.XOP_xr6_W.reg = 0x00;

// Ext. Reg. 7 (Vdd)               (xr7):46
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg = 0x46;		// 0x46 ??? Should it be 0x00?

// DTMF Tone 1                     (0B): 11,B3,5A,2C    ;   697 Hz  
	//                                       12,33,5A,C3    ;  770 Hz  
	//                                       13,3C,5B,32    ;  852 Hz  
	//                                       1D,1B,5C,CC    ;  941 Hz  
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3] = 0x11;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0] = 0x2C;

// DTMF Tone 2                     (0C): 32,32,52,B3    ;  1209 Hz  
	//                                       EC,1D,52,22    ;  1336 Hz  
	//                                       AA,AC,51,D2    ;  1477 Hz  
	//                                       9B,3B,51,25    ;  1633 Hz  
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0] = 0xB3;

}


static void DAA_Coeff_France(int board)
{
	IXJ *j = &ixj[board];

	int i;

	//-----------------------------------------------
	// CAO
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i++) {
		j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID[i] = 0;
	}

	// Bytes for IM-filter part 1 (04): 02,A2,43,2C,22,AF,A0,00
	    j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5] = 0x43;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4] = 0x2C;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2] = 0xAF;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1] = 0xA0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0] = 0x00;

// Bytes for IM-filter part 2 (05): 67,CE,00,0C,22,33,E0,08
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7] = 0x67;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6] = 0xCE;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4] = 0x2C;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2] = 0x33;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1] = 0xE0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0] = 0x08;

// Bytes for FRX-filter       (08): 07,9A,28,F6,23,4A,B0,08
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6] = 0x9A;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5] = 0x28;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4] = 0xF6;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3] = 0x23;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2] = 0x4A;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1] = 0xB0;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0] = 0x08;

// Bytes for FRR-filter       (07): 03,8F,F9,2F,9E,FA,20,08
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7] = 0x03;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6] = 0x8F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5] = 0xF9;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4] = 0x2F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3] = 0x9E;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2] = 0xFA;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1] = 0x20;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0] = 0x08;

// Bytes for AX-filter        (0A): 16,B5,DD,CA
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3] = 0x16;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1] = 0xDD;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0] = 0xCA;

// Bytes for AR-filter        (09): 52,C7,10,D6
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3] = 0xE2;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2] = 0xC7;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1] = 0x10;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0] = 0xD6;

// Bytes for TH-filter part 1 (00): 00,42,48,81,A6,80,00,98
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6] = 0x42;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5] = 0x48;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0] = 0x98;

// Bytes for TH-filter part 2 (01): 02,AC,2A,30,78,AC,8A,2C
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6] = 0xAC;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5] = 0x2A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4] = 0x30;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3] = 0x78;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2] = 0xAC;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1] = 0x8A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0] = 0x2C;

// Bytes for TH-filter part 3 (02): 00,88,DA,A5,22,BA,2C,45
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6] = 0x88;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4] = 0xA5;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1] = 0x2C;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0] = 0x45;

// ; idle

// Bytes for Ringing part 1 (03):1B,3C,93,3A,22,12,A3,23
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6] = 0x3C;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5] = 0x93;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4] = 0x3A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0] = 0x23;

// Bytes for Ringing part 2 (06):12,A2,A6,BA,22,7A,0A,D5
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2] = 0x7A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1] = 0x0A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0] = 0xD5;

// Levelmetering Ringing           (0D):32,45,B5,84     ; 50Hz 20V
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2] = 0x45;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0] = 0x84;

// Caller ID 1st Tone              (0E):CA,0E,CA,09,99,99,99,99
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0] = 0x99;

// Caller ID 2nd Tone              (0F):FD,B5,BA,07,DA,00,00,00
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7] = 0xFD;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0] = 0x00;

// ;CR Registers
	// Config. Reg. 0 (filters)        (cr0):FF
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg = 0xFF;

// Config. Reg. 1 (dialing)        (cr1):05
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg = 0x05;

// Config. Reg. 2 (caller ID)      (cr2):04
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg = 0x04;

// Config. Reg. 3 (testloops)      (cr3):00        ; 
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg = 0x00;

// Config. Reg. 4 (analog gain)    (cr4):01
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = 0x02;		//0x01;

// Config. Reg. 5 (Version)        (cr5):02
	// Config. Reg. 6 (Reserved)       (cr6):00
	// Config. Reg. 7 (Reserved)       (cr7):00

// ;xr Registers
	// Ext. Reg. 0 (Interrupt Reg.)    (xr0):02
	j->m_DAAShadowRegs.XOP_xr0_W.reg = 0x02;	// SO_1 set to '1' because it is inverted.

// Ext. Reg. 1 (Interrupt enable)  (xr1):1C
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg = 0x1C;		// RING, Caller ID, VDD_OK

// Ext. Reg. 2 (Cadence Time Out)  (xr2):7D
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg = 0x7D;

// Ext. Reg. 3 (DC Char)           (xr3):36        ; 
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg = 0x36;

// Ext. Reg. 4 (Cadence)           (xr4):00
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg = 0x00;

// Ext. Reg. 5 (Ring timer)        (xr5):22
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg = 0x22;

// Ext. Reg. 6 (Power State)       (xr6):00
	j->m_DAAShadowRegs.XOP_xr6_W.reg = 0x00;

// Ext. Reg. 7 (Vdd)               (xr7):46
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg = 0x46;		// 0x46 ??? Should it be 0x00?

// DTMF Tone 1                     (0B): 11,B3,5A,2C    ;   697 Hz  
	//                                       12,33,5A,C3    ;  770 Hz  
	//                                       13,3C,5B,32    ;  852 Hz  
	//                                       1D,1B,5C,CC    ;  941 Hz  
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3] = 0x11;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0] = 0x2C;

// DTMF Tone 2                     (0C): 32,32,52,B3    ;  1209 Hz  
	//                                       EC,1D,52,22    ;  1336 Hz  
	//                                       AA,AC,51,D2    ;  1477 Hz  
	//                                       9B,3B,51,25    ;  1633 Hz  
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0] = 0xB3;

}


static void DAA_Coeff_Germany(int board)
{
	IXJ *j = &ixj[board];

	int i;

	//-----------------------------------------------
	// CAO
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i++) {
		j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID[i] = 0;
	}

	// Bytes for IM-filter part 1 (04): 00,CE,BB,B8,D2,81,B0,00
	    j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6] = 0xCE;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5] = 0xBB;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4] = 0xB8;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3] = 0xD2;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1] = 0xB0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0] = 0x00;

// Bytes for IM-filter part 2 (05): 45,8F,00,0C,D2,3A,D0,08
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7] = 0x45;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6] = 0x8F;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4] = 0x0C;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3] = 0xD2;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2] = 0x3A;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1] = 0xD0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0] = 0x08;

// Bytes for FRX-filter       (08): 07,AA,E2,34,24,89,20,08
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6] = 0xAA;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5] = 0xE2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4] = 0x34;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3] = 0x24;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2] = 0x89;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1] = 0x20;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0] = 0x08;

// Bytes for FRR-filter       (07): 02,87,FA,37,9A,CA,B0,08
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6] = 0x87;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5] = 0xFA;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4] = 0x37;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3] = 0x9A;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1] = 0xB0;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0] = 0x08;

// Bytes for AX-filter        (0A): 72,D5,DD,CA
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3] = 0x72;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2] = 0xD5;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1] = 0xDD;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0] = 0xCA;

// Bytes for AR-filter        (09): 72,42,13,4B
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3] = 0x72;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2] = 0x42;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1] = 0x13;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0] = 0x4B;

// Bytes for TH-filter part 1 (00): 80,52,48,81,AD,80,00,98
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5] = 0x48;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3] = 0xAD;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0] = 0x98;

// Bytes for TH-filter part 2 (01): 02,42,5A,20,E8,1A,81,27
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6] = 0x42;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4] = 0x20;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3] = 0xE8;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2] = 0x1A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0] = 0x27;

// Bytes for TH-filter part 3 (02): 00,88,63,26,BD,4B,A3,C2
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6] = 0x88;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5] = 0x63;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4] = 0x26;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3] = 0xBD;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2] = 0x4B;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0] = 0xC2;

// ;  (10K, 0.68uF)

// Bytes for Ringing part 1 (03):1B,3B,9B,BA,D4,1C,B3,23
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6] = 0x3B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5] = 0x9B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3] = 0xD4;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2] = 0x1C;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0] = 0x23;

// Bytes for Ringing part 2 (06):13,42,A6,BA,D4,73,CA,D5
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7] = 0x13;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6] = 0x42;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3] = 0xD4;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2] = 0x73;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0] = 0xD5;

// Levelmetering Ringing        (0D):B2,45,0F,8E      
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3] = 0xB2;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2] = 0x45;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1] = 0x0F;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0] = 0x8E;

// Caller ID 1st Tone           (0E):CA,0E,CA,09,99,99,99,99
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0] = 0x99;

// Caller ID 2nd Tone           (0F):FD,B5,BA,07,DA,00,00,00
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7] = 0xFD;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0] = 0x00;

// ;CR Registers
	// Config. Reg. 0 (filters)        (cr0):FF ; all Filters enabled, CLK from ext. source
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg = 0xFF;

// Config. Reg. 1 (dialing)        (cr1):05 ; Manual Ring, Ring metering enabled
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg = 0x05;

// Config. Reg. 2 (caller ID)      (cr2):04 ; Analog Gain 0dB, FSC internal
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg = 0x04;

// Config. Reg. 3 (testloops)      (cr3):00 ; SEL Bit==0, HP-enabled
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg = 0x00;

// Config. Reg. 4 (analog gain)    (cr4):01
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = 0x02;		//0x01;

// Config. Reg. 5 (Version)        (cr5):02
	// Config. Reg. 6 (Reserved)       (cr6):00
	// Config. Reg. 7 (Reserved)       (cr7):00

// ;xr Registers
	// Ext. Reg. 0 (Interrupt Reg.)    (xr0):02
	j->m_DAAShadowRegs.XOP_xr0_W.reg = 0x02;	// SO_1 set to '1' because it is inverted.

// Ext. Reg. 1 (Interrupt enable)  (xr1):1C ; Ring, CID, VDDOK Interrupts enabled
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg = 0x1C;		// RING, Caller ID, VDD_OK

// Ext. Reg. 2 (Cadence Time Out)  (xr2):7D
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg = 0x7D;

// Ext. Reg. 3 (DC Char)           (xr3):32 ; B-Filter Off==1, U0=3.5V, R=200Ohm
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg = 0x32;

// Ext. Reg. 4 (Cadence)           (xr4):00
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg = 0x00;

// Ext. Reg. 5 (Ring timer)        (xr5):22
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg = 0x22;

// Ext. Reg. 6 (Power State)       (xr6):00
	j->m_DAAShadowRegs.XOP_xr6_W.reg = 0x00;

// Ext. Reg. 7 (Vdd)               (xr7):40 ; VDD=4.25 V
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg = 0x40;		// 0x40 ??? Should it be 0x00?

// DTMF Tone 1                     (0B): 11,B3,5A,2C    ;   697 Hz  
	//                                       12,33,5A,C3    ;  770 Hz  
	//                                       13,3C,5B,32    ;  852 Hz  
	//                                       1D,1B,5C,CC    ;  941 Hz  
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3] = 0x11;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0] = 0x2C;

// DTMF Tone 2                     (0C): 32,32,52,B3    ;  1209 Hz  
	//                                       EC,1D,52,22    ;  1336 Hz  
	//                                       AA,AC,51,D2    ;  1477 Hz  
	//                                       9B,3B,51,25    ;  1633 Hz  
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0] = 0xB3;

}


static void DAA_Coeff_Australia(int board)
{
	IXJ *j = &ixj[board];

	int i;

	//-----------------------------------------------
	// CAO
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i++) {
		j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID[i] = 0;
	}

	// Bytes for IM-filter part 1 (04): 00,A3,AA,28,B3,82,D0,00
	    j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5] = 0xAA;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4] = 0x28;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2] = 0x82;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1] = 0xD0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0] = 0x00;

// Bytes for IM-filter part 2 (05): 70,96,00,09,32,6B,C0,08
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7] = 0x70;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6] = 0x96;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2] = 0x6B;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1] = 0xC0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0] = 0x08;

// Bytes for FRX-filter       (08): 07,96,E2,34,32,9B,30,08
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6] = 0x96;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5] = 0xE2;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4] = 0x34;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2] = 0x9B;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1] = 0x30;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0] = 0x08;

// Bytes for FRR-filter       (07): 0F,9A,E9,2F,22,CC,A0,08
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7] = 0x0F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6] = 0x9A;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5] = 0xE9;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4] = 0x2F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2] = 0xCC;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1] = 0xA0;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0] = 0x08;

// Bytes for AX-filter        (0A): CB,45,DD,CA
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3] = 0xCB;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2] = 0x45;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1] = 0xDD;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0] = 0xCA;

// Bytes for AR-filter        (09): 1B,67,10,D6
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2] = 0x67;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1] = 0x10;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0] = 0xD6;

// Bytes for TH-filter part 1 (00): 80,52,48,81,AF,80,00,98
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5] = 0x48;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3] = 0xAF;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0] = 0x98;

// Bytes for TH-filter part 2 (01): 02,DB,52,B0,38,01,82,AC
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6] = 0xDB;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4] = 0xB0;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3] = 0x38;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2] = 0x01;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1] = 0x82;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0] = 0xAC;

// Bytes for TH-filter part 3 (02): 00,88,4A,3E,2C,3B,24,46
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6] = 0x88;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5] = 0x4A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4] = 0x3E;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3] = 0x2C;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2] = 0x3B;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1] = 0x24;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0] = 0x46;

// ;  idle

// Bytes for Ringing part 1 (03):1B,3C,93,3A,22,12,A3,23
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6] = 0x3C;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5] = 0x93;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4] = 0x3A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0] = 0x23;

// Bytes for Ringing part 2 (06):12,A2,A6,BA,22,7A,0A,D5
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2] = 0x7A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1] = 0x0A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0] = 0xD5;

// Levelmetering Ringing           (0D):32,45,B5,84   ; 50Hz 20V
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2] = 0x45;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0] = 0x84;

// Caller ID 1st Tone              (0E):CA,0E,CA,09,99,99,99,99
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0] = 0x99;

// Caller ID 2nd Tone              (0F):FD,B5,BA,07,DA,00,00,00
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7] = 0xFD;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0] = 0x00;

// ;CR Registers
	// Config. Reg. 0 (filters)        (cr0):FF
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg = 0xFF;

// Config. Reg. 1 (dialing)        (cr1):05
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg = 0x05;

// Config. Reg. 2 (caller ID)      (cr2):04
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg = 0x04;

// Config. Reg. 3 (testloops)      (cr3):00        ; 
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg = 0x00;

// Config. Reg. 4 (analog gain)    (cr4):01
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = 0x02;		//0x01;

// Config. Reg. 5 (Version)        (cr5):02
	// Config. Reg. 6 (Reserved)       (cr6):00
	// Config. Reg. 7 (Reserved)       (cr7):00

// ;xr Registers
	// Ext. Reg. 0 (Interrupt Reg.)    (xr0):02
	j->m_DAAShadowRegs.XOP_xr0_W.reg = 0x02;	// SO_1 set to '1' because it is inverted.

// Ext. Reg. 1 (Interrupt enable)  (xr1):1C
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg = 0x1C;		// RING, Caller ID, VDD_OK

// Ext. Reg. 2 (Cadence Time Out)  (xr2):7D
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg = 0x7D;

// Ext. Reg. 3 (DC Char)           (xr3):2B      ; 
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg = 0x2B;

// Ext. Reg. 4 (Cadence)           (xr4):00
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg = 0x00;

// Ext. Reg. 5 (Ring timer)        (xr5):22
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg = 0x22;

// Ext. Reg. 6 (Power State)       (xr6):00
	j->m_DAAShadowRegs.XOP_xr6_W.reg = 0x00;

// Ext. Reg. 7 (Vdd)               (xr7):40
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg = 0x40;		// 0x40 ??? Should it be 0x00?

// DTMF Tone 1                     (0B): 11,B3,5A,2C    ;   697 Hz  
	//                                       12,33,5A,C3    ;  770 Hz  
	//                                       13,3C,5B,32    ;  852 Hz  
	//                                       1D,1B,5C,CC    ;  941 Hz  
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3] = 0x11;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0] = 0x2C;

// DTMF Tone 2                     (0C): 32,32,52,B3    ;  1209 Hz  
	//                                       EC,1D,52,22    ;  1336 Hz  
	//                                       AA,AC,51,D2    ;  1477 Hz  
	//                                       9B,3B,51,25    ;  1633 Hz  
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0] = 0xB3;

}

static void DAA_Coeff_Japan(int board)
{
	IXJ *j = &ixj[board];

	int i;

	//-----------------------------------------------
	// CAO
	for (i = 0; i < ALISDAA_CALLERID_SIZE; i++) {
		j->m_DAAShadowRegs.CAO_REGS.CAO.CallerID[i] = 0;
	}

	// Bytes for IM-filter part 1 (04): 06,BD,E2,2D,BA,F9,A0,00
	    j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[7] = 0x06;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[6] = 0xBD;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[5] = 0xE2;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[4] = 0x2D;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[3] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[2] = 0xF9;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[1] = 0xA0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_1[0] = 0x00;

// Bytes for IM-filter part 2 (05): 6F,F7,00,0E,34,33,E0,08
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[7] = 0x6F;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[6] = 0xF7;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[5] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[4] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[3] = 0x34;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[2] = 0x33;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[1] = 0xE0;
	j->m_DAAShadowRegs.COP_REGS.COP.IMFilterCoeff_2[0] = 0x08;

// Bytes for FRX-filter       (08): 02,8F,68,77,9C,58,F0,08
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[6] = 0x8F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[5] = 0x68;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[4] = 0x77;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[3] = 0x9C;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[2] = 0x58;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[1] = 0xF0;
	j->m_DAAShadowRegs.COP_REGS.COP.FRXFilterCoeff[0] = 0x08;

// Bytes for FRR-filter       (07): 03,8F,38,73,87,EA,20,08
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[7] = 0x03;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[6] = 0x8F;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[5] = 0x38;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[4] = 0x73;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[3] = 0x87;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[2] = 0xEA;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[1] = 0x20;
	j->m_DAAShadowRegs.COP_REGS.COP.FRRFilterCoeff[0] = 0x08;

// Bytes for AX-filter        (0A): 51,C5,DD,CA
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[3] = 0x51;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[2] = 0xC5;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[1] = 0xDD;
	j->m_DAAShadowRegs.COP_REGS.COP.AXFilterCoeff[0] = 0xCA;

// Bytes for AR-filter        (09): 25,A7,10,D6
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[3] = 0x25;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[2] = 0xA7;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[1] = 0x10;
	j->m_DAAShadowRegs.COP_REGS.COP.ARFilterCoeff[0] = 0xD6;

// Bytes for TH-filter part 1 (00): 00,42,48,81,AE,80,00,98
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[6] = 0x42;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[5] = 0x48;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[4] = 0x81;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[3] = 0xAE;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[2] = 0x80;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_1[0] = 0x98;

// Bytes for TH-filter part 2 (01): 02,AB,2A,20,99,5B,89,28
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[7] = 0x02;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[6] = 0xAB;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[5] = 0x2A;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[4] = 0x20;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[2] = 0x5B;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[1] = 0x89;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_2[0] = 0x28;

// Bytes for TH-filter part 3 (02): 00,88,DA,25,34,C5,4C,BA
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[7] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[6] = 0x88;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[5] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[4] = 0x25;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[3] = 0x34;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[2] = 0xC5;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[1] = 0x4C;
	j->m_DAAShadowRegs.COP_REGS.COP.THFilterCoeff_3[0] = 0xBA;

// ;  idle

// Bytes for Ringing part 1 (03):1B,3C,93,3A,22,12,A3,23
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[7] = 0x1B;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[6] = 0x3C;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[5] = 0x93;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[4] = 0x3A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[2] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[1] = 0xA3;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_1[0] = 0x23;

// Bytes for Ringing part 2 (06):12,A2,A6,BA,22,7A,0A,D5
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[7] = 0x12;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[6] = 0xA2;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[5] = 0xA6;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[4] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[3] = 0x22;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[2] = 0x7A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[1] = 0x0A;
	j->m_DAAShadowRegs.COP_REGS.COP.RingerImpendance_2[0] = 0xD5;

// Levelmetering Ringing           (0D):AA,35,0F,8E    ; 25Hz 30V ?????????
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[3] = 0xAA;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[2] = 0x35;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[1] = 0x0F;
	j->m_DAAShadowRegs.COP_REGS.COP.LevelmeteringRinging[0] = 0x8E;

// Caller ID 1st Tone              (0E):CA,0E,CA,09,99,99,99,99
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[7] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[6] = 0x0E;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[5] = 0xCA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[4] = 0x09;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[3] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[2] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[1] = 0x99;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID1stTone[0] = 0x99;

// Caller ID 2nd Tone              (0F):FD,B5,BA,07,DA,00,00,00
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[7] = 0xFD;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[6] = 0xB5;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[5] = 0xBA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[4] = 0x07;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[3] = 0xDA;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[2] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[1] = 0x00;
	j->m_DAAShadowRegs.COP_REGS.COP.CallerID2ndTone[0] = 0x00;

// ;CR Registers
	// Config. Reg. 0 (filters)        (cr0):FF
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr0.reg = 0xFF;

// Config. Reg. 1 (dialing)        (cr1):05
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr1.reg = 0x05;

// Config. Reg. 2 (caller ID)      (cr2):04
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr2.reg = 0x04;

// Config. Reg. 3 (testloops)      (cr3):00        ; 
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr3.reg = 0x00;

// Config. Reg. 4 (analog gain)    (cr4):01
	j->m_DAAShadowRegs.SOP_REGS.SOP.cr4.reg = 0x02;		//0x01;

// Config. Reg. 5 (Version)        (cr5):02
	// Config. Reg. 6 (Reserved)       (cr6):00
	// Config. Reg. 7 (Reserved)       (cr7):00

// ;xr Registers
	// Ext. Reg. 0 (Interrupt Reg.)    (xr0):02
	j->m_DAAShadowRegs.XOP_xr0_W.reg = 0x02;	// SO_1 set to '1' because it is inverted.

// Ext. Reg. 1 (Interrupt enable)  (xr1):1C
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr1.reg = 0x1C;		// RING, Caller ID, VDD_OK

// Ext. Reg. 2 (Cadence Time Out)  (xr2):7D
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr2.reg = 0x7D;

// Ext. Reg. 3 (DC Char)           (xr3):22        ; 
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr3.reg = 0x22;

// Ext. Reg. 4 (Cadence)           (xr4):00
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr4.reg = 0x00;

// Ext. Reg. 5 (Ring timer)        (xr5):22
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr5.reg = 0x22;

// Ext. Reg. 6 (Power State)       (xr6):00
	j->m_DAAShadowRegs.XOP_xr6_W.reg = 0x00;

// Ext. Reg. 7 (Vdd)               (xr7):40
	j->m_DAAShadowRegs.XOP_REGS.XOP.xr7.reg = 0x40;		// 0x40 ??? Should it be 0x00?

// DTMF Tone 1                     (0B): 11,B3,5A,2C    ;   697 Hz  
	//                                       12,33,5A,C3    ;  770 Hz  
	//                                       13,3C,5B,32    ;  852 Hz  
	//                                       1D,1B,5C,CC    ;  941 Hz  
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[3] = 0x11;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[2] = 0xB3;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[1] = 0x5A;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone1Coeff[0] = 0x2C;

// DTMF Tone 2                     (0C): 32,32,52,B3    ;  1209 Hz  
	//                                       EC,1D,52,22    ;  1336 Hz  
	//                                       AA,AC,51,D2    ;  1477 Hz  
	//                                       9B,3B,51,25    ;  1633 Hz  

	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[3] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[2] = 0x32;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[1] = 0x52;
	j->m_DAAShadowRegs.COP_REGS.COP.Tone2Coeff[0] = 0xB3;

}

static s16 tone_table[][19] =
{
	{			// f20_50[]
		32538,		// A1 = 1.985962
		 -32325,	// A2 = -0.986511
		 -343,		// B2 = -0.010493
		 0,		// B1 = 0
		 343,		// B0 = 0.010493
		 32619,		// A1 = 1.990906
		 -32520,	// A2 = -0.992462
		 19179,		// B2 = 0.585327
		 -19178,	// B1 = -1.170593
		 19179,		// B0 = 0.585327
		 32723,		// A1 = 1.997314
		 -32686,	// A2 = -0.997528
		 9973,		// B2 = 0.304352
		 -9955,		// B1 = -0.607605
		 9973,		// B0 = 0.304352
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f133_200[]
		32072,		// A1 = 1.95752
		 -31896,	// A2 = -0.973419
		 -435,		// B2 = -0.013294
		 0,		// B1 = 0
		 435,		// B0 = 0.013294
		 32188,		// A1 = 1.9646
		 -32400,	// A2 = -0.98877
		 15139,		// B2 = 0.462036
		 -14882,	// B1 = -0.908356
		 15139,		// B0 = 0.462036
		 32473,		// A1 = 1.981995
		 -32524,	// A2 = -0.992584
		 23200,		// B2 = 0.708008
		 -23113,	// B1 = -1.410706
		 23200,		// B0 = 0.708008
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 300.txt
		31769,		// A1 = -1.939026
		 -32584,	// A2 = 0.994385
		 -475,		// B2 = -0.014522
		 0,		// B1 = 0.000000
		 475,		// B0 = 0.014522
		 31789,		// A1 = -1.940247
		 -32679,	// A2 = 0.997284
		 17280,		// B2 = 0.527344
		 -16865,	// B1 = -1.029358
		 17280,		// B0 = 0.527344
		 31841,		// A1 = -1.943481
		 -32681,	// A2 = 0.997345
		 543,		// B2 = 0.016579
		 -525,		// B1 = -0.032097
		 543,		// B0 = 0.016579
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f300_420[]
		30750,		// A1 = 1.876892
		 -31212,	// A2 = -0.952515
		 -804,		// B2 = -0.024541
		 0,		// B1 = 0
		 804,		// B0 = 0.024541
		 30686,		// A1 = 1.872925
		 -32145,	// A2 = -0.980988
		 14747,		// B2 = 0.450043
		 -13703,	// B1 = -0.836395
		 14747,		// B0 = 0.450043
		 31651,		// A1 = 1.931824
		 -32321,	// A2 = -0.986389
		 24425,		// B2 = 0.745422
		 -23914,	// B1 = -1.459595
		 24427,		// B0 = 0.745483
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 330.txt
		31613,		// A1 = -1.929565
		 -32646,	// A2 = 0.996277
		 -185,		// B2 = -0.005657
		 0,		// B1 = 0.000000
		 185,		// B0 = 0.005657
		 31620,		// A1 = -1.929932
		 -32713,	// A2 = 0.998352
		 19253,		// B2 = 0.587585
		 -18566,	// B1 = -1.133179
		 19253,		// B0 = 0.587585
		 31674,		// A1 = -1.933228
		 -32715,	// A2 = 0.998413
		 2575,		// B2 = 0.078590
		 -2495,		// B1 = -0.152283
		 2575,		// B0 = 0.078590
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f300_425[]
		30741,		// A1 = 1.876282
		 -31475,	// A2 = -0.960541
		 -703,		// B2 = -0.021484
		 0,		// B1 = 0
		 703,		// B0 = 0.021484
		 30688,		// A1 = 1.873047
		 -32248,	// A2 = -0.984161
		 14542,		// B2 = 0.443787
		 -13523,	// B1 = -0.825439
		 14542,		// B0 = 0.443817
		 31494,		// A1 = 1.922302
		 -32366,	// A2 = -0.987762
		 21577,		// B2 = 0.658508
		 -21013,	// B1 = -1.282532
		 21577,		// B0 = 0.658508
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f330_440[]
		30627,		// A1 = 1.869324
		 -31338,	// A2 = -0.95636
		 -843,		// B2 = -0.025749
		 0,		// B1 = 0
		 843,		// B0 = 0.025749
		 30550,		// A1 = 1.864685
		 -32221,	// A2 = -0.983337
		 13594,		// B2 = 0.414886
		 -12589,	// B1 = -0.768402
		 13594,		// B0 = 0.414886
		 31488,		// A1 = 1.921936
		 -32358,	// A2 = -0.987518
		 24684,		// B2 = 0.753296
		 -24029,	// B1 = -1.466614
		 24684,		// B0 = 0.753296
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 340.txt
		31546,		// A1 = -1.925476
		 -32646,	// A2 = 0.996277
		 -445,		// B2 = -0.013588
		 0,		// B1 = 0.000000
		 445,		// B0 = 0.013588
		 31551,		// A1 = -1.925781
		 -32713,	// A2 = 0.998352
		 23884,		// B2 = 0.728882
		 -22979,	// B1 = -1.402527
		 23884,		// B0 = 0.728882
		 31606,		// A1 = -1.929138
		 -32715,	// A2 = 0.998413
		 863,		// B2 = 0.026367
		 -835,		// B1 = -0.050985
		 863,		// B0 = 0.026367
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f350_400[]
		31006,		// A1 = 1.892517
		 -32029,	// A2 = -0.977448
		 -461,		// B2 = -0.014096
		 0,		// B1 = 0
		 461,		// B0 = 0.014096
		 30999,		// A1 = 1.892029
		 -32487,	// A2 = -0.991455
		 11325,		// B2 = 0.345612
		 -10682,	// B1 = -0.651978
		 11325,		// B0 = 0.345612
		 31441,		// A1 = 1.919067
		 -32526,	// A2 = -0.992615
		 24324,		// B2 = 0.74231
		 -23535,	// B1 = -1.436523
		 24324,		// B0 = 0.74231
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f350_440[]
		30634,		// A1 = 1.869751
		 -31533,	// A2 = -0.962341
		 -680,		// B2 = -0.020782
		 0,		// B1 = 0
		 680,		// B0 = 0.020782
		 30571,		// A1 = 1.865906
		 -32277,	// A2 = -0.985016
		 12894,		// B2 = 0.393524
		 -11945,	// B1 = -0.729065
		 12894,		// B0 = 0.393524
		 31367,		// A1 = 1.91449
		 -32379,	// A2 = -0.988129
		 23820,		// B2 = 0.726929
		 -23104,	// B1 = -1.410217
		 23820,		// B0 = 0.726929
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f350_450[]
		30552,		// A1 = 1.864807
		 -31434,	// A2 = -0.95929
		 -690,		// B2 = -0.021066
		 0,		// B1 = 0
		 690,		// B0 = 0.021066
		 30472,		// A1 = 1.859924
		 -32248,	// A2 = -0.984161
		 13385,		// B2 = 0.408478
		 -12357,	// B1 = -0.754242
		 13385,		// B0 = 0.408478
		 31358,		// A1 = 1.914001
		 -32366,	// A2 = -0.987732
		 26488,		// B2 = 0.80835
		 -25692,	// B1 = -1.568176
		 26490,		// B0 = 0.808411
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 360.txt
		31397,		// A1 = -1.916321
		 -32623,	// A2 = 0.995605
		 -117,		// B2 = -0.003598
		 0,		// B1 = 0.000000
		 117,		// B0 = 0.003598
		 31403,		// A1 = -1.916687
		 -32700,	// A2 = 0.997925
		 3388,		// B2 = 0.103401
		 -3240,		// B1 = -0.197784
		 3388,		// B0 = 0.103401
		 31463,		// A1 = -1.920410
		 -32702,	// A2 = 0.997986
		 13346,		// B2 = 0.407288
		 -12863,	// B1 = -0.785126
		 13346,		// B0 = 0.407288
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f380_420[]
		30831,		// A1 = 1.881775
		 -32064,	// A2 = -0.978546
		 -367,		// B2 = -0.01122
		 0,		// B1 = 0
		 367,		// B0 = 0.01122
		 30813,		// A1 = 1.880737
		 -32456,	// A2 = -0.990509
		 11068,		// B2 = 0.337769
		 -10338,	// B1 = -0.631042
		 11068,		// B0 = 0.337769
		 31214,		// A1 = 1.905212
		 -32491,	// A2 = -0.991577
		 16374,		// B2 = 0.499695
		 -15781,	// B1 = -0.963196
		 16374,		// B0 = 0.499695
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 392.txt
		31152,		// A1 = -1.901428
		 -32613,	// A2 = 0.995300
		 -314,		// B2 = -0.009605
		 0,		// B1 = 0.000000
		 314,		// B0 = 0.009605
		 31156,		// A1 = -1.901672
		 -32694,	// A2 = 0.997742
		 28847,		// B2 = 0.880371
		 -2734,		// B1 = -0.166901
		 28847,		// B0 = 0.880371
		 31225,		// A1 = -1.905823
		 -32696,	// A2 = 0.997803
		 462,		// B2 = 0.014108
		 -442,		// B1 = -0.027019
		 462,		// B0 = 0.014108
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f400_425[]
		30836,		// A1 = 1.882141
		 -32296,	// A2 = -0.985596
		 -324,		// B2 = -0.009903
		 0,		// B1 = 0
		 324,		// B0 = 0.009903
		 30825,		// A1 = 1.881409
		 -32570,	// A2 = -0.993958
		 16847,		// B2 = 0.51416
		 -15792,	// B1 = -0.963898
		 16847,		// B0 = 0.51416
		 31106,		// A1 = 1.89856
		 -32584,	// A2 = -0.994415
		 9579,		// B2 = 0.292328
		 -9164,		// B1 = -0.559357
		 9579,		// B0 = 0.292328
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f400_440[]
		30702,		// A1 = 1.873962
		 -32134,	// A2 = -0.980682
		 -517,		// B2 = -0.015793
		 0,		// B1 = 0
		 517,		// B0 = 0.015793
		 30676,		// A1 = 1.872375
		 -32520,	// A2 = -0.992462
		 8144,		// B2 = 0.24855
		 -7596,		// B1 = -0.463684
		 8144,		// B0 = 0.24855
		 31084,		// A1 = 1.897217
		 -32547,	// A2 = -0.993256
		 22713,		// B2 = 0.693176
		 -21734,	// B1 = -1.326599
		 22713,		// B0 = 0.693176
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f400_450[]
		30613,		// A1 = 1.86853
		 -32031,	// A2 = -0.977509
		 -618,		// B2 = -0.018866
		 0,		// B1 = 0
		 618,		// B0 = 0.018866
		 30577,		// A1 = 1.866272
		 -32491,	// A2 = -0.991577
		 9612,		// B2 = 0.293335
		 -8935,		// B1 = -0.54541
		 9612,		// B0 = 0.293335
		 31071,		// A1 = 1.896484
		 -32524,	// A2 = -0.992584
		 21596,		// B2 = 0.659058
		 -20667,	// B1 = -1.261414
		 21596,		// B0 = 0.659058
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 420.txt
		30914,		// A1 = -1.886841
		 -32584,	// A2 = 0.994385
		 -426,		// B2 = -0.013020
		 0,		// B1 = 0.000000
		 426,		// B0 = 0.013020
		 30914,		// A1 = -1.886841
		 -32679,	// A2 = 0.997314
		 17520,		// B2 = 0.534668
		 -16471,	// B1 = -1.005310
		 17520,		// B0 = 0.534668
		 31004,		// A1 = -1.892334
		 -32683,	// A2 = 0.997406
		 819,		// B2 = 0.025023
		 -780,		// B1 = -0.047619
		 819,		// B0 = 0.025023
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 425.txt
		30881,		// A1 = -1.884827
		 -32603,	// A2 = 0.994965
		 -496,		// B2 = -0.015144
		 0,		// B1 = 0.000000
		 496,		// B0 = 0.015144
		 30880,		// A1 = -1.884766
		 -32692,	// A2 = 0.997711
		 24767,		// B2 = 0.755859
		 -23290,	// B1 = -1.421509
		 24767,		// B0 = 0.755859
		 30967,		// A1 = -1.890076
		 -32694,	// A2 = 0.997772
		 728,		// B2 = 0.022232
		 -691,		// B1 = -0.042194
		 728,		// B0 = 0.022232
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f425_450[]
		30646,		// A1 = 1.870544
		 -32327,	// A2 = -0.986572
		 -287,		// B2 = -0.008769
		 0,		// B1 = 0
		 287,		// B0 = 0.008769
		 30627,		// A1 = 1.869324
		 -32607,	// A2 = -0.995087
		 13269,		// B2 = 0.404968
		 -12376,	// B1 = -0.755432
		 13269,		// B0 = 0.404968
		 30924,		// A1 = 1.887512
		 -32619,	// A2 = -0.995453
		 19950,		// B2 = 0.608826
		 -18940,	// B1 = -1.156006
		 19950,		// B0 = 0.608826
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f425_475[]
		30396,		// A1 = 1.855225
		 -32014,	// A2 = -0.97699
		 -395,		// B2 = -0.012055
		 0,		// B1 = 0
		 395,		// B0 = 0.012055
		 30343,		// A1 = 1.85199
		 -32482,	// A2 = -0.991302
		 17823,		// B2 = 0.543945
		 -16431,	// B1 = -1.002869
		 17823,		// B0 = 0.543945
		 30872,		// A1 = 1.884338
		 -32516,	// A2 = -0.99231
		 18124,		// B2 = 0.553101
		 -17246,	// B1 = -1.052673
		 18124,		// B0 = 0.553101
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 435.txt
		30796,		// A1 = -1.879639
		 -32603,	// A2 = 0.994965
		 -254,		// B2 = -0.007762
		 0,		// B1 = 0.000000
		 254,		// B0 = 0.007762
		 30793,		// A1 = -1.879456
		 -32692,	// A2 = 0.997711
		 18934,		// B2 = 0.577820
		 -17751,	// B1 = -1.083496
		 18934,		// B0 = 0.577820
		 30882,		// A1 = -1.884888
		 -32694,	// A2 = 0.997772
		 1858,		// B2 = 0.056713
		 -1758,		// B1 = -0.107357
		 1858,		// B0 = 0.056713
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f440_450[]
		30641,		// A1 = 1.870239
		 -32458,	// A2 = -0.99057
		 -155,		// B2 = -0.004735
		 0,		// B1 = 0
		 155,		// B0 = 0.004735
		 30631,		// A1 = 1.869568
		 -32630,	// A2 = -0.995789
		 11453,		// B2 = 0.349548
		 -10666,	// B1 = -0.651001
		 11453,		// B0 = 0.349548
		 30810,		// A1 = 1.880554
		 -32634,	// A2 = -0.995941
		 12237,		// B2 = 0.373474
		 -11588,	// B1 = -0.707336
		 12237,		// B0 = 0.373474
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f440_480[]
		30367,		// A1 = 1.853455
		 -32147,	// A2 = -0.981079
		 -495,		// B2 = -0.015113
		 0,		// B1 = 0
		 495,		// B0 = 0.015113
		 30322,		// A1 = 1.850769
		 -32543,	// A2 = -0.993134
		 10031,		// B2 = 0.306152
		 -9252,		// B1 = -0.564728
		 10031,		// B0 = 0.306152
		 30770,		// A1 = 1.878052
		 -32563,	// A2 = -0.993774
		 22674,		// B2 = 0.691956
		 -21465,	// B1 = -1.31012
		 22674,		// B0 = 0.691956
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 445.txt
		30709,		// A1 = -1.874329
		 -32603,	// A2 = 0.994965
		 -83,		// B2 = -0.002545
		 0,		// B1 = 0.000000
		 83,		// B0 = 0.002545
		 30704,		// A1 = -1.874084
		 -32692,	// A2 = 0.997711
		 10641,		// B2 = 0.324738
		 -9947,		// B1 = -0.607147
		 10641,		// B0 = 0.324738
		 30796,		// A1 = -1.879639
		 -32694,	// A2 = 0.997772
		 10079,		// B2 = 0.307587
		 9513,		// B1 = 0.580688
		 10079,		// B0 = 0.307587
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 450.txt
		30664,		// A1 = -1.871643
		 -32603,	// A2 = 0.994965
		 -164,		// B2 = -0.005029
		 0,		// B1 = 0.000000
		 164,		// B0 = 0.005029
		 30661,		// A1 = -1.871399
		 -32692,	// A2 = 0.997711
		 15294,		// B2 = 0.466736
		 -14275,	// B1 = -0.871307
		 15294,		// B0 = 0.466736
		 30751,		// A1 = -1.876953
		 -32694,	// A2 = 0.997772
		 3548,		// B2 = 0.108284
		 -3344,		// B1 = -0.204155
		 3548,		// B0 = 0.108284
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 452.txt
		30653,		// A1 = -1.870911
		 -32615,	// A2 = 0.995361
		 -209,		// B2 = -0.006382
		 0,		// B1 = 0.000000
		 209,		// B0 = 0.006382
		 30647,		// A1 = -1.870605
		 -32702,	// A2 = 0.997986
		 18971,		// B2 = 0.578979
		 -17716,	// B1 = -1.081299
		 18971,		// B0 = 0.578979
		 30738,		// A1 = -1.876099
		 -32702,	// A2 = 0.998016
		 2967,		// B2 = 0.090561
		 -2793,		// B1 = -0.170502
		 2967,		// B0 = 0.090561
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 475.txt
		30437,		// A1 = -1.857727
		 -32603,	// A2 = 0.994965
		 -264,		// B2 = -0.008062
		 0,		// B1 = 0.000000
		 264,		// B0 = 0.008062
		 30430,		// A1 = -1.857300
		 -32692,	// A2 = 0.997711
		 21681,		// B2 = 0.661682
		 -20082,	// B1 = -1.225708
		 21681,		// B0 = 0.661682
		 30526,		// A1 = -1.863220
		 -32694,	// A2 = 0.997742
		 1559,		// B2 = 0.047600
		 -1459,		// B1 = -0.089096
		 1559,		// B0 = 0.047600
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f480_620[]
		28975,		// A1 = 1.768494
		 -30955,	// A2 = -0.944672
		 -1026,		// B2 = -0.03133
		 0,		// B1 = 0
		 1026,		// B0 = 0.03133
		 28613,		// A1 = 1.746399
		 -32089,	// A2 = -0.979309
		 14214,		// B2 = 0.433807
		 -12202,	// B1 = -0.744812
		 14214,		// B0 = 0.433807
		 30243,		// A1 = 1.845947
		 -32238,	// A2 = -0.983856
		 24825,		// B2 = 0.757629
		 -23402,	// B1 = -1.428345
		 24825,		// B0 = 0.757629
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 494.txt
		30257,		// A1 = -1.846741
		 -32605,	// A2 = 0.995056
		 -249,		// B2 = -0.007625
		 0,		// B1 = 0.000000
		 249,		// B0 = 0.007625
		 30247,		// A1 = -1.846191
		 -32694,	// A2 = 0.997772
		 18088,		// B2 = 0.552002
		 -16652,	// B1 = -1.016418
		 18088,		// B0 = 0.552002
		 30348,		// A1 = -1.852295
		 -32696,	// A2 = 0.997803
		 2099,		// B2 = 0.064064
		 -1953,		// B1 = -0.119202
		 2099,		// B0 = 0.064064
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 500.txt
		30202,		// A1 = -1.843431
		 -32624,	// A2 = 0.995622
		 -413,		// B2 = -0.012622
		 0,		// B1 = 0.000000
		 413,		// B0 = 0.012622
		 30191,		// A1 = -1.842721
		 -32714,	// A2 = 0.998364
		 25954,		// B2 = 0.792057
		 -23890,	// B1 = -1.458131
		 25954,		// B0 = 0.792057
		 30296,		// A1 = -1.849172
		 -32715,	// A2 = 0.998397
		 2007,		// B2 = 0.061264
		 -1860,		// B1 = -0.113568
		 2007,		// B0 = 0.061264
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 520.txt
		30001,		// A1 = -1.831116
		 -32613,	// A2 = 0.995270
		 -155,		// B2 = -0.004750
		 0,		// B1 = 0.000000
		 155,		// B0 = 0.004750
		 29985,		// A1 = -1.830200
		 -32710,	// A2 = 0.998260
		 6584,		// B2 = 0.200928
		 -6018,		// B1 = -0.367355
		 6584,		// B0 = 0.200928
		 30105,		// A1 = -1.837524
		 -32712,	// A2 = 0.998291
		 23812,		// B2 = 0.726685
		 -21936,	// B1 = -1.338928
		 23812,		// B0 = 0.726685
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 523.txt
		29964,		// A1 = -1.828918
		 -32601,	// A2 = 0.994904
		 -101,		// B2 = -0.003110
		 0,		// B1 = 0.000000
		 101,		// B0 = 0.003110
		 29949,		// A1 = -1.827942
		 -32700,	// A2 = 0.997925
		 11041,		// B2 = 0.336975
		 -10075,	// B1 = -0.614960
		 11041,		// B0 = 0.336975
		 30070,		// A1 = -1.835388
		 -32702,	// A2 = 0.997986
		 16762,		// B2 = 0.511536
		 -15437,	// B1 = -0.942230
		 16762,		// B0 = 0.511536
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 525.txt
		29936,		// A1 = -1.827209
		 -32584,	// A2 = 0.994415
		 -91,		// B2 = -0.002806
		 0,		// B1 = 0.000000
		 91,		// B0 = 0.002806
		 29921,		// A1 = -1.826233
		 -32688,	// A2 = 0.997559
		 11449,		// B2 = 0.349396
		 -10426,	// B1 = -0.636383
		 11449,		// B0 = 0.349396
		 30045,		// A1 = -1.833862
		 -32688,	// A2 = 0.997589
		 13055,		// B2 = 0.398407
		 -12028,	// B1 = -0.734161
		 13055,		// B0 = 0.398407
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f540_660[]
		28499,		// A1 = 1.739441
		 -31129,	// A2 = -0.949982
		 -849,		// B2 = -0.025922
		 0,		// B1 = 0
		 849,		// B0 = 0.025922
		 28128,		// A1 = 1.716797
		 -32130,	// A2 = -0.98056
		 14556,		// B2 = 0.444214
		 -12251,	// B1 = -0.747772
		 14556,		// B0 = 0.444244
		 29667,		// A1 = 1.81073
		 -32244,	// A2 = -0.984039
		 23038,		// B2 = 0.703064
		 -21358,	// B1 = -1.303589
		 23040,		// B0 = 0.703125
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 587.txt
		29271,		// A1 = -1.786560
		 -32599,	// A2 = 0.994873
		 -490,		// B2 = -0.014957
		 0,		// B1 = 0.000000
		 490,		// B0 = 0.014957
		 29246,		// A1 = -1.785095
		 -32700,	// A2 = 0.997925
		 28961,		// B2 = 0.883850
		 -25796,	// B1 = -1.574463
		 28961,		// B0 = 0.883850
		 29383,		// A1 = -1.793396
		 -32700,	// A2 = 0.997955
		 1299,		// B2 = 0.039650
		 -1169,		// B1 = -0.071396
		 1299,		// B0 = 0.039650
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 590.txt
		29230,		// A1 = -1.784058
		 -32584,	// A2 = 0.994415
		 -418,		// B2 = -0.012757
		 0,		// B1 = 0.000000
		 418,		// B0 = 0.012757
		 29206,		// A1 = -1.782593
		 -32688,	// A2 = 0.997559
		 36556,		// B2 = 1.115601
		 -32478,	// B1 = -1.982300
		 36556,		// B0 = 1.115601
		 29345,		// A1 = -1.791077
		 -32688,	// A2 = 0.997589
		 897,		// B2 = 0.027397
		 -808,		// B1 = -0.049334
		 897,		// B0 = 0.027397
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 600.txt
		29116,		// A1 = -1.777100
		 -32603,	// A2 = 0.994965
		 -165,		// B2 = -0.005039
		 0,		// B1 = 0.000000
		 165,		// B0 = 0.005039
		 29089,		// A1 = -1.775452
		 -32708,	// A2 = 0.998199
		 6963,		// B2 = 0.212494
		 -6172,		// B1 = -0.376770
		 6963,		// B0 = 0.212494
		 29237,		// A1 = -1.784485
		 -32710,	// A2 = 0.998230
		 24197,		// B2 = 0.738464
		 -21657,	// B1 = -1.321899
		 24197,		// B0 = 0.738464
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 660.txt
		28376,		// A1 = -1.731934
		 -32567,	// A2 = 0.993896
		 -363,		// B2 = -0.011102
		 0,		// B1 = 0.000000
		 363,		// B0 = 0.011102
		 28337,		// A1 = -1.729614
		 -32683,	// A2 = 0.997434
		 21766,		// B2 = 0.664246
		 -18761,	// B1 = -1.145081
		 21766,		// B0 = 0.664246
		 28513,		// A1 = -1.740356
		 -32686,	// A2 = 0.997498
		 2509,		// B2 = 0.076584
		 -2196,		// B1 = -0.134041
		 2509,		// B0 = 0.076584
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 700.txt
		27844,		// A1 = -1.699463
		 -32563,	// A2 = 0.993744
		 -366,		// B2 = -0.011187
		 0,		// B1 = 0.000000
		 366,		// B0 = 0.011187
		 27797,		// A1 = -1.696655
		 -32686,	// A2 = 0.997498
		 22748,		// B2 = 0.694214
		 -19235,	// B1 = -1.174072
		 22748,		// B0 = 0.694214
		 27995,		// A1 = -1.708740
		 -32688,	// A2 = 0.997559
		 2964,		// B2 = 0.090477
		 -2546,		// B1 = -0.155449
		 2964,		// B0 = 0.090477
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 740.txt
		27297,		// A1 = -1.666077
		 -32551,	// A2 = 0.993408
		 -345,		// B2 = -0.010540
		 0,		// B1 = 0.000000
		 345,		// B0 = 0.010540
		 27240,		// A1 = -1.662598
		 -32683,	// A2 = 0.997406
		 22560,		// B2 = 0.688477
		 -18688,	// B1 = -1.140625
		 22560,		// B0 = 0.688477
		 27461,		// A1 = -1.676147
		 -32684,	// A2 = 0.997467
		 3541,		// B2 = 0.108086
		 -2985,		// B1 = -0.182220
		 3541,		// B0 = 0.108086
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 750.txt
		27155,		// A1 = -1.657410
		 -32551,	// A2 = 0.993408
		 -462,		// B2 = -0.014117
		 0,		// B1 = 0.000000
		 462,		// B0 = 0.014117
		 27097,		// A1 = -1.653870
		 -32683,	// A2 = 0.997406
		 32495,		// B2 = 0.991699
		 -26776,	// B1 = -1.634338
		 32495,		// B0 = 0.991699
		 27321,		// A1 = -1.667542
		 -32684,	// A2 = 0.997467
		 1835,		// B2 = 0.056007
		 -1539,		// B1 = -0.093948
		 1835,		// B0 = 0.056007
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f750_1450[]
		19298,		// A1 = 1.177917
		 -24471,	// A2 = -0.746796
		 -4152,		// B2 = -0.126709
		 0,		// B1 = 0
		 4152,		// B0 = 0.126709
		 12902,		// A1 = 0.787476
		 -29091,	// A2 = -0.887817
		 12491,		// B2 = 0.38121
		 -1794,		// B1 = -0.109528
		 12494,		// B0 = 0.381317
		 26291,		// A1 = 1.604736
		 -30470,	// A2 = -0.929901
		 28859,		// B2 = 0.880737
		 -26084,	// B1 = -1.592102
		 28861,		// B0 = 0.880798
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 770.txt
		26867,		// A1 = -1.639832
		 -32551,	// A2 = 0.993408
		 -123,		// B2 = -0.003755
		 0,		// B1 = 0.000000
		 123,		// B0 = 0.003755
		 26805,		// A1 = -1.636108
		 -32683,	// A2 = 0.997406
		 17297,		// B2 = 0.527863
		 -14096,	// B1 = -0.860382
		 17297,		// B0 = 0.527863
		 27034,		// A1 = -1.650085
		 -32684,	// A2 = 0.997467
		 12958,		// B2 = 0.395477
		 -10756,	// B1 = -0.656525
		 12958,		// B0 = 0.395477
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 800.txt
		26413,		// A1 = -1.612122
		 -32547,	// A2 = 0.993286
		 -223,		// B2 = -0.006825
		 0,		// B1 = 0.000000
		 223,		// B0 = 0.006825
		 26342,		// A1 = -1.607849
		 -32686,	// A2 = 0.997498
		 6391,		// B2 = 0.195053
		 -5120,		// B1 = -0.312531
		 6391,		// B0 = 0.195053
		 26593,		// A1 = -1.623108
		 -32688,	// A2 = 0.997559
		 23681,		// B2 = 0.722717
		 -19328,	// B1 = -1.179688
		 23681,		// B0 = 0.722717
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 816.txt
		26168,		// A1 = -1.597209
		 -32528,	// A2 = 0.992706
		 -235,		// B2 = -0.007182
		 0,		// B1 = 0.000000
		 235,		// B0 = 0.007182
		 26092,		// A1 = -1.592590
		 -32675,	// A2 = 0.997192
		 20823,		// B2 = 0.635498
		 -16510,	// B1 = -1.007751
		 20823,		// B0 = 0.635498
		 26363,		// A1 = -1.609070
		 -32677,	// A2 = 0.997253
		 6739,		// B2 = 0.205688
		 -5459,		// B1 = -0.333206
		 6739,		// B0 = 0.205688
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 850.txt
		25641,		// A1 = -1.565063
		 -32536,	// A2 = 0.992950
		 -121,		// B2 = -0.003707
		 0,		// B1 = 0.000000
		 121,		// B0 = 0.003707
		 25560,		// A1 = -1.560059
		 -32684,	// A2 = 0.997437
		 18341,		// B2 = 0.559753
		 -14252,	// B1 = -0.869904
		 18341,		// B0 = 0.559753
		 25837,		// A1 = -1.577026
		 -32684,	// A2 = 0.997467
		 16679,		// B2 = 0.509003
		 -13232,	// B1 = -0.807648
		 16679,		// B0 = 0.509003
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f857_1645[]
		16415,		// A1 = 1.001953
		 -23669,	// A2 = -0.722321
		 -4549,		// B2 = -0.138847
		 0,		// B1 = 0
		 4549,		// B0 = 0.138847
		 8456,		// A1 = 0.516174
		 -28996,	// A2 = -0.884918
		 13753,		// B2 = 0.419724
		 -12,		// B1 = -0.000763
		 13757,		// B0 = 0.419846
		 24632,		// A1 = 1.503418
		 -30271,	// A2 = -0.923828
		 29070,		// B2 = 0.887146
		 -25265,	// B1 = -1.542114
		 29073,		// B0 = 0.887268
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 900.txt
		24806,		// A1 = -1.514099
		 -32501,	// A2 = 0.991852
		 -326,		// B2 = -0.009969
		 0,		// B1 = 0.000000
		 326,		// B0 = 0.009969
		 24709,		// A1 = -1.508118
		 -32659,	// A2 = 0.996674
		 20277,		// B2 = 0.618835
		 -15182,	// B1 = -0.926636
		 20277,		// B0 = 0.618835
		 25022,		// A1 = -1.527222
		 -32661,	// A2 = 0.996735
		 4320,		// B2 = 0.131836
		 -3331,		// B1 = -0.203339
		 4320,		// B0 = 0.131836
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f900_1300[]
		19776,		// A1 = 1.207092
		 -27437,	// A2 = -0.837341
		 -2666,		// B2 = -0.081371
		 0,		// B1 = 0
		 2666,		// B0 = 0.081371
		 16302,		// A1 = 0.995026
		 -30354,	// A2 = -0.926361
		 10389,		// B2 = 0.317062
		 -3327,		// B1 = -0.203064
		 10389,		// B0 = 0.317062
		 24299,		// A1 = 1.483154
		 -30930,	// A2 = -0.943909
		 25016,		// B2 = 0.763428
		 -21171,	// B1 = -1.292236
		 25016,		// B0 = 0.763428
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f935_1215[]
		20554,		// A1 = 1.254517
		 -28764,	// A2 = -0.877838
		 -2048,		// B2 = -0.062515
		 0,		// B1 = 0
		 2048,		// B0 = 0.062515
		 18209,		// A1 = 1.11145
		 -30951,	// A2 = -0.94458
		 9390,		// B2 = 0.286575
		 -3955,		// B1 = -0.241455
		 9390,		// B0 = 0.286575
		 23902,		// A1 = 1.458923
		 -31286,	// A2 = -0.954803
		 23252,		// B2 = 0.709595
		 -19132,	// B1 = -1.167725
		 23252,		// B0 = 0.709595
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f941_1477[]
		17543,		// A1 = 1.07074
		 -26220,	// A2 = -0.800201
		 -3298,		// B2 = -0.100647
		 0,		// B1 = 0
		 3298,		// B0 = 0.100647
		 12423,		// A1 = 0.75827
		 -30036,	// A2 = -0.916626
		 12651,		// B2 = 0.386078
		 -2444,		// B1 = -0.14917
		 12653,		// B0 = 0.386154
		 23518,		// A1 = 1.435425
		 -30745,	// A2 = -0.938293
		 27282,		// B2 = 0.832581
		 -22529,	// B1 = -1.375122
		 27286,		// B0 = 0.832703
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 942.txt
		24104,		// A1 = -1.471252
		 -32507,	// A2 = 0.992065
		 -351,		// B2 = -0.010722
		 0,		// B1 = 0.000000
		 351,		// B0 = 0.010722
		 23996,		// A1 = -1.464600
		 -32671,	// A2 = 0.997040
		 22848,		// B2 = 0.697266
		 -16639,	// B1 = -1.015564
		 22848,		// B0 = 0.697266
		 24332,		// A1 = -1.485168
		 -32673,	// A2 = 0.997101
		 4906,		// B2 = 0.149727
		 -3672,		// B1 = -0.224174
		 4906,		// B0 = 0.149727
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 950.txt
		23967,		// A1 = -1.462830
		 -32507,	// A2 = 0.992065
		 -518,		// B2 = -0.015821
		 0,		// B1 = 0.000000
		 518,		// B0 = 0.015821
		 23856,		// A1 = -1.456055
		 -32671,	// A2 = 0.997040
		 26287,		// B2 = 0.802246
		 -19031,	// B1 = -1.161560
		 26287,		// B0 = 0.802246
		 24195,		// A1 = -1.476746
		 -32673,	// A2 = 0.997101
		 2890,		// B2 = 0.088196
		 -2151,		// B1 = -0.131317
		 2890,		// B0 = 0.088196
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f950_1400[]
		18294,		// A1 = 1.116638
		 -26962,	// A2 = -0.822845
		 -2914,		// B2 = -0.088936
		 0,		// B1 = 0
		 2914,		// B0 = 0.088936
		 14119,		// A1 = 0.861786
		 -30227,	// A2 = -0.922455
		 11466,		// B2 = 0.349945
		 -2833,		// B1 = -0.172943
		 11466,		// B0 = 0.349945
		 23431,		// A1 = 1.430115
		 -30828,	// A2 = -0.940796
		 25331,		// B2 = 0.773071
		 -20911,	// B1 = -1.276367
		 25331,		// B0 = 0.773071
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 975.txt
		23521,		// A1 = -1.435608
		 -32489,	// A2 = 0.991516
		 -193,		// B2 = -0.005915
		 0,		// B1 = 0.000000
		 193,		// B0 = 0.005915
		 23404,		// A1 = -1.428467
		 -32655,	// A2 = 0.996582
		 17740,		// B2 = 0.541412
		 -12567,	// B1 = -0.767029
		 17740,		// B0 = 0.541412
		 23753,		// A1 = -1.449829
		 -32657,	// A2 = 0.996613
		 9090,		// B2 = 0.277405
		 -6662,		// B1 = -0.406647
		 9090,		// B0 = 0.277405
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1000.txt
		23071,		// A1 = -1.408203
		 -32489,	// A2 = 0.991516
		 -293,		// B2 = -0.008965
		 0,		// B1 = 0.000000
		 293,		// B0 = 0.008965
		 22951,		// A1 = -1.400818
		 -32655,	// A2 = 0.996582
		 5689,		// B2 = 0.173645
		 -3951,		// B1 = -0.241150
		 5689,		// B0 = 0.173645
		 23307,		// A1 = -1.422607
		 -32657,	// A2 = 0.996613
		 18692,		// B2 = 0.570435
		 -13447,	// B1 = -0.820770
		 18692,		// B0 = 0.570435
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1020.txt
		22701,		// A1 = -1.385620
		 -32474,	// A2 = 0.991058
		 -292,		// B2 = -0.008933
		 0,		//163840      , // B1 = 10.000000
		 292,		// B0 = 0.008933
		 22564,		// A1 = -1.377258
		 -32655,	// A2 = 0.996552
		 20756,		// B2 = 0.633423
		 -14176,	// B1 = -0.865295
		 20756,		// B0 = 0.633423
		 22960,		// A1 = -1.401428
		 -32657,	// A2 = 0.996613
		 6520,		// B2 = 0.198990
		 -4619,		// B1 = -0.281937
		 6520,		// B0 = 0.198990
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1050.txt
		22142,		// A1 = -1.351501
		 -32474,	// A2 = 0.991058
		 -147,		// B2 = -0.004493
		 0,		// B1 = 0.000000
		 147,		// B0 = 0.004493
		 22000,		// A1 = -1.342834
		 -32655,	// A2 = 0.996552
		 15379,		// B2 = 0.469360
		 -10237,	// B1 = -0.624847
		 15379,		// B0 = 0.469360
		 22406,		// A1 = -1.367554
		 -32657,	// A2 = 0.996613
		 17491,		// B2 = 0.533783
		 -12096,	// B1 = -0.738312
		 17491,		// B0 = 0.533783
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f1100_1750[]
		12973,		// A1 = 0.79184
		 -24916,	// A2 = -0.760376
		 6655,		// B2 = 0.203102
		 367,		// B1 = 0.0224
		 6657,		// B0 = 0.203171
		 5915,		// A1 = 0.361053
		 -29560,	// A2 = -0.90213
		 -7777,		// B2 = -0.23735
		 0,		// B1 = 0
		 7777,		// B0 = 0.23735
		 20510,		// A1 = 1.251892
		 -30260,	// A2 = -0.923462
		 26662,		// B2 = 0.81366
		 -20573,	// B1 = -1.255737
		 26668,		// B0 = 0.813843
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1140.txt
		20392,		// A1 = -1.244629
		 -32460,	// A2 = 0.990601
		 -270,		// B2 = -0.008240
		 0,		// B1 = 0.000000
		 270,		// B0 = 0.008240
		 20218,		// A1 = -1.234009
		 -32655,	// A2 = 0.996582
		 21337,		// B2 = 0.651154
		 -13044,	// B1 = -0.796143
		 21337,		// B0 = 0.651154
		 20684,		// A1 = -1.262512
		 -32657,	// A2 = 0.996643
		 8572,		// B2 = 0.261612
		 -5476,		// B1 = -0.334244
		 8572,		// B0 = 0.261612
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1200.txt
		19159,		// A1 = -1.169373
		 -32456,	// A2 = 0.990509
		 -335,		// B2 = -0.010252
		 0,		// B1 = 0.000000
		 335,		// B0 = 0.010252
		 18966,		// A1 = -1.157593
		 -32661,	// A2 = 0.996735
		 6802,		// B2 = 0.207588
		 -3900,		// B1 = -0.238098
		 6802,		// B0 = 0.207588
		 19467,		// A1 = -1.188232
		 -32661,	// A2 = 0.996765
		 25035,		// B2 = 0.764008
		 -15049,	// B1 = -0.918579
		 25035,		// B0 = 0.764008
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1209.txt
		18976,		// A1 = -1.158264
		 -32439,	// A2 = 0.989990
		 -183,		// B2 = -0.005588
		 0,		// B1 = 0.000000
		 183,		// B0 = 0.005588
		 18774,		// A1 = -1.145874
		 -32650,	// A2 = 0.996429
		 15468,		// B2 = 0.472076
		 -8768,		// B1 = -0.535217
		 15468,		// B0 = 0.472076
		 19300,		// A1 = -1.177979
		 -32652,	// A2 = 0.996490
		 19840,		// B2 = 0.605499
		 -11842,	// B1 = -0.722809
		 19840,		// B0 = 0.605499
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1330.txt
		16357,		// A1 = -0.998413
		 -32368,	// A2 = 0.987793
		 -217,		// B2 = -0.006652
		 0,		// B1 = 0.000000
		 217,		// B0 = 0.006652
		 16107,		// A1 = -0.983126
		 -32601,	// A2 = 0.994904
		 11602,		// B2 = 0.354065
		 -5555,		// B1 = -0.339111
		 11602,		// B0 = 0.354065
		 16722,		// A1 = -1.020630
		 -32603,	// A2 = 0.994965
		 15574,		// B2 = 0.475311
		 -8176,		// B1 = -0.499069
		 15574,		// B0 = 0.475311
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1336.txt
		16234,		// A1 = -0.990875
		 32404,		// A2 = -0.988922
		 -193,		// B2 = -0.005908
		 0,		// B1 = 0.000000
		 193,		// B0 = 0.005908
		 15986,		// A1 = -0.975769
		 -32632,	// A2 = 0.995880
		 18051,		// B2 = 0.550903
		 -8658,		// B1 = -0.528473
		 18051,		// B0 = 0.550903
		 16591,		// A1 = -1.012695
		 -32634,	// A2 = 0.995941
		 15736,		// B2 = 0.480240
		 -8125,		// B1 = -0.495926
		 15736,		// B0 = 0.480240
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1366.txt
		15564,		// A1 = -0.949982
		 -32404,	// A2 = 0.988922
		 -269,		// B2 = -0.008216
		 0,		// B1 = 0.000000
		 269,		// B0 = 0.008216
		 15310,		// A1 = -0.934479
		 -32632,	// A2 = 0.995880
		 10815,		// B2 = 0.330063
		 -4962,		// B1 = -0.302887
		 10815,		// B0 = 0.330063
		 15924,		// A1 = -0.971924
		 -32634,	// A2 = 0.995941
		 18880,		// B2 = 0.576172
		 -9364,		// B1 = -0.571594
		 18880,		// B0 = 0.576172
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1380.txt
		15247,		// A1 = -0.930603
		 -32397,	// A2 = 0.988708
		 -244,		// B2 = -0.007451
		 0,		// B1 = 0.000000
		 244,		// B0 = 0.007451
		 14989,		// A1 = -0.914886
		 -32627,	// A2 = 0.995697
		 18961,		// B2 = 0.578644
		 -8498,		// B1 = -0.518707
		 18961,		// B0 = 0.578644
		 15608,		// A1 = -0.952667
		 -32628,	// A2 = 0.995758
		 11145,		// B2 = 0.340134
		 -5430,		// B1 = -0.331467
		 11145,		// B0 = 0.340134
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1400.txt
		14780,		// A1 = -0.902130
		 -32393,	// A2 = 0.988586
		 -396,		// B2 = -0.012086
		 0,		// B1 = 0.000000
		 396,		// B0 = 0.012086
		 14510,		// A1 = -0.885651
		 -32630,	// A2 = 0.995819
		 6326,		// B2 = 0.193069
		 -2747,		// B1 = -0.167671
		 6326,		// B0 = 0.193069
		 15154,		// A1 = -0.924957
		 -32632,	// A2 = 0.995850
		 23235,		// B2 = 0.709076
		 -10983,	// B1 = -0.670380
		 23235,		// B0 = 0.709076
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1477.txt
		13005,		// A1 = -0.793793
		 -32368,	// A2 = 0.987823
		 -500,		// B2 = -0.015265
		 0,		// B1 = 0.000000
		 500,		// B0 = 0.015265
		 12708,		// A1 = -0.775665
		 -32615,	// A2 = 0.995331
		 11420,		// B2 = 0.348526
		 -4306,		// B1 = -0.262833
		 11420,		// B0 = 0.348526
		 13397,		// A1 = -0.817688
		 -32615,	// A2 = 0.995361
		 9454,		// B2 = 0.288528
		 -3981,		// B1 = -0.243027
		 9454,		// B0 = 0.288528
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1600.txt
		10046,		// A1 = -0.613190
		 -32331,	// A2 = 0.986694
		 -455,		// B2 = -0.013915
		 0,		// B1 = 0.000000
		 455,		// B0 = 0.013915
		 9694,		// A1 = -0.591705
		 -32601,	// A2 = 0.994934
		 6023,		// B2 = 0.183815
		 -1708,		// B1 = -0.104279
		 6023,		// B0 = 0.183815
		 10478,		// A1 = -0.639587
		 -32603,	// A2 = 0.994965
		 22031,		// B2 = 0.672333
		 -7342,		// B1 = -0.448151
		 22031,		// B0 = 0.672333
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// f1633_1638[]
		9181,		// A1 = 0.560394
		 -32256,	// A2 = -0.984375
		 -556,		// B2 = -0.016975
		 0,		// B1 = 0
		 556,		// B0 = 0.016975
		 8757,		// A1 = 0.534515
		 -32574,	// A2 = -0.99408
		 8443,		// B2 = 0.25769
		 -2135,		// B1 = -0.130341
		 8443,		// B0 = 0.25769
		 9691,		// A1 = 0.591522
		 -32574,	// A2 = -0.99411
		 15446,		// B2 = 0.471375
		 -4809,		// B1 = -0.293579
		 15446,		// B0 = 0.471375
		 7,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1800.txt
		5076,		// A1 = -0.309875
		 -32304,	// A2 = 0.985840
		 -508,		// B2 = -0.015503
		 0,		// B1 = 0.000000
		 508,		// B0 = 0.015503
		 4646,		// A1 = -0.283600
		 -32605,	// A2 = 0.995026
		 6742,		// B2 = 0.205780
		 -878,		// B1 = -0.053635
		 6742,		// B0 = 0.205780
		 5552,		// A1 = -0.338928
		 -32605,	// A2 = 0.995056
		 23667,		// B2 = 0.722260
		 -4297,		// B1 = -0.262329
		 23667,		// B0 = 0.722260
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},
	{			// 1860.txt
		3569,		// A1 = -0.217865
		 -32292,	// A2 = 0.985504
		 -239,		// B2 = -0.007322
		 0,		// B1 = 0.000000
		 239,		// B0 = 0.007322
		 3117,		// A1 = -0.190277
		 -32603,	// A2 = 0.994965
		 18658,		// B2 = 0.569427
		 -1557,		// B1 = -0.095032
		 18658,		// B0 = 0.569427
		 4054,		// A1 = -0.247437
		 -32603,	// A2 = 0.994965
		 18886,		// B2 = 0.576385
		 -2566,		// B1 = -0.156647
		 18886,		// B0 = 0.576385
		 5,		// Internal filter scaling
		 159,		// Minimum in-band energy threshold
		 21,		// 21/32 in-band to broad-band ratio
		 0x0FF5		// shift-mask 0x0FF (look at 16 half-frames) bit count = 5
	},};

static int ixj_init_filter(int board, IXJ_FILTER * jf)
{
	unsigned short cmd;
	int cnt, max;
	IXJ *j = &ixj[board];

	if (jf->filter > 3) {
		return -1;
	}
	if (ixj_WriteDSPCommand(0x5154 + jf->filter, board))	// Select Filter

		return -1;

	if (!jf->enable) {
		if (ixj_WriteDSPCommand(0x5152, board))		// Disable Filter

			return -1;
		else
			return 0;
	} else {
		if (ixj_WriteDSPCommand(0x5153, board))		// Enable Filter

			return -1;

		// Select the filter (f0 - f3) to use.
		if (ixj_WriteDSPCommand(0x5154 + jf->filter, board))
			return -1;
	}
	if (jf->freq < 12 && jf->freq > 3) {
		// Select the frequency for the selected filter.
		if (ixj_WriteDSPCommand(0x5170 + jf->freq, board))
			return -1;
	} else if (jf->freq > 11) {
		// We need to load a programmable filter set for undefined
		// frequencies.  So we will point the filter to a programmable set.
		// Since there are only 4 filters and 4 programmable sets, we will
		// just point the filter to the same number set and program it for the
		// frequency we want.
		if (ixj_WriteDSPCommand(0x5170 + jf->filter, board))
			return -1;

		if (j->ver.low != 0x12) {
			cmd = 0x515B;
			max = 19;
		} else {
			cmd = 0x515E;
			max = 15;
		}
		if (ixj_WriteDSPCommand(cmd, board))
			return -1;

		for (cnt = 0; cnt < max; cnt++) {
			if (ixj_WriteDSPCommand(tone_table[jf->freq][cnt], board))
				return -1;
		}
/*    if(j->ver.low != 0x12)
   {
   if(ixj_WriteDSPCommand(7, board))
   return -1;
   if(ixj_WriteDSPCommand(159, board))
   return -1;
   if(ixj_WriteDSPCommand(21, board))
   return -1;
   if(ixj_WriteDSPCommand(0x0FF5, board))
   return -1;
   } */
	}
	return 0;
}

static int ixj_init_tone(int board, IXJ_TONE * ti)
{
	int freq0, freq1;
	unsigned short data;

	if (ti->freq0) {
		freq0 = ti->freq0;
	} else {
		freq0 = 0x7FFF;
	}

	if (ti->freq1) {
		freq1 = ti->freq1;
	} else {
		freq1 = 0x7FFF;
	}

//  if(ti->tone_index > 12 && ti->tone_index < 28)
	{
		if (ixj_WriteDSPCommand(0x6800 + ti->tone_index, board))
			return -1;

		if (ixj_WriteDSPCommand(0x6000 + (ti->gain0 << 4) + ti->gain1, board))
			return -1;

		data = freq0;
		if (ixj_WriteDSPCommand(data, board))
			return -1;

		data = freq1;
		if (ixj_WriteDSPCommand(data, board))
			return -1;
	}
	return freq0;
}
