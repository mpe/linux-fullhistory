/*
 * sound/sb16_dsp.c
 *
 * The low level driver for the SoundBlaster DSP chip.
 *
 * (C) 1993 J. Schubert (jsb@sth.ruhr-uni-bochum.de)
 *
 * based on SB-driver by (C) Hannu Savolainen
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

#define DEB(x)
#define DEB1(x)
/*
 * #define DEB_DMARES
 */
#include "sound_config.h"
#include "sb.h"
#include "sb_mixer.h"

#if defined(CONFIG_SB) && defined(CONFIG_AUDIO)

extern int      sbc_base;
extern sound_os_info *sb_osp;

static int      sb16_dsp_ok = 0;
static int      dsp_16bit = 0;
static int      dsp_stereo = 0;
static int      dsp_current_speed = 8000;
static int      dsp_busy = 0;
static int      dma16, dma8;
static int      trigger_bits = 0;
static unsigned long dsp_count = 0;

static int      irq_mode = IMODE_NONE;
static int      my_dev = 0;

static volatile int intr_active = 0;

static int      sb16_dsp_open (int dev, int mode);
static void     sb16_dsp_close (int dev);
static void     sb16_dsp_output_block (int dev, unsigned long buf, int count, int intrflag, int dma_restart);
static void     sb16_dsp_start_input (int dev, unsigned long buf, int count, int intrflag, int dma_restart);
static int      sb16_dsp_ioctl (int dev, unsigned int cmd, ioctl_arg arg, int local);
static int      sb16_dsp_prepare_for_input (int dev, int bsize, int bcount);
static int      sb16_dsp_prepare_for_output (int dev, int bsize, int bcount);
static void     sb16_dsp_reset (int dev);
static void     sb16_dsp_halt (int dev);
static void     sb16_dsp_trigger (int dev, int bits);
static int      dsp_set_speed (int);
static int      dsp_set_stereo (int);
static void     dsp_cleanup (void);
int             sb_reset_dsp (void);

static struct audio_operations sb16_dsp_operations =
{
  "SoundBlaster 16",
  DMA_AUTOMODE,
  AFMT_U8 | AFMT_S16_LE,
  NULL,
  sb16_dsp_open,
  sb16_dsp_close,
  sb16_dsp_output_block,
  sb16_dsp_start_input,
  sb16_dsp_ioctl,
  sb16_dsp_prepare_for_input,
  sb16_dsp_prepare_for_output,
  sb16_dsp_reset,
  sb16_dsp_halt,
  NULL,
  NULL,
  NULL,
  NULL,
  sb16_dsp_trigger
};

static int
sb_dsp_command01 (unsigned char val)
{
  int             i = 1 << 16;

  while (--i & (!inb (DSP_STATUS) & 0x80));
  if (!i)
    printk ("SB16 sb_dsp_command01 Timeout\n");
  return sb_dsp_command (val);
}

static int
dsp_set_speed (int mode)
{
  DEB (printk ("dsp_set_speed(%d)\n", mode));
  if (mode)
    {
      if (mode < 5000)
	mode = 5000;
      if (mode > 44100)
	mode = 44100;
      dsp_current_speed = mode;
    }
  return mode;
}

static int
dsp_set_stereo (int mode)
{
  DEB (printk ("dsp_set_stereo(%d)\n", mode));

  dsp_stereo = mode;

  return mode;
}

static int
dsp_set_bits (int arg)
{
  DEB (printk ("dsp_set_bits(%d)\n", arg));

  if (arg)
    switch (arg)
      {
      case 8:
	dsp_16bit = 0;
	break;
      case 16:
	dsp_16bit = 1;
	break;
      default:
	dsp_16bit = 0;
      }
  return dsp_16bit ? 16 : 8;
}

static int
sb16_dsp_ioctl (int dev, unsigned int cmd, ioctl_arg arg, int local)
{
  switch (cmd)
    {
    case SOUND_PCM_WRITE_RATE:
      if (local)
	return dsp_set_speed ((int) arg);
      return snd_ioctl_return ((int *) arg, dsp_set_speed (get_fs_long ((long *) arg)));

    case SOUND_PCM_READ_RATE:
      if (local)
	return dsp_current_speed;
      return snd_ioctl_return ((int *) arg, dsp_current_speed);

    case SNDCTL_DSP_STEREO:
      if (local)
	return dsp_set_stereo ((int) arg);
      return snd_ioctl_return ((int *) arg, dsp_set_stereo (get_fs_long ((long *) arg)));

    case SOUND_PCM_WRITE_CHANNELS:
      if (local)
	return dsp_set_stereo ((int) arg - 1) + 1;
      return snd_ioctl_return ((int *) arg, dsp_set_stereo (get_fs_long ((long *) arg) - 1) + 1);

    case SOUND_PCM_READ_CHANNELS:
      if (local)
	return dsp_stereo + 1;
      return snd_ioctl_return ((int *) arg, dsp_stereo + 1);

    case SNDCTL_DSP_SETFMT:
      if (local)
	return dsp_set_bits ((int) arg);
      return snd_ioctl_return ((int *) arg, dsp_set_bits (get_fs_long ((long *) arg)));

    case SOUND_PCM_READ_BITS:
      if (local)
	return dsp_16bit ? 16 : 8;
      return snd_ioctl_return ((int *) arg, dsp_16bit ? 16 : 8);

    case SOUND_PCM_WRITE_FILTER:	/*
					 * NOT YET IMPLEMENTED
					 */
      if (get_fs_long ((long *) arg) > 1)
	return snd_ioctl_return ((int *) arg, -EINVAL);
    default:
      return -EINVAL;
    }

  return -EINVAL;
}

static int
sb16_dsp_open (int dev, int mode)
{
  int             retval;

  DEB (printk ("sb16_dsp_open()\n"));
  if (!sb16_dsp_ok)
    {
      printk ("SB16 Error: SoundBlaster board not installed\n");
      return -ENXIO;
    }

  if (intr_active)
    return -EBUSY;

  retval = sb_get_irq ();
  if (retval < 0)
    return retval;

  sb_reset_dsp ();

  if (dma16 != dma8)
    if (sound_open_dma (dma16, "SB16 (16bit)"))
      {
	printk ("SB16: Unable to grab DMA%d\n", dma16);
	sb_free_irq ();
	return -EBUSY;
      }

  irq_mode = IMODE_NONE;
  dsp_busy = 1;
  trigger_bits = 0;

  return 0;
}

static void
sb16_dsp_close (int dev)
{
  unsigned long   flags;

  DEB (printk ("sb16_dsp_close()\n"));
  sb_dsp_command01 (0xd9);
  sb_dsp_command01 (0xd5);

  save_flags (flags);
  cli ();

  audio_devs[dev]->dmachan1 = dma8;

  if (dma16 != dma8)
    sound_close_dma (dma16);
  sb_free_irq ();
  dsp_cleanup ();
  dsp_busy = 0;
  restore_flags (flags);
}

static unsigned long trg_buf;
static int      trg_bytes;
static int      trg_intrflag;
static int      trg_restart;

static void
sb16_dsp_output_block (int dev, unsigned long buf, int count, int intrflag, int dma_restart)
{
  trg_buf = buf;
  trg_bytes = count;
  trg_intrflag = intrflag;
  trg_restart = dma_restart;
  irq_mode = IMODE_OUTPUT;
}

static void
actually_output_block (int dev, unsigned long buf, int count, int intrflag, int dma_restart)
{
  unsigned long   flags, cnt;

  cnt = count;
  if (dsp_16bit)
    cnt >>= 1;
  cnt--;

#ifdef DEB_DMARES
  printk ("output_block: %x %d %d\n", buf, count, intrflag);
  if (intrflag)
    {
      int             pos, chan = audio_devs[dev]->dmachan;

      save_flags (flags);
      cli ();
      clear_dma_ff (chan);
      disable_dma (chan);
      pos = get_dma_residue (chan);
      enable_dma (chan);
      restore_flags (flags);
      printk ("dmapos=%d %x\n", pos, pos);
    }
#endif
  if (audio_devs[dev]->flags & DMA_AUTOMODE &&
      intrflag &&
      cnt == dsp_count)
    {
      irq_mode = IMODE_OUTPUT;
      intr_active = 1;
      return;			/*
				 * Auto mode on. No need to react
				 */
    }
  save_flags (flags);
  cli ();

  if (dma_restart)
    {
      sb16_dsp_halt (dev);
      DMAbuf_start_dma (dev, buf, count, DMA_MODE_WRITE);
    }
  sb_dsp_command (0x41);
  sb_dsp_command ((unsigned char) ((dsp_current_speed >> 8) & 0xff));
  sb_dsp_command ((unsigned char) (dsp_current_speed & 0xff));
  sb_dsp_command ((unsigned char) (dsp_16bit ? 0xb6 : 0xc6));
  dsp_count = cnt;
  sb_dsp_command ((unsigned char) ((dsp_stereo ? 0x20 : 0) +
				   (dsp_16bit ? 0x10 : 0)));
  sb_dsp_command01 ((unsigned char) (cnt & 0xff));
  sb_dsp_command ((unsigned char) (cnt >> 8));

  irq_mode = IMODE_OUTPUT;
  intr_active = 1;
  restore_flags (flags);
}

static void
sb16_dsp_start_input (int dev, unsigned long buf, int count, int intrflag, int dma_restart)
{
  trg_buf = buf;
  trg_bytes = count;
  trg_intrflag = intrflag;
  trg_restart = dma_restart;
  irq_mode = IMODE_INPUT;
}

static void
actually_start_input (int dev, unsigned long buf, int count, int intrflag, int dma_restart)
{
  unsigned long   flags, cnt;

  cnt = count;
  if (dsp_16bit)
    cnt >>= 1;
  cnt--;

#ifdef DEB_DMARES
  printk ("start_input: %x %d %d\n", buf, count, intrflag);
  if (intrflag)
    {
      int             pos, chan = audio_devs[dev]->dmachan;

      save_flags (flags);
      cli ();
      clear_dma_ff (chan);
      disable_dma (chan);
      pos = get_dma_residue (chan);
      enable_dma (chan);
      restore_flags (flags);
      printk ("dmapos=%d %x\n", pos, pos);
    }
#endif
  if (audio_devs[dev]->flags & DMA_AUTOMODE &&
      intrflag &&
      cnt == dsp_count)
    {
      irq_mode = IMODE_INPUT;
      intr_active = 1;
      return;			/*
				 * Auto mode on. No need to react
				 */
    }
  save_flags (flags);
  cli ();

  if (dma_restart)
    {
      sb_reset_dsp ();
      DMAbuf_start_dma (dev, buf, count, DMA_MODE_READ);
    }

  sb_dsp_command (0x42);
  sb_dsp_command ((unsigned char) ((dsp_current_speed >> 8) & 0xff));
  sb_dsp_command ((unsigned char) (dsp_current_speed & 0xff));
  sb_dsp_command ((unsigned char) (dsp_16bit ? 0xbe : 0xce));
  dsp_count = cnt;
  sb_dsp_command ((unsigned char) ((dsp_stereo ? 0x20 : 0) +
				   (dsp_16bit ? 0x10 : 0)));
  sb_dsp_command01 ((unsigned char) (cnt & 0xff));
  sb_dsp_command ((unsigned char) (cnt >> 8));

  irq_mode = IMODE_INPUT;
  intr_active = 1;
  restore_flags (flags);
}

static int
sb16_dsp_prepare_for_input (int dev, int bsize, int bcount)
{
  audio_devs[my_dev]->dmachan1 = dsp_16bit ? dma16 : dma8;
  dsp_count = 0;
  dsp_cleanup ();
  trigger_bits = 0;
  sb_dsp_command (0xd4);
  return 0;
}

static int
sb16_dsp_prepare_for_output (int dev, int bsize, int bcount)
{
  audio_devs[my_dev]->dmachan1 = dsp_16bit ? dma16 : dma8;
  dsp_count = 0;
  dsp_cleanup ();
  trigger_bits = 0;
  sb_dsp_command (0xd4);
  return 0;
}

static void
sb16_dsp_trigger (int dev, int bits)
{
  trigger_bits = bits;

  if (!bits)
    sb_dsp_command (0xd0);	/* Halt DMA */
  else if (bits & irq_mode)
    switch (irq_mode)
      {
      case IMODE_INPUT:
	actually_start_input (my_dev, trg_buf, trg_bytes,
			      trg_intrflag, trg_restart);
	break;

      case IMODE_OUTPUT:
	actually_output_block (my_dev, trg_buf, trg_bytes,
			       trg_intrflag, trg_restart);
	break;
      }
}

static void
dsp_cleanup (void)
{
  irq_mode = IMODE_NONE;
  intr_active = 0;
}

static void
sb16_dsp_reset (int dev)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();

  sb_reset_dsp ();
  dsp_cleanup ();

  restore_flags (flags);
}

static void
sb16_dsp_halt (int dev)
{
  if (dsp_16bit)
    {
      sb_dsp_command01 (0xd9);
      sb_dsp_command01 (0xd5);
    }
  else
    {
      sb_dsp_command01 (0xda);
      sb_dsp_command01 (0xd0);
    }
  /* DMAbuf_reset_dma (dev); */
}

static void
set_irq_hw (int level)
{
  int             ival;

  switch (level)
    {
    case 5:
      ival = 2;
      break;
    case 7:
      ival = 4;
      break;
    case 9:
      ival = 1;
      break;
    case 10:
      ival = 8;
      break;
    default:
      printk ("SB16_IRQ_LEVEL %d does not exist\n", level);
      return;
    }
  sb_setmixer (IRQ_NR, ival);
}

long
sb16_dsp_init (long mem_start, struct address_info *hw_config)
{
  extern int      sbc_major, sbc_minor;

  if (sbc_major < 4)
    return mem_start;		/* Not a SB16 */

  sprintf (sb16_dsp_operations.name, "SoundBlaster 16 %d.%d", sbc_major, sbc_minor);

  conf_printf (sb16_dsp_operations.name, hw_config);

  if (num_audiodevs < MAX_AUDIO_DEV)
    {
      audio_devs[my_dev = num_audiodevs++] = &sb16_dsp_operations;
      audio_devs[my_dev]->dmachan1 = dma8;
      audio_devs[my_dev]->buffsize = DSP_BUFFSIZE;

      if (sound_alloc_dma (dma8, "SB16 (8bit)"))
	{
	  printk ("SB16: Unable to grab DMA%d\n", dma8);
	}

      if (dma16 != dma8)
	if (sound_alloc_dma (dma16, "SB16 (16bit)"))
	  {
	    printk ("SB16: Unable to grab DMA%d\n", dma16);
	  }
    }
  else
    printk ("SB: Too many DSP devices available\n");
  sb16_dsp_ok = 1;
  return mem_start;
}

int
sb16_dsp_detect (struct address_info *hw_config)
{
  struct address_info *sb_config;
  extern int      sbc_major, Jazz16_detected;

  extern void     Jazz16_set_dma16 (int dma);

  if (sb16_dsp_ok)
    return 1;			/* Can't drive two cards */

  if (Jazz16_detected)
    {
      Jazz16_set_dma16 (hw_config->dma);
      return 0;
    }

  if (!(sb_config = sound_getconf (SNDCARD_SB)))
    {
      printk ("SB16 Error: Plain SB not configured\n");
      return 0;
    }

  /*
   * sb_setmixer(OPSW,0xf); if(sb_getmixer(OPSW)!=0xf) return 0;
   */

  if (!sb_reset_dsp ())
    return 0;

  if (sbc_major < 4)		/* Set by the plain SB driver */
    return 0;			/* Not a SB16 */

  if (hw_config->dma < 4)
    if (hw_config->dma != sb_config->dma)
      {
	printk ("SB16 Error: Invalid DMA channel %d/%d\n",
		sb_config->dma, hw_config->dma);
	return 0;
      }

  dma16 = hw_config->dma;
  dma8 = sb_config->dma;
  set_irq_hw (sb_config->irq);
  sb_setmixer (DMA_NR, (1 << hw_config->dma) | (1 << sb_config->dma));

  DEB (printk ("SoundBlaster 16: IRQ %d DMA %d OK\n", sb_config->irq, hw_config->dma));

  /*
     * dsp_showmessage(0xe3,99);
   */
  sb16_dsp_ok = 1;
  return 1;
}

void
unload_sb16 (struct address_info *hw_config)
{

  sound_free_dma (dma8);

  if (dma16 != dma8)
    sound_free_dma (dma16);
}

void
sb16_dsp_interrupt (int unused)
{
  int             data;

  data = inb (DSP_DATA_AVL16);	/*
				   * Interrupt acknowledge
				 */

  if (intr_active)
    switch (irq_mode)
      {
      case IMODE_OUTPUT:
	DMAbuf_outputintr (my_dev, 1);
	break;

      case IMODE_INPUT:
	DMAbuf_inputintr (my_dev);
	break;

      default:
	printk ("SoundBlaster: Unexpected interrupt\n");
      }
}

#endif
