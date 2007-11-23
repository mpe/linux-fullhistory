/*
 * sound/sb_common.c
 *
 * Common routines for Sound Blaster compatible cards.
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
/*
 * Daniel J. Rodriksson: Modified sbintr to handle 8 and 16 bit interrupts
 *                       for full duplex support ( only sb16 by now )
 */
#include <linux/config.h>
#include <linux/delay.h>
#include <asm/init.h>

#include "sound_config.h"
#include "sound_firmware.h"

#ifdef CONFIG_SBDSP

#ifndef CONFIG_AUDIO
#error You will need to configure the sound driver with CONFIG_AUDIO option.
#endif

#include "sb_mixer.h"
#include "sb.h"

static sb_devc *detected_devc = NULL;	/* For communication from probe to init */
static sb_devc *last_devc = NULL;	/* For MPU401 initialization */

static unsigned char jazz_irq_bits[] = {
	0, 0, 2, 3, 0, 1, 0, 4, 0, 2, 5, 0, 0, 0, 0, 6
};

static unsigned char jazz_dma_bits[] = {
	0, 1, 0, 2, 0, 3, 0, 4
};

/*
 * Jazz16 chipset specific control variables
 */

static int jazz16_base = 0;		/* Not detected */
static unsigned char jazz16_bits = 0;	/* I/O relocation bits */

/*
 * Logitech Soundman Wave specific initialization code
 */

#ifdef SMW_MIDI0001_INCLUDED
#include "smw-midi0001.h"
#else
static unsigned char *smw_ucode = NULL;
static int      smw_ucodeLen = 0;

#endif

sb_devc *last_sb = NULL;		/* Last sb loaded */

int sb_dsp_command(sb_devc * devc, unsigned char val)
{
	int i;
	unsigned long limit;

	limit = jiffies + HZ / 10;	/* Timeout */
	
	/*
	 * Note! the i<500000 is an emergency exit. The sb_dsp_command() is sometimes
	 * called while interrupts are disabled. This means that the timer is
	 * disabled also. However the timeout situation is a abnormal condition.
	 * Normally the DSP should be ready to accept commands after just couple of
	 * loops.
	 */

	for (i = 0; i < 500000 && (limit-jiffies)>0; i++)
	{
		if ((inb(DSP_STATUS) & 0x80) == 0)
		{
			outb((val), DSP_COMMAND);
			return 1;
		}
	}
	printk(KERN_WARNING "Sound Blaster:  DSP command(%x) timeout.\n", val);
	return 0;
}

static int sb_dsp_get_byte(sb_devc * devc)
{
	int i;

	for (i = 1000; i; i--)
	{
		if (inb(DSP_DATA_AVAIL) & 0x80)
			return inb(DSP_READ);
	}
	return 0xffff;
}

int ess_write(sb_devc * devc, unsigned char reg, unsigned char data)
{
	/* Write a byte to an extended mode register of ES1688 */

	if (!sb_dsp_command(devc, reg))
		return 0;

	return sb_dsp_command(devc, data);
}

int ess_read(sb_devc * devc, unsigned char reg)
{
/* Read a byte from an extended mode register of ES1688 */
	if (!sb_dsp_command(devc, 0xc0))	/* Read register command */
		return -1;

	if (!sb_dsp_command(devc, reg))
		return -1;

	return sb_dsp_get_byte(devc);
}

static void sbintr(int irq, void *dev_id, struct pt_regs *dummy)
{
	int status;
	unsigned char   src = 0xff;

	sb_devc *devc = dev_id;

	devc->irq_ok = 1;
	if (devc->model == MDL_SB16)
	{
		src = sb_getmixer(devc, IRQ_STAT);	/* Interrupt source register */

#if defined(CONFIG_MIDI)&& defined(CONFIG_UART401)
		if (src & 4)
			uart401intr(devc->irq, devc->midi_irq_cookie, NULL);	/* MPU401 interrupt */
#endif

		if (!(src & 3))
			return;	/* Not a DSP interrupt */
	}
	if (devc->intr_active && (!devc->fullduplex || (src & 0x01)))
	{
		switch (devc->irq_mode)
		{
			case IMODE_OUTPUT:
				DMAbuf_outputintr(devc->dev, 1);
				break;

			case IMODE_INPUT:
				DMAbuf_inputintr(devc->dev);
				break;

			case IMODE_INIT:
				break;

			case IMODE_MIDI:
#ifdef CONFIG_MIDI
				sb_midi_interrupt(devc);
#endif
				break;

			default:
				/* printk(KERN_WARN "Sound Blaster: Unexpected interrupt\n"); */
				;
		}
	}
	else if (devc->intr_active_16 && (src & 0x02))
	{
		switch (devc->irq_mode_16)
		{
			case IMODE_OUTPUT:
				DMAbuf_outputintr(devc->dev, 1);
				break;

			case IMODE_INPUT:
				DMAbuf_inputintr(devc->dev);
				break;

			case IMODE_INIT:
				break;

			default:
				/* printk(KERN_WARN "Sound Blaster: Unexpected interrupt\n"); */
				;
		}
	}
	/*
	 * Acknowledge interrupts 
	 */

	if (src & 0x01)
		status = inb(DSP_DATA_AVAIL);

	if (devc->model == MDL_SB16 && src & 0x02)
		status = inb(DSP_DATA_AVL16);
}


int sb_dsp_reset(sb_devc * devc)
{
	int loopc;

	DEB(printk("Entered sb_dsp_reset()\n"));

	if (devc->model == MDL_ESS)
		outb(3, DSP_RESET);	/* Reset FIFO too */
	else
		outb(1, DSP_RESET);

	udelay(10);
	outb(0, DSP_RESET);
	udelay(30);

	for (loopc = 0; loopc < 1000 && !(inb(DSP_DATA_AVAIL) & 0x80); loopc++);

	if (inb(DSP_READ) != 0xAA)
	{
		DDB(printk("sb: No response to RESET\n"));
		return 0;	/* Sorry */
	}
	if (devc->model == MDL_ESS)
		sb_dsp_command(devc, 0xc6);	/* Enable extended mode */

	DEB(printk("sb_dsp_reset() OK\n"));
	return 1;
}

static void dsp_get_vers(sb_devc * devc)
{
	int i;

	unsigned long   flags;

	DDB(printk("Entered dsp_get_vers()\n"));
	save_flags(flags);
	cli();
	devc->major = devc->minor = 0;
	sb_dsp_command(devc, 0xe1);	/* Get version */

	for (i = 100000; i; i--)
	{
		if (inb(DSP_DATA_AVAIL) & 0x80)
		{
			if (devc->major == 0)
				devc->major = inb(DSP_READ);
			else
			{
				devc->minor = inb(DSP_READ);
				break;
			}
		}
	}
	DDB(printk("DSP version %d.%d\n", devc->major, devc->minor));
	restore_flags(flags);
}

static int sb16_set_dma_hw(sb_devc * devc)
{
	int bits;

	if (devc->dma8 != 0 && devc->dma8 != 1 && devc->dma8 != 3)
	{
		printk(KERN_ERR "SB16: Invalid 8 bit DMA (%d)\n", devc->dma8);
		return 0;
	}
	bits = (1 << devc->dma8);

	if (devc->dma16 >= 5 && devc->dma16 <= 7)
		bits |= (1 << devc->dma16);

	sb_setmixer(devc, DMA_NR, bits);
	return 1;
}

#if defined(CONFIG_MIDI) && defined(CONFIG_UART401)
static void sb16_set_mpu_port(sb_devc * devc, struct address_info *hw_config)
{
	/*
	 * This routine initializes new MIDI port setup register of SB Vibra (CT2502).
	 */
	unsigned char   bits = sb_getmixer(devc, 0x84) & ~0x06;

	switch (hw_config->io_base)
	{
		case 0x300:
			sb_setmixer(devc, 0x84, bits | 0x04);
			break;

		case 0x330:
			sb_setmixer(devc, 0x84, bits | 0x00);
			break;

		default:
			sb_setmixer(devc, 0x84, bits | 0x02);		/* Disable MPU */
			printk(KERN_ERR "SB16: Invalid MIDI I/O port %x\n", hw_config->io_base);
	}
}
#endif

static int sb16_set_irq_hw(sb_devc * devc, int level)
{
	int ival;

	switch (level)
	{
		case 5:
			ival = 2;
			break;
		case 7:
			ival = 4;
			break;
		case 9:
			ival = 1;
			break;
		case 10:
			ival = 8;
			break;
		default:
			printk(KERN_ERR "SB16: Invalid IRQ%d\n", level);
			return 0;
	}
	sb_setmixer(devc, IRQ_NR, ival);
	return 1;
}

static void relocate_Jazz16(sb_devc * devc, struct address_info *hw_config)
{
	unsigned char bits = 0;
	unsigned long flags;

	if (jazz16_base != 0 && jazz16_base != hw_config->io_base)
		return;

	switch (hw_config->io_base)
	{
		case 0x220:
			bits = 1;
			break;
		case 0x240:
			bits = 2;
			break;
		case 0x260:
			bits = 3;
			break;
		default:
			return;
	}
	bits = jazz16_bits = bits << 5;
	jazz16_base = hw_config->io_base;

	/*
	 *	Magic wake up sequence by writing to 0x201 (aka Joystick port)
	 */
	save_flags(flags);
	cli();
	outb((0xAF), 0x201);
	outb((0x50), 0x201);
	outb((bits), 0x201);
	restore_flags(flags);
}

static int init_Jazz16(sb_devc * devc, struct address_info *hw_config)
{
	char name[100];
	/*
	 * First try to check that the card has Jazz16 chip. It identifies itself
	 * by returning 0x12 as response to DSP command 0xfa.
	 */

	if (!sb_dsp_command(devc, 0xfa))
		return 0;

	if (sb_dsp_get_byte(devc) != 0x12)
		return 0;

	/*
	 * OK so far. Now configure the IRQ and DMA channel used by the card.
	 */
	if (hw_config->irq < 1 || hw_config->irq > 15 || jazz_irq_bits[hw_config->irq] == 0)
	{
		printk(KERN_ERR "Jazz16: Invalid interrupt (IRQ%d)\n", hw_config->irq);
		return 0;
	}
	if (hw_config->dma < 0 || hw_config->dma > 3 || jazz_dma_bits[hw_config->dma] == 0)
	{
		  printk(KERN_ERR "Jazz16: Invalid 8 bit DMA (DMA%d)\n", hw_config->dma);
		  return 0;
	}
	if (hw_config->dma2 < 0)
	{
		printk(KERN_ERR "Jazz16: No 16 bit DMA channel defined\n");
		return 0;
	}
	if (hw_config->dma2 < 5 || hw_config->dma2 > 7 || jazz_dma_bits[hw_config->dma2] == 0)
	{
		printk(KERN_ERR "Jazz16: Invalid 16 bit DMA (DMA%d)\n", hw_config->dma2);
		return 0;
	}
	devc->dma16 = hw_config->dma2;

	if (!sb_dsp_command(devc, 0xfb))
		return 0;

	if (!sb_dsp_command(devc, jazz_dma_bits[hw_config->dma] |
			(jazz_dma_bits[hw_config->dma2] << 4)))
		return 0;

	if (!sb_dsp_command(devc, jazz_irq_bits[hw_config->irq]))
		return 0;

	/*
	 * Now we have configured a standard Jazz16 device. 
	 */
	devc->model = MDL_JAZZ;
	strcpy(name, "Jazz16");

	hw_config->name = "Jazz16";
	devc->caps |= SB_NO_MIDI;
	return 1;
}

static void relocate_ess1688(sb_devc * devc)
{
	unsigned char bits;

	switch (devc->base)
	{
		case 0x220:
			bits = 0x04;
			break;
		case 0x230:
			bits = 0x05;
			break;
		case 0x240:
			bits = 0x06;
			break;
		case 0x250:
			bits = 0x07;
			break;
		default:
			return;	/* Wrong port */
	}

	DDB(printk("Doing ESS1688 address selection\n"));
	
	/*
	 * ES1688 supports two alternative ways for software address config.
	 * First try the so called Read-Sequence-Key method.
	 */

	/* Reset the sequence logic */
	inb(0x229);
	inb(0x229);
	inb(0x229);

	/* Perform the read sequence */
	inb(0x22b);
	inb(0x229);
	inb(0x22b);
	inb(0x229);
	inb(0x229);
	inb(0x22b);
	inb(0x229);

	/* Select the base address by reading from it. Then probe using the port. */
	inb(devc->base);
	if (sb_dsp_reset(devc))	/* Bingo */
		return;

#if 0				/* This causes system lockups (Nokia 386/25 at least) */
	/*
	 * The last resort is the system control register method.
	 */

	outb((0x00), 0xfb);	/* 0xFB is the unlock register */
	outb((0x00), 0xe0);	/* Select index 0 */
	outb((bits), 0xe1);	/* Write the config bits */
	outb((0x00), 0xf9);	/* 0xFB is the lock register */
#endif
}

static int ess_init(sb_devc * devc, struct address_info *hw_config)
{
	unsigned char cfg, irq_bits = 0, dma_bits = 0;
	int ess_major = 0, ess_minor = 0;
	int i;
	static char name[100];

	/*
	 * Try to detect ESS chips.
	 */

	sb_dsp_command(devc, 0xe7);	/* Return identification */

	for (i = 1000; i; i--)
	{
		if (inb(DSP_DATA_AVAIL) & 0x80)
		{
			if (ess_major == 0)
				ess_major = inb(DSP_READ);
			else
			{
				ess_minor = inb(DSP_READ);
				break;
			}
		}
	}

	if (ess_major == 0)
		return 0;

	if (ess_major == 0x48 && (ess_minor & 0xf0) == 0x80)
	{
		sprintf(name, "ESS ES488 AudioDrive (rev %d)",
			  ess_minor & 0x0f);
		hw_config->name = name;
		devc->model = MDL_SBPRO;
		return 1;
	}
	else if (ess_major == 0x68 && (ess_minor & 0xf0) == 0x80)
	{
		char *chip = "ES688";

		if ((ess_minor & 0x0f) >= 8)
			chip = "ES1688";

		sprintf(name,"ESS %s AudioDrive (rev %d)",
			chip, ess_minor & 0x0f);
	}
	else
		strcpy(name, "Jazz16");

	devc->model = MDL_ESS;
	devc->submodel = ess_minor & 0x0f;
	hw_config->name = name;
	sb_dsp_reset(devc);	/* Turn on extended mode */

	/*
	 *    Set IRQ configuration register
	 */

	cfg = 0x50;		/* Enable only DMA counter interrupt */

	switch (devc->irq)
	{
		case 2:
		case 9:
			irq_bits = 0;
			break;

		case 5:
			irq_bits = 1;
			break;

		case 7:
			irq_bits = 2;
			break;

		case 10:
			irq_bits = 3;
			break;

		default:
			irq_bits = 0;
			cfg = 0x10;	/* Disable all interrupts */
			printk(KERN_ERR "ESS1688: Invalid IRQ %d\n", devc->irq);
			return 0;
	}

	if (!ess_write(devc, 0xb1, cfg | (irq_bits << 2)))
		printk(KERN_ERR "ESS1688: Failed to write to IRQ config register\n");

	/*
	 *    Set DMA configuration register
	 */

	cfg = 0x50;		/* Extended mode DMA enable */

	if (devc->dma8 > 3 || devc->dma8 < 0 || devc->dma8 == 2)
	{
		dma_bits = 0;
		cfg = 0x00;	/* Disable all DMA */
		printk(KERN_ERR "ESS1688: Invalid DMA %d\n", devc->dma8);
	}
	else
	{
		if (devc->dma8 == 3)
			dma_bits = 3;
		else
			dma_bits = devc->dma8 + 1;
	}

	if (!ess_write(devc, 0xb2, cfg | (dma_bits << 2)))
		printk(KERN_ERR "ESS1688: Failed to write to DMA config register\n");

	/*
	 *	Enable joystick and OPL3
	 */

	cfg = sb_getmixer(devc, 0x40);
	sb_setmixer(devc, 0x40, cfg | 0x03);
	if (devc->submodel >= 8)	/* ES1688 */
		devc->caps |= SB_NO_MIDI;	/* ES1688 uses MPU401 MIDI mode */
	sb_dsp_reset(devc);
	return 1;
}

int sb_dsp_detect(struct address_info *hw_config)
{
	sb_devc sb_info;
	sb_devc *devc = &sb_info;

	memset((char *) &sb_info, 0, sizeof(sb_info));	/* Zero everything */
	sb_info.my_mididev = -1;
	sb_info.my_mixerdev = -1;
	sb_info.my_dev = -1;

	/*
	 * Initialize variables 
	 */
	
	DDB(printk("sb_dsp_detect(%x) entered\n", hw_config->io_base));
	if (check_region(hw_config->io_base, 16))
	{
#ifdef MODULE
		printk(KERN_INFO "sb: I/O region in use.\n");
#endif
		return 0;
	}

	devc->type = hw_config->card_subtype;

	devc->base = hw_config->io_base;
	devc->irq = hw_config->irq;
	devc->dma8 = hw_config->dma;

	devc->dma16 = -1;

	if (acer)
	{
		cli();
		inb(devc->base + 0x09);
		inb(devc->base + 0x09);
		inb(devc->base + 0x09);
		inb(devc->base + 0x0b);
		inb(devc->base + 0x09);
		inb(devc->base + 0x0b);
		inb(devc->base + 0x09);
		inb(devc->base + 0x09);
		inb(devc->base + 0x0b);
		inb(devc->base + 0x09);
		inb(devc->base + 0x00);
		sti();
	}
	/*
	 * Detect the device
	 */

	if (sb_dsp_reset(devc))
		dsp_get_vers(devc);
	else
		devc->major = 0;

	if (devc->type == 0 || devc->type == MDL_JAZZ || devc->type == MDL_SMW)
		if (devc->major == 0 || (devc->major == 3 && devc->minor == 1))
			relocate_Jazz16(devc, hw_config);

	if (devc->major == 0 && (devc->type == MDL_ESS || devc->type == 0))
		relocate_ess1688(devc);

	if (!sb_dsp_reset(devc))
	{
		DDB(printk("SB reset failed\n"));
#ifdef MODULE
		printk(KERN_INFO "sb: dsp reset failed.\n");
#endif
		return 0;
	}
	if (devc->major == 0)
		dsp_get_vers(devc);

	if (devc->major == 3 && devc->minor == 1)
	{
		if (devc->type == MDL_AZTECH)		/* SG Washington? */
		{
			if (sb_dsp_command(devc, 0x09))
				if (sb_dsp_command(devc, 0x00))	/* Enter WSS mode */
				{
					int i;

					/* Have some delay */
					for (i = 0; i < 10000; i++)
						inb(DSP_DATA_AVAIL);
					devc->caps = SB_NO_AUDIO | SB_NO_MIDI;	/* Mixer only */
					devc->model = MDL_AZTECH;
				}
		}
	}
	/*
	 * Save device information for sb_dsp_init()
	 */


	detected_devc = (sb_devc *)kmalloc(sizeof(sb_devc), GFP_KERNEL);
	if (detected_devc == NULL)
	{
		printk(KERN_ERR "sb: Can't allocate memory for device information\n");
		return 0;
	}
	memcpy((char *) detected_devc, (char *) devc, sizeof(sb_devc));
	MDB(printk("SB %d.%d detected OK (%x)\n", devc->major, devc->minor, hw_config->io_base));
	return 1;
}

int sb_dsp_init(struct address_info *hw_config)
{
	sb_devc *devc;
	char name[100];
	extern int sb_be_quiet;
	int	mixer22, mixer30;
	
/*
 * Check if we had detected a SB device earlier
 */
	DDB(printk("sb_dsp_init(%x) entered\n", hw_config->io_base));
	name[0] = 0;

	if (detected_devc == NULL)
	{
		MDB(printk("No detected device\n"));
		return 0;
	}
	devc = detected_devc;
	detected_devc = NULL;

	if (devc->base != hw_config->io_base)
	{
		DDB(printk("I/O port mismatch\n"));
		return 0;
	}
	/*
	 * Now continue initialization of the device
	 */

	devc->caps = hw_config->driver_use_1;

	if (!(devc->caps & SB_NO_AUDIO && devc->caps & SB_NO_MIDI) && hw_config->irq > 0)
	{			/* IRQ setup */
		if (request_irq(hw_config->irq, sbintr, 0, "soundblaster", devc) < 0)
		{
			printk(KERN_ERR "SB: Can't allocate IRQ%d\n", hw_config->irq);
			return 0;
		}
		devc->irq_ok = 0;

		if (devc->major == 4)
			if (!sb16_set_irq_hw(devc, devc->irq))	/* Unsupported IRQ */
			{
				free_irq(devc->irq, devc);
				return 0;
			}
		if ((devc->type == 0 || devc->type == MDL_ESS) &&
			devc->major == 3 && devc->minor == 1)
		{		/* Handle various chipsets which claim they are SB Pro compatible */
			if ((devc->type != 0 && devc->type != MDL_ESS) ||
				!ess_init(devc, hw_config))
			{
				if ((devc->type != 0 && devc->type != MDL_JAZZ &&
					 devc->type != MDL_SMW) || !init_Jazz16(devc, hw_config))
				{
					DDB(printk("This is a genuine SB Pro\n"));
				}
			}
		}
#if defined(__SMP__)
		/* Skip IRQ detection if SMP (doesn't work) */
		devc->irq_ok = 1;
#else
		if (devc->major == 4 && devc->minor <= 11 )	/* Won't work */
			devc->irq_ok = 1;
		else
		{
			int n;

			for (n = 0; n < 3 && devc->irq_ok == 0; n++)
			{
				if (sb_dsp_command(devc, 0xf2))	/* Cause interrupt immediately */
				{
					int i;

					for (i = 0; !devc->irq_ok && i < 10000; i++);
				}
			}
			if (!devc->irq_ok)
				printk(KERN_WARNING "sb: Interrupt test on IRQ%d failed - Probable IRQ conflict\n", devc->irq);
			else
			{
				DDB(printk("IRQ test OK (IRQ%d)\n", devc->irq));
			}
		}
#endif				/* __SMP__ */
	}			/* IRQ setup */
	request_region(hw_config->io_base, 16, "soundblaster");

	last_sb = devc;
	
	switch (devc->major)
	{
		case 1:		/* SB 1.0 or 1.5 */
			devc->model = hw_config->card_subtype = MDL_SB1;
			break;

		case 2:		/* SB 2.x */
			if (devc->minor == 0)
				devc->model = hw_config->card_subtype = MDL_SB2;
			else
				devc->model = hw_config->card_subtype = MDL_SB201;
			break;

		case 3:		/* SB Pro and most clones */
			if (devc->model == 0)
			{
				devc->model = hw_config->card_subtype = MDL_SBPRO;
				if (hw_config->name == NULL)
					hw_config->name = "Sound Blaster Pro (8 BIT ONLY)";
			}
			break;

		case 4:
			devc->model = hw_config->card_subtype = MDL_SB16;
			/* 
			 * ALS007 and ALS100 return DSP version 4.2 and have 2 post-reset !=0
			 * registers at 0x3c and 0x4c (output ctrl registers on ALS007) whereas
			 * a "standard" SB16 doesn't have a register at 0x4c.  ALS100 actively
			 * updates register 0x22 whenever 0x30 changes, as per the SB16 spec.
			 * Since ALS007 doesn't, this can be used to differentiate the 2 cards.
			 */
			if ((devc->minor == 2) && sb_getmixer(devc,0x3c) && sb_getmixer(devc,0x4c)) 
			{
				mixer30 = sb_getmixer(devc,0x30);
				sb_setmixer(devc,0x22,(mixer22=sb_getmixer(devc,0x22)) & 0x0f);
				sb_setmixer(devc,0x30,0xff);
				/* ALS100 will force 0x30 to 0xf8 like SB16; ALS007 will allow 0xff. */
				/* Register 0x22 & 0xf0 on ALS100 == 0xf0; on ALS007 it == 0x10.     */
				if ((sb_getmixer(devc,0x30) != 0xff) || ((sb_getmixer(devc,0x22) & 0xf0) != 0x10)) 
				{
					if (hw_config->name == NULL)
						hw_config->name = "Sound Blaster 16 (ALS-100)";
        			}
        			else
        			{
        				sb_setmixer(devc,0x3c,0x1f);    /* Enable all inputs */
					sb_setmixer(devc,0x4c,0x1f);
					sb_setmixer(devc,0x22,mixer22); /* Restore 0x22 to original value */
					devc->submodel = SUBMDL_ALS007;
					if (hw_config->name == NULL)
						hw_config->name = "Sound Blaster 16 (ALS-007)";
				}
				sb_setmixer(devc,0x30,mixer30);
			}
			else if (hw_config->name == NULL)
				hw_config->name = "Sound Blaster 16";

			if (hw_config->dma2 == -1)
				devc->dma16 = devc->dma8;
			else if (hw_config->dma2 < 5 || hw_config->dma2 > 7)
			{
				printk(KERN_WARNING  "SB16: Bad or missing 16 bit DMA channel\n");
				devc->dma16 = devc->dma8;
			}
			else
				devc->dma16 = hw_config->dma2;

			if(!sb16_set_dma_hw(devc)) {
				free_irq(devc->irq, devc);
			        release_region(hw_config->io_base, 16);
				return 0;
			}

			devc->caps |= SB_NO_MIDI;
	}

	if (!(devc->caps & SB_NO_MIXER))
		if (devc->major == 3 || devc->major == 4)
			sb_mixer_init(devc);

#ifdef CONFIG_MIDI
	if (!(devc->caps & SB_NO_MIDI))
		sb_dsp_midi_init(devc);
#endif

	if (hw_config->name == NULL)
		hw_config->name = "Sound Blaster (8 BIT/MONO ONLY)";

	sprintf(name, "%s (%d.%d)", hw_config->name, devc->major, devc->minor);
	conf_printf(name, hw_config);

	/*
	 * Assuming that a sound card is Sound Blaster (compatible) is the most common
	 * configuration error and the mother of all problems. Usually sound cards
	 * emulate SB Pro but in addition they have a 16 bit native mode which should be
	 * used in Unix. See Readme.cards for more information about configuring OSS/Free
	 * properly.
	 */
	if (devc->model <= MDL_SBPRO)
	{
		if (devc->major == 3 && devc->minor != 1)	/* "True" SB Pro should have v3.1 (rare ones may have 3.2). */
		{
			printk(KERN_INFO "This sound card may not be fully Sound Blaster Pro compatible.\n");
			printk(KERN_INFO "In many cases there is another way to configure OSS so that\n");
			printk(KERN_INFO "it works properly with OSS (for example in 16 bit mode).\n");
			printk(KERN_INFO "Please ignore this message if you _really_ have a SB Pro.\n");
		}
		else if (!sb_be_quiet && devc->model == MDL_SBPRO)
		{
			printk(KERN_INFO "SB DSP version is just %d.%d which means that your card is\n", devc->major, devc->minor);
			printk(KERN_INFO "several years old (8 bit only device) or alternatively the sound driver\n");
			printk(KERN_INFO "is incorrectly configured.\n");
		}
	}
	hw_config->card_subtype = devc->model;
	hw_config->slots[0]=devc->dev;
	last_devc = devc;	/* For SB MPU detection */

	if (!(devc->caps & SB_NO_AUDIO) && devc->dma8 >= 0)
	{
		if (sound_alloc_dma(devc->dma8, "SoundBlaster8"))
		{
			printk(KERN_WARNING "Sound Blaster: Can't allocate 8 bit DMA channel %d\n", devc->dma8);
		}
		if (devc->dma16 >= 0 && devc->dma16 != devc->dma8)
		{
			if (sound_alloc_dma(devc->dma16, "SoundBlaster16"))
				printk(KERN_WARNING "Sound Blaster:  can't allocate 16 bit DMA channel %d.\n", devc->dma16);
		}
		sb_audio_init(devc, name);
		hw_config->slots[0]=devc->dev;
	}
	else
	{
		MDB(printk("Sound Blaster:  no audio devices found.\n"));
	}
	return 1;
}

void sb_dsp_disable_midi(int io_base)
{
}

void sb_dsp_disable_recording(int io_base)
{
}

/* if (sbmpu) below we allow mpu401 to manage the midi devs
   otherwise we have to unload them. (Andrzej Krzysztofowicz) */
   
void sb_dsp_unload(struct address_info *hw_config, int sbmpu)
{
	sb_devc *devc;

	devc = audio_devs[hw_config->slots[0]]->devc;

	if (devc && devc->base == hw_config->io_base)
	{
		release_region(devc->base, 16);

		if (!(devc->caps & SB_NO_AUDIO))
		{
			sound_free_dma(devc->dma8);
			if (devc->dma16 >= 0)
				sound_free_dma(devc->dma16);
		}
		if (!(devc->caps & SB_NO_AUDIO && devc->caps & SB_NO_MIDI))
		{
			if (devc->irq > 0);
				free_irq(devc->irq, devc);

			sound_unload_mixerdev(devc->my_mixerdev);
			/* We don't have to do this bit any more the UART401 is its own
				master  -- Krzysztof Halasa */
			/* But we have to do it, if UART401 is not detected */
			if (!sbmpu)
				sound_unload_mididev(devc->my_mididev);
			sound_unload_audiodev(devc->my_dev);
		}
		kfree(devc);
	}
	else
		release_region(hw_config->io_base, 16);
	if(detected_devc)
		kfree(detected_devc);
}

/*
 *	Mixer access routines
 */

void sb_setmixer(sb_devc * devc, unsigned int port, unsigned int value)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	outb(((unsigned char) (port & 0xff)), MIXER_ADDR);

	udelay(20);
	outb(((unsigned char) (value & 0xff)), MIXER_DATA);
	udelay(20);
	restore_flags(flags);
}

unsigned int sb_getmixer(sb_devc * devc, unsigned int port)
{
	unsigned int val;
	unsigned long flags;

	save_flags(flags);
	cli();
	outb(((unsigned char) (port & 0xff)), MIXER_ADDR);

	udelay(20);
	val = inb(MIXER_DATA);
	udelay(20);
	restore_flags(flags);

	return val;
}

#ifdef CONFIG_MIDI

/*
 *	MPU401 MIDI initialization.
 */

static void smw_putmem(sb_devc * devc, int base, int addr, unsigned char val)
{
	unsigned long flags;

	save_flags(flags);
	cli();

	outb((addr & 0xff), base + 1);	/* Low address bits */
	outb((addr >> 8), base + 2);	/* High address bits */
	outb((val), base);	/* Data */

	restore_flags(flags);
}

static unsigned char smw_getmem(sb_devc * devc, int base, int addr)
{
	unsigned long flags;
	unsigned char val;

	save_flags(flags);
	cli();

	outb((addr & 0xff), base + 1);	/* Low address bits */
	outb((addr >> 8), base + 2);	/* High address bits */
	val = inb(base);	/* Data */

	restore_flags(flags);
	return val;
}

static int smw_midi_init(sb_devc * devc, struct address_info *hw_config)
{
	int mpu_base = hw_config->io_base;
	int mp_base = mpu_base + 4;		/* Microcontroller base */
	int i;
	unsigned char control;


	/*
	 *  Reset the microcontroller so that the RAM can be accessed
	 */

	control = inb(mpu_base + 7);
	outb((control | 3), mpu_base + 7);	/* Set last two bits to 1 (?) */
	outb(((control & 0xfe) | 2), mpu_base + 7);	/* xxxxxxx0 resets the mc */

	mdelay(3);	/* Wait at least 1ms */

	outb((control & 0xfc), mpu_base + 7);	/* xxxxxx00 enables RAM */

	/*
	 *  Detect microcontroller by probing the 8k RAM area
	 */
	smw_putmem(devc, mp_base, 0, 0x00);
	smw_putmem(devc, mp_base, 1, 0xff);
	udelay(10);

	if (smw_getmem(devc, mp_base, 0) != 0x00 || smw_getmem(devc, mp_base, 1) != 0xff)
	{
		DDB(printk("SM Wave: No microcontroller RAM detected (%02x, %02x)\n", smw_getmem(devc, mp_base, 0), smw_getmem(devc, mp_base, 1)));
		return 0;	/* No RAM */
	}
	/*
	 *  There is RAM so assume it's really a SM Wave
	 */

	devc->model = MDL_SMW;
	smw_mixer_init(devc);

#ifdef MODULE
	if (!smw_ucode)
	{
		extern void *smw_free;

		smw_ucodeLen = mod_firmware_load("/etc/sound/midi0001.bin", (void *) &smw_ucode);
		smw_free = smw_ucode;
	}
#endif
	if (smw_ucodeLen > 0)
	{
		if (smw_ucodeLen != 8192)
		{
			printk(KERN_ERR "SM Wave: Invalid microcode (MIDI0001.BIN) length\n");
			return 1;
		}
		/*
		 *  Download microcode
		 */

		for (i = 0; i < 8192; i++)
			smw_putmem(devc, mp_base, i, smw_ucode[i]);

		/*
		 *  Verify microcode
		 */

		for (i = 0; i < 8192; i++)
			if (smw_getmem(devc, mp_base, i) != smw_ucode[i])
			{
				printk(KERN_ERR "SM Wave: Microcode verification failed\n");
				return 0;
			}
	}
	control = 0;
#ifdef SMW_SCSI_IRQ
	/*
	 * Set the SCSI interrupt (IRQ2/9, IRQ3 or IRQ10). The SCSI interrupt
	 * is disabled by default.
	 *
	 * FIXME - make this a module option
	 *
	 * BTW the Zilog 5380 SCSI controller is located at MPU base + 0x10.
	 */
	{
		static unsigned char scsi_irq_bits[] = {
			0, 0, 3, 1, 0, 0, 0, 0, 0, 3, 2, 0, 0, 0, 0, 0
		};
		control |= scsi_irq_bits[SMW_SCSI_IRQ] << 6;
	}
#endif

#ifdef SMW_OPL4_ENABLE
	/*
	 *  Make the OPL4 chip visible on the PC bus at 0x380.
	 *
	 *  There is no need to enable this feature since this driver
	 *  doesn't support OPL4 yet. Also there is no RAM in SM Wave so
	 *  enabling OPL4 is pretty useless.
	 */
	control |= 0x10;	/* Uses IRQ12 if bit 0x20 == 0 */
	/* control |= 0x20;      Uncomment this if you want to use IRQ7 */
#endif
	outb((control | 0x03), mpu_base + 7);	/* xxxxxx11 restarts */
	hw_config->name = "SoundMan Wave";
	return 1;
}

static int ess_midi_init(sb_devc * devc, struct address_info *hw_config)
{
	unsigned char   cfg, tmp;

	cfg = sb_getmixer(devc, 0x40) & 0x03;

	if (devc->submodel < 8)
	{
		sb_setmixer(devc, 0x40, cfg | 0x03);	/* Enable OPL3 & joystick */
		return 0;	/* ES688 doesn't support MPU401 mode */
	}
	tmp = (hw_config->io_base & 0x0f0) >> 4;

	if (tmp > 3)
	{
		sb_setmixer(devc, 0x40, cfg);
		return 0;
	}
	cfg |= tmp << 3;

	tmp = 1;		/* MPU enabled without interrupts */

	/* May be shared: if so the value is -ve */
	
	switch(abs(hw_config->irq))
	{
		case 9:
			tmp = 0x4;
			break;
		case 5:
			tmp = 0x5;
			break;
		case 7:
			tmp = 0x6;
			break;
		case 10:
			tmp = 0x7;
			break;
		default:
			return 0;
	}

	cfg |= tmp << 5;
	sb_setmixer(devc, 0x40, cfg | 0x03);
	return 1;
}

static int init_Jazz16_midi(sb_devc * devc, struct address_info *hw_config)
{
	int mpu_base = hw_config->io_base;
	int sb_base = devc->base;
	int irq = hw_config->irq;

	unsigned char bits = 0;
	unsigned long flags;

	if (irq < 0)
		irq *= -1;

	if (irq < 1 || irq > 15 ||
	    jazz_irq_bits[irq] == 0)
	{
		printk(KERN_ERR "Jazz16: Invalid MIDI interrupt (IRQ%d)\n", irq);
		return 0;
	}
	switch (sb_base)
	{
		case 0x220:
			bits = 1;
			break;
		case 0x240:
			bits = 2;
			break;
		case 0x260:
			bits = 3;
			break;
		default:
			return 0;
	}
	bits = jazz16_bits = bits << 5;
	switch (mpu_base)
	{
		case 0x310:
			bits |= 1;
			break;
		case 0x320:
			bits |= 2;
			break;
		case 0x330:
			bits |= 3;
			break;
		default:
			printk(KERN_ERR "Jazz16: Invalid MIDI I/O port %x\n", mpu_base);
			return 0;
	}
	/*
	 *	Magic wake up sequence by writing to 0x201 (aka Joystick port)
	 */
	save_flags(flags);
	cli();
	outb(0xAF, 0x201);
	outb(0x50, 0x201);
	outb(bits, 0x201);
	restore_flags(flags);

	hw_config->name = "Jazz16";
	smw_midi_init(devc, hw_config);

	if (!sb_dsp_command(devc, 0xfb))
		return 0;

	if (!sb_dsp_command(devc, jazz_dma_bits[devc->dma8] |
			    (jazz_dma_bits[devc->dma16] << 4)))
		return 0;

	if (!sb_dsp_command(devc, jazz_irq_bits[devc->irq] |
			    (jazz_irq_bits[irq] << 4)))
		return 0;

	return 1;
}

void attach_sbmpu(struct address_info *hw_config)
{
#if defined(CONFIG_MIDI) && defined(CONFIG_UART401)
	attach_uart401(hw_config);
	last_sb->midi_irq_cookie=midi_devs[hw_config->slots[4]]->devc;
#endif
}

int probe_sbmpu(struct address_info *hw_config)
{
#if defined(CONFIG_MIDI) && defined(CONFIG_UART401)
	sb_devc *devc = last_devc;

	if (last_devc == NULL)
		return 0;

	last_devc = 0;

	if (hw_config->io_base <= 0)
		return 0;

	if (check_region(hw_config->io_base, 4))
	{
		printk(KERN_ERR "sbmpu: I/O port conflict (%x)\n", hw_config->io_base);
		return 0;
	}
	switch (devc->model)
	{
		case MDL_SB16:
			if (hw_config->io_base != 0x300 && hw_config->io_base != 0x330)
			{
				printk(KERN_ERR "SB16: Invalid MIDI port %x\n", hw_config->io_base);
				return 0;
			}
			hw_config->name = "Sound Blaster 16";
			hw_config->irq = -devc->irq;
			if (devc->minor > 12)		/* What is Vibra's version??? */
				sb16_set_mpu_port(devc, hw_config);
			break;

		case MDL_ESS:
			if (hw_config->irq < 3 || hw_config->irq == devc->irq)
				hw_config->irq = -devc->irq;
			if (!ess_midi_init(devc, hw_config))
				return 0;
			hw_config->name = "ESS ES1688";
			break;

		case MDL_JAZZ:
			if (hw_config->irq < 3 || hw_config->irq == devc->irq)
				hw_config->irq = -devc->irq;
			if (!init_Jazz16_midi(devc, hw_config))
				return 0;
			break;

		default:
			return 0;
	}
	return probe_uart401(hw_config);
#else
	return 0;
#endif
}

void unload_sbmpu(struct address_info *hw_config)
{
#if defined(CONFIG_MIDI) && defined(CONFIG_UART401)
	unload_uart401(hw_config);
#endif
}
#else				/* !CONFIG_MIDI */

void unload_sbmpu(struct address_info *hw_config)
{
}

int probe_sbmpu(struct address_info *hw_config)
{
	return 0;
}

void attach_sbmpu(struct address_info *hw_config)
{
}
#endif
#endif
