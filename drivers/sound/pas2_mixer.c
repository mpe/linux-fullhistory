#define _PAS2_MIXER_C_

/*
 * sound/pas2_mixer.c
 *
 * Mixer routines for the Pro Audio Spectrum cards.
 */
   
#include <linux/config.h>

#include "sound_config.h"

#if defined(CONFIG_PAS)

#ifndef DEB
#define DEB(what)		/* (what) */
#endif

extern int      translat_code;
extern char     pas_model;
extern int     *pas_osp;
extern int      pas_audiodev;

static int      rec_devices = (SOUND_MASK_MIC);		/* Default recording source */
static int      mode_control = 0;

#define POSSIBLE_RECORDING_DEVICES	(SOUND_MASK_SYNTH | SOUND_MASK_SPEAKER | SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD | SOUND_MASK_ALTPCM)

#define SUPPORTED_MIXER_DEVICES		(SOUND_MASK_SYNTH | SOUND_MASK_PCM | SOUND_MASK_SPEAKER | SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD | SOUND_MASK_ALTPCM | SOUND_MASK_IMIX | \
					 SOUND_MASK_VOLUME | SOUND_MASK_BASS | SOUND_MASK_TREBLE | SOUND_MASK_RECLEV)


static unsigned short levels[SOUND_MIXER_NRDEVICES] =
{
  0x3232,			/* Master Volume */
  0x3232,			/* Bass */
  0x3232,			/* Treble */
  0x5050,			/* FM */
  0x4b4b,			/* PCM */
  0x3232,			/* PC Speaker */
  0x4b4b,			/* Ext Line */
  0x4b4b,			/* Mic */
  0x4b4b,			/* CD */
  0x6464,			/* Recording monitor */
  0x4b4b,			/* SB PCM */
  0x6464			/* Recording level */
};

void
mix_write (unsigned char data, int ioaddr)
{
  /*
   * The Revision D cards have a problem with their MVA508 interface. The
   * kludge-o-rama fix is to make a 16-bit quantity with identical LSB and
   * MSBs out of the output byte and to do a 16-bit out to the mixer port -
   * 1. We need to do this because it isn't timing problem but chip access
   * sequence problem.
   */

  if (pas_model == 4)
    {
      outw (data | (data << 8), (ioaddr ^ translat_code) - 1);
      outb (0x80, 0);
    }
  else
    pas_write (data, ioaddr);
}

static int
mixer_output (int right_vol, int left_vol, int div, int bits,
	      int mixer)	/* Input or output mixer */
{
  int             left = left_vol * div / 100;
  int             right = right_vol * div / 100;


  if (bits & 0x10)
    {				/*
				 * Select input or output mixer
				 */
      left |= mixer;
      right |= mixer;
    }

  if (bits == 0x03 || bits == 0x04)
    {				/*
				 * Bass and treble are mono devices
				 */
      mix_write (0x80 | bits, 0x078B);
      mix_write (left, 0x078B);
      right_vol = left_vol;
    }
  else
    {
      mix_write (0x80 | 0x20 | bits, 0x078B);
      mix_write (left, 0x078B);
      mix_write (0x80 | 0x40 | bits, 0x078B);
      mix_write (right, 0x078B);
    }

  return (left_vol | (right_vol << 8));
}

void
set_mode (int new_mode)
{
  mix_write (0x80 | 0x05, 0x078B);
  mix_write (new_mode, 0x078B);

  mode_control = new_mode;
}

static int
pas_mixer_set (int whichDev, unsigned int level)
{
  int             left, right, devmask, changed, i, mixer = 0;

  DEB (printk ("static int pas_mixer_set(int whichDev = %d, unsigned int level = %X)\n", whichDev, level));

  left = level & 0x7f;
  right = (level & 0x7f00) >> 8;

  if (whichDev < SOUND_MIXER_NRDEVICES)
    if ((1 << whichDev) & rec_devices)
      mixer = 0x20;
    else
      mixer = 0x00;

  switch (whichDev)
    {
    case SOUND_MIXER_VOLUME:	/* Master volume (0-63) */
      levels[whichDev] = mixer_output (right, left, 63, 0x01, 0);
      break;

      /*
       * Note! Bass and Treble are mono devices. Will use just the left
       * channel.
       */
    case SOUND_MIXER_BASS:	/* Bass (0-12) */
      levels[whichDev] = mixer_output (right, left, 12, 0x03, 0);
      break;
    case SOUND_MIXER_TREBLE:	/* Treble (0-12) */
      levels[whichDev] = mixer_output (right, left, 12, 0x04, 0);
      break;

    case SOUND_MIXER_SYNTH:	/* Internal synthesizer (0-31) */
      levels[whichDev] = mixer_output (right, left, 31, 0x10 | 0x00, mixer);
      break;
    case SOUND_MIXER_PCM:	/* PAS PCM (0-31) */
      levels[whichDev] = mixer_output (right, left, 31, 0x10 | 0x05, mixer);
      break;
    case SOUND_MIXER_ALTPCM:	/* SB PCM (0-31) */
      levels[whichDev] = mixer_output (right, left, 31, 0x10 | 0x07, mixer);
      break;
    case SOUND_MIXER_SPEAKER:	/* PC speaker (0-31) */
      levels[whichDev] = mixer_output (right, left, 31, 0x10 | 0x06, mixer);
      break;
    case SOUND_MIXER_LINE:	/* External line (0-31) */
      levels[whichDev] = mixer_output (right, left, 31, 0x10 | 0x02, mixer);
      break;
    case SOUND_MIXER_CD:	/* CD (0-31) */
      levels[whichDev] = mixer_output (right, left, 31, 0x10 | 0x03, mixer);
      break;
    case SOUND_MIXER_MIC:	/* External microphone (0-31) */
      levels[whichDev] = mixer_output (right, left, 31, 0x10 | 0x04, mixer);
      break;
    case SOUND_MIXER_IMIX:	/* Recording monitor (0-31) (Output mixer only) */
      levels[whichDev] = mixer_output (right, left, 31, 0x10 | 0x01,
				       0x00);
      break;
    case SOUND_MIXER_RECLEV:	/* Recording level (0-15) */
      levels[whichDev] = mixer_output (right, left, 15, 0x02, 0);
      break;


    case SOUND_MIXER_RECSRC:
      devmask = level & POSSIBLE_RECORDING_DEVICES;

      changed = devmask ^ rec_devices;
      rec_devices = devmask;

      for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
	if (changed & (1 << i))
	  {
	    pas_mixer_set (i, levels[i]);
	  }
      return rec_devices;
      break;

    default:
      return -(EINVAL);
    }

  return (levels[whichDev]);
}

/*****/

static void
pas_mixer_reset (void)
{
  int             foo;

  DEB (printk ("pas2_mixer.c: void pas_mixer_reset(void)\n"));

  for (foo = 0; foo < SOUND_MIXER_NRDEVICES; foo++)
    pas_mixer_set (foo, levels[foo]);

  set_mode (0x04 | 0x01);
}

int
pas_mixer_ioctl (int dev, unsigned int cmd, caddr_t arg)
{
  DEB (printk ("pas2_mixer.c: int pas_mixer_ioctl(unsigned int cmd = %X, unsigned int arg = %X)\n", cmd, arg));

  if (cmd == SOUND_MIXER_PRIVATE1)	/* Set loudness bit */
    {
      int             level = get_fs_long ((long *) arg);

      if (level == -1)		/* Return current settings */
	{
	  if (mode_control & 0x04)
	    return snd_ioctl_return ((int *) arg, 1);
	  else
	    return snd_ioctl_return ((int *) arg, 0);
	}
      else
	{
	  mode_control &= ~0x04;
	  if (level)
	    mode_control |= 0x04;
	  set_mode (mode_control);
	  return snd_ioctl_return ((int *) arg, !!level);	/* 0 or 1 */
	}
    }


  if (cmd == SOUND_MIXER_PRIVATE2)	/* Set enhance bit */
    {
      int             level = get_fs_long ((long *) arg);

      if (level == -1)		/* Return current settings */
	{
	  if (!(mode_control & 0x03))
	    return snd_ioctl_return ((int *) arg, 0);
	  return snd_ioctl_return ((int *) arg, ((mode_control & 0x03) + 1) * 20);
	}
      else
	{
	  int             i = 0;

	  level &= 0x7f;
	  if (level)
	    i = (level / 20) - 1;

	  mode_control &= ~0x03;
	  mode_control |= i & 0x03;
	  set_mode (mode_control);

	  if (i)
	    i = (i + 1) * 20;

	  return i;
	}
    }

  if (cmd == SOUND_MIXER_PRIVATE3)	/* Set mute bit */
    {
      int             level = get_fs_long ((long *) arg);

      if (level == -1)		/* Return current settings */
	{
	  return snd_ioctl_return ((int *) arg,
				   !(pas_read (0x0B8A) &
				     0x20));
	}
      else
	{
	  if (level)
	    pas_write (pas_read (0x0B8A) & (~0x20),
		       0x0B8A);
	  else
	    pas_write (pas_read (0x0B8A) | 0x20,
		       0x0B8A);

	  return !(pas_read (0x0B8A) & 0x20);
	}
    }

  if (((cmd >> 8) & 0xff) == 'M')
    {
      if (_IOC_DIR (cmd) & _IOC_WRITE)
	return snd_ioctl_return ((int *) arg, pas_mixer_set (cmd & 0xff, get_fs_long ((long *) arg)));
      else
	{			/*
				 * Read parameters
				 */

	  switch (cmd & 0xff)
	    {

	    case SOUND_MIXER_RECSRC:
	      return snd_ioctl_return ((int *) arg, rec_devices);
	      break;

	    case SOUND_MIXER_STEREODEVS:
	      return snd_ioctl_return ((int *) arg, SUPPORTED_MIXER_DEVICES & ~(SOUND_MASK_BASS | SOUND_MASK_TREBLE));
	      break;

	    case SOUND_MIXER_DEVMASK:
	      return snd_ioctl_return ((int *) arg, SUPPORTED_MIXER_DEVICES);
	      break;

	    case SOUND_MIXER_RECMASK:
	      return snd_ioctl_return ((int *) arg, POSSIBLE_RECORDING_DEVICES & SUPPORTED_MIXER_DEVICES);
	      break;

	    case SOUND_MIXER_CAPS:
	      return snd_ioctl_return ((int *) arg, 0);		/* No special capabilities */
	      break;


	    default:
	      return snd_ioctl_return ((int *) arg, levels[cmd & 0xff]);
	    }
	}
    }
  return -(EINVAL);
}

static struct mixer_operations pas_mixer_operations =
{
  "PAS16",
  "Pro Audio Spectrum 16",
  pas_mixer_ioctl
};

int
pas_init_mixer (void)
{
  pas_mixer_reset ();

  if (num_mixers < MAX_MIXER_DEV)
    {
      audio_devs[pas_audiodev]->mixer_dev = num_mixers;
      mixer_devs[num_mixers++] = &pas_mixer_operations;
    }
  return 1;
}

#endif
