#define _PAS2_MIXER_C_

/*
 * sound/pas2_mixer.c
 *
 * Mixer routines for the Pro Audio Spectrum cards.
 *
 * Copyright by Hannu Savolainen 1993
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include "sound_config.h"

#if defined(CONFIGURE_SOUNDCARD) && !defined(EXCLUDE_PAS)

#include "pas.h"

#define TRACE(what)		/*
				   * * * (what)   */

extern int      translat_code;

static int      rec_devices = (SOUND_MASK_MIC);	/*


							 * *  * * Default *
							 * recording * source
							 *
							 * *  */
static int      mode_control = 0;

#define POSSIBLE_RECORDING_DEVICES	(SOUND_MASK_SYNTH | SOUND_MASK_SPEAKER | SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD | SOUND_MASK_ALTPCM)

#define SUPPORTED_MIXER_DEVICES		(SOUND_MASK_SYNTH | SOUND_MASK_PCM | SOUND_MASK_SPEAKER | SOUND_MASK_LINE | SOUND_MASK_MIC | \
					 SOUND_MASK_CD | SOUND_MASK_ALTPCM | SOUND_MASK_IMIX | \
					 SOUND_MASK_VOLUME | SOUND_MASK_BASS | SOUND_MASK_TREBLE | SOUND_MASK_RECLEV | \
					 SOUND_MASK_MUTE | SOUND_MASK_ENHANCE | SOUND_MASK_LOUD)

static unsigned short levels[SOUND_MIXER_NRDEVICES] =
{
  0x3232,			/*
				 * Master Volume
				 */
  0x3232,			/*
				 * Bass
				 */
  0x3232,			/*
				 * Treble
				 */
  0x5050,			/*
				 * FM
				 */
  0x4b4b,			/*
				 * PCM
				 */
  0x3232,			/*
				 * PC Speaker
				 */
  0x4b4b,			/*
				 * Ext Line
				 */
  0x4b4b,			/*
				 * Mic
				 */
  0x4b4b,			/*
				 * CD
				 */
  0x6464,			/*
				 * Recording monitor
				 */
  0x4b4b,			/*
				 * SB PCM
				 */
  0x6464};			/*


				 * *  * * Recording level   */

static int
mixer_output (int right_vol, int left_vol, int div, int bits,
	      int mixer		/*
				 * Input or output mixer
     	      	      	      	      	      				 */ )
{
  int             left = left_vol * div / 100;
  int             right = right_vol * div / 100;

  /*
   * The Revision D cards have a problem with their MVA508 interface. The
   * kludge-o-rama fix is to make a 16-bit quantity with identical LSB and
   * MSBs out of the output byte and to do a 16-bit out to the mixer port -
   * 1. We don't need to do this because the call to pas_write more than
   * compensates for the timing problems.
   */

  if (bits & P_M_MV508_MIXER)
    {				/*
				 * Select input or output mixer
				 */
      left |= mixer;
      right |= mixer;
    }

  if (bits == P_M_MV508_BASS || bits == P_M_MV508_TREBLE)
    {				/*
				 * Bass and treble are mono devices
				 */
      pas_write (P_M_MV508_ADDRESS | bits, PARALLEL_MIXER);
      pas_write (left, PARALLEL_MIXER);
      right_vol = left_vol;
    }
  else
    {
      pas_write (P_M_MV508_ADDRESS | P_M_MV508_LEFT | bits, PARALLEL_MIXER);
      pas_write (left, PARALLEL_MIXER);
      pas_write (P_M_MV508_ADDRESS | P_M_MV508_RIGHT | bits, PARALLEL_MIXER);
      pas_write (right, PARALLEL_MIXER);
    }

  return (left_vol | (right_vol << 8));
}

void
set_mode (int new_mode)
{
  pas_write (P_M_MV508_ADDRESS | P_M_MV508_MODE, PARALLEL_MIXER);
  pas_write (new_mode, PARALLEL_MIXER);

  mode_control = new_mode;
}

static int
pas_mixer_set (int whichDev, unsigned int level)
{
  int             left, right, devmask, changed, i, mixer = 0;

  TRACE (printk ("static int pas_mixer_set(int whichDev = %d, unsigned int level = %X)\n", whichDev, level));

  left = level & 0x7f;
  right = (level & 0x7f00) >> 8;

  if (whichDev < SOUND_MIXER_NRDEVICES)
    if ((1 << whichDev) & rec_devices)
      mixer = P_M_MV508_INPUTMIX;
    else
      mixer = P_M_MV508_OUTPUTMIX;

  switch (whichDev)
    {
    case SOUND_MIXER_VOLUME:	/*
				 * Master volume (0-63)
				 */
      levels[whichDev] = mixer_output (right, left, 63, P_M_MV508_MASTER_A, 0);
      break;

      /*
       * Note! Bass and Treble are mono devices. Will use just the left
       * channel.
       */
    case SOUND_MIXER_BASS:	/*
				 * Bass (0-12)
				 */
      levels[whichDev] = mixer_output (right, left, 12, P_M_MV508_BASS, 0);
      break;
    case SOUND_MIXER_TREBLE:	/*
				 * Treble (0-12)
				 */
      levels[whichDev] = mixer_output (right, left, 12, P_M_MV508_TREBLE, 0);
      break;

    case SOUND_MIXER_SYNTH:	/*
				 * Internal synthesizer (0-31)
				 */
      levels[whichDev] = mixer_output (right, left, 31, P_M_MV508_MIXER | P_M_MV508_FM, mixer);
      break;
    case SOUND_MIXER_PCM:	/*
				 * PAS PCM (0-31)
				 */
      levels[whichDev] = mixer_output (right, left, 31, P_M_MV508_MIXER | P_M_MV508_PCM, mixer);
      break;
    case SOUND_MIXER_ALTPCM:	/*
				 * SB PCM (0-31)
				 */
      levels[whichDev] = mixer_output (right, left, 31, P_M_MV508_MIXER | P_M_MV508_SB, mixer);
      break;
    case SOUND_MIXER_SPEAKER:	/*
				 * PC speaker (0-31)
				 */
      levels[whichDev] = mixer_output (right, left, 31, P_M_MV508_MIXER | P_M_MV508_SPEAKER, mixer);
      break;
    case SOUND_MIXER_LINE:	/*
				 * External line (0-31)
				 */
      levels[whichDev] = mixer_output (right, left, 31, P_M_MV508_MIXER | P_M_MV508_LINE, mixer);
      break;
    case SOUND_MIXER_CD:	/*
				 * CD (0-31)
				 */
      levels[whichDev] = mixer_output (right, left, 31, P_M_MV508_MIXER | P_M_MV508_CDROM, mixer);
      break;
    case SOUND_MIXER_MIC:	/*
				 * External microphone (0-31)
				 */
      levels[whichDev] = mixer_output (right, left, 31, P_M_MV508_MIXER | P_M_MV508_MIC, mixer);
      break;
    case SOUND_MIXER_IMIX:	/*
				 * Recording monitor (0-31) (Only available *
				 * on the Output Mixer)
				 */
      levels[whichDev] = mixer_output (right, left, 31, P_M_MV508_MIXER | P_M_MV508_IMIXER,
				       P_M_MV508_OUTPUTMIX);
      break;
    case SOUND_MIXER_RECLEV:	/*
				 * Recording level (0-15)
				 */
      levels[whichDev] = mixer_output (right, left, 15, P_M_MV508_MASTER_B, 0);
      break;

    case SOUND_MIXER_MUTE:
      return 0;
      break;

    case SOUND_MIXER_ENHANCE:
      i = 0;
      level &= 0x7f;
      if (level)
	i = (level / 20) - 1;

      mode_control &= ~P_M_MV508_ENHANCE_BITS;
      mode_control |= P_M_MV508_ENHANCE_BITS;
      set_mode (mode_control);

      if (i)
	i = (i + 1) * 20;
      return i;
      break;

    case SOUND_MIXER_LOUD:
      mode_control &= ~P_M_MV508_LOUDNESS;
      if (level)
	mode_control |= P_M_MV508_LOUDNESS;
      set_mode (mode_control);
      return !!level;		/*
				 * 0 or 1
				 */
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
      return RET_ERROR (EINVAL);
    }

  return (levels[whichDev]);
}

/*****/

static void
pas_mixer_reset (void)
{
  int             foo;

  TRACE (printk ("pas2_mixer.c: void pas_mixer_reset(void)\n"));

  for (foo = 0; foo < SOUND_MIXER_NRDEVICES; foo++)
    pas_mixer_set (foo, levels[foo]);

  set_mode (P_M_MV508_LOUDNESS | P_M_MV508_ENHANCE_40);
}

int
pas_mixer_ioctl (int dev, unsigned int cmd, unsigned int arg)
{
  TRACE (printk ("pas2_mixer.c: int pas_mixer_ioctl(unsigned int cmd = %X, unsigned int arg = %X)\n", cmd, arg));

  if (((cmd >> 8) & 0xff) == 'M')
    {
      if (cmd & IOC_IN)
	return IOCTL_OUT (arg, pas_mixer_set (cmd & 0xff, IOCTL_IN (arg)));
      else
	{			/*
				 * Read parameters
				 */

	  switch (cmd & 0xff)
	    {

	    case SOUND_MIXER_RECSRC:
	      return IOCTL_OUT (arg, rec_devices);
	      break;

	    case SOUND_MIXER_STEREODEVS:
	      return IOCTL_OUT (arg, SUPPORTED_MIXER_DEVICES & ~(SOUND_MASK_BASS | SOUND_MASK_TREBLE));
	      break;

	    case SOUND_MIXER_DEVMASK:
	      return IOCTL_OUT (arg, SUPPORTED_MIXER_DEVICES);
	      break;

	    case SOUND_MIXER_RECMASK:
	      return IOCTL_OUT (arg, POSSIBLE_RECORDING_DEVICES & SUPPORTED_MIXER_DEVICES);
	      break;

	    case SOUND_MIXER_CAPS:
	      return IOCTL_OUT (arg, 0);	/*
						 * No special capabilities
						 */
	      break;

	    case SOUND_MIXER_MUTE:
	      return IOCTL_OUT (arg, 0);	/*
						 * No mute yet
						 */
	      break;

	    case SOUND_MIXER_ENHANCE:
	      if (!(mode_control & P_M_MV508_ENHANCE_BITS))
		return IOCTL_OUT (arg, 0);
	      return IOCTL_OUT (arg, ((mode_control & P_M_MV508_ENHANCE_BITS) + 1) * 20);
	      break;

	    case SOUND_MIXER_LOUD:
	      if (mode_control & P_M_MV508_LOUDNESS)
		return IOCTL_OUT (arg, 1);
	      return IOCTL_OUT (arg, 0);
	      break;

	    default:
	      return IOCTL_OUT (arg, levels[cmd & 0xff]);
	    }
	}
    }
  return RET_ERROR (EINVAL);
}

static struct mixer_operations pas_mixer_operations =
{
  pas_mixer_ioctl
};

int
pas_init_mixer (void)
{
  pas_mixer_reset ();

  if (num_mixers < MAX_MIXER_DEV)
    mixer_devs[num_mixers++] = &pas_mixer_operations;
  return 1;
}

#endif
