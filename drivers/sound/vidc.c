/*
 *	drivers/sound/vidc.c
 *
 *	Detection routine for the VIDC.
 *
 *	Copyright (C) 1997 by Russell King <rmk@arm.uk.linux.org>
 */

#include <linux/module.h>
#include <linux/kernel.h>

#include <asm/io.h>
#include <asm/dma.h>
#include "sound_config.h"
#include "soundmodule.h"
#include "vidc.h"

int vidc_busy;

void vidc_update_filler(int format, int channels)
{
	int fillertype;

#define TYPE(fmt,ch) (((fmt)<<2) | ((ch)&3))

	fillertype = TYPE(format, channels);
printk("filler type: %X\n", fillertype);
	switch (fillertype)
	{
		default:
		case TYPE(AFMT_U8, 1):
			vidc_filler = vidc_fill_1x8_u;
			break;

		case TYPE(AFMT_U8, 2):
			vidc_filler = vidc_fill_2x8_u;
			break;

		case TYPE(AFMT_S8, 1):
			vidc_filler = vidc_fill_1x8_s;
			break;

		case TYPE(AFMT_S8, 2):
			vidc_filler = vidc_fill_2x8_s;
			break;

		case TYPE(AFMT_S16_LE, 1):
			vidc_filler = vidc_fill_1x16_s;
			break;

		case TYPE(AFMT_S16_LE, 2):
			vidc_filler = vidc_fill_2x16_s;
			break;
	}
}

void attach_vidc(struct address_info *hw_config)
{
	char name[32];
	int i;

	sprintf(name, "VIDC %d-bit sound", hw_config->card_subtype);
	conf_printf(name, hw_config);

	for (i = 0; i < 2; i++)
	{
		dma_buf[i] = get_free_page(GFP_KERNEL);
		dma_pbuf[i] = virt_to_phys(dma_buf[i]);
	}

	if (sound_alloc_dma(hw_config->dma, "VIDCsound"))
	{
		printk(KERN_ERR "VIDCsound: can't allocate virtual DMA channel\n");
		return;
	}
	if (request_irq(hw_config->irq, vidc_sound_dma_irq, 0, "VIDCsound", &dma_start))
	{
		printk(KERN_ERR "VIDCsound: can't allocate DMA interrupt\n");
		return;
	}
//	vidc_synth_init(hw_config);
	vidc_audio_init(hw_config);
	vidc_mixer_init(hw_config);
}

int probe_vidc(struct address_info *hw_config)
{
	hw_config->irq = IRQ_DMAS0;
	hw_config->dma = DMA_VIRTUAL_SOUND;
	hw_config->dma2 = -1;
	hw_config->card_subtype = 16;
	return 1;
}

void unload_vidc(struct address_info *hw_config)
{
	int i;

	free_irq(hw_config->irq, NULL);
	sound_free_dma(hw_config->dma);

	for (i = 0; i < 2; i++)
		free_page(dma_buf[i]);
}

#ifdef MODULE
static struct address_info config;

int init_module(void)
{
	if (probe_vidc(&config) == 0)
		return -ENODEV;
	printk("VIDC 16-bit serial sound\n");
	SOUND_LOCK;
	attach_vidc(&config);
	return 0;
}

void cleanup_module(void)
{
	unload_vidc(&config);
	SOUND_LOCK_END;
}

#endif
