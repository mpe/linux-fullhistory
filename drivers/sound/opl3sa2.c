/*
 * sound/opl3sa2.c
 *
 * A low level driver for Yamaha OPL3-SA[2,3,x] based cards.
 *
 * Scott Murray, Jun 14, 1998
 *
 *
 * Changes
 *      Paul J.Y. Lahaie        Changed probing / attach code order
 *
 */

/* Based on the CS4232 driver:
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */

#include <linux/config.h>
#include <linux/module.h>

#include "sound_config.h"
#include "soundmodule.h"

#ifdef CONFIG_OPL3SA2

int probe_opl3sa2_mpu(struct address_info *hw_config)
{
#if (defined(CONFIG_MPU401) || defined(CONFIG_MPU_EMU)) && defined(CONFIG_MIDI)
	return probe_mpu401(hw_config);
#else
	return 0;
#endif
}


void attach_opl3sa2_mpu(struct address_info *hw_config)
{
#if (defined(CONFIG_MPU401) || defined(CONFIG_MPU_EMU)) && defined(CONFIG_MIDI)
	attach_mpu401(hw_config);
#endif
}


void unload_opl3sa2_mpu(struct address_info *hw_config)
{
#if (defined(CONFIG_MPU401) || defined(CONFIG_MPU_EMU)) && defined(CONFIG_MIDI)
	unload_mpu401(hw_config);
#endif
}


int probe_opl3sa2_mss(struct address_info *hw_config)
{
	return probe_ms_sound(hw_config);
}


void attach_opl3sa2_mss(struct address_info *hw_config)
{
   	printk(KERN_INFO "opl3sa2.c: trying to init WSS\n");   
   
	attach_ms_sound(hw_config);

   	/* request_region(hw_config->io_base, 4, "Yamaha 7xx WSS Config"); */
   
	if (hw_config->slots[0] != -1 &&
	    audio_devs[hw_config->slots[0]]->mixer_dev != -1)
	{
  		AD1848_REROUTE(SOUND_MIXER_LINE1, SOUND_MIXER_CD);
  		AD1848_REROUTE(SOUND_MIXER_LINE2, SOUND_MIXER_SYNTH);
	        /* GSM! test the following: */
                AD1848_REROUTE(SOUND_MIXER_LINE3, SOUND_MIXER_LINE);
	}
}


void unload_opl3sa2_mss(struct address_info *hw_config)
{
        int mixer;

        /* Find mixer */
	mixer = audio_devs[hw_config->slots[0]]->mixer_dev;

        /* Unload MSS audio codec */
	unload_ms_sound(hw_config);

	sound_unload_audiodev(hw_config->slots[0]);

        /* Unload mixer if there */
	if(mixer >= 0)
	{
		sound_unload_mixerdev(mixer);
	}

        /* Release MSS config ports */
	release_region(hw_config->io_base, 4);
}


int probe_opl3sa2(struct address_info *hw_config)
{
	/*
	 * Verify that the I/O port range is free.
	 */

	printk(KERN_INFO "opl3sa2.c: Control using I/O port 0x%03x\n", hw_config->io_base);

	if (check_region(hw_config->io_base, 2))
	{
	    printk(KERN_ERR "opl3sa2.c: Control I/O port 0x%03x not free\n", hw_config->io_base);
	    return 0;
	}

	/* GSM!: Add some kind of other test here... */

	return 1;
}


void attach_opl3sa2(struct address_info *hw_config)
{
   	printk(KERN_INFO "opl3sa2.c: trying to init!\n");   
   
   	request_region(hw_config->io_base, 2, "Yamaha 7xx Control");

	/* GSM! Mixer stuff should go here... */
}   


void unload_opl3sa2(struct address_info *hw_config)
{
        /* Release control ports */
	release_region(hw_config->io_base, 2);

	/* GSM! Mixer stuff should go here... */
}


#ifdef MODULE

int io      = -1;
int mss_io  = -1;
int mpu_io  = -1;
int irq     = -1;
int dma     = -1;
int dma2    = -1;

MODULE_PARM(io,"i");
MODULE_PARM(mss_io,"i");
MODULE_PARM(mpu_io,"i");
MODULE_PARM(irq,"i");
MODULE_PARM(dma,"i");
MODULE_PARM(dma2,"i");

EXPORT_NO_SYMBOLS;

struct address_info cfg;
struct address_info mss_cfg;
struct address_info mpu_cfg;

/*
 *	Install a OPL3SA2 based card. Need to have ad1848 and mpu401
 *	loaded ready.
 */
int init_module(void)
{
        int i;

	if (io == -1 || irq == -1 || dma == -1 || dma2 == -1 || mss_io == -1)
	{
		printk(KERN_ERR "opl3sa2: io, mss_io, irq, dma, and dma2 must be set.\n");
		return -EINVAL;
	}
   
        /* Our own config: */
        cfg.io_base = io;
	cfg.irq     = irq;
	cfg.dma     = dma;
	cfg.dma2    = dma2;

        /* The MSS config: */
	mss_cfg.io_base      = mss_io;
	mss_cfg.irq          = irq;
	mss_cfg.dma          = dma;
	mss_cfg.dma2         = dma2;
	mss_cfg.card_subtype = 1;      /* No IRQ or DMA setup */

	/* Call me paranoid: */
	for(i = 0; i < 6; i++)
	{
	    cfg.slots[i] = mss_cfg.slots[i] = mpu_cfg.slots[i] = -1;
	}

	if (probe_opl3sa2(&cfg) == 0)
	{
	    return -ENODEV;
	}

        if (probe_opl3sa2_mss(&mss_cfg) == 0)
        {
            return -ENODEV;
        }

	attach_opl3sa2(&cfg);

	attach_opl3sa2_mss(&mss_cfg);

#if (defined(CONFIG_MPU401) || defined(CONFIG_MPU_EMU)) && defined(CONFIG_MIDI)
	if(mpu_io != -1)
	{
            /* MPU config: */
	    mpu_cfg.io_base       = mpu_io;
	    mpu_cfg.irq           = irq;
	    mpu_cfg.dma           = dma;
	    mpu_cfg.always_detect = 1;  /* It's there, so use shared IRQs */

	    if (probe_opl3sa2_mpu(&mpu_cfg))
	    {
	        attach_opl3sa2_mpu(&mpu_cfg);
	    }
	}
#endif
	SOUND_LOCK;
	return 0;
}


void cleanup_module(void)
{
#if (defined(CONFIG_MPU401) || defined(CONFIG_MPU_EMU)) && defined(CONFIG_MIDI)
        if(mpu_cfg.slots[1] != -1)
	{
            unload_opl3sa2_mpu(&mpu_cfg);
	}
#endif
	unload_opl3sa2_mss(&mss_cfg);
	unload_opl3sa2(&cfg);
	SOUND_LOCK_END;
}

#endif
#endif
