/*
 * drivers/sound/vidc_synth.c
 *
 * Synthesizer routines for the VIDC
 *
 * Copyright (C) 1997 Russell King <rmk@arm.uk.linux.org>
 */

#include "sound_config.h"
#include "vidc.h"
#if 0
static struct synth_info vidc_info =
{
	"VIDCsound",		/* name                 */
	0,			/* device               */
	SYNTH_TYPE_SAMPLE,	/* synth_type           */
	0,			/* synth_subtype        */
	0,			/* perc_mode            */
	16,			/* nr_voices            */
	0,			/* nr_drums             */
	0,			/* instr_bank_size      */
	0,			/* capabilities         */
};

int             vidc_sdev;
int             vidc_synth_volume;

static int vidc_synth_open(int dev, int mode)
{
	if (vidc_busy)
		return -EBUSY;

	vidc_busy = 1;
	return 0;
}

static void vidc_synth_close(int dev)
{
	vidc_busy = 0;
}


static struct synth_operations vidc_synth_operations =
{
	"VIDC Synth",		/* name			*/
	&vidc_info,		/* info 		*/
	0,			/* midi_dev		*/
	SYNTH_TYPE_SAMPLE,	/* synth_type		*/
	/*SAMPLE_TYPE_XXX */ 0, /* synth_subtype	*/
	vidc_synth_open,	/* open 		*/
	vidc_synth_close,	/* close		*/
	NULL,			/* ioctl		*/
	NULL,			/* kill_note		*/
	NULL,			/* start_note		*/
	NULL,			/* set_instr		*/
	NULL,			/* reset		*/
	NULL,			/* hw_control		*/
	NULL,			/* load_patch		*/
	NULL,			/* aftertouch		*/
	NULL,			/* controller		*/
	NULL,			/* panning		*/
	NULL,			/* volume_method	*/
	NULL,			/* bender		*/
	NULL,			/* alloc_voice		*/
	NULL,			/* setup_voice		*/
	NULL,			/* send_sysex		*/
				/* alloc		*/
				/* chn_info[16] 	*/
				/* syex_buf		*/
				/* syex_ptr		*/
};

int  vidc_synth_get_volume(void)
{
	return vidc_synth_volume;
}

int vidc_synth_set_volume(int newvol)
{
	return vidc_synth_volume = newvol;
}

void vidc_synth_init(struct address_info *hw_config)
{
	vidc_synth_volume = 100 | (100 << 8);
	if ((vidc_sdev=sound_alloc_synthdev())!=-1)
		synth_devs[vidc_sdev] = &vidc_synth_operations;
	else
		printk(KERN_ERR "VIDCsound: Too many synthesizers\n");
}
#endif
