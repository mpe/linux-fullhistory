/*
 * sound/sb_card.c
 *
 * Detection routine for the Sound Blaster cards.
 *
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

#ifdef CONFIG_SBDSP

#include "sb_mixer.h"
#include "sb.h"

void attach_sb_card(struct address_info *hw_config)
{
#if defined(CONFIG_AUDIO) || defined(CONFIG_MIDI)
	if(!sb_dsp_init(hw_config))
		hw_config->slots[0] = -1;
#endif
}

int probe_sb(struct address_info *hw_config)
{
	if (check_region(hw_config->io_base, 16))
	{
		printk(KERN_ERR "sb_card: I/O port %x is already in use\n\n", hw_config->io_base);
		return 0;
	}
	return sb_dsp_detect(hw_config);
}

void unload_sb(struct address_info *hw_config)
{
	if(hw_config->slots[0]!=-1)
		sb_dsp_unload(hw_config);
}

int sb_be_quiet=0;

#ifdef MODULE

static struct address_info config;
static struct address_info config_mpu;

/*
 *    Note DMA2 of -1 has the right meaning in the SB16 driver as well
 *      as here. It will cause either an error if it is needed or a fallback
 *      to the 8bit channel.
 */

int mpu_io = 0;
int io = -1;
int irq = -1;
int dma = -1;
int dma16 = -1;	/* Set this for modules that need it */
int type = 0;	/* Can set this to a specific card type */
int mad16 = 0;	/* Set mad16=1 to load this as support for mad16 */
int trix = 0;	/* Set trix=1 to load this as support for trix */
int pas2 = 0;	/* Set pas2=1 to load this as support for pas2 */
int sm_games = 0;	/* Mixer - see sb_mixer.c */
int acer = 0;	/* Do acer notebook init */

MODULE_PARM(io, "i");
MODULE_PARM(irq, "i");
MODULE_PARM(dma, "i");
MODULE_PARM(dma16, "i");
MODULE_PARM(mpu_io, "i");
MODULE_PARM(type, "i");
MODULE_PARM(mad16, "i");
MODULE_PARM(trix, "i");
MODULE_PARM(pas2, "i");
MODULE_PARM(sm_games, "i");

static int sbmpu = 0;

void *smw_free = NULL;

int init_module(void)
{
	printk(KERN_INFO "Soundblaster audio driver Copyright (C) by Hannu Savolainen 1993-1996\n");

	if (mad16 == 0 && trix == 0 && pas2 == 0)
	{
		if (io == -1 || dma == -1 || irq == -1)
		{
			printk(KERN_ERR "sb_card: I/O, IRQ, and DMA are mandatory\n");
			return -EINVAL;
		}
		config.io_base = io;
		config.irq = irq;
		config.dma = dma;
		config.dma2 = dma16;
		config.card_subtype = type;

		if (!probe_sb(&config))
			return -ENODEV;
		attach_sb_card(&config);
		
		if(config.slots[0]==-1)
			return -ENODEV;
#ifdef CONFIG_MIDI
		config_mpu.io_base = mpu_io;
		if (mpu_io && probe_sbmpu(&config_mpu))
			sbmpu = 1;
#endif
#ifdef CONFIG_MIDI
		if (sbmpu)
			attach_sbmpu(&config_mpu);
#endif
	}
	SOUND_LOCK;
	return 0;
}

void cleanup_module(void)
{
	if (smw_free)
		vfree(smw_free);
	if (!mad16 && !trix && !pas2)
		unload_sb(&config);
	if (sbmpu)
		unload_sbmpu(&config_mpu);
	SOUND_LOCK_END;
}

#else

#ifdef CONFIG_SM_GAMES
int             sm_games = 1;
#else
int             sm_games = 0;
#endif
#ifdef CONFIG_SB_ACER
int             acer = 1;
#else
int             acer = 0;
#endif
#endif

EXPORT_SYMBOL(sb_dsp_init);
EXPORT_SYMBOL(sb_dsp_detect);
EXPORT_SYMBOL(sb_dsp_unload);
EXPORT_SYMBOL(sb_dsp_disable_midi);
EXPORT_SYMBOL(attach_sb_card);
EXPORT_SYMBOL(probe_sb);
EXPORT_SYMBOL(unload_sb);
EXPORT_SYMBOL(sb_be_quiet);

#endif
