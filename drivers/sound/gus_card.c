/*
 * sound/gus_card.c
 *
 * Detection routine for the Gravis Ultrasound.
 */
 
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
/*
 * Frank van de Pol : Fixed GUS MAX interrupt handling, enabled simultanious
 *                    usage of CS4231A codec, GUS wave and MIDI for GUS MAX.
 *
 * Status:
 *              Tested... 
 */
      
 
#include <linux/config.h>
#include <linux/module.h>

#include "sound_config.h"
#include "soundmodule.h"

#ifdef CONFIG_GUS

#include "gus_hw.h"

void            gusintr(int irq, void *dev_id, struct pt_regs *dummy);

int             gus_base = 0, gus_irq = 0, gus_dma = 0;
int             gus_no_wave_dma = 0; 
extern int      gus_wave_volume;
extern int      gus_pcm_volume;
extern int      have_gus_max;
int             gus_pnp_flag = 0;
#ifdef CONFIG_GUS16
static int      db16 = 0;	/* Has a Gus16 AD1848 on it */
#endif

void attach_gus_card(struct address_info *hw_config)
{
	if(request_irq(hw_config->irq, gusintr, 0,  "Gravis Ultrasound", hw_config)<0)
		printk(KERN_ERR "gus_card.c: Unable to allocate IRQ %d\n", hw_config->irq);

	gus_wave_init(hw_config);

	request_region(hw_config->io_base, 16, "GUS");
	request_region(hw_config->io_base + 0x100, 12, "GUS");	/* 0x10c-> is MAX */

	if (sound_alloc_dma(hw_config->dma, "GUS"))
		printk(KERN_ERR "gus_card.c: Can't allocate DMA channel %d\n", hw_config->dma);
	if (hw_config->dma2 != -1 && hw_config->dma2 != hw_config->dma)
		if (sound_alloc_dma(hw_config->dma2, "GUS(2)"))
			printk(KERN_ERR "gus_card.c: Can't allocate DMA channel %d\n", hw_config->dma2);
#ifdef CONFIG_MIDI
	gus_midi_init(hw_config);
#endif
}

int probe_gus(struct address_info *hw_config)
{
	int             irq;
	int             io_addr;

	if (hw_config->card_subtype == 1)
		gus_pnp_flag = 1;

	irq = hw_config->irq;

	if (hw_config->card_subtype == 0)	/* GUS/MAX/ACE */
		if (irq != 3 && irq != 5 && irq != 7 && irq != 9 &&
		    irq != 11 && irq != 12 && irq != 15)
		  {
			  printk(KERN_ERR "GUS: Unsupported IRQ %d\n", irq);
			  return 0;
		  }
	if (check_region(hw_config->io_base, 16))
		printk(KERN_ERR "GUS: I/O range conflict (1)\n");
	else if (check_region(hw_config->io_base + 0x100, 16))
		printk(KERN_ERR "GUS: I/O range conflict (2)\n");
	else if (gus_wave_detect(hw_config->io_base))
		return 1;

#ifndef EXCLUDE_GUS_IODETECT

	/*
	 * Look at the possible base addresses (0x2X0, X=1, 2, 3, 4, 5, 6)
	 */

	for (io_addr = 0x210; io_addr <= 0x260; io_addr += 0x10)
		if (io_addr != hw_config->io_base)	/*
							 * Already tested
							 */
			if (!check_region(io_addr, 16))
				if (!check_region(io_addr + 0x100, 16))
					if (gus_wave_detect(io_addr))
					  {
						  hw_config->io_base = io_addr;
						  return 1;
					  }
#endif

	return 0;
}

void unload_gus(struct address_info *hw_config)
{
	DDB(printk("unload_gus(%x)\n", hw_config->io_base));

	gus_wave_unload(hw_config);

	release_region(hw_config->io_base, 16);
	release_region(hw_config->io_base + 0x100, 12);		/* 0x10c-> is MAX */
	free_irq(hw_config->irq, hw_config);

	sound_free_dma(hw_config->dma);

	if (hw_config->dma2 != -1 && hw_config->dma2 != hw_config->dma)
		sound_free_dma(hw_config->dma2);
}

void gusintr(int irq, void *dev_id, struct pt_regs *dummy)
{
	unsigned char src;
	extern int gus_timer_enabled;

	sti();

#ifdef CONFIG_GUSMAX
	if (have_gus_max) {
		struct address_info *hw_config = dev_id;
		adintr(irq, (void *)hw_config->slots[1], NULL);
	}
#endif
#ifdef CONFIG_GUS16
	if (db16) {
		struct address_info *hw_config = dev_id;
		adintr(irq, (void *)hw_config->slots[3], NULL);
	}
#endif

	while (1)
	{
		if (!(src = inb(u_IrqStatus)))
			return;

		if (src & DMA_TC_IRQ)
		{
			guswave_dma_irq();
		}
		if (src & (MIDI_TX_IRQ | MIDI_RX_IRQ))
		{
#ifdef CONFIG_MIDI
			gus_midi_interrupt(0);
#endif
		}
		if (src & (GF1_TIMER1_IRQ | GF1_TIMER2_IRQ))
		{
#ifdef CONFIG_SEQUENCER
			if (gus_timer_enabled)
				sound_timer_interrupt();
			gus_write8(0x45, 0);	/* Ack IRQ */
			gus_timer_command(4, 0x80);		/* Reset IRQ flags */
#else
			gus_write8(0x45, 0);	/* Stop timers */
#endif
		}
		if (src & (WAVETABLE_IRQ | ENVELOPE_IRQ))
			gus_voice_irq();
	}
}

#endif

/*
 *	Some extra code for the 16 bit sampling option
 */

#ifdef CONFIG_GUS16

int probe_gus_db16(struct address_info *hw_config)
{
	return ad1848_detect(hw_config->io_base, NULL, hw_config->osp);
}

void attach_gus_db16(struct address_info *hw_config)
{
#ifdef CONFIG_GUS
	gus_pcm_volume = 100;
	gus_wave_volume = 90;
#endif

	hw_config->slots[3] = ad1848_init("GUS 16 bit sampling", hw_config->io_base,
					  hw_config->irq,
					  hw_config->dma,
					  hw_config->dma, 0,
					  hw_config->osp);
}

void unload_gus_db16(struct address_info *hw_config)
{

	ad1848_unload(hw_config->io_base,
		      hw_config->irq,
		      hw_config->dma,
		      hw_config->dma, 0);
	sound_unload_audiodev(hw_config->slots[3]);
}
#endif



#ifdef MODULE

static struct address_info config;

/*
 *    Note DMA2 of -1 has the right meaning in the GUS driver as well
 *      as here. 
 */

int             io = -1;
int             irq = -1;
int             dma = -1;
int             dma16 = -1;	/* Set this for modules that need it */
int             type = 0;	/* 1 for PnP */
int             gus16 = 0;
#ifdef CONFIG_GUSMAX
static int      no_wave_dma = 0;/* Set if no dma is to be used for the 
				   wave table (GF1 chip) */
#endif

MODULE_PARM(io, "i");
MODULE_PARM(irq, "i");
MODULE_PARM(dma, "i");
MODULE_PARM(dma16, "i");
MODULE_PARM(type, "i");
MODULE_PARM(gus16, "i");
#ifdef CONFIG_GUSMAX
MODULE_PARM(no_wave_dma, "i");
#endif
#ifdef CONFIG_GUS16
MODULE_PARM(db16, "i");
#endif

int init_module(void)
{
	printk(KERN_INFO "Gravis Ultrasound audio driver Copyright (C) by Hannu Savolainen 1993-1996\n");

	if (io == -1 || dma == -1 || irq == -1)
	{
		printk(KERN_ERR "I/O, IRQ, and DMA are mandatory\n");
		return -EINVAL;
	}
	config.io_base = io;
	config.irq = irq;
	config.dma = dma;
	config.dma2 = dma16;
	config.card_subtype = type;
	
#ifdef CONFIG_GUSMAX
	gus_no_wave_dma = no_wave_dma;
#endif

#if defined(CONFIG_GUS16)
	if (probe_gus_db16(&config) && gus16)
	{
		attach_gus_db16(&config);
		db16 = 1;
	}	
#endif
	if (probe_gus(&config) == 0)
		return -ENODEV;
	attach_gus_card(&config);
	SOUND_LOCK;
	return 0;
}

void cleanup_module(void)
{
#if defined(CONFIG_GUS16)
	if (db16)
		unload_gus_db16(&config);
#endif
	unload_gus(&config);
	SOUND_LOCK_END;
}

#endif
