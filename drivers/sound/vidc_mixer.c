/*
 * drivers/sound/vidc_mixer.c
 *
 * Mixer routines for VIDC
 *
 * Copyright (C) 1997 Russell King <rmk@arm.uk.linux.org>
 */

#include "sound_config.h"
#include "vidc.h"

int vidc_volume;

static int vidc_get_volume(void)
{
	return vidc_volume;
}

static int vidc_set_volume(int newvol)
{
	vidc_volume = newvol;
/*  printk ("vidc_set_volume: %X\n", newvol); */
	return newvol;
}

static int vidc_default_mixer_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	int ret;

	switch (cmd)
	{
		case SOUND_MIXER_READ_VOLUME:
			ret = vidc_get_volume();
			break;

		case SOUND_MIXER_WRITE_VOLUME:
			if (get_user(ret, (int *) arg))
				return -EINVAL;
			ret = vidc_set_volume(ret);
			break;

		case SOUND_MIXER_READ_BASS:
		case SOUND_MIXER_WRITE_BASS:
		case SOUND_MIXER_READ_TREBLE:
		case SOUND_MIXER_WRITE_TREBLE:
			ret = 50;
			break;

		case SOUND_MIXER_READ_SYNTH:
//			ret = vidc_synth_get_volume();
			ret = 0;
			break;

		case SOUND_MIXER_WRITE_SYNTH:
			if (get_user(ret, (int *) arg))
				return -EINVAL;
//			ret = vidc_synth_set_volume(ret);
			ret = 0;
			break;

		case SOUND_MIXER_READ_PCM:
			ret = vidc_audio_get_volume();
			break;

		case SOUND_MIXER_WRITE_PCM:
			if (get_user(ret, (int *) arg))
				return -EINVAL;
			ret = vidc_audio_set_volume(ret);
			break;

		case SOUND_MIXER_READ_SPEAKER:
			ret = 100;
			break;

		case SOUND_MIXER_WRITE_SPEAKER:
			ret = 100;
			break;

		case SOUND_MIXER_READ_LINE:
		case SOUND_MIXER_WRITE_LINE:
		case SOUND_MIXER_READ_MIC:
		case SOUND_MIXER_WRITE_MIC:
			ret = 0;
			break;

		case SOUND_MIXER_READ_CD:
		case SOUND_MIXER_WRITE_CD:
			ret = 100 | (100 << 8);
			break;

		case SOUND_MIXER_READ_IMIX:
		case SOUND_MIXER_WRITE_IMIX:
		case SOUND_MIXER_READ_ALTPCM:
		case SOUND_MIXER_WRITE_ALTPCM:
		case SOUND_MIXER_READ_LINE1:
		case SOUND_MIXER_WRITE_LINE1:
		case SOUND_MIXER_READ_LINE2:
		case SOUND_MIXER_WRITE_LINE2:
		case SOUND_MIXER_READ_LINE3:
		case SOUND_MIXER_WRITE_LINE3:
			ret = 0;
			break;

		case SOUND_MIXER_READ_RECSRC:
			ret = 0;
			break;

		case SOUND_MIXER_WRITE_RECSRC:
			return -EINVAL;
			break;

		case SOUND_MIXER_READ_DEVMASK:
			ret = SOUND_MASK_VOLUME | SOUND_MASK_PCM | SOUND_MASK_SYNTH;
			break;

		case SOUND_MIXER_READ_RECMASK:
			ret = 0;
			break;

		case SOUND_MIXER_READ_STEREODEVS:
			ret = SOUND_MASK_VOLUME | SOUND_MASK_PCM | SOUND_MASK_SYNTH;
			break;

		case SOUND_MIXER_READ_CAPS:
			ret = 0;
			break;

		case SOUND_MIXER_READ_MUTE:
			return -EINVAL;
			break;

		default:
			return -EINVAL;
			break;
	}
	return put_user(ret, (int *) arg);
}

static struct mixer_operations vidc_mixer_operations = {
	"VIDC",
	"VIDCsound",
	vidc_default_mixer_ioctl	/* ioctl                */
};

void vidc_mixer_init(struct address_info *hw_config)
{
	int vidc_mixer = sound_alloc_mixerdev();
	vidc_volume = 100 | (100 << 8);
	if (num_mixers < MAX_MIXER_DEV)
		mixer_devs[vidc_mixer] = &vidc_mixer_operations;
}
