
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
 */
#include <linux/config.h>


#include "sound_config.h"

#if defined(CONFIG_SBDSP) || defined(MODULE)
#define __SB_MIXER_C__

#include "sb.h"
#include "sb_mixer.h"

static int      sbmixnum = 1;

static void     sb_mixer_reset(sb_devc * devc);

void
sb_mixer_set_stereo(sb_devc * devc, int mode)
{
	sb_setmixer(devc, OUT_FILTER, ((sb_getmixer(devc, OUT_FILTER) & ~STEREO_DAC)
				       | (mode ? STEREO_DAC : MONO_DAC)));
}

static int
detect_mixer(sb_devc * devc)
{
	/* Just trust the mixer is there */
	return 1;
}

static void
change_bits(sb_devc * devc, unsigned char *regval, int dev, int chn, int newval)
{
	unsigned char   mask;
	int             shift;

	mask = (1 << (*devc->iomap)[dev][chn].nbits) - 1;
	newval = (int) ((newval * mask) + 50) / 100;	/* Scale */

	shift = (*devc->iomap)[dev][chn].bitoffs - (*devc->iomap)[dev][LEFT_CHN].nbits + 1;

	*regval &= ~(mask << shift);	/* Mask out previous value */
	*regval |= (newval & mask) << shift;	/* Set the new value */
}

static int
sb_mixer_get(sb_devc * devc, int dev)
{
	if (!((1 << dev) & devc->supported_devices))
		return -EINVAL;

	return devc->levels[dev];
}

void
smw_mixer_init(sb_devc * devc)
{
	int             i;

	sb_setmixer(devc, 0x00, 0x18);	/* Mute unused (Telephone) line */
	sb_setmixer(devc, 0x10, 0x38);	/* Config register 2 */

	devc->supported_devices = 0;
	for (i = 0; i < sizeof(smw_mix_regs); i++)
		if (smw_mix_regs[i] != 0)
			devc->supported_devices |= (1 << i);

	devc->supported_rec_devices = devc->supported_devices &
	    ~(SOUND_MASK_BASS | SOUND_MASK_TREBLE | SOUND_MASK_PCM |
	      SOUND_MASK_VOLUME);

	sb_mixer_reset(devc);
}

static int
smw_mixer_set(sb_devc * devc, int dev, int value)
{
	int             left = value & 0x000000ff;
	int             right = (value & 0x0000ff00) >> 8;
	int             reg, val;

	if (left > 100)
		left = 100;
	if (right > 100)
		right = 100;

	if (dev > 31)
		return -EINVAL;

	if (!(devc->supported_devices & (1 << dev)))	/* Not supported */
		return -EINVAL;

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

static int
sb_mixer_set(sb_devc * devc, int dev, int value)
{
	int             left = value & 0x000000ff;
	int             right = (value & 0x0000ff00) >> 8;

	int             regoffs;
	unsigned char   val;

	if (devc->model == MDL_SMW)
		return smw_mixer_set(devc, dev, value);

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

static void
set_recsrc(sb_devc * devc, int src)
{
	sb_setmixer(devc, RECORD_SRC, (sb_getmixer(devc, RECORD_SRC) & ~7) | (src & 0x7));
}

static int
set_recmask(sb_devc * devc, int mask)
{
	int             devmask, i;
	unsigned char   regimageL, regimageR;

	devmask = mask & devc->supported_rec_devices;

	switch (devc->model)
	  {
	  case MDL_SBPRO:
	  case MDL_ESS:
	  case MDL_JAZZ:
	  case MDL_SMW:

		  if (devmask != SOUND_MASK_MIC &&
		      devmask != SOUND_MASK_LINE &&
		      devmask != SOUND_MASK_CD)
		    {		/*
				 * More than one devices selected. Drop the *
				 * previous selection
				 */
			    devmask &= ~devc->recmask;
		    }
		  if (devmask != SOUND_MASK_MIC &&
		      devmask != SOUND_MASK_LINE &&
		      devmask != SOUND_MASK_CD)
		    {		/*
				 * More than one devices selected. Default to
				 * * mic
				 */
			    devmask = SOUND_MASK_MIC;
		    }
		  if (devmask ^ devc->recmask)	/*
						   * Input source changed
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

		  regimageL = regimageR = 0;
		  for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
			  if ((1 << i) & devmask)
			    {
				    regimageL |= sb16_recmasks_L[i];
				    regimageR |= sb16_recmasks_R[i];
			    }
		  sb_setmixer(devc, SB16_IMASK_L, regimageL);
		  sb_setmixer(devc, SB16_IMASK_R, regimageR);
		  break;
	  }

	devc->recmask = devmask;
	return devc->recmask;
}

static int
sb_mixer_ioctl(int dev, unsigned int cmd, caddr_t arg)
{
	sb_devc        *devc = mixer_devs[dev]->devc;
	int             val;

/*
 * Use ioctl(fd, SOUND_MIXER_PRIVATE1, &mode) to turn AGC off (0) or on (1).
 */
	if (cmd == SOUND_MIXER_PRIVATE1 && devc->model == MDL_SB16)
	  {
		  int             tmp;

		  tmp = *(int *) arg;

		  sb_setmixer(devc, 0x43, (~tmp) & 0x01);
		  return 0;
	  }
	if (((cmd >> 8) & 0xff) == 'M')
	  {
		  if (_SIOC_DIR(cmd) & _SIOC_WRITE)
			  switch (cmd & 0xff)
			    {
			    case SOUND_MIXER_RECSRC:
				    val = *(int *) arg;
				    return (*(int *) arg = set_recmask(devc, val));
				    break;

			    default:

				    val = *(int *) arg;
				    return (*(int *) arg = sb_mixer_set(devc, cmd & 0xff, val));
		  } else
			  switch (cmd & 0xff)
			    {

			    case SOUND_MIXER_RECSRC:
				    return (*(int *) arg = devc->recmask);
				    break;

			    case SOUND_MIXER_DEVMASK:
				    return (*(int *) arg = devc->supported_devices);
				    break;

			    case SOUND_MIXER_STEREODEVS:
				    if (devc->model == MDL_JAZZ || devc->model == MDL_SMW)
					    return (*(int *) arg = devc->supported_devices);
				    else
					    return (*(int *) arg = devc->supported_devices & ~(SOUND_MASK_MIC | SOUND_MASK_SPEAKER | SOUND_MASK_IMIX));
				    break;

			    case SOUND_MIXER_RECMASK:
				    return (*(int *) arg = devc->supported_rec_devices);
				    break;

			    case SOUND_MIXER_CAPS:
				    return (*(int *) arg = devc->mixer_caps);
				    break;

			    default:
				    return (*(int *) arg = sb_mixer_get(devc, cmd & 0xff));
			    }
	} else
		return -EINVAL;
}

static struct mixer_operations sb_mixer_operations =
{
	"SB",
	"Sound Blaster",
	sb_mixer_ioctl
};

static void
sb_mixer_reset(sb_devc * devc)
{
	char            name[32];
	int             i;
	extern int      sm_games;

	sprintf(name, "SB_%d", devc->sbmixnum);

	if (sm_games)
		devc->levels = load_mixer_volumes(name, smg_default_levels, 1);
	else
		devc->levels = load_mixer_volumes(name, sb_default_levels, 1);

	for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
		sb_mixer_set(devc, i, devc->levels[i]);

	set_recmask(devc, SOUND_MASK_MIC);
}

int
sb_mixer_init(sb_devc * devc)
{
	int             mixer_type = 0;
	int             m;

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
		  devc->supported_devices = ES688_MIXER_DEVICES;
		  devc->supported_rec_devices = ES688_RECORDING_DEVICES;
		  devc->iomap = &es688_mix;
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
		  devc->supported_devices = SB16_MIXER_DEVICES;
		  devc->supported_rec_devices = SB16_RECORDING_DEVICES;
		  devc->iomap = &sb16_mix;
		  break;

	  default:
		  printk("SB Warning: Unsupported mixer type %d\n", devc->model);
		  return 0;
	  }

	m = sound_alloc_mixerdev();
	if (m == -1)
		return 0;


	mixer_devs[m] = (struct mixer_operations *) (sound_mem_blocks[sound_nblocks] = vmalloc(sizeof(struct mixer_operations)));
	sound_mem_sizes[sound_nblocks] = sizeof(struct mixer_operations);

	if (sound_nblocks < 1024)
		sound_nblocks++;;
	if (mixer_devs[m] == NULL)
	  {
		  printk(KERN_ERR "sb_mixer: Can't allocate memory\n");
		  sound_unload_mixerdev(m);
		  return 0;
	  }
	memcpy((char *) mixer_devs[m], (char *) &sb_mixer_operations,
	       sizeof(struct mixer_operations));

	mixer_devs[m]->devc = devc;

	devc->my_mixerdev = m;
	sb_mixer_reset(devc);
	return 1;
}

#endif
