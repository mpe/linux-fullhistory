/*
 * sound/ad1848.c
 *
 * The low level driver for the AD1848/CS4248 codec chip which
 * is used for example in the MS Sound System.
 *
 * The CS4231 which is used in the GUS MAX and some other cards is
 * upwards compatible with AD1848 and this driver is able to drive it.
 *
 * CS4231A and AD1845 are upward compatible with CS4231. However
 * the new features of these chips are different.
 *
 * CS4232 is a PnP audio chip which contains a CS4231A (and SB, MPU).
 * CS4232A is an improved version of CS4232.
 */

/*
 * Copyright by Hannu Savolainen 1993-1996
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
 */
#include <linux/config.h>


#define DEB(x)
#define DEB1(x)
#include "sound_config.h"

#if defined(CONFIG_AD1848)

#include "ad1848_mixer.h"

typedef struct
  {
    int             base;
    int             irq;
    int             dual_dma;	/* 1, when two DMA channels allocated */
    unsigned char   MCE_bit;
    unsigned char   saved_regs[16];
    int             debug_flag;

    int             speed;
    unsigned char   speed_bits;
    int             channels;
    int             audio_format;
    unsigned char   format_bits;

    int             xfer_count;
    int             irq_mode;
    int             intr_active;
    int             opened;
    char           *chip_name;
    int             mode;
#define MD_1848		1
#define MD_4231		2
#define MD_4231A	3
#define MD_1845		4
#define MD_4232		5

    /* Mixer parameters */
    int             recmask;
    int             supported_devices;
    int             supported_rec_devices;
    unsigned short  levels[32];
    int             dev_no;
    volatile unsigned long timer_ticks;
    int             timer_running;
    int             irq_ok;
    int            *osp;
  }

ad1848_info;

static int      nr_ad1848_devs = 0;
static volatile char irq2dev[17] =
{-1, -1, -1, -1, -1, -1, -1, -1,
 -1, -1, -1, -1, -1, -1, -1, -1, -1};

static int      timer_installed = -1;

static char     mixer2codec[MAX_MIXER_DEV] =
{0};

static int      ad_format_mask[6 /*devc->mode */ ] =
{
  0,
  AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW,
  AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_U16_LE | AFMT_IMA_ADPCM,
  AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_U16_LE | AFMT_IMA_ADPCM,
  AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW,	/* AD1845 */
  AFMT_U8 | AFMT_S16_LE | AFMT_MU_LAW | AFMT_A_LAW | AFMT_U16_LE | AFMT_IMA_ADPCM
};

static ad1848_info dev_info[MAX_AUDIO_DEV];

#define io_Index_Addr(d)	((d)->base)
#define io_Indexed_Data(d)	((d)->base+1)
#define io_Status(d)		((d)->base+2)
#define io_Polled_IO(d)		((d)->base+3)

static int      ad1848_open (int dev, int mode);
static void     ad1848_close (int dev);
static int      ad1848_ioctl (int dev, unsigned int cmd, caddr_t arg, int local);
static void     ad1848_output_block (int dev, unsigned long buf, int count, int intrflag, int dma_restart);
static void     ad1848_start_input (int dev, unsigned long buf, int count, int intrflag, int dma_restart);
static int      ad1848_prepare_for_IO (int dev, int bsize, int bcount);
static void     ad1848_reset (int dev);
static void     ad1848_halt (int dev);
static void     ad1848_halt_input (int dev);
static void     ad1848_halt_output (int dev);
static void     ad1848_trigger (int dev, int bits);
static int      ad1848_tmr_install (int dev);
static void     ad1848_tmr_reprogram (int dev);

static int
ad_read (ad1848_info * devc, int reg)
{
  unsigned long   flags;
  int             x;
  int             timeout = 900000;

  while (timeout > 0 && inb (devc->base) == 0x80)	/*Are we initializing */
    timeout--;

  save_flags (flags);
  cli ();
  outb ((unsigned char) (reg & 0xff) | devc->MCE_bit, io_Index_Addr (devc));
  x = inb (io_Indexed_Data (devc));
  /*  printk("(%02x<-%02x) ", reg|devc->MCE_bit, x); */
  restore_flags (flags);

  return x;
}

static void
ad_write (ad1848_info * devc, int reg, int data)
{
  unsigned long   flags;
  int             timeout = 900000;

  while (timeout > 0 &&
	 inb (devc->base) == 0x80)	/*Are we initializing */
    timeout--;

  save_flags (flags);
  cli ();
  outb ((unsigned char) (reg & 0xff) | devc->MCE_bit, io_Index_Addr (devc));
  outb ((unsigned char) (data & 0xff), io_Indexed_Data (devc));
  /* printk("(%02x->%02x) ", reg|devc->MCE_bit, data); */
  restore_flags (flags);
}

static void
wait_for_calibration (ad1848_info * devc)
{
  int             timeout = 0;

  /*
     * Wait until the auto calibration process has finished.
     *
     * 1)       Wait until the chip becomes ready (reads don't return 0x80).
     * 2)       Wait until the ACI bit of I11 gets on and then off.
   */

  timeout = 100000;
  while (timeout > 0 && inb (devc->base) == 0x80)
    timeout--;
  if (inb (devc->base) & 0x80)
    printk ("ad1848: Auto calibration timed out(1).\n");

  timeout = 100;
  while (timeout > 0 && !(ad_read (devc, 11) & 0x20))
    timeout--;
  if (!(ad_read (devc, 11) & 0x20))
    return;

  timeout = 80000;
  while (timeout > 0 && ad_read (devc, 11) & 0x20)
    timeout--;
  if (ad_read (devc, 11) & 0x20)
    printk ("ad1848: Auto calibration timed out(3).\n");
}

static void
ad_mute (ad1848_info * devc)
{
  int             i;
  unsigned char   prev;

  if (devc->mode != MD_1848)
    return;
  /*
     * Save old register settings and mute output channels
   */
  for (i = 6; i < 8; i++)
    {
      prev = devc->saved_regs[i] = ad_read (devc, i);
      ad_write (devc, i, prev | 0x80);
    }
}

static void
ad_unmute (ad1848_info * devc)
{
  int             i;

  /*
     * Restore back old volume registers (unmute)
   */
  for (i = 6; i < 8; i++)
    {
      ad_write (devc, i, devc->saved_regs[i] & ~0x80);
    }
}

static void
ad_enter_MCE (ad1848_info * devc)
{
  unsigned long   flags;
  int             timeout = 1000;
  unsigned short  prev;

  while (timeout > 0 && inb (devc->base) == 0x80)	/*Are we initializing */
    timeout--;

  save_flags (flags);
  cli ();

  devc->MCE_bit = 0x40;
  prev = inb (io_Index_Addr (devc));
  if (prev & 0x40)
    {
      restore_flags (flags);
      return;
    }

  outb (devc->MCE_bit, io_Index_Addr (devc));
  restore_flags (flags);
}

static void
ad_leave_MCE (ad1848_info * devc)
{
  unsigned long   flags;
  unsigned char   prev;
  int             timeout = 1000;

  while (timeout > 0 && inb (devc->base) == 0x80)	/*Are we initializing */
    timeout--;

  save_flags (flags);
  cli ();

  devc->MCE_bit = 0x00;
  prev = inb (io_Index_Addr (devc));
  outb (0x00, io_Index_Addr (devc));	/* Clear the MCE bit */

  if ((prev & 0x40) == 0)	/* Not in MCE mode */
    {
      restore_flags (flags);
      return;
    }

  outb (0x00, io_Index_Addr (devc));	/* Clear the MCE bit */
  wait_for_calibration (devc);
  restore_flags (flags);
}


static int
ad1848_set_recmask (ad1848_info * devc, int mask)
{
  unsigned char   recdev;
  int             i, n;

  mask &= devc->supported_rec_devices;

  n = 0;
  for (i = 0; i < 32; i++)	/* Count selected device bits */
    if (mask & (1 << i))
      n++;

  if (n == 0)
    mask = SOUND_MASK_MIC;
  else if (n != 1)		/* Too many devices selected */
    {
      mask &= ~devc->recmask;	/* Filter out active settings */

      n = 0;
      for (i = 0; i < 32; i++)	/* Count selected device bits */
	if (mask & (1 << i))
	  n++;

      if (n != 1)
	mask = SOUND_MASK_MIC;
    }

  switch (mask)
    {
    case SOUND_MASK_MIC:
      recdev = 2;
      break;

    case SOUND_MASK_LINE:
    case SOUND_MASK_LINE3:
      recdev = 0;
      break;

    case SOUND_MASK_CD:
    case SOUND_MASK_LINE1:
      recdev = 1;
      break;

    case SOUND_MASK_IMIX:
      recdev = 3;
      break;

    default:
      mask = SOUND_MASK_MIC;
      recdev = 2;
    }

  recdev <<= 6;
  ad_write (devc, 0, (ad_read (devc, 0) & 0x3f) | recdev);
  ad_write (devc, 1, (ad_read (devc, 1) & 0x3f) | recdev);

  devc->recmask = mask;
  return mask;
}

static void
change_bits (unsigned char *regval, int dev, int chn, int newval)
{
  unsigned char   mask;
  int             shift;

  if (mix_devices[dev][chn].polarity == 1)	/* Reverse */
    newval = 100 - newval;

  mask = (1 << mix_devices[dev][chn].nbits) - 1;
  shift = mix_devices[dev][chn].bitpos;
  newval = (int) ((newval * mask) + 50) / 100;	/* Scale it */

  *regval &= ~(mask << shift);	/* Clear bits */
  *regval |= (newval & mask) << shift;	/* Set new value */
}

static int
ad1848_mixer_get (ad1848_info * devc, int dev)
{
  if (!((1 << dev) & devc->supported_devices))
    return -EINVAL;

  return devc->levels[dev];
}

static int
ad1848_mixer_set (ad1848_info * devc, int dev, int value)
{
  int             left = value & 0x000000ff;
  int             right = (value & 0x0000ff00) >> 8;
  int             retvol;

  int             regoffs;
  unsigned char   val;

  if (left > 100)
    left = 100;
  if (right > 100)
    right = 100;

  if (mix_devices[dev][RIGHT_CHN].nbits == 0)	/* Mono control */
    right = left;

  retvol = left | (right << 8);

  /* Scale volumes */
  left = mix_cvt[left];
  right = mix_cvt[right];

  /* Scale it again */
  left = mix_cvt[left];
  right = mix_cvt[right];

  if (dev > 31)
    return -EINVAL;

  if (!(devc->supported_devices & (1 << dev)))
    return -EINVAL;

  if (mix_devices[dev][LEFT_CHN].nbits == 0)
    return -EINVAL;

  devc->levels[dev] = retvol;

  /*
     * Set the left channel
   */

  regoffs = mix_devices[dev][LEFT_CHN].regno;
  val = ad_read (devc, regoffs);
  change_bits (&val, dev, LEFT_CHN, left);
  ad_write (devc, regoffs, val);
  devc->saved_regs[regoffs] = val;

  /*
     * Set the right channel
   */

  if (mix_devices[dev][RIGHT_CHN].nbits == 0)
    return retvol;		/* Was just a mono channel */

  regoffs = mix_devices[dev][RIGHT_CHN].regno;
  val = ad_read (devc, regoffs);
  change_bits (&val, dev, RIGHT_CHN, right);
  ad_write (devc, regoffs, val);
  devc->saved_regs[regoffs] = val;

  return retvol;
}

static void
ad1848_mixer_reset (ad1848_info * devc)
{
  int             i;

  switch (devc->mode)
    {
    case MD_4231:
    case MD_4231A:
    case MD_1845:
      devc->supported_devices = MODE2_MIXER_DEVICES;
      break;

    case MD_4232:
      devc->supported_devices = MODE3_MIXER_DEVICES;
      break;

    default:
      devc->supported_devices = MODE1_MIXER_DEVICES;
    }

  devc->supported_rec_devices = MODE1_REC_DEVICES;

  for (i = 0; i < SOUND_MIXER_NRDEVICES; i++)
    if (devc->supported_devices & (1 << i))
      ad1848_mixer_set (devc, i, default_mixer_levels[i]);
  ad1848_set_recmask (devc, SOUND_MASK_MIC);
}

static int
ad1848_mixer_ioctl (int dev, unsigned int cmd, caddr_t arg)
{
  ad1848_info    *devc;

  int             codec_dev = mixer2codec[dev];

  if (!codec_dev)
    return -ENXIO;

  codec_dev--;

  devc = (ad1848_info *) audio_devs[codec_dev]->devc;

  if (((cmd >> 8) & 0xff) == 'M')
    {

      if (_IOC_DIR (cmd) & _IOC_WRITE)
	switch (cmd & 0xff)
	  {
	  case SOUND_MIXER_RECSRC:
	    return snd_ioctl_return ((int *) arg, ad1848_set_recmask (devc, get_fs_long ((long *) arg)));
	    break;

	  default:
	    return snd_ioctl_return ((int *) arg, ad1848_mixer_set (devc, cmd & 0xff, get_fs_long ((long *) arg)));
	  }
      else
	switch (cmd & 0xff)	/*
				 * Return parameters
				 */
	  {

	  case SOUND_MIXER_RECSRC:
	    return snd_ioctl_return ((int *) arg, devc->recmask);
	    break;

	  case SOUND_MIXER_DEVMASK:
	    return snd_ioctl_return ((int *) arg, devc->supported_devices);
	    break;

	  case SOUND_MIXER_STEREODEVS:
	    return snd_ioctl_return ((int *) arg, devc->supported_devices & ~(SOUND_MASK_SPEAKER | SOUND_MASK_IMIX));
	    break;

	  case SOUND_MIXER_RECMASK:
	    return snd_ioctl_return ((int *) arg, devc->supported_rec_devices);
	    break;

	  case SOUND_MIXER_CAPS:
	    return snd_ioctl_return ((int *) arg, SOUND_CAP_EXCL_INPUT);
	    break;

	  default:
	    return snd_ioctl_return ((int *) arg, ad1848_mixer_get (devc, cmd & 0xff));
	  }
    }
  else
    return -EINVAL;
}

static struct audio_operations ad1848_pcm_operations[MAX_AUDIO_DEV] =
{
  {
    "Generic AD1848 codec",
    DMA_AUTOMODE,
    AFMT_U8,			/* Will be set later */
    NULL,
    ad1848_open,
    ad1848_close,
    ad1848_output_block,
    ad1848_start_input,
    ad1848_ioctl,
    ad1848_prepare_for_IO,
    ad1848_prepare_for_IO,
    ad1848_reset,
    ad1848_halt,
    NULL,
    NULL,
    ad1848_halt_input,
    ad1848_halt_output,
    ad1848_trigger
  }};

static struct mixer_operations ad1848_mixer_operations =
{
  "AD1848/CS4248/CS4231",
  ad1848_mixer_ioctl
};

static int
ad1848_open (int dev, int mode)
{
  ad1848_info    *devc = NULL;
  unsigned long   flags;

  if (dev < 0 || dev >= num_audiodevs)
    return -ENXIO;

  devc = (ad1848_info *) audio_devs[dev]->devc;


  save_flags (flags);
  cli ();
  if (devc->opened)
    {
      restore_flags (flags);
      printk ("ad1848: Already opened\n");
      return -EBUSY;
    }


  devc->dual_dma = 0;

  if (audio_devs[dev]->flags & DMA_DUPLEX)
    {
      devc->dual_dma = 1;
    }

  devc->intr_active = 0;
  devc->opened = 1;
  devc->irq_mode = 0;
  ad1848_trigger (dev, 0);
  restore_flags (flags);
/*
 * Mute output until the playback really starts. This decreases clicking (hope so).
 */
  ad_mute (devc);

  return 0;
}

static void
ad1848_close (int dev)
{
  unsigned long   flags;
  ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;

  DEB (printk ("ad1848_close(void)\n"));

  save_flags (flags);
  cli ();

  devc->intr_active = 0;
  ad1848_reset (dev);


  devc->opened = 0;
  devc->irq_mode = 0;

  ad_unmute (devc);
  restore_flags (flags);
}

static int
set_speed (ad1848_info * devc, int arg)
{
  /*
     * The sampling speed is encoded in the least significant nibble of I8. The
     * LSB selects the clock source (0=24.576 MHz, 1=16.9344 Mhz) and other
     * three bits select the divisor (indirectly):
     *
     * The available speeds are in the following table. Keep the speeds in
     * the increasing order.
   */
  typedef struct
  {
    int             speed;
    unsigned char   bits;
  }
  speed_struct;

  static speed_struct speed_table[] =
  {
    {5510, (0 << 1) | 1},
    {5510, (0 << 1) | 1},
    {6620, (7 << 1) | 1},
    {8000, (0 << 1) | 0},
    {9600, (7 << 1) | 0},
    {11025, (1 << 1) | 1},
    {16000, (1 << 1) | 0},
    {18900, (2 << 1) | 1},
    {22050, (3 << 1) | 1},
    {27420, (2 << 1) | 0},
    {32000, (3 << 1) | 0},
    {33075, (6 << 1) | 1},
    {37800, (4 << 1) | 1},
    {44100, (5 << 1) | 1},
    {48000, (6 << 1) | 0}
  };

  int             i, n, selected = -1;

  n = sizeof (speed_table) / sizeof (speed_struct);

  if (devc->mode == MD_1845)	/* AD1845 has different timer than others */
    {
      if (arg < 4000)
	arg = 4000;
      if (arg > 50000)
	arg = 50000;

      devc->speed = arg;
      devc->speed_bits = speed_table[3].bits;
      return devc->speed;
    }

  if (arg < speed_table[0].speed)
    selected = 0;
  if (arg > speed_table[n - 1].speed)
    selected = n - 1;

  for (i = 1 /*really */ ; selected == -1 && i < n; i++)
    if (speed_table[i].speed == arg)
      selected = i;
    else if (speed_table[i].speed > arg)
      {
	int             diff1, diff2;

	diff1 = arg - speed_table[i - 1].speed;
	diff2 = speed_table[i].speed - arg;

	if (diff1 < diff2)
	  selected = i - 1;
	else
	  selected = i;
      }

  if (selected == -1)
    {
      printk ("ad1848: Can't find speed???\n");
      selected = 3;
    }

  devc->speed = speed_table[selected].speed;
  devc->speed_bits = speed_table[selected].bits;
  return devc->speed;
}

static int
set_channels (ad1848_info * devc, int arg)
{
  if (arg != 1 && arg != 2)
    return devc->channels;

  devc->channels = arg;
  return arg;
}

static int
set_format (ad1848_info * devc, int arg)
{

  static struct format_tbl
  {
    int             format;
    unsigned char   bits;
  }
  format2bits[] =
  {
    {
      0, 0
    }
    ,
    {
      AFMT_MU_LAW, 1
    }
    ,
    {
      AFMT_A_LAW, 3
    }
    ,
    {
      AFMT_IMA_ADPCM, 5
    }
    ,
    {
      AFMT_U8, 0
    }
    ,
    {
      AFMT_S16_LE, 2
    }
    ,
    {
      AFMT_S16_BE, 6
    }
    ,
    {
      AFMT_S8, 0
    }
    ,
    {
      AFMT_U16_LE, 0
    }
    ,
    {
      AFMT_U16_BE, 0
    }
  };
  int             i, n = sizeof (format2bits) / sizeof (struct format_tbl);

  if (!(arg & ad_format_mask[devc->mode]))
    arg = AFMT_U8;

  devc->audio_format = arg;

  for (i = 0; i < n; i++)
    if (format2bits[i].format == arg)
      {
	if ((devc->format_bits = format2bits[i].bits) == 0)
	  return devc->audio_format = AFMT_U8;	/* Was not supported */

	return arg;
      }

  /* Still hanging here. Something must be terribly wrong */
  devc->format_bits = 0;
  return devc->audio_format = AFMT_U8;
}

static int
ad1848_ioctl (int dev, unsigned int cmd, caddr_t arg, int local)
{
  ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;

  switch (cmd)
    {
    case SOUND_PCM_WRITE_RATE:
      if (local)
	return set_speed (devc, (int) arg);
      return snd_ioctl_return ((int *) arg, set_speed (devc, get_fs_long ((long *) arg)));

    case SOUND_PCM_READ_RATE:
      if (local)
	return devc->speed;
      return snd_ioctl_return ((int *) arg, devc->speed);

    case SNDCTL_DSP_STEREO:
      if (local)
	return set_channels (devc, (int) arg + 1) - 1;
      return snd_ioctl_return ((int *) arg, set_channels (devc, get_fs_long ((long *) arg) + 1) - 1);

    case SOUND_PCM_WRITE_CHANNELS:
      if (local)
	return set_channels (devc, (int) arg);
      return snd_ioctl_return ((int *) arg, set_channels (devc, get_fs_long ((long *) arg)));

    case SOUND_PCM_READ_CHANNELS:
      if (local)
	return devc->channels;
      return snd_ioctl_return ((int *) arg, devc->channels);

    case SNDCTL_DSP_SAMPLESIZE:
      if (local)
	return set_format (devc, (int) arg);
      return snd_ioctl_return ((int *) arg, set_format (devc, get_fs_long ((long *) arg)));

    case SOUND_PCM_READ_BITS:
      if (local)
	return devc->audio_format;
      return snd_ioctl_return ((int *) arg, devc->audio_format);

    default:;
    }
  return -EINVAL;
}

static void
ad1848_output_block (int dev, unsigned long buf, int count, int intrflag, int dma_restart)
{
  unsigned long   flags, cnt;
  ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;

  cnt = count;

  if (devc->audio_format == AFMT_IMA_ADPCM)
    {
      cnt /= 4;
    }
  else
    {
      if (devc->audio_format & (AFMT_S16_LE | AFMT_S16_BE))	/* 16 bit data */
	cnt >>= 1;
    }
  if (devc->channels > 1)
    cnt >>= 1;
  cnt--;

  if (devc->irq_mode & PCM_ENABLE_OUTPUT && audio_devs[dev]->flags & DMA_AUTOMODE &&
      intrflag &&
      cnt == devc->xfer_count)
    {
      devc->irq_mode |= PCM_ENABLE_OUTPUT;
      devc->intr_active = 1;
      return;			/*
				 * Auto DMA mode on. No need to react
				 */
    }
  save_flags (flags);
  cli ();

  if (dma_restart)
    {
      ad_write (devc, 9, ad_read (devc, 9) & ~0x01);	/* Playback disable */
      /* ad1848_halt (dev); */
      DMAbuf_start_dma (dev, buf, count, DMA_MODE_WRITE);
    }

  ad_write (devc, 15, (unsigned char) (cnt & 0xff));
  ad_write (devc, 14, (unsigned char) ((cnt >> 8) & 0xff));

  ad_unmute (devc);

  devc->xfer_count = cnt;
  devc->irq_mode |= PCM_ENABLE_OUTPUT;
  devc->intr_active = 1;
  restore_flags (flags);
}

static void
ad1848_start_input (int dev, unsigned long buf, int count, int intrflag, int dma_restart)
{
  unsigned long   flags, cnt;
  ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;

  cnt = count;
  if (devc->audio_format == AFMT_IMA_ADPCM)
    {
      cnt /= 4;
    }
  else
    {
      if (devc->audio_format & (AFMT_S16_LE | AFMT_S16_BE))	/* 16 bit data */
	cnt >>= 1;
    }
  if (devc->channels > 1)
    cnt >>= 1;
  cnt--;

  if (devc->irq_mode & PCM_ENABLE_INPUT && audio_devs[dev]->flags & DMA_AUTOMODE &&
      intrflag &&
      cnt == devc->xfer_count)
    {
      devc->irq_mode |= PCM_ENABLE_INPUT;
      devc->intr_active = 1;
      return;			/*
				 * Auto DMA mode on. No need to react
				 */
    }
  save_flags (flags);
  cli ();

  if (dma_restart)
    {
      /* ad1848_halt (dev); */
      ad_write (devc, 9, ad_read (devc, 9) & ~0x02);	/* Capture disable */
      DMAbuf_start_dma (dev, buf, count, DMA_MODE_READ);
    }

  if (devc->mode == MD_1848 || !devc->dual_dma)		/* Single DMA channel mode */
    {
      ad_write (devc, 15, (unsigned char) (cnt & 0xff));
      ad_write (devc, 14, (unsigned char) ((cnt >> 8) & 0xff));
    }
  else
    /* Dual DMA channel mode */
    {
      ad_write (devc, 31, (unsigned char) (cnt & 0xff));
      ad_write (devc, 30, (unsigned char) ((cnt >> 8) & 0xff));
    }

  ad_unmute (devc);

  devc->xfer_count = cnt;
  devc->irq_mode |= PCM_ENABLE_INPUT;
  devc->intr_active = 1;
  restore_flags (flags);
}

static int
ad1848_prepare_for_IO (int dev, int bsize, int bcount)
{
  int             timeout;
  unsigned char   fs, old_fs, tmp = 0;
  unsigned long   flags;
  ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;

  if (devc->irq_mode)
    return 0;

  save_flags (flags);
  cli ();
  fs = devc->speed_bits | (devc->format_bits << 5);

  if (devc->channels > 1)
    fs |= 0x10;

  if (devc->mode == MD_1845)	/* Use alternate speed select registers */
    {
      fs &= 0xf0;		/* Mask off the rate select bits */

      ad_write (devc, 22, (devc->speed >> 8) & 0xff);	/* Speed MSB */
      ad_write (devc, 23, devc->speed & 0xff);	/* Speed LSB */
    }

  old_fs = ad_read (devc, 8);

  if (devc->mode != MD_4232)
    if (fs == old_fs)		/* No change */
      {
	restore_flags (flags);
	devc->xfer_count = 0;
	return 0;
      }

  ad_enter_MCE (devc);		/* Enables changes to the format select reg */

  if (devc->mode == MD_4232)
    {
      tmp = ad_read (devc, 16);
      ad_write (devc, 16, tmp | 0x30);
    }

  ad_write (devc, 8, fs);
  /*
   * Write to I8 starts resynchronization. Wait until it completes.
   */
  timeout = 10000;
  while (timeout > 0 && inb (devc->base) == 0x80)
    timeout--;

  /*
     * If mode == 2 (CS4231), set I28 also. It's the capture format register.
   */
  if (devc->mode != MD_1848)
    {
      ad_write (devc, 28, fs);

      /*
         * Write to I28 starts resynchronization. Wait until it completes.
       */
      timeout = 10000;
      while (timeout > 0 && inb (devc->base) == 0x80)
	timeout--;

    }

  if (devc->mode == MD_4232)
    ad_write (devc, 16, tmp & ~0x30);

  ad_leave_MCE (devc);		/*
				 * Starts the calibration process.
				 */
  restore_flags (flags);
  devc->xfer_count = 0;

#ifdef CONFIG_SEQUENCER
  if (dev == timer_installed && devc->timer_running)
    if ((fs & 0x01) != (old_fs & 0x01))
      {
	ad1848_tmr_reprogram (dev);
      }
#endif
  return 0;
}

static void
ad1848_reset (int dev)
{
  ad1848_halt (dev);
}

static void
ad1848_halt (int dev)
{
  ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;

  unsigned char   bits = ad_read (devc, 9);

  if (bits & 0x01)
    ad1848_halt_output (dev);

  if (bits & 0x02)
    ad1848_halt_input (dev);
}

static void
ad1848_halt_input (int dev)
{
  ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;
  unsigned long   flags;

  save_flags (flags);
  cli ();

  ad_mute (devc);

  if (devc->mode == MD_4232)	/* Use applied black magic */
    {
      int             tmout;

      disable_dma (audio_devs[dev]->dmachan1);

      for (tmout = 0; tmout < 100000; tmout++)
	if (ad_read (devc, 11) & 0x10)
	  break;
      ad_write (devc, 9, ad_read (devc, 9) & ~0x01);	/* Stop playback */

      enable_dma (audio_devs[dev]->dmachan1);
      restore_flags (flags);
      return;
    }
  ad_write (devc, 9, ad_read (devc, 9) & ~0x02);	/* Stop capture */


  outb (0, io_Status (devc));	/* Clear interrupt status */
  outb (0, io_Status (devc));	/* Clear interrupt status */

  devc->irq_mode &= ~PCM_ENABLE_INPUT;

  restore_flags (flags);
}

static void
ad1848_halt_output (int dev)
{
  ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;
  unsigned long   flags;

  save_flags (flags);
  cli ();

  ad_mute (devc);
  if (devc->mode == MD_4232)	/* Use applied black magic */
    {
      int             tmout;

      disable_dma (audio_devs[dev]->dmachan1);

      for (tmout = 0; tmout < 100000; tmout++)
	if (ad_read (devc, 11) & 0x10)
	  break;
      ad_write (devc, 9, ad_read (devc, 9) & ~0x01);	/* Stop playback */

      enable_dma (audio_devs[dev]->dmachan1);
      restore_flags (flags);
      return;
    }
  ad_write (devc, 9, ad_read (devc, 9) & ~0x01);	/* Stop playback */


  outb (0, io_Status (devc));	/* Clear interrupt status */
  outb (0, io_Status (devc));	/* Clear interrupt status */

  devc->irq_mode &= ~PCM_ENABLE_OUTPUT;

  restore_flags (flags);
}

static void
ad1848_trigger (int dev, int state)
{
  ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;
  unsigned long   flags;
  unsigned char   tmp;

  save_flags (flags);
  cli ();
  state &= devc->irq_mode;

  tmp = ad_read (devc, 9) & ~0x03;
  if (state & PCM_ENABLE_INPUT)
    tmp |= 0x02;
  if (state & PCM_ENABLE_OUTPUT)
    tmp |= 0x01;
  ad_write (devc, 9, tmp);

  restore_flags (flags);
}

int
ad1848_detect (int io_base, int *ad_flags, int *osp)
{

  unsigned char   tmp;
  ad1848_info    *devc = &dev_info[nr_ad1848_devs];
  unsigned char   tmp1 = 0xff, tmp2 = 0xff;
  int             i;

  DDB (printk ("ad1848_detect(%x)\n", io_base));

  if (ad_flags)
    *ad_flags = 0;

  if (nr_ad1848_devs >= MAX_AUDIO_DEV)
    {
      DDB (printk ("ad1848 detect error - step 0\n"));
      return 0;
    }
  if (check_region (io_base, 4))
    {
      printk ("\n\nad1848.c: Port %x not free.\n\n", io_base);
      return 0;
    }

  devc->base = io_base;
  devc->irq_ok = 0;
  devc->timer_running = 0;
  devc->MCE_bit = 0x40;
  devc->irq = 0;
  devc->opened = 0;
  devc->chip_name = "AD1848";
  devc->mode = MD_1848;		/* AD1848 or CS4248 */
  devc->osp = osp;
  devc->debug_flag = 0;

  /*
     * Check that the I/O address is in use.
     *
     * The bit 0x80 of the base I/O port is known to be 0 after the
     * chip has performed its power-on initialization. Just assume
     * this has happened before the OS is starting.
     *
     * If the I/O address is unused, it typically returns 0xff.
   */

  DDB (printk ("ad1848_detect() - step A\n"));
  if ((inb (devc->base) & 0x80) != 0x00)	/* Not a AD1848 */
    {
      DDB (printk ("ad1848 detect error - step A (%02x)\n",
		   inb (devc->base)));
      return 0;
    }

  DDB (printk ("ad1848: regs: "));
  for (i = 0; i < 32; i++)
    DDB (printk ("%02x ", ad_read (devc, i)));
  DDB (printk ("\n"));

  /*
     * Test if it's possible to change contents of the indirect registers.
     * Registers 0 and 1 are ADC volume registers. The bit 0x10 is read only
     * so try to avoid using it.
   */

  DDB (printk ("ad1848_detect() - step B\n"));
  ad_write (devc, 0, 0xaa);
  ad_write (devc, 1, 0x45);	/* 0x55 with bit 0x10 clear */

  if ((tmp1 = ad_read (devc, 0)) != 0xaa || (tmp2 = ad_read (devc, 1)) != 0x45)
    {
      DDB (printk ("ad1848 detect error - step B (%x/%x)\n", tmp1, tmp2));
      return 0;
    }

  DDB (printk ("ad1848_detect() - step C\n"));
  ad_write (devc, 0, 0x45);
  ad_write (devc, 1, 0xaa);

  if ((tmp1 = ad_read (devc, 0)) != 0x45 || (tmp2 = ad_read (devc, 1)) != 0xaa)
    {
      DDB (printk ("ad1848 detect error - step C (%x/%x)\n", tmp1, tmp2));
      return 0;
    }

  /*
     * The indirect register I12 has some read only bits. Let's
     * try to change them.
   */

  DDB (printk ("ad1848_detect() - step D\n"));
  tmp = ad_read (devc, 12);
  ad_write (devc, 12, (~tmp) & 0x0f);

  if ((tmp & 0x0f) != ((tmp1 = ad_read (devc, 12)) & 0x0f))
    {
      DDB (printk ("ad1848 detect error - step D (%x)\n", tmp1));
      return 0;
    }

  /*
     * NOTE! Last 4 bits of the reg I12 tell the chip revision.
     *   0x01=RevB and 0x0A=RevC.
   */

  /*
     * The original AD1848/CS4248 has just 15 indirect registers. This means
     * that I0 and I16 should return the same value (etc.).
     * Ensure that the Mode2 enable bit of I12 is 0. Otherwise this test fails
     * with CS4231.
   */


  /*
     * Try to switch the chip to mode2 (CS4231) by setting the MODE2 bit (0x40).
     * The bit 0x80 is always 1 in CS4248 and CS4231.
   */

  DDB (printk ("ad1848_detect() - step G\n"));
  ad_write (devc, 12, 0x40);	/* Set mode2, clear 0x80 */

  tmp1 = ad_read (devc, 12);
  if (tmp1 & 0x80)
    {
      if (ad_flags)
	*ad_flags |= AD_F_CS4248;

      devc->chip_name = "CS4248";	/* Our best knowledge just now */
    }

  if ((tmp1 & 0xc0) == (0x80 | 0x40))
    {
      /*
         *      CS4231 detected - is it?
         *
         *      Verify that setting I0 doesn't change I16.
       */
      DDB (printk ("ad1848_detect() - step H\n"));
      ad_write (devc, 16, 0);	/* Set I16 to known value */

      ad_write (devc, 0, 0x45);
      if ((tmp1 = ad_read (devc, 16)) != 0x45)	/* No change -> CS4231? */
	{

	  ad_write (devc, 0, 0xaa);
	  if ((tmp1 = ad_read (devc, 16)) == 0xaa)	/* Rotten bits? */
	    {
	      DDB (printk ("ad1848 detect error - step H(%x)\n", tmp1));
	      return 0;
	    }

	  /*
	     * Verify that some bits of I25 are read only.
	   */

	  DDB (printk ("ad1848_detect() - step I\n"));
	  tmp1 = ad_read (devc, 25);	/* Original bits */
	  ad_write (devc, 25, ~tmp1);	/* Invert all bits */
	  if ((ad_read (devc, 25) & 0xe7) == (tmp1 & 0xe7))
	    {
	      int             id;

	      /*
	       *      It's at least CS4231
	       */
	      devc->chip_name = "CS4231";

	      devc->mode = MD_4231;

	      /*
	       * It could be an AD1845 or CS4231A as well.
	       * CS4231 and AD1845 report the same revision info in I25
	       * while the CS4231A reports different.
	       */

	      DDB (printk ("ad1848_detect() - step I\n"));
	      id = ad_read (devc, 25) & 0xe7;

	      switch (id)
		{

		case 0xa0:
		  devc->chip_name = "CS4231A";
		  devc->mode = MD_4231A;
		  break;

		case 0xa2:
		  devc->chip_name = "CS4232";
		  devc->mode = MD_4232;
		  break;

		case 0xb2:
		  devc->chip_name = "CS4232A";
		  devc->mode = MD_4232;
		  break;

		case 0x80:
		  {
		    /* 
		     * It must be a CS4231 or AD1845. The register I23 of
		     * CS4231 is undefined and it appears to be read only.
		     * AD1845 uses I23 for setting sample rate. Assume
		     * the chip is AD1845 if I23 is changeable.
		     */

		    unsigned char   tmp = ad_read (devc, 23);

		    ad_write (devc, 23, ~tmp);
		    if (ad_read (devc, 23) != tmp)	/* AD1845 ? */
		      {
			devc->chip_name = "AD1845";
			devc->mode = MD_1845;
		      }

		    ad_write (devc, 23, tmp);	/* Restore */
		  }
		  break;

		default:	/* Assume CS4231 */
		  devc->mode = MD_4231;

		}
	    }
	  ad_write (devc, 25, tmp1);	/* Restore bits */

	  DDB (printk ("ad1848_detect() - step K\n"));
	}
    }

  DDB (printk ("ad1848_detect() - step L\n"));
  if (ad_flags)
    {
      if (devc->mode != MD_1848)
	*ad_flags |= AD_F_CS4231;
    }

  DDB (printk ("ad1848_detect() - Detected OK\n"));
  return 1;
}

void
ad1848_init (char *name, int io_base, int irq, int dma_playback, int dma_capture, int share_dma, int *osp)
{
  /*
     * NOTE! If irq < 0, there is another driver which has allocated the IRQ
     *   so that this driver doesn't need to allocate/deallocate it.
     *   The actually used IRQ is ABS(irq).
   */

  /*
     * Initial values for the indirect registers of CS4248/AD1848.
   */
  static int      init_values[] =
  {
    0xa8, 0xa8, 0x08, 0x08, 0x08, 0x08, 0x00, 0x00,
    0x00, 0x0c, 0x02, 0x00, 0x8a, 0x01, 0x00, 0x00,

  /* Positions 16 to 31 just for CS4231 */
    0x80, 0x00, 0x10, 0x10, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
  };
  int             i, my_dev;

  ad1848_info    *devc = &dev_info[nr_ad1848_devs];

  if (!ad1848_detect (io_base, NULL, osp))
    return;

  request_region (devc->base, 4, devc->chip_name);

  devc->irq = (irq > 0) ? irq : 0;
  devc->opened = 0;
  devc->timer_ticks = 0;
  devc->osp = osp;

  if (nr_ad1848_devs != 0)
    {
      memcpy ((char *) &ad1848_pcm_operations[nr_ad1848_devs],
	      (char *) &ad1848_pcm_operations[0],
	      sizeof (struct audio_operations));
    }

  for (i = 0; i < 16; i++)
    ad_write (devc, i, init_values[i]);

  ad_mute (devc);		/* Initialize some variables */
  ad_unmute (devc);		/* Leave it unmuted now */

  if (devc->mode > MD_1848)
    {
      if (dma_capture == dma_playback || dma_capture == -1 || dma_playback == -1)
	{
	  ad_write (devc, 9, ad_read (devc, 9) | 0x04);		/* Single DMA mode */
	  ad1848_pcm_operations[nr_ad1848_devs].flags &= ~DMA_DUPLEX;
	}
      else
	{
	  ad_write (devc, 9, ad_read (devc, 9) & ~0x04);	/* Dual DMA mode */
	  ad1848_pcm_operations[nr_ad1848_devs].flags |= DMA_DUPLEX;
	}

      ad_write (devc, 12, ad_read (devc, 12) | 0x40);	/* Mode2 = enabled */
      for (i = 16; i < 32; i++)
	ad_write (devc, i, init_values[i]);


      if (devc->mode == MD_1845)
	ad_write (devc, 27, ad_read (devc, 27) | 0x08);		/* Alternate freq select enabled */
    }
  else
    {
      ad1848_pcm_operations[nr_ad1848_devs].flags &= ~DMA_DUPLEX;
      ad_write (devc, 9, ad_read (devc, 9) | 0x04);	/* Single DMA mode */
    }

  outb (0, io_Status (devc));	/* Clear pending interrupts */

  if (name != NULL && name[0] != 0)
    sprintf (ad1848_pcm_operations[nr_ad1848_devs].name,
	     "%s (%s)", name, devc->chip_name);
  else
    sprintf (ad1848_pcm_operations[nr_ad1848_devs].name,
	     "Generic audio codec (%s)", devc->chip_name);

  conf_printf2 (ad1848_pcm_operations[nr_ad1848_devs].name,
		devc->base, devc->irq, dma_playback, dma_capture);

  if (num_audiodevs < MAX_AUDIO_DEV)
    {
      audio_devs[my_dev = num_audiodevs++] = &ad1848_pcm_operations[nr_ad1848_devs];
      if (irq > 0)
	{
	  audio_devs[my_dev]->devc = devc;
	  irq2dev[irq] = devc->dev_no = my_dev;
	  if (snd_set_irq_handler (devc->irq, ad1848_interrupt,
				   audio_devs[my_dev]->name,
				   devc->osp) < 0)
	    {
	      printk ("ad1848: IRQ in use\n");
	    }

#ifdef NO_IRQ_TEST
	  if (devc->mode != MD_1848)
	    {
	      int             x;
	      unsigned char   tmp = ad_read (devc, 16);

	      devc->timer_ticks = 0;

	      ad_write (devc, 21, 0x00);	/* Timer msb */
	      ad_write (devc, 20, 0x10);	/* Timer lsb */

	      ad_write (devc, 16, tmp | 0x40);	/* Enable timer */
	      for (x = 0; x < 100000 && devc->timer_ticks == 0; x++);
	      ad_write (devc, 16, tmp & ~0x40);		/* Disable timer */

	      if (devc->timer_ticks == 0)
		printk ("[IRQ conflict???]");
	      else
		devc->irq_ok = 1;

	    }
	  else
	    devc->irq_ok = 1;	/* Couldn't test. assume it's OK */
#else
	  devc->irq_ok = 1;
#endif
	}
      else if (irq < 0)
	irq2dev[-irq] = devc->dev_no = my_dev;

      audio_devs[my_dev]->dmachan1 = dma_playback;
      audio_devs[my_dev]->dmachan2 = dma_capture;
      audio_devs[my_dev]->buffsize = DSP_BUFFSIZE;
      audio_devs[my_dev]->devc = devc;
      audio_devs[my_dev]->format_mask = ad_format_mask[devc->mode];
      nr_ad1848_devs++;

#ifdef CONFIG_SEQUENCER
      if (devc->mode != MD_1848 && devc->mode != MD_1845 && devc->irq_ok)
	ad1848_tmr_install (my_dev);
#endif

      if (!share_dma)
	{
	  if (sound_alloc_dma (dma_playback, "Sound System"))
	    printk ("ad1848.c: Can't allocate DMA%d\n", dma_playback);

	  if (dma_capture != dma_playback)
	    if (sound_alloc_dma (dma_capture, "Sound System (capture)"))
	      printk ("ad1848.c: Can't allocate DMA%d\n", dma_capture);
	}

      /*
         * Toggle the MCE bit. It completes the initialization phase.
       */

      ad_enter_MCE (devc);	/* In case the bit was off */
      ad_leave_MCE (devc);

      if (num_mixers < MAX_MIXER_DEV)
	{
	  mixer2codec[num_mixers] = my_dev + 1;
	  audio_devs[my_dev]->mixer_dev = num_mixers;
	  mixer_devs[num_mixers++] = &ad1848_mixer_operations;
	  ad1848_mixer_reset (devc);
	}
    }
  else
    printk ("AD1848: Too many PCM devices available\n");
}

void
ad1848_unload (int io_base, int irq, int dma_playback, int dma_capture, int share_dma)
{
  int             i, dev = 0;
  ad1848_info    *devc = NULL;

  for (i = 0; devc == NULL && i < nr_ad1848_devs; i++)
    if (dev_info[i].base == io_base)
      {
	devc = &dev_info[i];
	dev = devc->dev_no;
      }

  if (devc != NULL)
    {
      release_region (devc->base, 4);

      if (!share_dma)
	{
	  if (irq > 0)
	    snd_release_irq (devc->irq);

	  sound_free_dma (audio_devs[dev]->dmachan1);

	  if (audio_devs[dev]->dmachan2 != audio_devs[dev]->dmachan1)
	    sound_free_dma (audio_devs[dev]->dmachan2);
	}
    }
  else
    printk ("ad1848: Can't find device to be unloaded. Base=%x\n",
	    io_base);
}

void
ad1848_interrupt (int irq, void *dev_id, struct pt_regs *dummy)
{
  unsigned char   status;
  ad1848_info    *devc;
  int             dev;
  int             alt_stat = 0xff;

  if (irq < 0 || irq > 15)
    {
      dev = -1;
    }
  else
    dev = irq2dev[irq];

  if (dev < 0 || dev >= num_audiodevs)
    {
      for (irq = 0; irq < 17; irq++)
	if (irq2dev[irq] != -1)
	  break;

      if (irq > 15)
	{
	  /* printk ("ad1848.c: Bogus interrupt %d\n", irq); */
	  return;
	}

      dev = irq2dev[irq];
      devc = (ad1848_info *) audio_devs[dev]->devc;
    }
  else
    devc = (ad1848_info *) audio_devs[dev]->devc;

  status = inb (io_Status (devc));

  if (status == 0x80)
    printk ("ad1848_interrupt: Why?\n");

  if (status & 0x01)
    {

      if (devc->mode != MD_1848)
	alt_stat = ad_read (devc, 24);

      if (devc->opened && devc->irq_mode & PCM_ENABLE_INPUT && alt_stat & 0x20)
	{
	  DMAbuf_inputintr (dev);
	}

      if (devc->opened && devc->irq_mode & PCM_ENABLE_OUTPUT &&
	  alt_stat & 0x10)
	{
	  DMAbuf_outputintr (dev, 1);
	}

      if (devc->mode != MD_1848 && alt_stat & 0x40)	/* Timer interrupt */
	{
	  devc->timer_ticks++;
#ifdef CONFIG_SEQUENCER
	  if (timer_installed == dev && devc->timer_running)
	    sound_timer_interrupt ();
#endif
	}
    }

  if (devc->mode != MD_1848)
    ad_write (devc, 24, ad_read (devc, 24) & ~alt_stat);	/* Selective ack */
  else
    outb (0, io_Status (devc));	/* Clear interrupt status */
}

/*
 * Some extra code for the MS Sound System
 */

void
check_opl3 (int base, struct address_info *hw_config)
{

#ifdef CONFIG_YM3812
  if (check_region (base, 4))
    {
      printk ("\n\nopl3.c: I/O port %x already in use\n\n", base);
      return;
    }

  if (!opl3_detect (base, hw_config->osp))
    return;

  opl3_init (0, base, hw_config->osp);
  request_region (base, 4, "OPL3/OPL2");
#endif
}

int
probe_ms_sound (struct address_info *hw_config)
{
  unsigned char   tmp;

  DDB (printk ("Entered probe_ms_sound(%x, %d)\n", hw_config->io_base, hw_config->card_subtype));

  if (check_region (hw_config->io_base, 8))
    {
      printk ("MSS: I/O port conflict\n");
      return 0;
    }

  if (hw_config->card_subtype == 1)	/* Has no IRQ/DMA registers */
    {
      /* check_opl3(0x388, hw_config); */
      return ad1848_detect (hw_config->io_base + 4, NULL, hw_config->osp);
    }

  /*
     * Check if the IO port returns valid signature. The original MS Sound
     * system returns 0x04 while some cards (AudioTriX Pro for example)
     * return 0x00 or 0x0f.
   */

  if ((tmp = inb (hw_config->io_base + 3)) == 0xff)	/* Bus float */
    {
      int             ret;

      DDB (printk ("I/O address is inactive (%x)\n", tmp));
      if (!(ret = ad1848_detect (hw_config->io_base + 4, NULL, hw_config->osp)))
	return 0;
      return 1;
    }
  if ((tmp & 0x3f) != 0x04 &&
      (tmp & 0x3f) != 0x0f &&
      (tmp & 0x3f) != 0x00)
    {
      int             ret;

      DDB (printk ("No MSS signature detected on port 0x%x (0x%x)\n",
		   hw_config->io_base, inb (hw_config->io_base + 3)));
      DDB (printk ("Trying to detect codec anyway but IRQ/DMA may not work\n"));
      if (!(ret = ad1848_detect (hw_config->io_base + 4, NULL, hw_config->osp)))
	return 0;

      return 1;
    }

  if (hw_config->irq > 11)
    {
      printk ("MSS: Bad IRQ %d\n", hw_config->irq);
      return 0;
    }

  if (hw_config->dma != 0 && hw_config->dma != 1 && hw_config->dma != 3)
    {
      printk ("MSS: Bad DMA %d\n", hw_config->dma);
      return 0;
    }

  /*
     * Check that DMA0 is not in use with a 8 bit board.
   */

  if (hw_config->dma == 0 && inb (hw_config->io_base + 3) & 0x80)
    {
      printk ("MSS: Can't use DMA0 with a 8 bit card/slot\n");
      return 0;
    }

  if (hw_config->irq > 7 && hw_config->irq != 9 && inb (hw_config->io_base + 3) & 0x80)
    {
      printk ("MSS: Can't use IRQ%d with a 8 bit card/slot\n", hw_config->irq);
      return 0;
    }

  return ad1848_detect (hw_config->io_base + 4, NULL, hw_config->osp);
}

long
attach_ms_sound (long mem_start, struct address_info *hw_config)
{
  static char     interrupt_bits[12] =
  {
    -1, -1, -1, -1, -1, -1, -1, 0x08, -1, 0x10, 0x18, 0x20
  };
  char            bits;

  static char     dma_bits[4] =
  {
    1, 2, 0, 3
  };

  int             config_port = hw_config->io_base + 0;
  int             version_port = hw_config->io_base + 3;

  if (!ad1848_detect (hw_config->io_base + 4, NULL, hw_config->osp))
    return mem_start;

  if (hw_config->card_subtype == 1)	/* Has no IRQ/DMA registers */
    {
      ad1848_init ("MS Sound System", hw_config->io_base + 4,
		   hw_config->irq,
		   hw_config->dma,
		   hw_config->dma2, 0, hw_config->osp);
      request_region (hw_config->io_base, 4, "WSS config");
      return mem_start;
    }

  /*
     * Set the IRQ and DMA addresses.
   */

  bits = interrupt_bits[hw_config->irq];
  if (bits == -1)
    return mem_start;

  outb (bits | 0x40, config_port);
  if ((inb (version_port) & 0x40) == 0)
    printk ("[IRQ Conflict?]");

  outb (bits | dma_bits[hw_config->dma], config_port);	/* Write IRQ+DMA setup */

  ad1848_init ("MS Sound System", hw_config->io_base + 4,
	       hw_config->irq,
	       hw_config->dma,
	       hw_config->dma, 0, hw_config->osp);
  request_region (hw_config->io_base, 4, "WSS config");
  return mem_start;
}

void
unload_ms_sound (struct address_info *hw_config)
{
  ad1848_unload (hw_config->io_base + 4,
		 hw_config->irq,
		 hw_config->dma,
		 hw_config->dma, 0);
  release_region (hw_config->io_base, 4);
}

/*
 * WSS compatible PnP codec support
 */

int
probe_pnp_ad1848 (struct address_info *hw_config)
{
  return ad1848_detect (hw_config->io_base, NULL, hw_config->osp);
}

long
attach_pnp_ad1848 (long mem_start, struct address_info *hw_config)
{

  ad1848_init (hw_config->name, hw_config->io_base,
	       hw_config->irq,
	       hw_config->dma,
	       hw_config->dma2, 0, hw_config->osp);
  return mem_start;
}

void
unload_pnp_ad1848 (struct address_info *hw_config)
{
  ad1848_unload (hw_config->io_base,
		 hw_config->irq,
		 hw_config->dma,
		 hw_config->dma2, 0);
  release_region (hw_config->io_base, 4);
}

#ifdef CONFIG_SEQUENCER
/*
 * Timer stuff (for /dev/music).
 */

static unsigned int current_interval = 0;

static unsigned int
ad1848_tmr_start (int dev, unsigned int usecs)
{
  unsigned long   flags;
  ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;
  unsigned long   xtal_nsecs;	/* nanoseconds per xtal oscillator tick */
  unsigned long   divider;

  save_flags (flags);
  cli ();

/*
 * Length of the timer interval (in nanoseconds) depends on the
 * selected crystal oscillator. Check this from bit 0x01 of I8.
 *
 * AD1845 has just one oscillator which has cycle time of 10.050 us
 * (when a 24.576 MHz xtal oscillator is used).
 *
 * Convert requested interval to nanoseconds before computing
 * the timer divider.
 */

  if (devc->mode == MD_1845)
    xtal_nsecs = 10050;
  else if (ad_read (devc, 8) & 0x01)
    xtal_nsecs = 9920;
  else
    xtal_nsecs = 9969;

  divider = (usecs * 1000 + xtal_nsecs / 2) / xtal_nsecs;

  if (divider < 100)		/* Don't allow shorter intervals than about 1ms */
    divider = 100;

  if (divider > 65535)		/* Overflow check */
    divider = 65535;

  ad_write (devc, 21, (divider >> 8) & 0xff);	/* Set upper bits */
  ad_write (devc, 20, divider & 0xff);	/* Set lower bits */
  ad_write (devc, 16, ad_read (devc, 16) | 0x40);	/* Start the timer */
  devc->timer_running = 1;
  restore_flags (flags);

  return current_interval = (divider * xtal_nsecs + 500) / 1000;
}

static void
ad1848_tmr_reprogram (int dev)
{
/*
 *    Audio driver has changed sampling rate so that a different xtal
 *      oscillator was selected. We have to reprogram the timer rate.
 */

  ad1848_tmr_start (dev, current_interval);
  sound_timer_syncinterval (current_interval);
}

static void
ad1848_tmr_disable (int dev)
{
  unsigned long   flags;
  ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;

  save_flags (flags);
  cli ();
  ad_write (devc, 16, ad_read (devc, 16) & ~0x40);
  devc->timer_running = 0;
  restore_flags (flags);
}

static void
ad1848_tmr_restart (int dev)
{
  unsigned long   flags;
  ad1848_info    *devc = (ad1848_info *) audio_devs[dev]->devc;

  if (current_interval == 0)
    return;

  save_flags (flags);
  cli ();
  ad_write (devc, 16, ad_read (devc, 16) | 0x40);
  devc->timer_running = 1;
  restore_flags (flags);
}

static struct sound_lowlev_timer ad1848_tmr =
{
  0,
  ad1848_tmr_start,
  ad1848_tmr_disable,
  ad1848_tmr_restart
};

static int
ad1848_tmr_install (int dev)
{

  if (timer_installed != -1)
    return 0;			/* Don't install another timer */

  timer_installed = ad1848_tmr.dev = dev;
  sound_timer_init (&ad1848_tmr, audio_devs[dev]->name);

  return 1;
}
#endif
#endif
