/*
 * sound/wf_midi.c
 *
 * The low level driver for the WaveFront ICS2115 MIDI interface(s)
 * Note that there is also an MPU-401 emulation (actually, a UART-401
 * emulation) on the CS4232 on the Tropez Plus. This code has nothing
 * to do with that interface at all.
 *
 * The interface is essentially just a UART-401, but is has the
 * interesting property of supporting what Turtle Beach called
 * "Virtual MIDI" mode. In this mode, there are effectively *two*
 * MIDI buses accessible via the interface, one that is routed
 * solely to/from the external WaveFront synthesizer and the other
 * corresponding to the pin/socket connector used to link external
 * MIDI devices to the board.
 *
 * This driver fully supports this mode, allowing two distinct
 * midi devices (/dev/midiNN and /dev/midiNN+1) to be used
 * completely independently, giving 32 channels of MIDI routing,
 * 16 to the WaveFront synth and 16 to the external MIDI bus.
 *
 * Switching between the two is accomplished externally by the driver
 * using the two otherwise unused MIDI bytes. See the code for more details.
 *
 * NOTE: VIRTUAL MIDI MODE IS ON BY DEFAULT (see wavefront.c)
 *
 * The main reason to turn off Virtual MIDI mode is when you want to
 * tightly couple the WaveFront synth with an external MIDI
 * device. You won't be able to distinguish the source of any MIDI
 * data except via SysEx ID, but thats probably OK, since for the most
 * part, the WaveFront won't be sending any MIDI data at all.
 *  
 * The main reason to turn on Virtual MIDI Mode is to provide two
 * completely independent 16-channel MIDI buses, one to the
 * WaveFront and one to any external MIDI devices. Given the 32
 * voice nature of the WaveFront, its pretty easy to find a use
 * for all 16 channels driving just that synth.
 *
 */

/*
 * Copyright (C) by Paul Barton-Davis 1998
 * Substantial portions of this file are derived from work that is:
 *
 *    Copyright (C) by Hannu Savolainen 1993-1996
 *
 * USS/Lite for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */

#include <linux/config.h>
#include "sound_config.h"
#include "soundmodule.h"

#include <linux/wavefront.h>

#if (defined(CONFIG_WAVEFRONT) && defined(CONFIG_MIDI)) || defined(MODULE)

struct wf_mpu_config {
	int             base;	/* I/O base */
	int             irq;
	int             opened;	/* Open mode */
	int             devno;
	int             synthno;
	int             mode;
#define MODE_MIDI	1
#define MODE_SYNTH	2

	void            (*inputintr) (int dev, unsigned char data);
    
	/* Virtual MIDI support */
    
	char configured_for_virtual;   /* setup for virtual completed */
	char isvirtual;                /* do virtual I/O stuff */
	char isexternal;               /* i am an external interface */
	int internal;                  /* external interface midi_devno */
	int external;                  /* external interface midi_devno */
};

#define	DATAPORT(base)   (base)
#define	COMDPORT(base)   (base+1)
#define	STATPORT(base)   (base+1)

static void     start_uart_mode (struct wf_mpu_config *devc);

static int 
wf_mpu_status (struct wf_mpu_config *devc)
{
	return inb (STATPORT (devc->base));
}

static void 
wf_mpu_cmd (struct wf_mpu_config *devc, unsigned char cmd)
{
	outb ((cmd), COMDPORT(devc->base));
}

#define input_avail(devc)		(!(wf_mpu_status(devc)&INPUT_AVAIL))
#define output_ready(devc)		(!(wf_mpu_status(devc)&OUTPUT_READY))

static int 
read_data (struct wf_mpu_config *devc)
{
	return inb (DATAPORT (devc->base));
}

static void 
write_data (struct wf_mpu_config *devc, unsigned char byte)
{
	outb (byte, DATAPORT (devc->base));
}

#define	OUTPUT_READY	0x40
#define	INPUT_AVAIL	0x80
#define	MPU_ACK		0xFE
#define	MPU_RESET	0xFF
#define	UART_MODE_ON	0x3F

static struct wf_mpu_config dev_conf[MAX_MIDI_DEV] =
{
	{0}
};

static volatile int irq2dev[17] =
{-1, -1, -1, -1, -1, -1, -1, -1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1};

static struct synth_info wf_mpu_synth_info_proto =
{"WaveFront MPU-401 interface", 0,
 SYNTH_TYPE_MIDI, MIDI_TYPE_MPU401, 0, 128, 0, 128, SYNTH_CAP_INPUT};

static struct synth_info wf_mpu_synth_info[MAX_MIDI_DEV];

/*
 * States for the input scanner (should be in dev_table.h)
 */

#define MST_SYSMSG		100	/* System message (sysx etc). */
#define MST_MTC			102	/* Midi Time Code (MTC) qframe msg */
#define MST_SONGSEL		103	/* Song select */
#define MST_SONGPOS		104	/* Song position pointer */
#define MST_TIMED		105	/* Leading timing byte rcvd */

/* buffer space check for input scanner */

#define BUFTEST(mi) if (mi->m_ptr >= MI_MAX || mi->m_ptr < 0) \
{printk(KERN_ERR "WF-MPU: Invalid buffer pointer %d/%d, s=%d\n", \
	mi->m_ptr, mi->m_left, mi->m_state);mi->m_ptr--;}

static unsigned char len_tab[] =	/* # of data bytes following a status
					 */
{
	2,				/* 8x */
	2,				/* 9x */
	2,				/* Ax */
	2,				/* Bx */
	1,				/* Cx */
	1,				/* Dx */
	2,				/* Ex */
	0				/* Fx */
};

static int
wf_mpu_input_scanner (struct wf_mpu_config *devc, unsigned char midic)
{
	struct midi_input_info *mi;

	mi = &midi_devs[devc->devno]->in_info;

	switch (mi->m_state) {
	case MST_INIT:
		switch (midic) {
		case 0xf8:
			/* Timer overflow */
			break;
		
		case 0xfc:
			break;
		
		case 0xfd:
			/* XXX do something useful with this. If there is
			   an external MIDI timer (e.g. a hardware sequencer,
			   a useful timer can be derived ...
		   
			   For now, no timer support.
			*/
			break;
		
		case 0xfe:
			return MPU_ACK;
			break;
		
		case 0xf0:
		case 0xf1:
		case 0xf2:
		case 0xf3:
		case 0xf4:
		case 0xf5:
		case 0xf6:
		case 0xf7:
			break;
		
		case 0xf9:
			break;
		
		case 0xff:
			mi->m_state = MST_SYSMSG;
			break;
		
		default:
			if (midic <= 0xef) {
				mi->m_state = MST_TIMED;
			}
			else
				printk (KERN_ERR "<MPU: Unknown event %02x> ",
					midic);
		}
		break;
	  
	case MST_TIMED:
	{
		int             msg = ((int) (midic & 0xf0) >> 4);
	  
		mi->m_state = MST_DATA;
	  
		if (msg < 8) {	/* Data byte */
	      
			msg = ((int) (mi->m_prev_status & 0xf0) >> 4);
			msg -= 8;
			mi->m_left = len_tab[msg] - 1;
	      
			mi->m_ptr = 2;
			mi->m_buf[0] = mi->m_prev_status;
			mi->m_buf[1] = midic;

			if (mi->m_left <= 0) {
				mi->m_state = MST_INIT;
				do_midi_msg (devc->synthno, mi->m_buf,
					     mi->m_ptr);
				mi->m_ptr = 0;
			}
		} else if (msg == 0xf) {	/* MPU MARK */
	      
			mi->m_state = MST_INIT;

			switch (midic) {
			case 0xf8:
				break;
		    
			case 0xf9:
				break;
		    
			case 0xfc:
				break;
		    
			default:
			}
		} else {
			mi->m_prev_status = midic;
			msg -= 8;
			mi->m_left = len_tab[msg];
	      
			mi->m_ptr = 1;
			mi->m_buf[0] = midic;
	      
			if (mi->m_left <= 0) {
				mi->m_state = MST_INIT;
				do_midi_msg (devc->synthno, mi->m_buf,
					     mi->m_ptr);
				mi->m_ptr = 0;
			}
		}
	}
	break;

	case MST_SYSMSG:
		switch (midic) {
		case 0xf0:
			mi->m_state = MST_SYSEX;
			break;
	    
		case 0xf1:
			mi->m_state = MST_MTC;
			break;

		case 0xf2:
			mi->m_state = MST_SONGPOS;
			mi->m_ptr = 0;
			break;
	    
		case 0xf3:
			mi->m_state = MST_SONGSEL;
			break;
	    
		case 0xf6:
			mi->m_state = MST_INIT;
	    
			/*
			 *    Real time messages
			 */
		case 0xf8:
			/* midi clock */
			mi->m_state = MST_INIT;
			/* XXX need ext MIDI timer support */
			break;
	    
		case 0xfA:
			mi->m_state = MST_INIT;
			/* XXX need ext MIDI timer support */
			break;
	    
		case 0xFB:
			mi->m_state = MST_INIT;
			/* XXX need ext MIDI timer support */
			break;
	    
		case 0xFC:
			mi->m_state = MST_INIT;
			/* XXX need ext MIDI timer support */
			break;
	    
		case 0xFE:
			/* active sensing */
			mi->m_state = MST_INIT;
			break;
	    
		case 0xff:
			mi->m_state = MST_INIT;
			break;

		default:
			printk (KERN_ERR "unknown MIDI sysmsg %0x\n", midic);
			mi->m_state = MST_INIT;
		}
		break;

	case MST_MTC:
		mi->m_state = MST_INIT;
		break;

	case MST_SYSEX:
		if (midic == 0xf7) {
			mi->m_state = MST_INIT;
		} else {
			/* XXX fix me */
		}
		break;

	case MST_SONGPOS:
		BUFTEST (mi);
		mi->m_buf[mi->m_ptr++] = midic;
		if (mi->m_ptr == 2) {
			mi->m_state = MST_INIT;
			mi->m_ptr = 0;
			/* XXX need ext MIDI timer support */
		}
		break;

	case MST_DATA:
		BUFTEST (mi);
		mi->m_buf[mi->m_ptr++] = midic;
		if ((--mi->m_left) <= 0) {
			mi->m_state = MST_INIT;
			do_midi_msg (devc->synthno, mi->m_buf, mi->m_ptr);
			mi->m_ptr = 0;
		}
		break;

	default:
		printk (KERN_ERR "Bad state %d ", mi->m_state);
		mi->m_state = MST_INIT;
	}

	return 1;
}

void wf_mpuintr (int irq, void *dev_id, struct pt_regs *dummy)
{
	struct wf_mpu_config *devc;
	int dev;
	static struct wf_mpu_config *isrc = 0;
	int n;
	struct midi_input_info *mi;

	if (irq < 0 || irq > 15) 
	{
		printk (KERN_ERR "WF-MPU: bogus interrupt #%d", irq);
		return;
	}
	dev = irq2dev[irq];
	mi = &midi_devs[dev]->in_info;
	if (mi->m_busy)
		return;
	mi->m_busy = 1;
	
	sti (); 

	n = 50;

	/* guarantee that we're working with the "real" (internal)
	   interface before doing anything physical.
	*/

	devc = &dev_conf[dev];
	devc = &dev_conf[devc->internal];

	if (isrc == 0) {
      
		/* This is just an initial setting. If Virtual MIDI mode is
		   enabled on the ICS2115, we'll get a switch char before
		   anything else, and if it isn't, then the guess will be
		   correct for our purposes.
		*/
      
		isrc = &dev_conf[devc->internal];
	}
  
	while (input_avail (devc) && n-- > 0) {
		unsigned char c = read_data (devc);
      
		if (devc->isvirtual) {
			if (c == WF_EXTERNAL_SWITCH) {
				isrc = &dev_conf[devc->external];
				continue;
			} else if (c == WF_INTERNAL_SWITCH) { 
				isrc = &dev_conf[devc->internal];
				continue;
			} /* else just leave it as it is */
		} else {
			isrc = &dev_conf[devc->internal];
		}

		if (isrc->mode == MODE_SYNTH) {
	  
			wf_mpu_input_scanner (isrc, c);
	  
		} else if (isrc->opened & OPEN_READ) {
	  
			if (isrc->inputintr) {
				isrc->inputintr (isrc->devno, c);
			} 
		}
	}

	mi->m_busy = 0;
}

static int wf_mpu_open (int dev, int mode,
	     void            (*input) (int dev, unsigned char data),
	     void            (*output) (int dev)
	)
{
	struct wf_mpu_config *devc;

	if (dev < 0 || dev >= num_midis || midi_devs[dev]==NULL)
		return -(ENXIO);

	devc = &dev_conf[dev];

	if (devc->opened) {
		return -(EBUSY);
	}

	devc->mode = MODE_MIDI;
	devc->opened = mode;
	devc->synthno = 0;

	devc->inputintr = input;
	return 0;
}
 
static void
wf_mpu_close (int dev)
{
	struct wf_mpu_config *devc;

	devc = &dev_conf[dev];
	devc->mode = 0;
	devc->inputintr = NULL;
	devc->opened = 0;
}

static int
wf_mpu_out (int dev, unsigned char midi_byte)
{
	int             timeout;
	unsigned long   flags;
	static int lastoutdev = -1;

	struct wf_mpu_config *devc;
	unsigned char switchch;

	/* The actual output has to occur using the "internal" config info
	 */
  
	devc = &dev_conf[dev_conf[dev].internal];
  
	if (devc->isvirtual && lastoutdev != dev) {
      
		if (dev == devc->internal) { 
			switchch = WF_INTERNAL_SWITCH;
		} else if (dev == devc->external) { 
			switchch = WF_EXTERNAL_SWITCH;
		} else {
			printk (KERN_ERR "WF-MPU: bad device number %d", dev);
			return (0);
		}
      
		for (timeout = 30000; timeout > 0 && !output_ready (devc);
		     timeout--);
      
		save_flags (flags);
		cli ();
      
		if (!output_ready (devc)) {
			printk (KERN_WARNING "WF-MPU: Send switch "
				"byte timeout\n");
			restore_flags (flags);
			return 0;
		}
      
		write_data (devc, switchch);
		restore_flags (flags);
	} 

	lastoutdev = dev;

	/*
	 * Sometimes it takes about 30000 loops before the output becomes ready
	 * (After reset). Normally it takes just about 10 loops.
	 */

	for (timeout = 30000; timeout > 0 && !output_ready (devc); timeout--);

	save_flags (flags);
	cli ();
	if (!output_ready (devc)) {
		printk (KERN_WARNING "WF-MPU: Send data timeout\n");
		restore_flags (flags);
		return 0;
	}

	write_data (devc, midi_byte);
	restore_flags (flags);

	return 1;
}

static int
wf_mpu_start_read (int dev)
{
	return 0;
}

static int
wf_mpu_end_read (int dev)
{
	return 0;
}

static int
wf_mpu_ioctl (int dev, unsigned cmd, caddr_t arg)
{
	printk (KERN_WARNING
		"WF-MPU: Intelligent mode not supported by hardware.\n");
	return -(EINVAL);
}

static void
wf_mpu_kick (int dev)
{
}

static int
wf_mpu_buffer_status (int dev)
{
	return 0;			/*
					 * No data in buffers
					 */
}

static int
wf_mpu_synth_ioctl (int dev,
		    unsigned int cmd, caddr_t arg)
{
	int             midi_dev;

	midi_dev = synth_devs[dev]->midi_dev;

	if (midi_dev < 0 || midi_dev > num_midis || midi_devs[midi_dev]==NULL)
		return -(ENXIO);

	switch (cmd) {

	case SNDCTL_SYNTH_INFO:
		copy_to_user (&((char *) arg)[0],
			      &wf_mpu_synth_info[midi_dev],
			      sizeof (struct synth_info));
	
		return 0;
		break;
	
	case SNDCTL_SYNTH_MEMAVL:
		return 0x7fffffff;
		break;
	
	default:
		return -(EINVAL);
	}
}

static int
wf_mpu_synth_open (int dev, int mode)
{
	int             midi_dev;
	struct wf_mpu_config *devc;

	midi_dev = synth_devs[dev]->midi_dev;

	if (midi_dev < 0 || midi_dev > num_midis || midi_devs[midi_dev]==NULL) {
		return -(ENXIO);
	}
  
	devc = &dev_conf[midi_dev];
	if (devc->opened) {
		return -(EBUSY);
	}
  
	devc->mode = MODE_SYNTH;
	devc->synthno = dev;
	devc->opened = mode;
	devc->inputintr = NULL;
	return 0;
}

static void
wf_mpu_synth_close (int dev)
{
	int             midi_dev;
	struct wf_mpu_config *devc;

	midi_dev = synth_devs[dev]->midi_dev;

	devc = &dev_conf[midi_dev];
	devc->inputintr = NULL;
	devc->opened = 0;
	devc->mode = 0;
}

#define MIDI_SYNTH_NAME	"WaveFront (MIDI)"
#define MIDI_SYNTH_CAPS	SYNTH_CAP_INPUT
#include "midi_synth.h"

static struct synth_operations wf_mpu_synth_proto =
{
	"WaveFront (ICS2115)",
	NULL,  /* info field, filled in during configuration */
	0,     /* MIDI dev XXX should this be -1 ? */
	SYNTH_TYPE_MIDI,
	SAMPLE_TYPE_WAVEFRONT,
	wf_mpu_synth_open,
	wf_mpu_synth_close,
	wf_mpu_synth_ioctl,
	midi_synth_kill_note,
	midi_synth_start_note,
	midi_synth_set_instr,
	midi_synth_reset,
	midi_synth_hw_control,
	midi_synth_load_patch,
	midi_synth_aftertouch,
	midi_synth_controller,
	midi_synth_panning,
	NULL,
	midi_synth_bender,
	NULL,				/* alloc */
	midi_synth_setup_voice,
	midi_synth_send_sysex
};

static struct synth_operations wf_mpu_synth_operations[2];
static struct midi_operations  wf_mpu_midi_operations[2];
static int wfmpu_cnt = 0;

static struct midi_operations wf_mpu_midi_proto =
{
	{"WF-MPU MIDI", 0, MIDI_CAP_MPU401, SNDCARD_MPU401},
	NULL,  /*converter*/
	{0},   /* in_info */
	wf_mpu_open,
	wf_mpu_close,
	wf_mpu_ioctl,
	wf_mpu_out,
	wf_mpu_start_read,
	wf_mpu_end_read,
	wf_mpu_kick,
	NULL,
	wf_mpu_buffer_status,
	NULL
};


static int
config_wf_mpu (int dev, struct address_info *hw_config)

{
	struct wf_mpu_config *devc;
	int                internal;

	if (wfmpu_cnt >= 2) {
		printk (KERN_ERR "WF-MPU: more MPU devices than cards ?!!\n");
		return (-1);
	}
  
	/* There is no synth available on the external interface,
	   so do the synth stuff to the internal interface only.
	*/

	internal = dev_conf[dev].internal;
	devc = &dev_conf[internal];

	if (!dev_conf[dev].isexternal) {
		memcpy ((char *) &wf_mpu_synth_operations[wfmpu_cnt],
			(char *) &wf_mpu_synth_proto,
			sizeof (struct synth_operations));
	}

	memcpy ((char *) &wf_mpu_midi_operations[wfmpu_cnt],
		(char *) &wf_mpu_midi_proto,
		sizeof (struct midi_operations));
  
	if (dev_conf[dev].isexternal) {
		wf_mpu_midi_operations[wfmpu_cnt].converter = NULL;
	} else {
		wf_mpu_midi_operations[wfmpu_cnt].converter =
			&wf_mpu_synth_operations[wfmpu_cnt];
	}

	memcpy ((char *) &wf_mpu_synth_info[dev],
		(char *) &wf_mpu_synth_info_proto,
		sizeof (struct synth_info));

	strcpy (wf_mpu_synth_info[dev].name, hw_config->name);
	strcpy (wf_mpu_midi_operations[wfmpu_cnt].info.name, hw_config->name);

	conf_printf (hw_config->name, hw_config);

	if (!dev_conf[dev].isexternal) {
		wf_mpu_synth_operations[wfmpu_cnt].midi_dev = dev;
	}
	wf_mpu_synth_operations[wfmpu_cnt].info = &wf_mpu_synth_info[dev];

	midi_devs[dev] = &wf_mpu_midi_operations[wfmpu_cnt];

	dev_conf[dev].opened = 0;
	dev_conf[dev].mode = 0;
	dev_conf[dev].configured_for_virtual = 0;
	dev_conf[dev].devno = dev;

	midi_devs[dev]->in_info.m_busy = 0;
	midi_devs[dev]->in_info.m_state = MST_INIT;
	midi_devs[dev]->in_info.m_ptr = 0;
	midi_devs[dev]->in_info.m_left = 0;
	midi_devs[dev]->in_info.m_prev_status = 0;

	wfmpu_cnt++;

	return (0);
}

int
virtual_midi_enable (int dev, struct address_info *hw_config)

{
	int idev;
	int edev;
	struct wf_mpu_config *devc;

	devc = &dev_conf[dev];
	
	if (devc->configured_for_virtual) {

		idev = devc->internal;
		edev = devc->external;

	} else {

		if (hw_config == NULL) {
			printk (KERN_ERR
				"WF-MPU: virtual midi first "
				"enabled without hw_config!\n");
			return -EINVAL;
		}

		idev = devc->internal;

		if ((edev = sound_alloc_mididev()) == -1) {
			printk (KERN_ERR
				"WF-MPU: too many midi devices detected\n");
			return -1;
		}

		hw_config->slots[WF_EXTERNAL_MIDI_SLOT] = edev;
	}

	dev_conf[edev].isvirtual = 1;
	dev_conf[idev].isvirtual = 1;
    
	if (dev_conf[idev].configured_for_virtual) {
		return 0;
	} 

	/* Configure external interface struct */

	devc = &dev_conf[edev];
	devc->internal = idev;
	devc->external = edev;
	devc->isexternal = 1;

	/* Configure external interface struct 
	   (devc->isexternal and devc->internal set in attach_wf_mpu())
	*/

	devc = &dev_conf[idev];
	devc->external = edev;

	/* Configure the tables for the external */

	if (config_wf_mpu (edev, hw_config)) {
		printk (KERN_WARNING "WF-MPU: configuration for MIDI "
			"device %d failed\n", edev);
		return (-1);
	}

	/* Don't bother to do this again if we are toggled back and
	   forth between virtual MIDI mode and "normal" operation.
	*/

	dev_conf[edev].configured_for_virtual = 1;
	dev_conf[idev].configured_for_virtual = 1;

	return 0;
}

void
virtual_midi_disable (int dev)

{
	struct wf_mpu_config *devc;
	unsigned long flags;

	save_flags (flags);
	cli();

	/* Assumes for logical purposes that the caller has taken
	   care of fiddling with WaveFront hardware commands to
	   turn off Virtual MIDI mode. 
	*/
    
	devc = &dev_conf[dev];

	devc = &dev_conf[devc->internal];
	devc->isvirtual = 0;

	devc = &dev_conf[devc->external];
	devc->isvirtual = 0;

	restore_flags (flags);
}

void
attach_wf_mpu (struct address_info *hw_config)
{
	int m;
	struct wf_mpu_config *devc;

	if (request_irq (hw_config->irq, wf_mpuintr,
			 0, "WaveFront MIDI", NULL) < 0) {
		printk (KERN_ERR "WF-MPU: Failed to allocate IRQ%d\n",
			hw_config->irq);
		return;
	}

	if ((m = sound_alloc_mididev()) == -1){
		printk (KERN_ERR "WF-MPU: Too many MIDI devices detected.\n");
		free_irq (hw_config->irq, NULL);
		release_region (hw_config->io_base, 2);
		return;
	}

	request_region (hw_config->io_base, 2, "WaveFront MPU");

	hw_config->slots[WF_INTERNAL_MIDI_SLOT] = m;
	devc = &dev_conf[m];
	devc->base = hw_config->io_base;
	devc->irq = hw_config->irq;
	devc->isexternal = 0;
	devc->internal = m;
	devc->external = -1;
	devc->isvirtual = 0;

	irq2dev[devc->irq] = m;

	if (config_wf_mpu (m, hw_config)) {
		printk (KERN_WARNING
			"WF-MPU: configuration for MIDI device %d failed\n", m);
		sound_unload_mididev (m);
	}

	/* This being a WaveFront (ICS-2115) emulated MPU-401, we have
	   to switch it into UART (dumb) mode, because otherwise, it
	   won't do anything at all.
	*/
  
	start_uart_mode (devc);

}

int
probe_wf_mpu (struct address_info *hw_config)

{
	if (hw_config->irq < 0 || hw_config->irq > 16) {
		printk (KERN_WARNING "WF-MPU: bogus IRQ value requested (%d)\n",
			hw_config->irq);
		return 0;
	}

	if (check_region (hw_config->io_base, 2)) {
		printk (KERN_WARNING "WF-MPU: I/O port %x already in use\n\n",
			hw_config->io_base);
		return 0;
	}

	if (inb (hw_config->io_base + 1) == 0xff) { /* Just bus float? */
		printk ("WF-MPU: Port %x looks dead.\n", hw_config->io_base);
		return 0;
	}

	return 1;
}

void
unload_wf_mpu (struct address_info *hw_config)
{

	release_region (hw_config->io_base, 2); 
	sound_unload_mididev (hw_config->slots[WF_INTERNAL_MIDI_SLOT]);
	if (hw_config->irq > 0) {
		free_irq (hw_config->irq, NULL);
	}
	if (hw_config->slots[WF_EXTERNAL_MIDI_SLOT] > 0) {
		sound_unload_mididev (hw_config->slots[WF_EXTERNAL_MIDI_SLOT]);
	}
}

static void
start_uart_mode (struct wf_mpu_config *devc)
{
	int             ok, timeout;
	unsigned long   flags;

	save_flags (flags);
	cli ();

	for (timeout = 30000; timeout > 0 && !output_ready (devc); timeout--);

	wf_mpu_cmd (devc, UART_MODE_ON);

	for (ok = 0, timeout = 50000; timeout > 0 && !ok; timeout--) {
		if (input_avail (devc)) {
			if (read_data (devc) == MPU_ACK) {
				ok = 1;
			}
		}
	}

	restore_flags (flags);
}

#endif


