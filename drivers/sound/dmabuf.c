/*
 * sound/dmabuf.c
 *
 * The DMA buffer manager for digitized voice applications
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


#include "sound_config.h"

#if defined(CONFIG_AUDIO) || defined(CONFIG_GUS)

static wait_handle *in_sleeper[MAX_AUDIO_DEV] =
{NULL};
static volatile struct snd_wait in_sleep_flag[MAX_AUDIO_DEV] =
{
  {0}};
static wait_handle *out_sleeper[MAX_AUDIO_DEV] =
{NULL};
static volatile struct snd_wait out_sleep_flag[MAX_AUDIO_DEV] =
{
  {0}};

#define NEUTRAL8	0x80
#define NEUTRAL16	0x00

static int      ndmaps = 0;

#define MAX_DMAP (MAX_AUDIO_DEV*2)

static struct dma_buffparms dmaps[MAX_DMAP] =
{
  {0}};

static int      space_in_queue (int dev);

static void     dma_reset_output (int dev);
static void     dma_reset_input (int dev);

static void
reorganize_buffers (int dev, struct dma_buffparms *dmap, int recording)
{
  /*
   * This routine breaks the physical device buffers to logical ones.
   */

  struct audio_operations *dsp_dev = audio_devs[dev];

  unsigned        i, n;
  unsigned        sr, nc, sz, bsz;

  if (dmap->fragment_size == 0)
    {				/* Compute the fragment size using the default algorithm */

      sr = dsp_dev->ioctl (dev, SOUND_PCM_READ_RATE, 0, 1);
      nc = dsp_dev->ioctl (dev, SOUND_PCM_READ_CHANNELS, 0, 1);
      sz = dsp_dev->ioctl (dev, SOUND_PCM_READ_BITS, 0, 1);

      if (sz == 8)
	dmap->neutral_byte = NEUTRAL8;
      else
	dmap->neutral_byte = NEUTRAL16;

      if (sr < 1 || nc < 1 || sz < 1)
	{
	  printk ("Warning: Invalid PCM parameters[%d] sr=%d, nc=%d, sz=%d\n",
		  dev, sr, nc, sz);
	  sr = DSP_DEFAULT_SPEED;
	  nc = 1;
	  sz = 8;
	}

      sz = sr * nc * sz;

      sz /= 8;			/* #bits -> #bytes */

      /*
         * Compute a buffer size for time not exeeding 1 second.
         * Usually this algorithm gives a buffer size for 0.5 to 1.0 seconds
         * of sound (using the current speed, sample size and #channels).
       */

      bsz = dsp_dev->buffsize;
      while (bsz > sz)
	bsz /= 2;

      if (bsz == dsp_dev->buffsize)
	bsz /= 2;		/* Needs at least 2 buffers */

/*
 *    Split the computed fragment to smaller parts. After 3.5a9
 *      the default subdivision is 4 which should give better
 *      results when recording.
 */

      if (dmap->subdivision == 0)	/* Not already set */
	{
	  dmap->subdivision = 1;	/* Init to the default value */
#ifndef V35A9_COMPATIBLE
	  if (recording)
	    dmap->subdivision = 4;	/* Use shorter fragments when recording */
#endif
	}

      bsz /= dmap->subdivision;

      if (bsz < 16)
	bsz = 16;		/* Just a sanity check */

      dmap->fragment_size = bsz;
    }
  else
    {
      /*
         * The process has specified the buffer sice with SNDCTL_DSP_SETFRAGMENT or
         * the buffer sice computation has already been done.
       */
      if (dmap->fragment_size > (audio_devs[dev]->buffsize / 2))
	dmap->fragment_size = (audio_devs[dev]->buffsize / 2);
      bsz = dmap->fragment_size;
    }

  bsz &= ~0x03;			/* Force size which is multiple of 4 bytes */
#ifdef OS_DMA_ALIGN_CHECK
  OS_DMA_ALIGN_CHECK (bsz);
#endif

  n = dsp_dev->buffsize / bsz;
  if (n > MAX_SUB_BUFFERS)
    n = MAX_SUB_BUFFERS;
  if (n > dmap->max_fragments)
    n = dmap->max_fragments;
  dmap->nbufs = n;
  dmap->bytes_in_use = n * bsz;

  memset (dmap->raw_buf,
	  dmap->neutral_byte,
	  dmap->bytes_in_use);

  for (i = 0; i < dmap->nbufs; i++)
    {
      dmap->counts[i] = 0;
    }

  dmap->flags |= DMA_ALLOC_DONE | DMA_EMPTY;
}

static void
dma_init_buffers (int dev, struct dma_buffparms *dmap)
{
  if (dmap == audio_devs[dev]->dmap_out)
    {
      out_sleep_flag[dev].mode = WK_NONE;
    }
  else
    {
      in_sleep_flag[dev].mode = WK_NONE;
    }

  dmap->flags = DMA_BUSY;	/* Other flags off */
  dmap->qlen = dmap->qhead = dmap->qtail = 0;
  dmap->nbufs = 1;
  dmap->bytes_in_use = audio_devs[dev]->buffsize;

  dmap->dma_mode = DMODE_NONE;
  dmap->mapping_flags = 0;
  dmap->neutral_byte = NEUTRAL8;
  dmap->cfrag = -1;
  dmap->closing = 0;
}

static int
open_dmap (int dev, int mode, struct dma_buffparms *dmap, int chan)
{
  if (dmap->flags & DMA_BUSY)
    return -EBUSY;

  {
    int             err;

    if ((err = sound_alloc_dmap (dev, dmap, chan)) < 0)
      return err;
  }

  if (dmap->raw_buf == NULL)
    return -ENOSPC;		/* Memory allocation failed during boot */

  if (sound_open_dma (chan, audio_devs[dev]->name))
    {
      printk ("Unable to grab(2) DMA%d for the audio driver\n", chan);
      return -EBUSY;
    }

  dmap->open_mode = mode;
  dmap->subdivision = dmap->underrun_count = 0;
  dmap->fragment_size = 0;
  dmap->max_fragments = 65536;	/* Just a large value */
  dmap->byte_counter = 0;

  dma_init_buffers (dev, dmap);

  return 0;
}

static void
close_dmap (int dev, struct dma_buffparms *dmap, int chan)
{
  sound_close_dma (chan);

  if (dmap->flags & DMA_BUSY)
    dmap->dma_mode = DMODE_NONE;
  dmap->flags &= ~DMA_BUSY;

  sound_free_dmap (dev, dmap);
}

int
DMAbuf_open (int dev, int mode)
{
  int             retval;
  struct dma_buffparms *dmap_in = NULL;
  struct dma_buffparms *dmap_out = NULL;

  if (dev >= num_audiodevs)
    {
      printk ("PCM device %d not installed.\n", dev);
      return -ENXIO;
    }

  if (!audio_devs[dev])
    {
      printk ("PCM device %d not initialized\n", dev);
      return -ENXIO;
    }

  if (!(audio_devs[dev]->flags & DMA_DUPLEX))
    {
      audio_devs[dev]->dmap_in = audio_devs[dev]->dmap_out;
      audio_devs[dev]->dmachan2 = audio_devs[dev]->dmachan1;
    }

  if ((retval = audio_devs[dev]->open (dev, mode)) < 0)
    return retval;

  dmap_out = audio_devs[dev]->dmap_out;
  dmap_in = audio_devs[dev]->dmap_in;

  if ((retval = open_dmap (dev, mode, dmap_out, audio_devs[dev]->dmachan1)) < 0)
    {
      audio_devs[dev]->close (dev);
      return retval;
    }

  audio_devs[dev]->enable_bits = mode;
  if (audio_devs[dev]->flags & DMA_DUPLEX && dmap_out != dmap_in)
    if ((retval = open_dmap (dev, mode, dmap_in, audio_devs[dev]->dmachan2)) < 0)
      {
	audio_devs[dev]->close (dev);
	close_dmap (dev, dmap_out, audio_devs[dev]->dmachan1);
	return retval;
      }
  audio_devs[dev]->open_mode = mode;
  audio_devs[dev]->go = 1;
  in_sleep_flag[dev].mode = WK_NONE;
  out_sleep_flag[dev].mode = WK_NONE;

  audio_devs[dev]->ioctl (dev, SOUND_PCM_WRITE_BITS, (caddr_t) 8, 1);
  audio_devs[dev]->ioctl (dev, SOUND_PCM_WRITE_CHANNELS, (caddr_t) 1, 1);
  audio_devs[dev]->ioctl (dev, SOUND_PCM_WRITE_RATE, (caddr_t) DSP_DEFAULT_SPEED, 1);

  return 0;
}

static void
dma_reset (int dev)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();
  audio_devs[dev]->reset (dev);
  restore_flags (flags);

  dma_reset_output (dev);

  if (audio_devs[dev]->flags & DMA_DUPLEX)
    dma_reset_input (dev);
}

static void
dma_reset_output (int dev)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();
  if (!(audio_devs[dev]->flags & DMA_DUPLEX) ||
      !audio_devs[dev]->halt_output)
    audio_devs[dev]->reset (dev);
  else
    audio_devs[dev]->halt_output (dev);
  restore_flags (flags);

  dma_init_buffers (dev, audio_devs[dev]->dmap_out);
  reorganize_buffers (dev, audio_devs[dev]->dmap_out, 0);
}

static void
dma_reset_input (int dev)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();
  if (!(audio_devs[dev]->flags & DMA_DUPLEX) ||
      !audio_devs[dev]->halt_input)
    audio_devs[dev]->reset (dev);
  else
    audio_devs[dev]->halt_input (dev);
  restore_flags (flags);

  dma_init_buffers (dev, audio_devs[dev]->dmap_in);
  reorganize_buffers (dev, audio_devs[dev]->dmap_in, 1);
}

static int
dma_sync (int dev)
{
  unsigned long   flags;

  if (!audio_devs[dev]->go && (!audio_devs[dev]->enable_bits & PCM_ENABLE_OUTPUT))
    return 0;

  if (audio_devs[dev]->dmap_out->dma_mode == DMODE_OUTPUT)
    {
      save_flags (flags);
      cli ();

      audio_devs[dev]->dmap_out->underrun_count = 0;
      while (!current_got_fatal_signal ()
	     && audio_devs[dev]->dmap_out->qlen
	     && audio_devs[dev]->dmap_out->underrun_count == 0)
	{

	  {
	    unsigned long   tl;

	    if (HZ)
	      current_set_timeout (tl = jiffies + (HZ));
	    else
	      tl = (unsigned long) -1;
	    out_sleep_flag[dev].mode = WK_SLEEP;
	    module_interruptible_sleep_on (&out_sleeper[dev]);
	    if (!(out_sleep_flag[dev].mode & WK_WAKEUP))
	      {
		if (jiffies >= tl)
		  out_sleep_flag[dev].mode |= WK_TIMEOUT;
	      }
	    out_sleep_flag[dev].mode &= ~WK_SLEEP;
	  };
	  if ((out_sleep_flag[dev].mode & WK_TIMEOUT))
	    {
	      restore_flags (flags);
	      return audio_devs[dev]->dmap_out->qlen;
	    }
	}
      restore_flags (flags);

      /*
       * Some devices such as GUS have huge amount of on board RAM for the
       * audio data. We have to wait until the device has finished playing.
       */

      save_flags (flags);
      cli ();
      if (audio_devs[dev]->local_qlen)	/* Device has hidden buffers */
	{
	  while (!(current_got_fatal_signal ())
		 && audio_devs[dev]->local_qlen (dev))
	    {

	      {
		unsigned long   tl;

		if (HZ)
		  current_set_timeout (tl = jiffies + (HZ));
		else
		  tl = (unsigned long) -1;
		out_sleep_flag[dev].mode = WK_SLEEP;
		module_interruptible_sleep_on (&out_sleeper[dev]);
		if (!(out_sleep_flag[dev].mode & WK_WAKEUP))
		  {
		    if (jiffies >= tl)
		      out_sleep_flag[dev].mode |= WK_TIMEOUT;
		  }
		out_sleep_flag[dev].mode &= ~WK_SLEEP;
	      };
	    }
	}
      restore_flags (flags);
    }
  return audio_devs[dev]->dmap_out->qlen;
}

int
DMAbuf_release (int dev, int mode)
{
  unsigned long   flags;

  audio_devs[dev]->dmap_out->closing = 1;
  audio_devs[dev]->dmap_in->closing = 1;

  if (!(current_got_fatal_signal ())
      && (audio_devs[dev]->dmap_out->dma_mode == DMODE_OUTPUT))
    {
      dma_sync (dev);

      memset (audio_devs[dev]->dmap_out->raw_buf,
	      audio_devs[dev]->dmap_out->neutral_byte,
	      audio_devs[dev]->dmap_out->bytes_in_use);
    }

  save_flags (flags);
  cli ();

  audio_devs[dev]->close (dev);

  close_dmap (dev, audio_devs[dev]->dmap_out, audio_devs[dev]->dmachan1);

  if (audio_devs[dev]->flags & DMA_DUPLEX)
    close_dmap (dev, audio_devs[dev]->dmap_in, audio_devs[dev]->dmachan2);
  audio_devs[dev]->open_mode = 0;

  restore_flags (flags);

  return 0;
}

static int
activate_recording (int dev, struct dma_buffparms *dmap)
{
  if (!(audio_devs[dev]->enable_bits & PCM_ENABLE_INPUT))
    return 0;

  if (dmap->flags & DMA_RESTART)
    {
      dma_reset_input (dev);
      dmap->flags &= ~DMA_RESTART;
    }

  if (dmap->dma_mode == DMODE_OUTPUT)	/* Direction change */
    {
      dma_sync (dev);
      dma_reset (dev);
      dmap->dma_mode = DMODE_NONE;
    }

  if (!(dmap->flags & DMA_ALLOC_DONE))
    reorganize_buffers (dev, dmap, 1);

  if (!dmap->dma_mode)
    {
      int             err;

      if ((err = audio_devs[dev]->prepare_for_input (dev,
				     dmap->fragment_size, dmap->nbufs)) < 0)
	{
	  return err;
	}
      dmap->dma_mode = DMODE_INPUT;
    }

  if (!(dmap->flags & DMA_ACTIVE))
    {
      audio_devs[dev]->start_input (dev, dmap->raw_buf_phys +
				    dmap->qtail * dmap->fragment_size,
				    dmap->fragment_size, 0,
				 !(audio_devs[dev]->flags & DMA_AUTOMODE) ||
				    !(dmap->flags & DMA_STARTED));
      dmap->flags |= DMA_ACTIVE | DMA_STARTED;
      if (audio_devs[dev]->trigger)
	audio_devs[dev]->trigger (dev,
			audio_devs[dev]->enable_bits * audio_devs[dev]->go);
    }
  return 0;
}

int
DMAbuf_getrdbuffer (int dev, char **buf, int *len, int dontblock)
{
  unsigned long   flags;
  int             err = EIO;
  struct dma_buffparms *dmap = audio_devs[dev]->dmap_in;

  save_flags (flags);
  cli ();
  if (audio_devs[dev]->dmap_in->mapping_flags & DMA_MAP_MAPPED)
    {
      printk ("Sound: Can't read from mmapped device (1)\n");
      return -EINVAL;
    }
  else if (!dmap->qlen)
    {
      int             tmout;

      if ((err = activate_recording (dev, dmap)) < 0)
	{
	  restore_flags (flags);
	  return err;
	}

      /* Wait for the next block */

      if (dontblock)
	{
	  restore_flags (flags);
	  return -EAGAIN;
	}

      if (!(audio_devs[dev]->enable_bits & PCM_ENABLE_INPUT) &
	  audio_devs[dev]->go)
	{
	  restore_flags (flags);
	  return -EAGAIN;
	}

      if (!audio_devs[dev]->go)
	tmout = 0;
      else
	tmout = 2 * HZ;


      {
	unsigned long   tl;

	if (tmout)
	  current_set_timeout (tl = jiffies + (tmout));
	else
	  tl = (unsigned long) -1;
	in_sleep_flag[dev].mode = WK_SLEEP;
	module_interruptible_sleep_on (&in_sleeper[dev]);
	if (!(in_sleep_flag[dev].mode & WK_WAKEUP))
	  {
	    if (jiffies >= tl)
	      in_sleep_flag[dev].mode |= WK_TIMEOUT;
	  }
	in_sleep_flag[dev].mode &= ~WK_SLEEP;
      };
      if ((in_sleep_flag[dev].mode & WK_TIMEOUT))
	{
	  printk ("Sound: DMA (input) timed out - IRQ/DRQ config error?\n");
	  err = EIO;
	  audio_devs[dev]->reset (dev);
	  ;
	}
      else
	err = EINTR;
    }
  restore_flags (flags);

  if (!dmap->qlen)
    return -err;

  *buf = &dmap->raw_buf[dmap->qhead * dmap->fragment_size + dmap->counts[dmap->qhead]];
  *len = dmap->fragment_size - dmap->counts[dmap->qhead];

  return dmap->qhead;
}

int
DMAbuf_rmchars (int dev, int buff_no, int c)
{
  struct dma_buffparms *dmap = audio_devs[dev]->dmap_in;

  int             p = dmap->counts[dmap->qhead] + c;

  if (audio_devs[dev]->dmap_in->mapping_flags & DMA_MAP_MAPPED)
    {
      printk ("Sound: Can't read from mmapped device (2)\n");
      return -EINVAL;
    }
  else if (p >= dmap->fragment_size)
    {				/* This buffer is completely empty */
      dmap->counts[dmap->qhead] = 0;
      if (dmap->qlen <= 0 || dmap->qlen > dmap->nbufs)
	printk ("\nSound: Audio queue1 corrupted for dev%d (%d/%d)\n",
		dev, dmap->qlen, dmap->nbufs);
      dmap->qlen--;
      dmap->qhead = (dmap->qhead + 1) % dmap->nbufs;
    }
  else
    dmap->counts[dmap->qhead] = p;

  return 0;
}

static int
dma_subdivide (int dev, struct dma_buffparms *dmap, caddr_t arg, int fact)
{
  if (fact == 0)
    {
      fact = dmap->subdivision;
      if (fact == 0)
	fact = 1;
      return snd_ioctl_return ((int *) arg, fact);
    }

  if (dmap->subdivision != 0 ||
      dmap->fragment_size)	/* Loo late to change */
    return -EINVAL;

  if (fact > MAX_REALTIME_FACTOR)
    return -EINVAL;

  if (fact != 1 && fact != 2 && fact != 4 && fact != 8 && fact != 16)
    return -EINVAL;

  dmap->subdivision = fact;
  return snd_ioctl_return ((int *) arg, fact);
}

static int
dma_set_fragment (int dev, struct dma_buffparms *dmap, caddr_t arg, int fact)
{
  int             bytes, count;

  if (fact == 0)
    return -EIO;

  if (dmap->subdivision != 0 ||
      dmap->fragment_size)	/* Loo late to change */
    return -EINVAL;

  bytes = fact & 0xffff;
  count = (fact >> 16) & 0xffff;

  if (count == 0)
    count = MAX_SUB_BUFFERS;

  if (bytes < 4 || bytes > 17)	/* <16 || > 128k */
    return -EINVAL;

  if (count < 2)
    return -EINVAL;

#ifdef OS_DMA_MINBITS
  if (bytes < OS_DMA_MINBITS)
    bytes = OS_DMA_MINBITS;
#endif

  dmap->fragment_size = (1 << bytes);
  dmap->max_fragments = count;

  if (dmap->fragment_size > audio_devs[dev]->buffsize)
    dmap->fragment_size = audio_devs[dev]->buffsize;

  if (dmap->fragment_size == audio_devs[dev]->buffsize &&
      audio_devs[dev]->flags & DMA_AUTOMODE)
    dmap->fragment_size /= 2;	/* Needs at least 2 buffers */

  dmap->subdivision = 1;	/* Disable SNDCTL_DSP_SUBDIVIDE */
  return snd_ioctl_return ((int *) arg, bytes | (count << 16));
}

static int
get_buffer_pointer (int dev, int chan, struct dma_buffparms *dmap)
{
  int             pos;
  unsigned long   flags;

  save_flags (flags);
  cli ();
  if (!(dmap->flags & DMA_ACTIVE))
    pos = 0;
  else
    {
      clear_dma_ff (chan);
      disable_dma (chan);
      pos = get_dma_residue (chan);
      enable_dma (chan);
    }
  restore_flags (flags);
  /* printk ("%04x ", pos); */

  if (audio_devs[dev]->flags & DMA_AUTOMODE)
    return dmap->bytes_in_use - pos;
  else
    {
      pos = dmap->fragment_size - pos;
      if (pos < 0)
	return 0;
      return pos;
    }
}


int
DMAbuf_ioctl (int dev, unsigned int cmd, caddr_t arg, int local)
{
  struct dma_buffparms *dmap_out = audio_devs[dev]->dmap_out;
  struct dma_buffparms *dmap_in = audio_devs[dev]->dmap_in;

  switch (cmd)
    {
    case SNDCTL_DSP_RESET:
      dma_reset (dev);
      return 0;
      break;

    case SNDCTL_DSP_SYNC:
      dma_sync (dev);
      dma_reset (dev);
      return 0;
      break;

    case SNDCTL_DSP_GETBLKSIZE:
      if (!(dmap_out->flags & DMA_ALLOC_DONE))
	{
	  reorganize_buffers (dev, dmap_out,
			      (audio_devs[dev]->open_mode == OPEN_READ));
	  if (audio_devs[dev]->flags & DMA_DUPLEX)
	    reorganize_buffers (dev, dmap_in,
				(audio_devs[dev]->open_mode == OPEN_READ));
	}

      return snd_ioctl_return ((int *) arg, dmap_out->fragment_size);
      break;

    case SNDCTL_DSP_SUBDIVIDE:
      {
	int             fact = get_fs_long ((long *) arg);
	int             ret;

	ret = dma_subdivide (dev, dmap_out, arg, fact);
	if (ret < 0)
	  return ret;

	if (audio_devs[dev]->flags & DMA_DUPLEX)
	  ret = dma_subdivide (dev, dmap_in, arg, fact);

	return ret;
      }
      break;

    case SNDCTL_DSP_SETFRAGMENT:
      {
	int             fact = get_fs_long ((long *) arg);
	int             ret;

	ret = dma_set_fragment (dev, dmap_out, arg, fact);
	if (ret < 0)
	  return ret;

	if (audio_devs[dev]->flags & DMA_DUPLEX)
	  ret = dma_set_fragment (dev, dmap_in, arg, fact);

	return ret;
      }
      break;

    case SNDCTL_DSP_GETISPACE:
    case SNDCTL_DSP_GETOSPACE:
      if (!local)
	return -EINVAL;
      else
	{
	  struct dma_buffparms *dmap = dmap_out;

	  audio_buf_info *info = (audio_buf_info *) arg;

	  if (cmd == SNDCTL_DSP_GETISPACE && audio_devs[dev]->flags & DMA_DUPLEX)
	    dmap = dmap_in;

	  if (dmap->mapping_flags & DMA_MAP_MAPPED)
	    return -EINVAL;

	  if (!(dmap->flags & DMA_ALLOC_DONE))
	    reorganize_buffers (dev, dmap, (cmd == SNDCTL_DSP_GETISPACE));

	  info->fragstotal = dmap->nbufs;

	  if (cmd == SNDCTL_DSP_GETISPACE)
	    info->fragments = dmap->qlen;
	  else
	    {
	      if (!space_in_queue (dev))
		info->fragments = 0;
	      else
		{
		  info->fragments = dmap->nbufs - dmap->qlen;
		  if (audio_devs[dev]->local_qlen)
		    {
		      int             tmp = audio_devs[dev]->local_qlen (dev);

		      if (tmp && info->fragments)
			tmp--;	/*
				   * This buffer has been counted twice
				 */
		      info->fragments -= tmp;
		    }
		}
	    }

	  if (info->fragments < 0)
	    info->fragments = 0;
	  else if (info->fragments > dmap->nbufs)
	    info->fragments = dmap->nbufs;

	  info->fragsize = dmap->fragment_size;
	  info->bytes = info->fragments * dmap->fragment_size;

	  if (cmd == SNDCTL_DSP_GETISPACE && dmap->qlen)
	    info->bytes -= dmap->counts[dmap->qhead];
	}
      return 0;

    case SNDCTL_DSP_SETTRIGGER:
      {
	unsigned long   flags;

	int             bits = get_fs_long ((long *) arg) & audio_devs[dev]->open_mode;
	int             changed;

	if (audio_devs[dev]->trigger == NULL)
	  return -EINVAL;

	if (!(audio_devs[dev]->flags & DMA_DUPLEX))
	  if ((bits & PCM_ENABLE_INPUT) && (bits & PCM_ENABLE_OUTPUT))
	    {
	      printk ("Sound: Device doesn't have full duplex capability\n");
	      return -EINVAL;
	    }

	save_flags (flags);
	cli ();
	changed = audio_devs[dev]->enable_bits ^ bits;

	if ((changed & bits) & PCM_ENABLE_INPUT && audio_devs[dev]->go)
	  {
	    int             err;

	    if (!(dmap_in->flags & DMA_ALLOC_DONE))
	      {
		reorganize_buffers (dev, dmap_in, 1);
	      }

	    if ((err = audio_devs[dev]->prepare_for_input (dev,
			       dmap_in->fragment_size, dmap_in->nbufs)) < 0)
	      return -err;

	    activate_recording (dev, dmap_in);
	  }

	if ((changed & bits) & PCM_ENABLE_OUTPUT &&
	    dmap_out->mapping_flags & DMA_MAP_MAPPED &&
	    audio_devs[dev]->go)
	  {
	    int             err;

	    if (!(dmap_out->flags & DMA_ALLOC_DONE))
	      {
		reorganize_buffers (dev, dmap_out, 0);
	      }

	    if ((err = audio_devs[dev]->prepare_for_output (dev,
			     dmap_out->fragment_size, dmap_out->nbufs)) < 0)
	      return -err;

	    dmap_out->counts[dmap_out->qhead] = dmap_out->fragment_size;
	    DMAbuf_start_output (dev, 0, dmap_out->fragment_size);
	  }

	audio_devs[dev]->enable_bits = bits;
	if (changed && audio_devs[dev]->trigger)
	  audio_devs[dev]->trigger (dev, bits * audio_devs[dev]->go);
	restore_flags (flags);
      }
    case SNDCTL_DSP_GETTRIGGER:
      return snd_ioctl_return ((int *) arg, audio_devs[dev]->enable_bits);
      break;

    case SNDCTL_DSP_SETSYNCRO:

      if (!audio_devs[dev]->trigger)
	return -EINVAL;

      audio_devs[dev]->trigger (dev, 0);
      audio_devs[dev]->go = 0;
      return 0;
      break;

    case SNDCTL_DSP_GETIPTR:
      {
	count_info      info;
	unsigned long   flags;

	save_flags (flags);
	cli ();
	info.bytes = audio_devs[dev]->dmap_in->byte_counter;
	info.ptr = get_buffer_pointer (dev, audio_devs[dev]->dmachan2, audio_devs[dev]->dmap_in);
	info.blocks = audio_devs[dev]->dmap_in->qlen;
	info.bytes += info.ptr;
	memcpy_tofs ((&((char *) arg)[0]), (char *) &info, sizeof (info));

	if (audio_devs[dev]->dmap_in->mapping_flags & DMA_MAP_MAPPED)
	  audio_devs[dev]->dmap_in->qlen = 0;	/* Acknowledge interrupts */
	restore_flags (flags);
	return 0;
      }
      break;

    case SNDCTL_DSP_GETOPTR:
      {
	count_info      info;
	unsigned long   flags;

	save_flags (flags);
	cli ();
	info.bytes = audio_devs[dev]->dmap_out->byte_counter;
	info.ptr = get_buffer_pointer (dev, audio_devs[dev]->dmachan1, audio_devs[dev]->dmap_out);
	info.blocks = audio_devs[dev]->dmap_out->qlen;
	info.bytes += info.ptr;
	memcpy_tofs ((&((char *) arg)[0]), (char *) &info, sizeof (info));

	if (audio_devs[dev]->dmap_out->mapping_flags & DMA_MAP_MAPPED)
	  audio_devs[dev]->dmap_out->qlen = 0;	/* Acknowledge interrupts */
	restore_flags (flags);
	return 0;
      }
      break;


    default:
      return audio_devs[dev]->ioctl (dev, cmd, arg, local);
    }

}

/*
 * DMAbuf_start_devices() is called by the /dev/music driver to start
 * one or more audio devices at desired moment.
 */

void
DMAbuf_start_devices (unsigned int devmask)
{
  int             dev;

  for (dev = 0; dev < num_audiodevs; dev++)
    if (devmask & (1 << dev))
      if (audio_devs[dev]->open_mode != 0)
	if (!audio_devs[dev]->go)
	  {
	    /* OK to start the device */
	    audio_devs[dev]->go = 1;

	    if (audio_devs[dev]->trigger)
	      audio_devs[dev]->trigger (dev,
			audio_devs[dev]->enable_bits * audio_devs[dev]->go);
	  }
}

static int
space_in_queue (int dev)
{
  int             len, max, tmp;
  struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

  if (dmap->qlen >= dmap->nbufs)	/* No space at all */
    return 0;

  /*
     * Verify that there are no more pending buffers than the limit
     * defined by the process.
   */

  max = dmap->max_fragments;
  len = dmap->qlen;

  if (audio_devs[dev]->local_qlen)
    {
      tmp = audio_devs[dev]->local_qlen (dev);
      if (tmp && len)
	tmp--;			/*
				   * This buffer has been counted twice
				 */
      len += tmp;
    }

  if (len >= max)
    return 0;
  return 1;
}

int
DMAbuf_getwrbuffer (int dev, char **buf, int *size, int dontblock)
{
  unsigned long   flags;
  int             abort, err = EIO;
  struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

  if (audio_devs[dev]->dmap_out->mapping_flags & DMA_MAP_MAPPED)
    {
      printk ("Sound: Can't write to mmapped device (3)\n");
      return -EINVAL;
    }

  if (dmap->dma_mode == DMODE_INPUT)	/* Direction change */
    {
      dma_reset (dev);
      dmap->dma_mode = DMODE_NONE;
    }
  else if (dmap->flags & DMA_RESTART)	/* Restart buffering */
    {
      dma_sync (dev);
      dma_reset_output (dev);
    }

  dmap->flags &= ~(DMA_RESTART | DMA_EMPTY);

  if (!(dmap->flags & DMA_ALLOC_DONE))
    reorganize_buffers (dev, dmap, 0);

  if (!dmap->dma_mode)
    {
      int             err;

      dmap->dma_mode = DMODE_OUTPUT;
      if ((err = audio_devs[dev]->prepare_for_output (dev,
				     dmap->fragment_size, dmap->nbufs)) < 0)
	return err;
    }

  save_flags (flags);
  cli ();

  abort = 0;
  while (!space_in_queue (dev) &&
	 !abort)
    {
      int             tmout;

      if (dontblock)
	{
	  restore_flags (flags);
	  return -EAGAIN;
	}

      if (!(audio_devs[dev]->enable_bits & PCM_ENABLE_OUTPUT) &&
	  audio_devs[dev]->go)
	{
	  restore_flags (flags);
	  return -EAGAIN;
	}

      /*
       * Wait for free space
       */
      if (!audio_devs[dev]->go)
	tmout = 0;
      else
	tmout = 2 * HZ;


      {
	unsigned long   tl;

	if (tmout)
	  current_set_timeout (tl = jiffies + (tmout));
	else
	  tl = (unsigned long) -1;
	out_sleep_flag[dev].mode = WK_SLEEP;
	module_interruptible_sleep_on (&out_sleeper[dev]);
	if (!(out_sleep_flag[dev].mode & WK_WAKEUP))
	  {
	    if (jiffies >= tl)
	      out_sleep_flag[dev].mode |= WK_TIMEOUT;
	  }
	out_sleep_flag[dev].mode &= ~WK_SLEEP;
      };
      if ((out_sleep_flag[dev].mode & WK_TIMEOUT))
	{
	  printk ("Sound: DMA (output) timed out - IRQ/DRQ config error?\n");
	  err = EIO;
	  abort = 1;
	  ;
	  audio_devs[dev]->reset (dev);
	}
      else if (current_got_fatal_signal ())
	{
	  err = EINTR;
	  abort = 1;
	}
    }
  restore_flags (flags);

  if (!space_in_queue (dev))
    {
      return -err;		/* Caught a signal ? */
    }

  *buf = dmap->raw_buf + dmap->qtail * dmap->fragment_size;
  *size = dmap->fragment_size;
  dmap->counts[dmap->qtail] = 0;

  return dmap->qtail;
}

int
DMAbuf_get_curr_buffer (int dev, int *buf_no, char **dma_buf, int *buf_ptr, int *buf_size)
{
  struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

  if (dmap->cfrag < 0)
    return -1;

  *dma_buf = dmap->raw_buf + dmap->qtail * dmap->fragment_size;
  *buf_ptr = dmap->counts[dmap->qtail];
  *buf_size = dmap->fragment_size;
  return *buf_no = dmap->cfrag;
}

int
DMAbuf_set_count (int dev, int buff_no, int l)
{
  struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

  if (buff_no == dmap->qtail)
    {
      dmap->cfrag = buff_no;
      dmap->counts[buff_no] = l;
    }
  else
    dmap->cfrag = -1;
  return 0;
}

int
DMAbuf_start_output (int dev, int buff_no, int l)
{
  struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

  dmap->cfrag = -1;
/*
 * Bypass buffering if using mmaped access
 */

  if (audio_devs[dev]->dmap_out->mapping_flags & DMA_MAP_MAPPED)
    {
      l = dmap->fragment_size;
      dmap->counts[dmap->qtail] = l;
      dmap->flags &= ~DMA_RESTART;
      dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
    }
  else
    {


      dmap->qlen++;
      if (dmap->qlen <= 0 || dmap->qlen > dmap->nbufs)
	printk ("\nSound: Audio queue2 corrupted for dev%d (%d/%d)\n",
		dev, dmap->qlen, dmap->nbufs);

      dmap->counts[dmap->qtail] = l;
      if (l < dmap->fragment_size)
	{
	  int             p = dmap->fragment_size * dmap->qtail;

	  memset (dmap->raw_buf + p + l,
		  dmap->neutral_byte,
		  dmap->fragment_size - l);
	}

      if ((l != dmap->fragment_size) &&
	  ((audio_devs[dev]->flags & DMA_AUTOMODE) &&
	   audio_devs[dev]->flags & NEEDS_RESTART))
	dmap->flags |= DMA_RESTART;
      else
	dmap->flags &= ~DMA_RESTART;

      dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
    }

  if (!(dmap->flags & DMA_ACTIVE))
    {
      dmap->flags |= DMA_ACTIVE;
      audio_devs[dev]->output_block (dev, dmap->raw_buf_phys +
				     dmap->qhead * dmap->fragment_size,
				     dmap->counts[dmap->qhead], 0,
				 !(audio_devs[dev]->flags & DMA_AUTOMODE) ||
				     !(dmap->flags & DMA_STARTED));
      dmap->flags |= DMA_STARTED;
      if (audio_devs[dev]->trigger)
	audio_devs[dev]->trigger (dev,
			audio_devs[dev]->enable_bits * audio_devs[dev]->go);
    }

  return 0;
}

int
DMAbuf_start_dma (int dev, unsigned long physaddr, int count, int dma_mode)
{
  int             chan;
  struct dma_buffparms *dmap;
  unsigned long   flags;

  if (dma_mode == DMA_MODE_WRITE)
    {
      chan = audio_devs[dev]->dmachan1;
      dmap = audio_devs[dev]->dmap_out;
    }
  else
    {
      chan = audio_devs[dev]->dmachan2;
      dmap = audio_devs[dev]->dmap_in;
    }

  /*
   * The count must be one less than the actual size. This is handled by
   * set_dma_addr()
   */

  if (audio_devs[dev]->flags & DMA_AUTOMODE)
    {				/*
				 * Auto restart mode. Transfer the whole *
				 * buffer
				 */
      save_flags (flags);
      cli ();
      disable_dma (chan);
      clear_dma_ff (chan);
      set_dma_mode (chan, dma_mode | DMA_AUTOINIT);
      set_dma_addr (chan, dmap->raw_buf_phys);
      set_dma_count (chan, dmap->bytes_in_use);
      enable_dma (chan);
      restore_flags (flags);
    }
  else
    {
      save_flags (flags);
      cli ();
      disable_dma (chan);
      clear_dma_ff (chan);
      set_dma_mode (chan, dma_mode);
      set_dma_addr (chan, physaddr);
      set_dma_count (chan, count);
      enable_dma (chan);
      restore_flags (flags);
    }

  return count;
}

long
DMAbuf_init (long mem_start)
{
  int             dev;


  /*
     * NOTE! This routine could be called several times.
   */

  for (dev = 0; dev < num_audiodevs; dev++)
    if (audio_devs[dev]->dmap_out == NULL)
      {
	audio_devs[dev]->dmap_out =
	  audio_devs[dev]->dmap_in =
	  &dmaps[ndmaps++];

	if (audio_devs[dev]->flags & DMA_DUPLEX)
	  audio_devs[dev]->dmap_in =
	    &dmaps[ndmaps++];
      }
  return mem_start;
}

static void
polish_buffers (struct dma_buffparms *dmap)
{
  int             i;

  if (dmap->cfrag < 0)
    {
      memset (dmap->raw_buf,
	      dmap->neutral_byte,
	      dmap->bytes_in_use);
      return;
    }

  for (i = 0; i < dmap->nbufs; i++)
    {
      int             p, l;

      p = dmap->fragment_size * i;

      if (i == dmap->cfrag)
	{
	  l = dmap->fragment_size - dmap->counts[i];
	}
      else
	l = dmap->fragment_size;

      if (l)
	{
	  memset (dmap->raw_buf + p,
		  dmap->neutral_byte,
		  l);
	}
    }
}

void
DMAbuf_outputintr (int dev, int event_type)
{
  /*
     * Event types:
     *  0 = DMA transfer done. Device still has more data in the local
     *      buffer.
     *  1 = DMA transfer done. Device doesn't have local buffer or it's
     *      empty now.
     *  2 = No DMA transfer but the device has now more space in it's local
     *      buffer.
   */

  unsigned long   flags;
  struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

  dmap->byte_counter += dmap->counts[dmap->qhead];

#ifdef OS_DMA_INTR
  sound_dma_intr (dev, audio_devs[dev]->dmap_out, audio_devs[dev]->dmachan1);
#endif

  if (dmap->mapping_flags & DMA_MAP_MAPPED)
    {
      /* mmapped access */

      dmap->qhead = (dmap->qhead + 1) % dmap->nbufs;
      dmap->qlen++;		/* Yes increment it (don't decrement) */
      dmap->flags &= ~DMA_ACTIVE;
      dmap->counts[dmap->qhead] = dmap->fragment_size;

      if (!(audio_devs[dev]->flags & DMA_AUTOMODE))
	{
	  audio_devs[dev]->output_block (dev, dmap->raw_buf_phys +
					 dmap->qhead * dmap->fragment_size,
					 dmap->counts[dmap->qhead], 1,
				  !(audio_devs[dev]->flags & DMA_AUTOMODE));
	  if (audio_devs[dev]->trigger)
	    audio_devs[dev]->trigger (dev,
			audio_devs[dev]->enable_bits * audio_devs[dev]->go);
	}
      dmap->flags |= DMA_ACTIVE;
    }
  else if (event_type != 2)
    {
      if (dmap->qlen <= 0 || dmap->qlen > dmap->nbufs)
	{
	  printk ("\nSound: Audio queue3 corrupted for dev%d (%d/%d)\n",
		  dev, dmap->qlen, dmap->nbufs);
	  return;
	}

      dmap->qlen--;
      dmap->qhead = (dmap->qhead + 1) % dmap->nbufs;
      dmap->flags &= ~DMA_ACTIVE;

      if (event_type == 1 && dmap->qlen < 1)
	{
	  dmap->underrun_count++;
	  /* Ignore underrun. Just move the tail pointer forward and go */
	  if (dmap->closing)
	    {
	      polish_buffers (dmap);
	      audio_devs[dev]->halt_xfer (dev);
	    }
	  else
	    {
	      dmap->qlen++;
	      dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;

	      if (!(dmap->flags & DMA_EMPTY))
		polish_buffers (dmap);

	      dmap->cfrag = -1;
	      dmap->flags |= DMA_EMPTY;
	      dmap->counts[dmap->qtail] = dmap->fragment_size;
	    }
	}

      if (dmap->qlen)
	{
	  if (!(audio_devs[dev]->flags & DMA_AUTOMODE))
	    {

	      if (dmap->counts[dmap->qhead] == 0)
		dmap->counts[dmap->qhead] = dmap->fragment_size;

	      audio_devs[dev]->output_block (dev, dmap->raw_buf_phys +
					  dmap->qhead * dmap->fragment_size,
					     dmap->counts[dmap->qhead], 1,
				  !(audio_devs[dev]->flags & DMA_AUTOMODE));
	      if (audio_devs[dev]->trigger)
		audio_devs[dev]->trigger (dev,
			audio_devs[dev]->enable_bits * audio_devs[dev]->go);
	    }
	  dmap->flags |= DMA_ACTIVE;
	}
    }				/* event_type != 2 */

  save_flags (flags);
  cli ();
  if ((out_sleep_flag[dev].mode & WK_SLEEP))
    {
      {
	out_sleep_flag[dev].mode = WK_WAKEUP;
	module_wake_up (&out_sleeper[dev]);
      };
    }
  restore_flags (flags);
}

void
DMAbuf_inputintr (int dev)
{
  unsigned long   flags;
  struct dma_buffparms *dmap = audio_devs[dev]->dmap_in;

  dmap->byte_counter += dmap->fragment_size;

#ifdef OS_DMA_INTR
  sound_dma_intr (dev, audio_devs[dev]->dmap_in, audio_devs[dev]->dmachan2);
#endif

  if (dmap->mapping_flags & DMA_MAP_MAPPED)
    {
      dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
      dmap->qlen++;

      if (!(audio_devs[dev]->flags & DMA_AUTOMODE))
	{
	  audio_devs[dev]->start_input (dev, dmap->raw_buf_phys +
					dmap->qtail * dmap->fragment_size,
					dmap->fragment_size, 1,
				  !(audio_devs[dev]->flags & DMA_AUTOMODE));
	  if (audio_devs[dev]->trigger)
	    audio_devs[dev]->trigger (dev,
			audio_devs[dev]->enable_bits * audio_devs[dev]->go);
	}

      dmap->flags |= DMA_ACTIVE;
    }
  else if (dmap->qlen == (dmap->nbufs - 1))
    {
      printk ("Sound: Recording overrun\n");
      dmap->underrun_count++;

      if (audio_devs[dev]->flags & DMA_AUTOMODE)
	{
	  /* Force restart on next write */
	  if ((audio_devs[dev]->flags & DMA_DUPLEX) &&
	      audio_devs[dev]->halt_input)
	    audio_devs[dev]->halt_input (dev);
	  else
	    audio_devs[dev]->halt_xfer (dev);

	  dmap->flags &= ~DMA_ACTIVE;
	  if (audio_devs[dev]->flags & DMA_AUTOMODE)
	    dmap->flags |= DMA_RESTART;
	  else
	    dmap->flags &= ~DMA_RESTART;
	}
    }
  else
    {
      dmap->qlen++;
      if (dmap->qlen <= 0 || dmap->qlen > dmap->nbufs)
	printk ("\nSound: Audio queue4 corrupted for dev%d (%d/%d)\n",
		dev, dmap->qlen, dmap->nbufs);
      dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
    }

  if (!(audio_devs[dev]->flags & DMA_AUTOMODE))
    {
      audio_devs[dev]->start_input (dev, dmap->raw_buf_phys +
				    dmap->qtail * dmap->fragment_size,
				    dmap->fragment_size, 1,
				  !(audio_devs[dev]->flags & DMA_AUTOMODE));
      if (audio_devs[dev]->trigger)
	audio_devs[dev]->trigger (dev,
			audio_devs[dev]->enable_bits * audio_devs[dev]->go);
    }

  dmap->flags |= DMA_ACTIVE;

  save_flags (flags);
  cli ();
  if ((in_sleep_flag[dev].mode & WK_SLEEP))
    {
      {
	in_sleep_flag[dev].mode = WK_WAKEUP;
	module_wake_up (&in_sleeper[dev]);
      };
    }
  restore_flags (flags);
}

int
DMAbuf_open_dma (int dev)
{
  int             chan = audio_devs[dev]->dmachan1;
  int             err;
  unsigned long   flags;

  if ((err = open_dmap (dev, OPEN_READWRITE, audio_devs[dev]->dmap_out, audio_devs[dev]->dmachan1)) < 0)
    {
      return -EBUSY;
    }
  dma_init_buffers (dev, audio_devs[dev]->dmap_out);
  audio_devs[dev]->dmap_out->flags |= DMA_ALLOC_DONE;
  audio_devs[dev]->dmap_out->fragment_size = audio_devs[dev]->buffsize;
  /* reorganize_buffers (dev, audio_devs[dev]->dmap_out); */

  if (audio_devs[dev]->flags & DMA_DUPLEX)
    {
      if ((err = open_dmap (dev, OPEN_READWRITE, audio_devs[dev]->dmap_in, audio_devs[dev]->dmachan2)) < 0)
	{
	  printk ("Unable to grab DMA%d for the audio driver\n",
		  audio_devs[dev]->dmachan2);
	  close_dmap (dev, audio_devs[dev]->dmap_out, audio_devs[dev]->dmachan1);
	  return -EBUSY;
	}
      dma_init_buffers (dev, audio_devs[dev]->dmap_in);
      audio_devs[dev]->dmap_in->flags |= DMA_ALLOC_DONE;
      audio_devs[dev]->dmap_in->fragment_size = audio_devs[dev]->buffsize;
      /* reorganize_buffers (dev, audio_devs[dev]->dmap_in); */
    }
  else
    {
      audio_devs[dev]->dmap_in = audio_devs[dev]->dmap_out;
      audio_devs[dev]->dmachan2 = audio_devs[dev]->dmachan1;
    }


  save_flags (flags);
  cli ();
  disable_dma (chan);
  clear_dma_ff (chan);
  restore_flags (flags);

  return 0;
}

void
DMAbuf_close_dma (int dev)
{
  DMAbuf_reset_dma (dev);
  close_dmap (dev, audio_devs[dev]->dmap_out, audio_devs[dev]->dmachan1);

  if (audio_devs[dev]->flags & DMA_DUPLEX)
    close_dmap (dev, audio_devs[dev]->dmap_in, audio_devs[dev]->dmachan2);

}

void
DMAbuf_reset_dma (int dev)
{
}

int
DMAbuf_select (int dev, struct fileinfo *file, int sel_type, select_table_handle * wait)
{
  struct dma_buffparms *dmap;
  unsigned long   flags;

  switch (sel_type)
    {
    case SEL_IN:
      dmap = audio_devs[dev]->dmap_in;

      if (dmap->mapping_flags & DMA_MAP_MAPPED)
	{
	  if (dmap->qlen)
	    return 1;

	  save_flags (flags);
	  cli ();
	  in_sleep_flag[dev].mode = WK_SLEEP;
	  module_select_wait (&in_sleeper[dev], wait);
	  restore_flags (flags);
	  return 0;
	}

      if (dmap->dma_mode != DMODE_INPUT)
	{
	  if ((audio_devs[dev]->flags & DMA_DUPLEX) && !dmap->qlen &&
	      audio_devs[dev]->enable_bits & PCM_ENABLE_INPUT &&
	      audio_devs[dev]->go)
	    {
	      unsigned long   flags;

	      save_flags (flags);
	      cli ();
	      activate_recording (dev, dmap);
	      restore_flags (flags);
	    }
	  return 0;
	}

      if (!dmap->qlen)
	{
	  save_flags (flags);
	  cli ();
	  in_sleep_flag[dev].mode = WK_SLEEP;
	  module_select_wait (&in_sleeper[dev], wait);
	  restore_flags (flags);
	  return 0;
	}
      return 1;
      break;

    case SEL_OUT:
      dmap = audio_devs[dev]->dmap_out;

      if (dmap->mapping_flags & DMA_MAP_MAPPED)
	{
	  if (dmap->qlen)
	    return 1;

	  save_flags (flags);
	  cli ();
	  out_sleep_flag[dev].mode = WK_SLEEP;
	  module_select_wait (&out_sleeper[dev], wait);
	  restore_flags (flags);
	  return 0;
	}

      if (dmap->dma_mode == DMODE_INPUT)
	{
	  return 0;
	}

      if (dmap->dma_mode == DMODE_NONE)
	{
	  return 1;
	}

      if (!space_in_queue (dev))
	{
	  save_flags (flags);
	  cli ();
	  out_sleep_flag[dev].mode = WK_SLEEP;
	  module_select_wait (&out_sleeper[dev], wait);
	  restore_flags (flags);
	  return 0;
	}
      return 1;
      break;

    case SEL_EX:
      return 0;
    }

  return 0;
}


#else /* CONFIG_AUDIO */
/*
 * Stub versions if audio services not included
 */

int
DMAbuf_open (int dev, int mode)
{
  return -ENXIO;
}

int
DMAbuf_release (int dev, int mode)
{
  return 0;
}

int
DMAbuf_getwrbuffer (int dev, char **buf, int *size, int dontblock)
{
  return -EIO;
}

int
DMAbuf_getrdbuffer (int dev, char **buf, int *len, int dontblock)
{
  return -EIO;
}

int
DMAbuf_rmchars (int dev, int buff_no, int c)
{
  return -EIO;
}

int
DMAbuf_start_output (int dev, int buff_no, int l)
{
  return -EIO;
}

int
DMAbuf_ioctl (int dev, unsigned int cmd, caddr_t arg, int local)
{
  return -EIO;
}

long
DMAbuf_init (long mem_start)
{
  return mem_start;
}

int
DMAbuf_start_dma (int dev, unsigned long physaddr, int count, int dma_mode)
{
  return -EIO;
}

int
DMAbuf_open_dma (int dev)
{
  return -ENXIO;
}

void
DMAbuf_close_dma (int dev)
{
  return;
}

void
DMAbuf_reset_dma (int dev)
{
  return;
}

void
DMAbuf_inputintr (int dev)
{
  return;
}

void
DMAbuf_outputintr (int dev, int underrun_flag)
{
  return;
}
#endif
