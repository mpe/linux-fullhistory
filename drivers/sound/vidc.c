/*
 *	drivers/sound/vidc.c
 *
 *	Detection routine for the VIDC.
 *
 *	Copyright (C) 1997 by Russell King <rmk@arm.uk.linux.org>
 */

#include <asm/io.h>
#include <asm/dma.h>
#include "sound_config.h"
#include "vidc.h"

int vidc_busy;

void vidc_update_filler(int bits, int channels)
{
	int filltype;

	filltype = bits + channels;
	switch (filltype)
	{
		default:
		case 9:
			vidc_filler = vidc_fill_1x8;
			break;
		case 10:
			vidc_filler = vidc_fill_2x8;
			break;
		case 17:
			vidc_filler = vidc_fill_1x16;
			break;
		case 18:
			vidc_filler = vidc_fill_2x16;
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
	if (request_irq(hw_config->irq, vidc_sound_dma_irq, 0, "VIDCsound", NULL))
	{
		printk(KERN_ERR "VIDCsound: can't allocate DMA interrupt\n");
		return;
	}
	vidc_synth_init(hw_config);
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
	free_irq(hw_config->irq, NULL);
	sound_free_dma(hw_config->dma);
}
