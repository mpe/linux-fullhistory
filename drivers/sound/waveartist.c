/*
 * drivers/sound/waveartist.c
 *
 * The low level driver for the RWA010 Rockwell Wave Artist
 * codec chip used in the Corel Computer NetWinder.
 *
 * Cleaned up and integrated into 2.1 by Russell King (rmk@arm.linux.org.uk)
 */

/*
 * Copyright (C) by Corel Computer 1998
 *
 * RWA010 specs received under NDA from Rockwell
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */

/* Debugging */
#define DEBUG_CMD	1
#define DEBUG_OUT	2
#define DEBUG_IN	4
#define DEBUG_INTR	8
#define DEBUG_MIXER	16
#define DEBUG_TRIGGER	32

#define debug_flg (0)

#define DEB(x)
#define DDB(x)
#define DEB1(x)

#include <linux/module.h>
#include <linux/config.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/smp.h>

#include <asm/hardware.h>

#include "soundmodule.h"
#include "sound_config.h"
#include "waveartist.h"

#define	VNC_TIMER_PERIOD (HZ/4)	//check slider 4 times/sec

#define	MIXER_PRIVATE3_RESET	0x53570000
#define	MIXER_PRIVATE3_READ	0x53570001
#define	MIXER_PRIVATE3_WRITE	0x53570002

#define	VNC_INTERNAL_SPKR	0x01	//the sw mute on/off control bit
#define	VNC_INTERNAL_MIC	0x10	//the hw internal/handset mic bit

/* Use RECSRC = speaker to mark the internal microphone
 *
 * Some cheating involved here: there is no way to relay
 * to the system, which microphone in in use
 * (left = handset, or right = internal)
 *
 * So while I do not flag SPEAKER in the Recording Devices
 * Mask, when on internal
 *
 * mike - I set the speaker bit hi. Some mixers can be
 * confused a bit...
 */

#define POSSIBLE_RECORDING_DEVICES	(SOUND_MASK_LINE |\
					 SOUND_MASK_MIC |\
					 SOUND_MASK_LINE1)	//Line1 = analog phone

#define SUPPORTED_MIXER_DEVICES		(SOUND_MASK_SYNTH |\
					 SOUND_MASK_PCM |\
					 SOUND_MASK_LINE |\
					 SOUND_MASK_MIC | \
					 SOUND_MASK_LINE1 |\
					 SOUND_MASK_RECLEV |\
					 SOUND_MASK_VOLUME)

static unsigned short levels[SOUND_MIXER_NRDEVICES] = {
	0x5555,		/* Master Volume	 */
	0x0000,		/* Bass			 */
	0x0000,		/* Treble		 */
	0x5555,		/* Synth (FM)		 */
	0x4b4b,		/* PCM			 */
	0x0000,		/* PC Speaker		 */
	0x0000,		/* Ext Line		 */
	0x0000,		/* Mic			 */
	0x0000,		/* CD			 */
	0x0000,		/* Recording monitor	 */
	0x0000,		/* SB PCM (ALT PCM)	 */
	0x0000,		/* Recording level	 */
	0x0000,		/* Input gain		 */
	0x0000,		/* Output gain		 */
	0x0000,		/* Line1 (Aux1)		 */
	0x0000,		/* Line2 (Aux2)		 */
	0x0000,		/* Line3 (Aux3)		 */
	0x0000,		/* Digital1		 */
	0x0000,		/* Digital2		 */
	0x0000,		/* Digital3		 */
	0x0000,		/* Phone In		 */
	0x0000,		/* Phone Out		 */
	0x0000,		/* Video		 */
	0x0000,		/* Radio		 */
	0x0000		/* Monitor		 */
};

typedef struct {
	struct address_info  hw;	/* hardware */
	char		*chip_name;

	int		xfer_count;
	int		audio_mode;
	int		open_mode;
	int		audio_flags;
	int		record_dev;
	int		playback_dev;
	int		dev_no;

	/* Mixer parameters */
	unsigned short	*levels;
	int		handset_state;
	signed int	slider_vol;	   /* hardware slider volume */
	int		recmask;	   /* currently enabled recording device! */
	int             supported_devices; /* SUPPORTED_MIXER_DEVICES */
	int             rec_devices;	   /* POSSIBLE_RECORDING_DEVICES */
	int		handset_mute_sw	:1;/* 1 - handset controlled in sw */
	int		use_slider	:1;/* use slider setting for o/p vol */
	int		mute_state	:1;
} wavnc_info;

typedef struct wavnc_port_info {
	int		open_mode;
	int		speed;
	int		channels;
	int		audio_format;
} wavnc_port_info;

static int		 nr_waveartist_devs;
static wavnc_info	 adev_info[MAX_AUDIO_DEV];
static struct timer_list vnc_timer;


static inline void
waveartist_set_ctlr(struct address_info *hw, unsigned char clear, unsigned char set)
{
	unsigned int ctlr_port = hw->io_base + CTLR;

	clear = ~clear & inb(ctlr_port);

	outb(clear | set, ctlr_port);
}

/* Toggle IRQ acknowledge line
 */
static inline void
waveartist_iack(wavnc_info *devc)
{
	unsigned int ctlr_port = devc->hw.io_base + CTLR;
	int old_ctlr;

	old_ctlr = inb(ctlr_port) & ~IRQ_ACK;

	outb(old_ctlr | IRQ_ACK, ctlr_port);
	outb(old_ctlr, ctlr_port);
}

static inline int
waveartist_sleep(int timeout_ms)
{
	unsigned int timeout = timeout_ms * 10 * HZ / 100;

	do {
		current->state = TASK_INTERRUPTIBLE;
		timeout = schedule_timeout(timeout);
	} while (timeout);

	return 0;
}

static int
waveartist_reset(wavnc_info *devc)
{
	struct address_info *hw = &devc->hw;
	unsigned int timeout, res = -1;

	waveartist_set_ctlr(hw, -1, RESET);
	waveartist_sleep(2);
	waveartist_set_ctlr(hw, RESET, 0);

	timeout = 500;
	do {
		mdelay(2);

		if (inb(hw->io_base + STATR) & CMD_RF) {
			res = inw(hw->io_base + CMDR);
			if (res == 0x55aa)
				break;
		}
	} while (timeout--);

	if (timeout == 0) {
		printk("WaveArtist: reset timeout ");
		if (res != (unsigned int)-1)
			printk("(res=%04X)", res);
		printk("\n");
		return 1;
	}
	return 0;
}

static int
waveartist_cmd(wavnc_info *devc,
		int nr_cmd, unsigned int *cmd,
		int nr_resp, unsigned int *resp)
{
	unsigned int io_base = devc->hw.io_base;
	unsigned int timed_out = 0;
	unsigned int i;

	if (debug_flg & DEBUG_CMD) {
		printk("waveartist_cmd: cmd=");

		for (i = 0; i < nr_cmd; i++)
			printk("%04X ", cmd[i]);

		printk("\n");
	}

	if (inb(io_base + STATR) & CMD_RF) {
		int old_data;

		/* flush the port
		 */

		old_data = inw(io_base + CMDR);

		if (debug_flg & DEBUG_CMD)
			printk("flushed %04X...", old_data);

		udelay(10);
	}

	for (i = 0; !timed_out && i < nr_cmd; i++) {
		int count;

		for (count = 5000; count; count--)
			if (inb(io_base + STATR) & CMD_WE)
				break;

		if (!count)
			timed_out = 1;
		else
			outw(cmd[i], io_base + CMDR);
	}

	for (i = 0; !timed_out && i < nr_resp; i++) {
		int count;

		for (count = 5000; count; count--)
			if (inb(io_base + STATR) & CMD_RF)
				break;

		if (!count)
			timed_out = 1;
		else
			resp[i] = inw(io_base + CMDR);
	}

	if (debug_flg & DEBUG_CMD) {
		if (!timed_out) {
			printk("waveartist_cmd: resp=");

			for (i = 0; i < nr_resp; i++)
				printk("%04X ", resp[i]);

			printk("\n");
		} else
			printk("waveartist_cmd: timed out\n");
	}

	return timed_out ? 1 : 0;
}

static inline int
waveartist_cmd2(wavnc_info *devc, unsigned int cmd, unsigned int arg)
{
	unsigned int vals[2];

	vals[0] = cmd;
	vals[1] = arg;

	waveartist_cmd(devc, 2, vals, 1, vals);

	return 0;
}

static inline int
waveartist_cmd3(wavnc_info *devc, unsigned int cmd,
		unsigned int arg1, unsigned int arg2)
{
	unsigned int vals[3];

	vals[0] = cmd;
	vals[1] = arg1;
	vals[2] = arg2;

	return waveartist_cmd(devc, 3, vals, 0, NULL);
}

static int
waveartist_sendcmd(struct address_info *hw, unsigned int cmd)
{
	int count;

	if (debug_flg & DEBUG_CMD)
		printk("waveartist_sendcmd: cmd=0x%04X...", cmd);

	udelay(10);

	if (inb(hw->io_base + STATR) & CMD_RF) {
		/*
		 * flush the port
		 */
		count = inw(hw->io_base + CMDR);

		udelay(10);

		if (debug_flg & DEBUG_CMD)
			printk(" flushed %04X...", count);
	}

	/*
	 * preset timeout at 5000 loops
	 */
	count = 5000;

	while (count --)
		if (inb(hw->io_base + STATR) & CMD_WE) {
			/* wait till CMD_WE is high
			 * then output the command
			 */
			outw(cmd, hw->io_base + CMDR);
			break;
		}

	/* ready BEFORE timeout?
	 */
	if (debug_flg & DEBUG_CMD)
		printk(" %s\n", count ? "Done OK." : "Error!");

	udelay(10);

	return count ? 0 : 1;
}

static int
waveartist_getrev(struct address_info *hw, char *rev)
{
	int temp;

	waveartist_sendcmd(hw, 0);
	udelay(20);
	temp = inw(hw->io_base + CMDR);
	udelay(20);
	inw(hw->io_base + CMDR);	// discard second word == 0

	rev[0] = temp >> 8;
	rev[1] = temp & 255;
	rev[2] = '\0';

	return temp;
}

inline void
waveartist_mute(wavnc_info *devc, int mute)
{
}

static void waveartist_halt_output(int dev);
static void waveartist_halt_input(int dev);
static void waveartist_halt(int dev);
static void waveartist_trigger(int dev, int state);

static int
waveartist_open(int dev, int mode)
{
	wavnc_info	*devc;
	wavnc_port_info	*portc;
	unsigned long	flags;

	if (dev < 0 || dev >= num_audiodevs)
		return -ENXIO;

	devc  = (wavnc_info *) audio_devs[dev]->devc;
	portc = (wavnc_port_info *) audio_devs[dev]->portc;

	save_flags(flags);
	cli();
	if (portc->open_mode || (devc->open_mode & mode)) {
		restore_flags(flags);
		return -EBUSY;
	}

	devc->audio_mode  = 0;
	devc->open_mode  |= mode;
	portc->open_mode  = mode;
	waveartist_trigger(dev, 0);

	if (mode & OPEN_READ)
		devc->record_dev = dev;
	if (mode & OPEN_WRITE)
		devc->playback_dev = dev;
	restore_flags(flags);

	/*
	 * Mute output until the playback really starts. This
	 * decreases clicking (hope so).
	 */
	waveartist_mute(devc, 1);

	return 0;
}

static void
waveartist_close(int dev)
{
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	unsigned long	flags;

	save_flags(flags);
	cli();

	waveartist_halt(dev);

	devc->audio_mode = 0;
	devc->open_mode &= ~portc->open_mode;
	portc->open_mode = 0;

	waveartist_mute(devc, 1);

	restore_flags(flags);
}

static void
waveartist_output_block(int dev, unsigned long buf, int __count, int intrflag)
{
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	unsigned long	flags;
	unsigned int	count = __count; 

	if (debug_flg & DEBUG_OUT)
		printk("waveartist: output block, buf=0x%lx, count=0x%x...\n",
			buf, count);
	/*
	 * 16 bit data
	 */
	if (portc->audio_format & (AFMT_S16_LE | AFMT_S16_BE))
		count >>= 1;

	if (portc->channels > 1)
		count >>= 1;

	count -= 1;

	if (devc->audio_mode & PCM_ENABLE_OUTPUT &&
	    audio_devs[dev]->flags & DMA_AUTOMODE &&
	    intrflag &&
	    count == devc->xfer_count) {
		devc->audio_mode |= PCM_ENABLE_OUTPUT;
		return;	/*
			 * Auto DMA mode on. No need to react
			 */
	}

	save_flags(flags);
	cli();

	/*
	 * set sample count
	 */
	waveartist_cmd2(devc, 0x0024, count);

	devc->xfer_count = count;
	devc->audio_mode |= PCM_ENABLE_OUTPUT;

	restore_flags(flags);
}

static void
waveartist_start_input(int dev, unsigned long buf, int __count, int intrflag)
{
	wavnc_port_info *portc = (wavnc_port_info *) audio_devs[dev]->portc;
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	unsigned long	flags;
	unsigned int	count = __count;

	if (debug_flg & DEBUG_IN)
		printk("waveartist: start input, buf=0x%lx, count=0x%x...\n",
			buf, count);

	if (portc->audio_format & (AFMT_S16_LE | AFMT_S16_BE))	/* 16 bit data */
		count >>= 1;

	if (portc->channels > 1)
		count >>= 1;

	count -= 1;

	if (devc->audio_mode & PCM_ENABLE_INPUT &&
	    audio_devs[dev]->flags & DMA_AUTOMODE &&
	    intrflag &&
	    count == devc->xfer_count) {
		devc->audio_mode |= PCM_ENABLE_INPUT;
		return;	/*
			 * Auto DMA mode on. No need to react
			 */
	}

	save_flags(flags);
	cli();

	/*
	 * set sample count
	 */
	waveartist_cmd2(devc, 0x0014, count);
	waveartist_mute(devc, 0);

	devc->xfer_count = count;
	devc->audio_mode |= PCM_ENABLE_INPUT;

	restore_flags(flags);
}

static int
waveartist_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	return -EINVAL;
}

static unsigned int
waveartist_get_speed(wavnc_port_info *portc)
{
	unsigned int speed;

	/*
	 * program the speed, channels, bits
	 */
	if (portc->speed == 8000)
		speed = 0x2E71;
	else if (portc->speed == 11025)
		speed = 0x4000;
	else if (portc->speed == 22050)
		speed = 0x8000;
	else if (portc->speed == 44100)
		speed = 0x0;
	else {
		/*
		 * non-standard - just calculate
		 */
		speed = portc->speed << 16;

		speed = (speed / 44100) & 65535;
	}

	return speed;
}

static unsigned int
waveartist_get_bits(wavnc_port_info *portc)
{
	unsigned int bits;

	if (portc->audio_format == AFMT_S16_LE)
		bits = 1;
	else if (portc->audio_format == AFMT_S8)
		bits = 0;
	else
		bits = 2;	//default AFMT_U8

	return bits;
}

static int
waveartist_prepare_for_input(int dev, int bsize, int bcount)
{
	unsigned long	flags;
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	unsigned int	speed, bits;

	if (devc->audio_mode)
		return 0;

	speed = waveartist_get_speed(portc);
	bits  = waveartist_get_bits(portc);

	save_flags(flags);
	cli();

	if (waveartist_cmd2(devc, WACMD_INPUTFORMAT, bits))
		printk("waveartist: error setting the record format to %d\n",
		       portc->audio_format);

	if (waveartist_cmd2(devc, WACMD_INPUTCHANNELS, portc->channels))
		printk("waveartist: error setting record to %d channels\n",
		       portc->channels);

	/*
	 * write cmd SetSampleSpeedTimeConstant
	 */
	if (waveartist_cmd2(devc, WACMD_INPUTSPEED, speed))
		printk("waveartist: error setting the record speed "
		       "to %dHz.\n", portc->speed);

	if (waveartist_cmd2(devc, WACMD_INPUTDMA, 1))
		printk("waveartist: error setting the record data path "
		       "to 0x%X\n", 1);

	if (waveartist_cmd2(devc, WACMD_INPUTFORMAT, bits))
		printk("waveartist: error setting the record format to %d\n",
		       portc->audio_format);

	devc->xfer_count = 0;
	restore_flags(flags);
	waveartist_halt_input(dev);

	if (debug_flg & DEBUG_INTR) {
		printk("WA CTLR reg: 0x%02X.\n",inb(devc->hw.io_base + CTLR));
		printk("WA STAT reg: 0x%02X.\n",inb(devc->hw.io_base + STATR));
		printk("WA IRQS reg: 0x%02X.\n",inb(devc->hw.io_base + IRQSTAT));
	}

	return 0;
}

static int
waveartist_prepare_for_output(int dev, int bsize, int bcount)
{
	unsigned long	flags;
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	unsigned int	speed, bits;

	/*
	 * program the speed, channels, bits
	 */
	speed = waveartist_get_speed(portc);
	bits  = waveartist_get_bits(portc);

	save_flags(flags);
	cli();

	if (waveartist_cmd2(devc, WACMD_OUTPUTSPEED, speed) &&
	    waveartist_cmd2(devc, WACMD_OUTPUTSPEED, speed))
		printk("waveartist: error setting the playback speed "
		       "to %dHz.\n", portc->speed);

	if (waveartist_cmd2(devc, WACMD_OUTPUTCHANNELS, portc->channels))
		printk("waveartist: error setting the playback to"
		       " %d channels\n", portc->channels);

	if (waveartist_cmd2(devc, WACMD_OUTPUTDMA, 0))
		printk("waveartist: error setting the playback data path "
		       "to 0x%X\n", 0);

	if (waveartist_cmd2(devc, WACMD_OUTPUTFORMAT, bits))
		printk("waveartist: error setting the playback format to %d\n",
		       portc->audio_format);

	devc->xfer_count = 0;
	restore_flags(flags);
	waveartist_halt_output(dev);

	if (debug_flg & DEBUG_INTR) {
		printk("WA CTLR reg: 0x%02X.\n",inb(devc->hw.io_base + CTLR));
		printk("WA STAT reg: 0x%02X.\n",inb(devc->hw.io_base + STATR));
		printk("WA IRQS reg: 0x%02X.\n",inb(devc->hw.io_base + IRQSTAT));
	}

	return 0;
}

static void
waveartist_halt(int dev)
{
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	wavnc_info	*devc;


	if (portc->open_mode & OPEN_WRITE)
		waveartist_halt_output(dev);

	if (portc->open_mode & OPEN_READ)
		waveartist_halt_input(dev);

	devc = (wavnc_info *) audio_devs[dev]->devc;
	devc->audio_mode = 0;
}

static void
waveartist_halt_input(int dev)
{
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	unsigned long	flags;

	save_flags(flags);
	cli();

	waveartist_mute(devc, 1);

//RMK	disable_dma(audio_devs[dev]->dmap_in->dma);

	/*
	 * Stop capture
	 */
	waveartist_sendcmd(&devc->hw, 0x17);

//RMK	enable_dma(audio_devs[dev]->dmap_in->dma);
	devc->audio_mode &= ~PCM_ENABLE_INPUT;

	/*
	 * Clear interrupt by toggling
	 * the IRQ_ACK bit in CTRL
	 */
	if (inb(devc->hw.io_base + STATR) & IRQ_REQ)
		waveartist_iack(devc);

//	devc->audio_mode &= ~PCM_ENABLE_INPUT;

	restore_flags(flags);
}

static void
waveartist_halt_output(int dev)
{
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	unsigned long	flags;

	save_flags(flags);
	cli();

	waveartist_mute(devc, 1);

//RMK	disable_dma(audio_devs[dev]->dmap_out->dma);

	waveartist_sendcmd(&devc->hw, 0x27);

//RMK	enable_dma(audio_devs[dev]->dmap_out->dma);

	devc->audio_mode &= ~PCM_ENABLE_OUTPUT;

	/*
	 * Clear interrupt by toggling
	 * the IRQ_ACK bit in CTRL
	 */
	if (inb(devc->hw.io_base + STATR) & IRQ_REQ)
		waveartist_iack(devc);

//	devc->audio_mode &= ~PCM_ENABLE_OUTPUT;

	restore_flags(flags);
}

static void
waveartist_trigger(int dev, int state)
{
	wavnc_info	*devc = (wavnc_info *) audio_devs[dev]->devc;
	wavnc_port_info	*portc = (wavnc_port_info *) audio_devs[dev]->portc;
	unsigned long	flags;

	if (debug_flg & DEBUG_TRIGGER) {
		printk("wavnc: audio trigger ");
		if (state & PCM_ENABLE_INPUT)
			printk("in ");
		if (state & PCM_ENABLE_OUTPUT)
			printk("out");
		printk("\n");
	}

	save_flags(flags);
	cli();

	state &= devc->audio_mode;

	if (portc->open_mode & OPEN_READ &&
	    state & PCM_ENABLE_INPUT)
		/*
		 * enable ADC Data Transfer to PC
		 */
		waveartist_sendcmd(&devc->hw, 0x15);

	if (portc->open_mode & OPEN_WRITE &&
	    state & PCM_ENABLE_OUTPUT)
		/*
		 * enable DAC data transfer from PC
		 */
		waveartist_sendcmd(&devc->hw, 0x25);

	waveartist_mute(devc, 0);

	restore_flags(flags);
}

static int
waveartist_set_speed(int dev, int arg)
{
	wavnc_port_info *portc = (wavnc_port_info *) audio_devs[dev]->portc;

	if (arg <= 0)
		return portc->speed;

	if (arg < 5000)
		arg = 5000;
	if (arg > 44100)
		arg = 44100;

	portc->speed = arg;
	return portc->speed;

}

static short
waveartist_set_channels(int dev, short arg)
{
	wavnc_port_info *portc = (wavnc_port_info *) audio_devs[dev]->portc;

	if (arg != 1 && arg != 2)
		return portc->channels;

	portc->channels = arg;
	return arg;
}

static unsigned int
waveartist_set_bits(int dev, unsigned int arg)
{
	wavnc_port_info *portc = (wavnc_port_info *) audio_devs[dev]->portc;

	if (arg == 0)
		return portc->audio_format;

	if ((arg != AFMT_U8) && (arg != AFMT_S16_LE) && (arg != AFMT_S8))
		arg = AFMT_U8;

	portc->audio_format = arg;

	return arg;
}

static struct audio_driver waveartist_audio_driver = {
	waveartist_open,
	waveartist_close,
	waveartist_output_block,
	waveartist_start_input,
	waveartist_ioctl,
	waveartist_prepare_for_input,
	waveartist_prepare_for_output,
	waveartist_halt,
	NULL,
	NULL,
	waveartist_halt_input,
	waveartist_halt_output,
	waveartist_trigger,
	waveartist_set_speed,
	waveartist_set_bits,
	waveartist_set_channels
};


static void
waveartist_intr(int irq, void *dev_id, struct pt_regs *regs)
{
	wavnc_info *devc = (wavnc_info *)dev_id;
	int	   irqstatus, status;

	irqstatus = inb(devc->hw.io_base + IRQSTAT);
	status    = inb(devc->hw.io_base + STATR);

	if (debug_flg & DEBUG_INTR)
		printk("waveartist_intr: stat=%02x, irqstat=%02x\n",
		       status, irqstatus);

	if (status & IRQ_REQ)	/* Clear interrupt */
		waveartist_iack(devc);
	else
		printk("waveartist: unexpected interrupt\n");

#ifdef CONFIG_AUDIO
	if (irqstatus & 0x01) {
		int temp = 1;

		/* PCM buffer done
		 */
		if ((status & DMA0) && (devc->audio_mode & PCM_ENABLE_OUTPUT)) {
			DMAbuf_outputintr(devc->playback_dev, 1);
			temp = 0;
		}
		if ((status & DMA1) && (devc->audio_mode & PCM_ENABLE_INPUT)) {
			DMAbuf_inputintr(devc->record_dev);
			temp = 0;
		}
		if (temp)	//default:
			printk("WaveArtist: Unknown interrupt\n");
	}
#endif
	if (irqstatus & 0x2)
		// We do not use SB mode natively...
		printk("WaveArtist: Unexpected SB interrupt...\n");
}

/* -------------------------------------------------------------------------
 * Mixer stuff
 */
static void
waveartist_mixer_update(wavnc_info *devc, int whichDev)
{
	unsigned int mask, reg_l, reg_r;
	unsigned int lev_left, lev_right;
	unsigned int vals[3];

	lev_left  = devc->levels[whichDev] & 0xff;
	lev_right = devc->levels[whichDev] >> 8;

#define SCALE(lev,max)	((lev) * (max) / 100)

	switch(whichDev) {
	case SOUND_MIXER_VOLUME:
		mask  = 0x000e;
		reg_l = 0x200;
		reg_r = 0x600;
		lev_left  = SCALE(lev_left,  7) << 1;
		lev_right = SCALE(lev_right, 7) << 1;
		break;

	case SOUND_MIXER_LINE:
		mask  = 0x07c0;
		reg_l = 0x000;
		reg_r = 0x400;
		lev_left  = SCALE(lev_left,  31) << 6;
		lev_right = SCALE(lev_right, 31) << 6;
		break;

	case SOUND_MIXER_MIC:
		mask  = 0x0030;
		reg_l = 0x200;
		reg_r = 0x600;
		lev_left  = SCALE(lev_left,  3) << 4;
		lev_right = SCALE(lev_right, 3) << 4;
		break;

	case SOUND_MIXER_RECLEV:
		mask  = 0x000f;
		reg_l = 0x300;
		reg_r = 0x700;
		lev_left  = SCALE(lev_left,  10);
		lev_right = SCALE(lev_right, 10);
		break;

	case SOUND_MIXER_LINE1:
		mask  = 0x003e;
		reg_l = 0x000;
		reg_r = 0x400;
		lev_left  = SCALE(lev_left,  31) << 1;
		lev_right = SCALE(lev_right, 31) << 1;
		break;

	case SOUND_MIXER_PCM:
		waveartist_cmd3(devc, 0x0031, SCALE(lev_left,  32767),
				SCALE(lev_right, 32767));
		return;

	case SOUND_MIXER_SYNTH:
		waveartist_cmd3(devc, 0x0131, SCALE(lev_left,  32767),
				SCALE(lev_right, 32767));
		return;

	default:
		return;
	}

	/* read left setting */
	vals[0] = reg_l + 0x30;
	waveartist_cmd(devc, 1, vals, 1, vals + 1);

	/* read right setting */
	vals[0] = reg_r + 0x30;
	waveartist_cmd(devc, 1, vals, 1, vals + 2);

	vals[1] = (vals[1] & ~mask) | (lev_left  & mask);
	vals[2] = (vals[2] & ~mask) | (lev_right & mask);

	/* write left,right back */
	vals[0] = 0x32;
	waveartist_cmd(devc, 3, vals, 0, NULL);
}

static void
waveartist_select_input(wavnc_info *devc, unsigned int input)
{
	unsigned int vals[3];
#if 1
	/* New mixer programming - switch recording source
	 * using R/L_ADC_Mux_Select.  We are playing with
	 * left/right mux bit fields in reg 9.
	 *
	 * We can not switch Mux_Select while recording, so
	 * for microphones, enable both left and right and
	 * play with levels only!
	 *
	 * Unfortunately, we need to select the src of mono
	 * recording (left or right) before starting the
	 * recording - so can not dynamically switch between
	 * handset amd internal microphones...
	 */

	/*
	 * Get reg 9
	 */
	vals[0] = 0x0830;
	waveartist_cmd(devc, 1, vals, 1, vals + 1);

	/*
	 * Get reg 10, only so that we can write it back.
	 */
	vals[0] = 0x0930;
	waveartist_cmd(devc, 1, vals, 1, vals + 2);

	if (debug_flg & DEBUG_MIXER)
		printk("RECSRC: old left: 0x%04X, old right: 0x%04X.\n",
			vals[1] & 0x07, (vals[1] >> 3) & 0x07);

	vals[1] &= ~0x03F;	//kill current left/right mux input select

	switch (input) {
		/*
		 * Handset or internal MIC
		 */
	case SOUND_MASK_MIC:
		/*
		 * handset not plugged in?
		 */
		if (devc->handset_state & VNC_INTERNAL_MIC) {
			/*
			 * set mono recording from right mic
			 */
			waveartist_sendcmd(&devc->hw, 0x0134);
#if 0
			/*
			 * right=mic, left=none
			 */
			vals[1] |= 0x0028;
			/*
			 * pretend int mic
			 */
			devc->rec_devices |= SOUND_MASK_SPEAKER;
#endif
		} else {
			/*
			 * set mono rec from left mic
			 */
			waveartist_sendcmd(&devc->hw, 0x0034);
#if 0
			/*
			 * right=none, left=mic
			 */
			vals[1] |= 0x0005;
			/*
			 * show no int mic
			 */
			devc->rec_devices &= ~SOUND_MASK_SPEAKER;
#endif
		}
		/*
		 * right=mic, left=mic
		 */
		vals[1] |= 0x002D;
		break;

	case SOUND_MASK_LINE1:
		/*
		 * set mono rec from left aux1
		 */
		waveartist_sendcmd(&devc->hw, 0x0034);
		/*
		 * right=none, left=Aux1;
		 */
		vals[1] |= 0x0004;
		break;

	case SOUND_MASK_LINE:
		/*
		 * set mono rec from left (default)
		 */
		waveartist_sendcmd(&devc->hw, 0x0034);
		/*
		 * right=Line, left=Line;
		 */
		vals[1] |= 0x0012;
		break;
	}

	if (debug_flg & DEBUG_MIXER)
		printk("RECSRC %d: left=0x%04X, right=0x%04X.\n", input,
			vals[1] & 0x07, (vals[1] >> 3) & 0x07);

#else
	/* This part is good, if input connected to
	 * a mixer, so can be used for record-only modes...
	 */

	/*
	 * get reg 4
	 */
	vals[0] = 0x0330;
	waveartist_cmd(devc, 1, vals, 1, vals + 1);

	/*
	 * get reg 8
	 */
	vals[0] = 0x0730;
	waveartist_cmd(devc, 1, vals, 1, vals + 2);

	if (debug_flg & DEBUG_MIXER)
		printk("RECSRC: old left: 0x%04X, old right: 0x%04X.\n",
			vals[1], vals[2]);

	/*
	 * kill current left/right mux input select
	 */
	vals[1] &= ~0x07F8;
	vals[2] &= ~0x07F8;

	switch (input) {
		/*
		 * handset or internal mic
		 */
	case SOUND_MASK_MIC:
		/*
		 * handset not plugged in?
		 */
		if (devc->handset_state & VNC_INTERNAL_MIC) {
			/*
			 * set mono recording from right mic
			 */
			waveartist_sendcmd(&devc->hw, 0x0134);
			/*
			 * left = none, right = mic, RX filter gain
			 */
			vals[1] |= 0x0C00;
			vals[2] |= 0x0C88;
			/*
			 * pretend int mic
			 */
			devc->rec_devices |= SOUND_MASK_SPEAKER;
		} else {
			/*
			 * set mono rec from left mic
			 */
			waveartist_sendcmd(&devc->hw, 0x0034);
			/*
			 * left = mic, RX filter gain, right = none;
			 */
			vals[1] |= 0x0C88;
			vals[2] |= 0x0C00;
			/*
			 * show no int mic
			 */
			devc->rec_devices &= ~SOUND_MASK_SPEAKER;
		}
		break;

	case SOUND_MASK_LINE1:
		/*
		 * set mono rec from left aux1
		 */
		waveartist_sendcmd(&devc->hw, 0x0034);
		/*
		 * left = Aux1, right = none
		 */
		vals[1] |= 0x0C40;
		vals[2] |= 0x0C00;
		break;
	
	case SOUND_MASK_LINE:
		/*
		 * left = Line, right = Line
		 */
		vals[1] |= 0x0C10;
		vals[2] |= 0x0C10;
		break;
	}

	if (debug_flg & DEBUG_MIXER)
		printk("RECSRC %d: left(4) 0x%04X, right(8) 0x%04X.\n",
			level, vals[1], vals[2]);
#endif
	/*
	 * and finally - write the reg pair back....
	 */
	vals[0] = 0x32;

	waveartist_cmd(devc, 3, vals, 0, NULL);
}

static int
waveartist_mixer_set(wavnc_info *devc, int whichDev, unsigned int level)
{
	unsigned int lev_left  = level & 0x007f;
	unsigned int lev_right = (level & 0x7f00) >> 8;

	int left, right, devmask, changed, i;

	left = level & 0x7f;
	right = (level & 0x7f00) >> 8;

	if (debug_flg & DEBUG_MIXER)
		printk("wa_mixer_set(dev=%d, level=%X)\n",
			whichDev, level);

	switch (whichDev) {
	/* Master volume (0-7)
	 * We have 3 bits on the Left/Right Mixer Gain,
	 * bits 3,2,1 on 3 and 7
	 */
	case SOUND_MIXER_VOLUME:
		devc->levels[whichDev] = lev_left | lev_right << 8;
		waveartist_mixer_update(devc, whichDev);
		break;


	/* External line (0-31)
	 * use LOUT/ROUT bits 10...6, reg 1 and 5
	 */
	case SOUND_MIXER_LINE:
		devc->levels[whichDev] = lev_left | lev_right << 8;
		waveartist_mixer_update(devc, whichDev);
		break;

	/* Mono microphone (0-3) mute,
	 * 0db,10db,20db
	 */
	case SOUND_MIXER_MIC:
#if 1
		devc->levels[whichDev] = lev_left | lev_right << 8;
#else
		/* we do not need to mute volume of
		 * an unused mic - it is simply unused...
		 */
		if (devc->handset_state & VNC_INTERNAL_MIC)
			devc->levels[whichDev] = lev_right << 8;
		else
			levels[whichDev] = lev_left;
#endif
		waveartist_mixer_update(devc, whichDev);
		break;

	/* Recording level (0-7)
	 */
	case SOUND_MIXER_RECLEV:
		devc->levels[whichDev] = lev_left | lev_right << 8;
		waveartist_mixer_update(devc, whichDev);
		break;

	/* Mono External Aux1 (0-31)
	 * use LINE1 bits 5...1, reg 1 and 5
	 */
	case SOUND_MIXER_LINE1:
		devc->levels[whichDev] = lev_left | lev_right << 8;
		waveartist_mixer_update(devc, whichDev);
		break;

	/* WaveArtist PCM (0-32767)
	 */
	case SOUND_MIXER_PCM:
		devc->levels[whichDev] = lev_left | lev_right << 8;
		waveartist_mixer_update(devc, whichDev);
		break;

	/* Internal synthesizer (0-31)
	 */
	case SOUND_MIXER_SYNTH:
		devc->levels[whichDev] = lev_left | lev_right << 8;
		waveartist_mixer_update(devc, whichDev);
		break;


	/* Select recording input source
	 */
	case SOUND_MIXER_RECSRC:
		devmask = level & POSSIBLE_RECORDING_DEVICES;

		changed = devmask ^ devc->recmask;
		devc->recmask = devmask;

		for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
			if (changed & (1 << i))
				waveartist_mixer_update(devc, i);

		waveartist_select_input(devc, level);
		/*
		 * do not save in "levels", return current setting
		 */
		return devc->recmask;

	default:
		return -EINVAL;
	}

	return devc->levels[whichDev];
}

static void
waveartist_mixer_reset(wavnc_info *devc)
{
	int i;

	if (debug_flg & DEBUG_MIXER)
		printk("%s: mixer_reset\n", devc->hw.name);

	/*
	 * reset mixer cmd
	 */
	waveartist_sendcmd(&devc->hw, 0x33);

	/*
	 * set input for ADC to come from
	 * a mux (left and right) == reg 9,
	 * initially none
	 */
	waveartist_cmd3(devc, 0x0032, 0x9800, 0xa836);

	/*
	 * set mixer input select to none, RX filter gains 0 db
	 */
	waveartist_cmd3(devc, 0x0032, 0x4c00, 0x8c00);

	/*
	 * set bit 0 reg 2 to 1 - unmute MonoOut
	 */
	waveartist_cmd3(devc, 0x0032, 0x2801, 0x6800);

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		waveartist_mixer_update(devc, i);

	/* set default input device = internal mic
	 * current recording device = none
	 * no handset
	 */
	devc->recmask = 0;
	devc->handset_state = VNC_INTERNAL_MIC;
//	waveartist_mixer_set(devc, SOUND_MIXER_RECSRC, SOUND_MASK_MIC);

	/*
	 * start from enabling the hw setting
	 */
	devc->handset_mute_sw = 0;
	devc->supported_devices = SUPPORTED_MIXER_DEVICES;
	devc->rec_devices = POSSIBLE_RECORDING_DEVICES;
}

static int
waveartist_mixer_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	wavnc_info *devc = (wavnc_info *)audio_devs[dev]->devc;
#if 0
	//use this call to override the automatic handset behaviour - ignore handset
	//bit 0x80 = total control over handset - do not react to plug/unplug
	//bit 0x10 = 1 == internal mic, otherwise handset mic
	//bit 0x01 = 1 == mute internal speaker, otherwise unmute

	if (cmd == SOUND_MIXER_PRIVATE1) {
		int             val, temp;

		val = *(int *) arg;

//		printk("MIXER_PRIVATE1: passed parameter = 0x%X.\n",val);
		return -EINVAL;		//check if parameter is logical...

		devc->soft_mute_flag = val;

		temp = val & VNC_INTERNAL_SPKR;
		if (temp != devc->mute_state) {
//			printk("MIXER_PRIVATE1: mute_mono(0x%X).\n",temp);
			vnc_mute(devc, temp);
		}

//		temp = devc->handset_state;

		// do not check if it is not already in
		// the right setting, since we are
		// laying about the current state...

//		if ((val & VNC_INTERNAL_MIC) != temp) {
			devc->handset_state = val & VNC_INTERNAL_MIC;
//			printk("MIXER_PRIVATE1: mixer_set(0x%X).\n",devc->handset_state);
			wa_mixer_set(devc, SOUND_MIXER_RECSRC, SOUND_MASK_MIC);
//			devc->handset_state = temp;
		}
		return 0;
	}
#if 0
	if (cmd == SOUND_MIXER_PRIVATE2) {
#define VNC_SOUND_PAUSE         0x53    //to pause the DSP
#define VNC_SOUND_RESUME        0x57    //to unpause the DSP
		int             val;

		val = *(int *) arg;

		printk("MIXER_PRIVATE2: passed parameter = 0x%X.\n",val);

		if (val == VNC_SOUND_PAUSE) {
			wa_sendcmd(0x16);    //PAUSE the ADC
		} else if (val == VNC_SOUND_RESUME) {
			wa_sendcmd(0x18);    //RESUME the ADC
		} else {
			return -EINVAL;      //invalid parameters...
		}
		return 0;
	}
#endif

	if (cmd == SOUND_MIXER_PRIVATE3) {
		long unsigned   flags;
		int             mixer_reg[15];	//reg 14 is actually a command: read,write,reset

		int             val;
		int             i;

		val = *(int *) arg;

		if (verify_area(VERIFY_READ, (void *) val, sizeof(mixer_reg) == -EFAULT))
			return (-EFAULT);

		memcpy_fromfs(&mixer_reg, (void *) val, sizeof(mixer_reg));

		if (mixer_reg[0x0E] == MIXER_PRIVATE3_RESET) { 	//reset command??
			wavnc_mixer_reset(devc);
			return (0);
		} else if (mixer_reg[0x0E] == MIXER_PRIVATE3_WRITE) {	//write command??
//			printk("WaveArtist Mixer: Private write command.\n");

			wa_sendcmd(0x32);	//Pair1 - word 1 and 5
			wa_sendcmd(mixer_reg[0]);
			wa_sendcmd(mixer_reg[4]);

			wa_sendcmd(0x32);	//Pair2 - word 2 and 6
			wa_sendcmd(mixer_reg[1]);
			wa_sendcmd(mixer_reg[5]);

			wa_sendcmd(0x32);	//Pair3 - word 3 and 7
			wa_sendcmd(mixer_reg[2]);
			wa_sendcmd(mixer_reg[6]);

			wa_sendcmd(0x32);	//Pair4 - word 4 and 8
			wa_sendcmd(mixer_reg[3]);
			wa_sendcmd(mixer_reg[7]);

			wa_sendcmd(0x32);	//Pair5 - word 9 and 10
			wa_sendcmd(mixer_reg[8]);
			wa_sendcmd(mixer_reg[9]);

			wa_sendcmd(0x0031);		//set left and right PCM
			wa_sendcmd(mixer_reg[0x0A]);
			wa_sendcmd(mixer_reg[0x0B]);

			wa_sendcmd(0x0131);		//set left and right FM
			wa_sendcmd(mixer_reg[0x0C]);
			wa_sendcmd(mixer_reg[0x0D]);

			return 0;
		} else if (mixer_reg[0x0E] == MIXER_PRIVATE3_READ) {	//read command?
//			printk("WaveArtist Mixer: Private read command.\n");

			//first read all current values...
			save_flags(flags);
			cli();

			for (i = 0; i < 14; i++) {
				wa_sendcmd((i << 8) + 0x30);	// get ready for command nn30H

				while (!(inb(STATR) & CMD_RF)) {
				};	//wait for response ready...

				mixer_reg[i] = inw(CMDR);
			}
			restore_flags(flags);

			if (verify_area(VERIFY_WRITE, (void *) val, sizeof(mixer_reg) == -EFAULT))
				return (-EFAULT);

			memcpy_tofs((void *) val, &mixer_reg, sizeof(mixer_reg));
			return 0;
		} else
			return -EINVAL;
	}
#endif
	if (((cmd >> 8) & 0xff) == 'M') {
		if (_SIOC_DIR(cmd) & _SIOC_WRITE) {
			int val;

			if (get_user(val, (int *)arg))
				return -EFAULT;

			/*
			 * special case for master volume: if we
			 * received this call - switch from hw
			 * volume control to a software volume
			 * control, till the hw volume is modified
			 * to signal that user wants to be back in
			 * hardware...
			 */
			if ((cmd & 0xff) == SOUND_MIXER_VOLUME)
				devc->use_slider = 0;

			return waveartist_mixer_set(devc, cmd & 0xff, val);
		} else {
			int ret;

			/*
			 * Return parameters
			 */
			switch (cmd & 0xff) {
			case SOUND_MIXER_RECSRC:
				ret = devc->recmask;

				if (devc->handset_state & VNC_INTERNAL_MIC)
					ret |= SOUND_MASK_SPEAKER;
				break;

			case SOUND_MIXER_DEVMASK:
				ret = devc->supported_devices;
				break;

			case SOUND_MIXER_STEREODEVS:
				ret = devc->supported_devices &
					~(SOUND_MASK_SPEAKER|SOUND_MASK_IMIX);
				break;

			case SOUND_MIXER_RECMASK:
				ret = devc->rec_devices;
				break;

			case SOUND_MIXER_CAPS:
				ret = SOUND_CAP_EXCL_INPUT;
				break;

			default:
				if ((cmd & 0xff) < SOUND_MIXER_NRDEVICES)
					ret = devc->levels[cmd & 0xff];
				else
					return -EINVAL;
			}

			return put_user(ret, (int *)arg) ? -EINVAL : 0;
		}
	}

	return -ENOIOCTLCMD;
}

static struct mixer_operations waveartist_mixer_operations =
{
	"WaveArtist",
	"WaveArtist NetWinder",
	waveartist_mixer_ioctl
};

static int
waveartist_init(wavnc_info *devc)
{
	wavnc_port_info *portc;
	char rev[3], dev_name[64];
	int my_dev;

	waveartist_reset(devc);

	sprintf(dev_name, "%s (%s", devc->hw.name, devc->chip_name);

	if (waveartist_getrev(&devc->hw, rev)) {
		strcat(dev_name, " rev. ");
		strcat(dev_name, rev);
	}
	strcat(dev_name, ")");

	conf_printf2(dev_name, devc->hw.io_base, devc->hw.irq,
		     devc->hw.dma, devc->hw.dma2);

	portc = (wavnc_port_info *)kmalloc(sizeof(wavnc_port_info), GFP_KERNEL);
	if (portc == NULL)
		goto nomem;

	memset(portc, 0, sizeof(wavnc_port_info));

	my_dev = sound_install_audiodrv(AUDIO_DRIVER_VERSION, dev_name,
			&waveartist_audio_driver, sizeof(struct audio_driver),
			devc->audio_flags, AFMT_U8 | AFMT_S16_LE | AFMT_S8,
			devc, devc->hw.dma, devc->hw.dma2);

	if (my_dev < 0)
		goto free;

	audio_devs[my_dev]->portc = portc;

	waveartist_mixer_reset(devc);

	/*
	 * clear any pending interrupt
	 */
	waveartist_iack(devc);

	if (request_irq(devc->hw.irq, waveartist_intr, 0, devc->hw.name, devc) < 0) {
		printk("%s: IRQ %d in use\n",
			devc->hw.name, devc->hw.irq);
		goto uninstall;
	}

	if (sound_alloc_dma(devc->hw.dma, devc->hw.name)) {
		printk("%s: Can't allocate DMA%d\n",
			devc->hw.name, devc->hw.dma);
		goto uninstall_irq;
	}

	if (devc->hw.dma != devc->hw.dma2 && devc->hw.dma2 != NO_DMA)
		if (sound_alloc_dma(devc->hw.dma2, devc->hw.name)) {
			printk("%s: can't allocate DMA%d\n",
				devc->hw.name, devc->hw.dma2);
			goto uninstall_dma;
		}

	waveartist_set_ctlr(&devc->hw, 0, DMA1_IE | DMA0_IE);

	audio_devs[my_dev]->mixer_dev =
		sound_install_mixer(MIXER_DRIVER_VERSION,
				dev_name,
				&waveartist_mixer_operations,
				sizeof(struct mixer_operations),
				devc);

	return my_dev;

uninstall_dma:
	sound_free_dma(devc->hw.dma);

uninstall_irq:
	free_irq(devc->hw.irq, devc);

uninstall:
	sound_unload_audiodev(my_dev);

free:
	kfree(portc);

nomem:
	return -1;
}

/*
 * Corel Netwinder specifics...
 */
static void
vnc_mute(wavnc_info *devc, int mute)
{
	extern spinlock_t gpio_lock;
	unsigned long flags;

	spin_lock_irqsave(&gpio_lock, flags);
	cpld_modify(CPLD_UNMUTE, mute ? 0 : CPLD_UNMUTE);
	spin_unlock_irqrestore(&gpio_lock, flags);

	devc->mute_state = mute;
}

static int
vnc_volume_slider(wavnc_info *devc)
{
	static signed int old_slider_volume;
	unsigned long flags;
	signed int volume = 255;

	*CSR_TIMER1_LOAD = 0x00ffffff;

	save_flags(flags);
	cli();

	outb(0xFF, 0x201);
	*CSR_TIMER1_CNTL = TIMER_CNTL_ENABLE | TIMER_CNTL_DIV1;

	while (volume && (inb(0x201) & 0x01))
		volume--;

	*CSR_TIMER1_CNTL = 0;

	restore_flags(flags);
	
	volume = 0x00ffffff - *CSR_TIMER1_VALUE;


#ifndef REVERSE
	volume = 150 - (volume >> 5);
#else
	volume = (volume >> 6) - 25;
#endif

	if (volume < 0)
		volume = 0;

	if (volume > 100)
		volume = 100;

	/*
	 * slider quite often reads +-8, so debounce this random noise
	 */
	if ((volume - old_slider_volume) > 7 ||
	    (old_slider_volume - volume) > 7) {
		old_slider_volume = volume;

		DEB(printk("Slider volume: %d.\n", old_slider_volume));
	}

	return old_slider_volume;
}

static int
vnc_slider(wavnc_info *devc)
{
	signed int slider_volume;
	unsigned int temp;

	/*
	 * read the "buttons" state.
	 *  Bit 4 = handset present,
	 *  Bit 5 = offhook
	 */
	// the state should be "querable" via a private IOCTL call
	temp = inb(0x201) & 0x30;

	if (!devc->handset_mute_sw &&
	    (temp ^ devc->handset_state) & VNC_INTERNAL_MIC) {
		devc->handset_state = temp;
		devc->handset_mute_sw = 0;

		vnc_mute(devc, (temp & VNC_INTERNAL_MIC) ? 1 : 0);
	}

	slider_volume = vnc_volume_slider(devc);

	/*
	 * If we're using software controlled volume, and
	 * the slider moves by more than 20%, then we
	 * switch back to slider controlled volume.
	 */
	if (devc->slider_vol > slider_volume) {
		if (devc->slider_vol - slider_volume > 20)
			devc->use_slider = 1;
	} else {
		if (slider_volume - devc->slider_vol > 20)
			devc->use_slider = 1;
	}

	/*
	 * use only left channel
	 */
	temp = levels[SOUND_MIXER_VOLUME] & 0xFF;

	if (slider_volume != temp && devc->use_slider) {
		devc->slider_vol = slider_volume;

		waveartist_mixer_set(devc, SOUND_MIXER_VOLUME,
			slider_volume | slider_volume << 8);

		return 1;
	}

	return 0;
}

static void
vnc_slider_tick(unsigned long data)
{
	int next_timeout;

	if (vnc_slider(adev_info + data))
		next_timeout = 5;	// mixer reported change
	else
		next_timeout = VNC_TIMER_PERIOD;

	mod_timer(&vnc_timer, jiffies + next_timeout);
}

int
probe_waveartist(struct address_info *hw_config)
{
	wavnc_info *devc = &adev_info[nr_waveartist_devs];

	if (nr_waveartist_devs >= MAX_AUDIO_DEV) {
		printk("waveartist: too many audio devices\n");
		return 0;
	}

	if (check_region(hw_config->io_base, 15))  {
		printk("WaveArtist: I/O port conflict\n");
		return 0;
	}

	if (hw_config->irq > 31 || hw_config->irq < 16) {
		printk("WaveArtist: Bad IRQ %d\n", hw_config->irq);
		return 0;
	}

	if (hw_config->dma != 3) {
		printk("WaveArtist: Bad DMA %d\n", hw_config->dma);
		return 0;
	}

	hw_config->name = "WaveArtist";
	devc->hw = *hw_config;
	devc->open_mode = 0;
	devc->chip_name = "RWA-010";

	return 1;
}

void
attach_waveartist(struct address_info *hw)
{
	wavnc_info *devc = &adev_info[nr_waveartist_devs];

	/*
	 * NOTE! If irq < 0, there is another driver which has allocated the
	 *   IRQ so that this driver doesn't need to allocate/deallocate it.
	 *   The actually used IRQ is ABS(irq).
	 */
	devc->hw = *hw;
	devc->hw.irq = (hw->irq > 0) ? hw->irq : 0;
	devc->open_mode = 0;
	devc->playback_dev = 0;
	devc->record_dev = 0;
	devc->audio_flags = DMA_AUTOMODE;
	devc->levels = levels;

	if (hw->dma != hw->dma2 && hw->dma2 != NO_DMA)
		devc->audio_flags |= DMA_DUPLEX;

	request_region(hw->io_base, 15, devc->hw.name);

	devc->dev_no = waveartist_init(devc);

	if (devc->dev_no < 0)
		release_region(hw->io_base, 15);
	else {
		init_timer(&vnc_timer);
		vnc_timer.function = vnc_slider_tick;
		vnc_timer.expires  = jiffies;
		vnc_timer.data     = nr_waveartist_devs;
		add_timer(&vnc_timer);

		nr_waveartist_devs += 1;

		vnc_mute(devc, 0);
	}
}

void
unload_waveartist(struct address_info *hw)
{
	wavnc_info *devc = NULL;
	int i;

	for (i = 0; i < nr_waveartist_devs; i++)
		if (hw->io_base == adev_info[i].hw.io_base) {
			devc = adev_info + i;
			break;
		}

	if (devc != NULL) {
		int mixer;

		release_region(devc->hw.io_base, 15);

		waveartist_set_ctlr(&devc->hw, DMA1_IE|DMA0_IE, 0);

		if (devc->hw.irq >= 0)
			free_irq(devc->hw.irq, devc);

		sound_free_dma(devc->hw.dma);

		if (devc->hw.dma != devc->hw.dma2 &&
		    devc->hw.dma2 != NO_DMA)
			sound_free_dma(devc->hw.dma2);

		del_timer(&vnc_timer);

		mixer = audio_devs[devc->dev_no]->mixer_dev;

		if (mixer >= 0)
			sound_unload_mixerdev(mixer);

		if (devc->dev_no >= 0)
			sound_unload_audiodev(devc->dev_no);

		nr_waveartist_devs -= 1;

		for (; i < nr_waveartist_devs; i++)
			adev_info[i] = adev_info[i + 1];
	} else
		printk("waveartist: can't find device to unload\n");
}

#ifdef MODULE

MODULE_PARM(io, "i");		/* IO base */
MODULE_PARM(irq, "i");		/* IRQ */
MODULE_PARM(dma, "i");		/* DMA */
MODULE_PARM(dma2, "i");		/* DMA2 */

int io   = CONFIG_WAVEARTIST_BASE;
int irq  = CONFIG_WAVEARTIST_IRQ;
int dma  = CONFIG_WAVEARTIST_DMA;
int dma2 = CONFIG_WAVEARTIST_DMA2;

static int attached;

struct address_info hw_config;

int init_module(void)
{
	hw_config.io_base = io;
	hw_config.irq = irq;
	hw_config.dma = dma;
	hw_config.dma2 = dma2;

	if (!probe_waveartist(&hw_config))
		return -ENODEV;

	attach_waveartist(&hw_config);
	attached = 1;

	SOUND_LOCK;
	return 0;
}

void cleanup_module(void)
{
	if (attached) {
		SOUND_LOCK_END;

		unload_waveartist(&hw_config);
	}
}
#endif
