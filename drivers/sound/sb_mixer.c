
/*
 * sound/sb_mixer.c
 *
 * The low level mixer driver for the Sound Blaster compatible cards.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *
 * Thomas Sailer   : ioctl code reworked (vmalloc/vfree removed)
 * Rolf Fokkens    : ES18XX recording level support
 */

/*
 * About ES18XX support:
 *
 * The standard ES688 support doesn't take care of the ES18XX recording
 * levels very well. Whenever a device is selected (recmask) for recording
 * it's recording level is loud, and it cannot be changed.
 *
 * The ES18XX has separate registers to controll the recording levels. The
 * ES18XX specific software makes these level the same as their corresponding
 * playback levels, unless recmask says they aren't recorded. In tha latter
 * case the recording volumens are 0.
 *
 * Now recording levels of inputs can be controlled, by changing the playback
 * levels.
 * Futhermore several devices can be recorded together (which is not possible
 * with the ES688.
 */

#include <linux/config.h>
#include "sound_config.h"

#ifdef CONFIG_SBDSP
#define __SB_MIXER_C__

#include "sb.h"
#include "sb_mixer.h"

static int      sbmixnum = 1;

static void     sb_mixer_reset(sb_devc * devc);

void sb_mixer_set_stereo(sb_devc * devc, int mode)
{
	sb_setmixer(devc, OUT_FILTER, ((sb_getmixer(devc, OUT_FILTER) & ~STEREO_DAC)
				       | (mode ? STEREO_DAC : MONO_DAC)));
}

static int detect_mixer(sb_devc * devc)
{
	/* Just trust the mixer is there */
	return 1;
}

static void change_bits(sb_devc * devc, unsigned char *regval, int dev, int chn, int newval)
{
	unsigned char mask;
	int shift;

	mask = (1 << (*devc->iomap)[dev][chn].nbits) - 1;
	newval = (int) ((newval * mask) + 50) / 100;	/* Scale */

	shift = (*devc->iomap)[dev][chn].bitoffs - (*devc->iomap)[dev][LEFT_CHN].nbits + 1;

	*regval &= ~(mask << shift);	/* Mask out previous value */
	*regval |= (newval & mask) << shift;	/* Set the new value */
}

static int sb_mixer_get(sb_devc * devc, int dev)
{
	if (!((1 << dev) & devc->supported_devices))
		return -EINVAL;
	return devc->levels[dev];
}

void smw_mixer_init(sb_devc * devc)
{
	int i;

	sb_setmixer(devc, 0x00, 0x18);	/* Mute unused (Telephone) line */
	sb_setmixer(devc, 0x10, 0x38);	/* Config register 2 */

	devc->supported_devices = 0;
	for (i = 0; i < sizeof(smw_mix_regs); i++)
		if (smw_mix_regs[i] != 0)
			devc->supported_devices |= (1 << i);

	devc->supported_rec_devices = devc->supported_devices &
		~(SOUND_MASK_BASS | SOUND_MASK_TREBLE | SOUND_MASK_PCM | SOUND_MASK_VOLUME);
	sb_mixer_reset(devc);
}

static int common_mixer_set(sb_devc * devc, int dev, int left, int right)
{
	int regoffs;
	unsigned char val;

	regoffs = (*devc->iomap)[dev][LEFT_CHN].regno;

	if (regoffs == 0)
		return -EINVAL;

	val = sb_getmixer(devc, regoffs);
	change_bits(devc, &val, dev, LEFT_CHN, left);

	devc->levels[dev] = left | (left << 8);

	if ((*devc->iomap)[dev][RIGHT_CHN].regno != regoffs)	/*
								 * Change register
								 */
	{
		sb_setmixer(devc, regoffs, val);	/*
							 * Save the old one
							 */
		regoffs = (*devc->iomap)[dev][RIGHT_CHN].regno;

		if (regoffs == 0)
			return left | (left << 8);	/*
							 * Just left channel present
							 */

		val = sb_getmixer(devc, regoffs);	/*
							 * Read the new one
							 */
	}
	change_bits(devc, &val, dev, RIGHT_CHN, right);

	sb_setmixer(devc, regoffs, val);

	devc->levels[dev] = left | (right << 8);
	return left | (right << 8);
}

/*
 * Changing input levels at ES18XX means having to take care of recording
 * levels of recorded inputs too!
 */
static int es18XX_mixer_set(sb_devc * devc, int dev, int left, int right)
{
	if (devc->recmask & (1 << dev)) {
		common_mixer_set(devc, dev + ES18XX_MIXER_RECDIFF, left, right);
	}
	return common_mixer_set(devc, dev, left, right);
}

static int smw_mixer_set(sb_devc * devc, int dev, int left, int right)
{
	int reg, val;

	switch (dev)
	{
		case SOUND_MIXER_VOLUME:
			sb_setmixer(devc, 0x0b, 96 - (96 * left / 100));	/* 96=mute, 0=max */
			sb_setmixer(devc, 0x0c, 96 - (96 * right / 100));
			break;

		case SOUND_MIXER_BASS:
		case SOUND_MIXER_TREBLE:
			devc->levels[dev] = left | (right << 8);
			/* Set left bass and treble values */
			val = ((devc->levels[SOUND_MIXER_TREBLE] & 0xff) * 16 / (unsigned) 100) << 4;
			val |= ((devc->levels[SOUND_MIXER_BASS] & 0xff) * 16 / (unsigned) 100) & 0x0f;
			sb_setmixer(devc, 0x0d, val);

			/* Set right bass and treble values */
			val = (((devc->levels[SOUND_MIXER_TREBLE] >> 8) & 0xff) * 16 / (unsigned) 100) << 4;
			val |= (((devc->levels[SOUND_MIXER_BASS] >> 8) & 0xff) * 16 / (unsigned) 100) & 0x0f;
			sb_setmixer(devc, 0x0e, val);
		
			break;

		default:
			reg = smw_mix_regs[dev];
			if (reg == 0)
				return -EINVAL;
			sb_setmixer(devc, reg, (24 - (24 * left / 100)) | 0x20);	/* 24=mute, 0=max */
			sb_setmixer(devc, reg + 1, (24 - (24 * right / 100)) | 0x40);
	}

	devc->levels[dev] = left | (right << 8);
	return left | (right << 8);
}

static int sb_mixer_set(sb_devc * devc, int dev, int value)
{
	int left = value & 0x000000ff;
	int right = (value & 0x0000ff00) >> 8;

	if (left > 100)
		left = 100;
	if (right > 100)
		right = 100;

	if (dev > 31)
		return -EINVAL;

	if (!(devc->supported_devices & (1 << dev)))	/*
							 * Not supported
							 */
		return -EINVAL;

	/* Differentiate dependong on the chipsets */
	switch (devc->model) {
	case MDL_SMW:
		return smw_mixer_set(devc, dev, left, right);
		break;
	case MDL_ESS:
		if (devc->submodel == SUBMDL_ES18XX) {
			return es18XX_mixer_set(devc, dev, left, right);
		}
		break;
	}

	return common_mixer_set(devc, dev, left, right);
}

/*
 * set_recsrc doesn't apply to ES18XX
 */
static void set_recsrc(sb_devc * devc, int src)
{
	sb_setmixer(devc, RECORD_SRC, (sb_getmixer(devc, RECORD_SRC) & ~7) | (src & 0x7));
}

/*
 * Changing the recmask on a ES18XX means:
 * (1) Find the differences
 * (2) For "turned-on"  inputs: make the recording level the playback level
 * (3) For "turned-off" inputs: make the recording level zero
 */
static int es18XX_set_recmask(sb_devc * devc, int mask)
{
	int i, i_mask, cur_mask, diff_mask;
	int value, left, right;

	cur_mask  = devc->recmask;
	diff_mask = (cur_mask ^ mask);

	for (i = 0; i < 32; i++) {
		i_mask = (1 << i);
		if (diff_mask & i_mask) {
			if (mask & i_mask) {	/* Turn it on (2) */
				value = devc->levels[i];
				left  = value & 0x000000ff;
    				right = (value & 0x0000ff00) >> 8;
			} else {		/* Turn it off (3) */
				left  = 0;
				right = 0;
			}
			common_mixer_set(devc, i + ES18XX_MIXER_RECDIFF, left, right);
		}
	}
	return mask;
}

static int set_recmask(sb_devc * devc, int mask)
{
	int devmask, i;
	unsigned char  regimageL, regimageR;

	devmask = mask & devc->supported_rec_devices;

	switch (devc->model)
	{
		case MDL_ESS:	/* ES18XX needs a separate approach */
			if (devc->submodel == SUBMDL_ES18XX) {
				devmask = es18XX_set_recmask(devc, devmask);
				break;
			};
		case MDL_SBPRO:
		case MDL_JAZZ:
		case MDL_SMW:

			if (devmask != SOUND_MASK_MIC &&
				devmask != SOUND_MASK_LINE &&
				devmask != SOUND_MASK_CD)
			{
				/*
				 * More than one device selected. Drop the
				 * previous selection
				 */
				devmask &= ~devc->recmask;
			}
			if (devmask != SOUND_MASK_MIC &&
				devmask != SOUND_MASK_LINE &&
				devmask != SOUND_MASK_CD)
			{
				/*
				 * More than one device selected. Default to
				 * mic
				 */
				devmask = SOUND_MASK_MIC;
			}
			if (devmask ^ devc->recmask)	/*
							 *	Input source changed
							 */
			{
				switch (devmask)
				{
					case SOUND_MASK_MIC:
						set_recsrc(devc, SRC__MIC);
						break;

					case SOUND_MASK_LINE:
						set_recsrc(devc, SRC__LINE);
						break;

					case SOUND_MASK_CD:
						set_recsrc(devc, SRC__CD);
						break;

					default:
						set_recsrc(devc, SRC__MIC);
				}
			}
			break;

		case MDL_SB16:
			if (!devmask)
				devmask = SOUND_MASK_MIC;

			if (devc->submodel == SUBMDL_ALS007) 
			{
				switch (devmask) 
				{
					case SOUND_MASK_LINE:
						sb_setmixer(devc, ALS007_RECORD_SRC, ALS007_LINE);
						break;
					case SOUND_MASK_CD:
						sb_setmixer(devc, ALS007_RECORD_SRC, ALS007_CD);
						break;
					case SOUND_MASK_SYNTH:
						sb_setmixer(devc, ALS007_RECORD_SRC, ALS007_SYNTH);
						break;
					default:           /* Also takes care of SOUND_MASK_MIC case */
						sb_setmixer(devc, ALS007_RECORD_SRC, ALS007_MIC);
						break;
				}
			}
			else
			{
				regimageL = regimageR = 0;
				for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				{
					if ((1 << i) & devmask)
					{
						regimageL |= sb16_recmasks_L[i];
						regimageR |= sb16_recmasks_R[i];
					}
					sb_setmixer (devc, SB16_IMASK_L, regimageL);
					sb_setmixer (devc, SB16_IMASK_R, regimageR);
				}
			}
			break;
	}
	devc->recmask = devmask;
	return devc->recmask;
}

static int set_outmask(sb_devc * devc, int mask)
{
	int devmask, i;
	unsigned char  regimage;

	devmask = mask & devc->supported_out_devices;

	switch (devc->model)
	{
		case MDL_SB16:
			if (devc->submodel == SUBMDL_ALS007) 
				break;
			else
			{
				regimage = 0;
				for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
				{
					if ((1 << i) & devmask)
					{
						regimage |= (sb16_recmasks_L[i] | sb16_recmasks_R[i]);
					}
					sb_setmixer (devc, SB16_OMASK, regimage);
				}
			}
			break;
		default:
			break;
	}

	devc->outmask = devmask;
	return devc->outmask;
}

static int sb_mixer_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	sb_devc *devc = mixer_devs[dev]->devc;
	int val, ret;

	/*
	 * Use ioctl(fd, SOUND_MIXER_PRIVATE1, &mode) to turn AGC off (0) or on (1).
	 */
	if (cmd == SOUND_MIXER_PRIVATE1 && devc->model == MDL_SB16) 
	{
		if (get_user(val, (int *)arg))
			return -EFAULT;
		sb_setmixer(devc, 0x43, (~val) & 0x01);
		return 0;
	}
	if (((cmd >> 8) & 0xff) == 'M') 
	{
		if (_SIOC_DIR(cmd) & _SIOC_WRITE) 
		{
			if (get_user(val, (int *)arg))
				return -EFAULT;
			switch (cmd & 0xff) 
			{
				case SOUND_MIXER_RECSRC:
					ret = set_recmask(devc, val);
					break;

				case SOUND_MIXER_OUTSRC:
					ret = set_outmask(devc, val);
					break;

				default:
					ret = sb_mixer_set(devc, cmd & 0xff, val);
			}
		}
		else switch (cmd & 0xff) 
		{
			case SOUND_MIXER_RECSRC:
				ret = devc->recmask;
				break;
				  
			case SOUND_MIXER_OUTSRC:
				ret = devc->outmask;
				break;
				  
			case SOUND_MIXER_DEVMASK:
				ret = devc->supported_devices;
				break;
				  
			case SOUND_MIXER_STEREODEVS:
				ret = devc->supported_devices;
				/* The ESS seems to have stereo mic controls */
				if (devc->model == MDL_ESS)
					ret &= ~(SOUND_MASK_SPEAKER|SOUND_MASK_IMIX);
				else if (devc->model != MDL_JAZZ && devc->model != MDL_SMW)
					ret &= ~(SOUND_MASK_MIC | SOUND_MASK_SPEAKER | SOUND_MASK_IMIX);
				break;
				  
			case SOUND_MIXER_RECMASK:
				ret = devc->supported_rec_devices;
				break;
				  
			case SOUND_MIXER_OUTMASK:
				ret = devc->supported_out_devices;
				break;
				  
			case SOUND_MIXER_CAPS:
				ret = devc->mixer_caps;
				break;
				    
			default:
				ret = sb_mixer_get(devc, cmd & 0xff);
				break;
		}
		return put_user(ret, (int *)arg); 
	} else
		return -EINVAL;
}

static struct mixer_operations sb_mixer_operations =
{
	"SB",
	"Sound Blaster",
	sb_mixer_ioctl
};

static struct mixer_operations als007_mixer_operations =
{
	"ALS007",
	"Avance ALS-007",
	sb_mixer_ioctl
};

static void sb_mixer_reset(sb_devc * devc)
{
	char name[32];
	int i, regval;
	extern int sm_games;

	sprintf(name, "SB_%d", devc->sbmixnum);

	if (sm_games)
		devc->levels = load_mixer_volumes(name, smg_default_levels, 1);
	else
		devc->levels = load_mixer_volumes(name, sb_default_levels, 1);

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		sb_mixer_set(devc, i, devc->levels[i]);

	/*
	 * Separate actions for ES18XX:
	 * Change registers 7a and 1c to make the record mixer the input for
	 *
	 * Then call set_recmask twice to do extra ES18XX initializations
	 */
	if (devc->model == MDL_ESS && devc->submodel == SUBMDL_ES18XX) {
	        regval = sb_getmixer(devc, 0x7a);
		regval = (regval & 0xe7) | 0x08;
	        sb_setmixer(devc, 0x7a, regval);
		regval = sb_getmixer(devc, 0x1c);
		regval = (regval & 0xf8) | 0x07;
		sb_setmixer(devc, 0x1c, regval);

		set_recmask(devc, ES18XX_RECORDING_DEVICES);
		set_recmask(devc, 0);
	}
	set_recmask(devc, SOUND_MASK_MIC);
}

int sb_mixer_init(sb_devc * devc)
{
	int mixer_type = 0;
	int m;

	devc->sbmixnum = sbmixnum++;
	devc->levels = NULL;

	sb_setmixer(devc, 0x00, 0);	/* Reset mixer */

	if (!(mixer_type = detect_mixer(devc)))
		return 0;	/* No mixer. Why? */

	switch (devc->model)
	{
		case MDL_SBPRO:
		case MDL_AZTECH:
		case MDL_JAZZ:
			devc->mixer_caps = SOUND_CAP_EXCL_INPUT;
			devc->supported_devices = SBPRO_MIXER_DEVICES;
			devc->supported_rec_devices = SBPRO_RECORDING_DEVICES;
			devc->iomap = &sbpro_mix;
			break;

		case MDL_ESS:
			devc->mixer_caps = SOUND_CAP_EXCL_INPUT;

			/*
			 * Take care of ES18XX specifics...
			 */
			switch (devc->submodel) {
			case SUBMDL_ES18XX:
				devc->supported_devices
					= ES18XX_MIXER_DEVICES;
				devc->supported_rec_devices
					= ES18XX_RECORDING_DEVICES;
				devc->iomap = &es18XX_mix;
				break;
			default:
				devc->supported_devices
					= ES688_MIXER_DEVICES;
				devc->supported_rec_devices
					= ES688_RECORDING_DEVICES;
				devc->iomap = &es688_mix;
			}

			break;

		case MDL_SMW:
			devc->mixer_caps = SOUND_CAP_EXCL_INPUT;
			devc->supported_devices = 0;
			devc->supported_rec_devices = 0;
			devc->iomap = &sbpro_mix;
			smw_mixer_init(devc);
			break;

		case MDL_SB16:
			devc->mixer_caps = 0;
			devc->supported_rec_devices = SB16_RECORDING_DEVICES;
			devc->supported_out_devices = SB16_OUTFILTER_DEVICES;
			if (devc->submodel != SUBMDL_ALS007)
			{
				devc->supported_devices = SB16_MIXER_DEVICES;
				devc->iomap = &sb16_mix;
			}
			else
			{
				devc->supported_devices = ALS007_MIXER_DEVICES;
				devc->iomap = &als007_mix;
			}
			break;

		default:
			printk(KERN_WARNING "sb_mixer: Unsupported mixer type %d\n", devc->model);
			return 0;
	}

	m = sound_alloc_mixerdev();
	if (m == -1)
		return 0;

	mixer_devs[m] = (struct mixer_operations *)kmalloc(sizeof(struct mixer_operations), GFP_KERNEL);
	if (mixer_devs[m] == NULL)
	{
		printk(KERN_ERR "sb_mixer: Can't allocate memory\n");
		sound_unload_mixerdev(m);
		return 0;
	}

	if (devc->submodel != SUBMDL_ALS007)
		memcpy ((char *) mixer_devs[m], (char *) &sb_mixer_operations, sizeof (struct mixer_operations));
	else
		memcpy ((char *) mixer_devs[m], (char *) &als007_mixer_operations, sizeof (struct mixer_operations));

	mixer_devs[m]->devc = devc;
	devc->my_mixerdev = m;
	sb_mixer_reset(devc);
	return 1;
}

#endif
