/*
 * sound/dmabuf.c
 *
 * The DMA buffer manager for digitized voice applications
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
#include <linux/config.h>

#define BE_CONSERVATIVE

#include "sound_config.h"

#if defined(CONFIG_AUDIO) || defined(CONFIG_GUSHW)

static struct wait_queue *in_sleeper[MAX_AUDIO_DEV] =
{NULL};
static volatile struct snd_wait in_sleep_flag[MAX_AUDIO_DEV] =
{
	{0}};
static struct wait_queue *out_sleeper[MAX_AUDIO_DEV] =
{NULL};
static volatile struct snd_wait out_sleep_flag[MAX_AUDIO_DEV] =
{
	{0}};

static int      ndmaps = 0;

#define MAX_DMAP (MAX_AUDIO_DEV*2)

static struct dma_buffparms dmaps[MAX_DMAP] =
{
	{0}};

static void     dma_reset_output(int dev);
static void     dma_reset_input(int dev);
static int      local_start_dma(int dev, unsigned long physaddr, int count, int dma_mode);

static void
dma_init_buffers(int dev, struct dma_buffparms *dmap)
{

	dmap->qlen = dmap->qhead = dmap->qtail = dmap->user_counter = 0;
	dmap->byte_counter = 0;
	dmap->max_byte_counter = 8000 * 60 * 60;
	dmap->bytes_in_use = dmap->buffsize;

	dmap->dma_mode = DMODE_NONE;
	dmap->mapping_flags = 0;
	dmap->neutral_byte = 0x80;
	dmap->data_rate = 8000;
	dmap->cfrag = -1;
	dmap->closing = 0;
	dmap->nbufs = 1;
	dmap->flags = DMA_BUSY;	/* Other flags off */
}

static int
open_dmap(int dev, int mode, struct dma_buffparms *dmap, int chan)
{
	if (dmap->flags & DMA_BUSY)
		return -EBUSY;

	{
		int             err;

		if ((err = sound_alloc_dmap(dev, dmap, chan)) < 0)
			return err;
	}

	if (dmap->raw_buf == NULL)
	  {
		  printk("Sound: DMA buffers not available\n");
		  return -ENOSPC;	/* Memory allocation failed during boot */
	  }
	if (sound_open_dma(chan, audio_devs[dev]->name))
	  {
		  printk("Unable to grab(2) DMA%d for the audio driver\n", chan);
		  return -EBUSY;
	  }
	dma_init_buffers(dev, dmap);
	dmap->open_mode = mode;
	dmap->subdivision = dmap->underrun_count = 0;
	dmap->fragment_size = 0;
	dmap->max_fragments = 65536;	/* Just a large value */
	dmap->byte_counter = 0;
	dmap->max_byte_counter = 8000 * 60 * 60;
	dmap->applic_profile = APF_NORMAL;
	dmap->needs_reorg = 1;
	dmap->audio_callback = NULL;
	dmap->callback_parm = 0;


	if (dmap->dma_mode & DMODE_OUTPUT)
	  {
		  out_sleep_flag[dev].opts = WK_NONE;
	} else
	  {
		  in_sleep_flag[dev].opts = WK_NONE;
	  }

	return 0;
}

static void
close_dmap(int dev, struct dma_buffparms *dmap, int chan)
{
	sound_close_dma(chan);

	if (dmap->flags & DMA_BUSY)
		dmap->dma_mode = DMODE_NONE;
	dmap->flags &= ~DMA_BUSY;

	disable_dma(dmap->dma);
}


static unsigned int
default_set_bits(int dev, unsigned int bits)
{
	return audio_devs[dev]->d->ioctl(dev, SNDCTL_DSP_SETFMT, (caddr_t) & bits);
}

static int
default_set_speed(int dev, int speed)
{
	return audio_devs[dev]->d->ioctl(dev, SNDCTL_DSP_SPEED, (caddr_t) & speed);
}

static short
default_set_channels(int dev, short channels)
{
	int             c = channels;

	return audio_devs[dev]->d->ioctl(dev, SNDCTL_DSP_CHANNELS, (caddr_t) & c);
}

static void
check_driver(struct audio_driver *d)
{
	if (d->set_speed == NULL)
		d->set_speed = default_set_speed;
	if (d->set_bits == NULL)
		d->set_bits = default_set_bits;
	if (d->set_channels == NULL)
		d->set_channels = default_set_channels;
}

int
DMAbuf_open(int dev, int mode)
{
	int             retval;
	struct dma_buffparms *dmap_in = NULL;
	struct dma_buffparms *dmap_out = NULL;

	if (dev >= num_audiodevs || audio_devs[dev] == NULL)
	  {
		  return -ENXIO;
	  }
	if (!audio_devs[dev])
	  {
		  return -ENXIO;
	  }
	if (!(audio_devs[dev]->flags & DMA_DUPLEX))
	  {
		  audio_devs[dev]->dmap_in = audio_devs[dev]->dmap_out;
		  audio_devs[dev]->dmap_in->dma = audio_devs[dev]->dmap_out->dma;
	  }
	check_driver(audio_devs[dev]->d);

	if ((retval = audio_devs[dev]->d->open(dev, mode)) < 0)
		return retval;

	dmap_out = audio_devs[dev]->dmap_out;
	dmap_in = audio_devs[dev]->dmap_in;

	if (dmap_in == dmap_out)
		audio_devs[dev]->flags &= ~DMA_DUPLEX;

	if (mode & OPEN_WRITE)
	  {
		  if ((retval = open_dmap(dev, mode, dmap_out, audio_devs[dev]->dmap_out->dma)) < 0)
		    {
			    audio_devs[dev]->d->close(dev);
			    return retval;
		    }
	  }
	audio_devs[dev]->enable_bits = mode;

	if (mode == OPEN_READ || (mode != OPEN_WRITE &&
				  audio_devs[dev]->flags & DMA_DUPLEX))
	  {
		  if ((retval = open_dmap(dev, mode, dmap_in, audio_devs[dev]->dmap_in->dma)) < 0)
		    {
			    audio_devs[dev]->d->close(dev);

			    if (mode & OPEN_WRITE)
			      {
				      close_dmap(dev, dmap_out, audio_devs[dev]->dmap_out->dma);
			      }
			    return retval;
		    }
	  }
	audio_devs[dev]->open_mode = mode;
	audio_devs[dev]->go = 1;

	if (mode & OPEN_READ)
		in_sleep_flag[dev].opts = WK_NONE;

	if (mode & OPEN_WRITE)
		out_sleep_flag[dev].opts = WK_NONE;

	audio_devs[dev]->d->set_bits(dev, 8);
	audio_devs[dev]->d->set_channels(dev, 1);
	audio_devs[dev]->d->set_speed(dev, DSP_DEFAULT_SPEED);

	if (audio_devs[dev]->dmap_out->dma_mode == DMODE_OUTPUT)
	  {
		  memset(audio_devs[dev]->dmap_out->raw_buf,
			 audio_devs[dev]->dmap_out->neutral_byte,
			 audio_devs[dev]->dmap_out->bytes_in_use);
	  }
	return 0;
}

void
DMAbuf_reset(int dev)
{
	if (audio_devs[dev]->open_mode & OPEN_WRITE)
		dma_reset_output(dev);

	if (audio_devs[dev]->open_mode & OPEN_READ)
		dma_reset_input(dev);
}

static void
dma_reset_output(int dev)
{
	unsigned long   flags;
	int             tmout;

	struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;


	if (!(dmap->flags & DMA_STARTED))	/* DMA is not active */
		return;

/*
 * First wait until the current fragment has been played completely
 */
	save_flags(flags);
	cli();

	tmout =
	    (dmap->fragment_size * HZ) / dmap->data_rate;

	tmout += HZ / 5;	/* Some safety distance */

	if (tmout < (HZ / 2))
		tmout = HZ / 2;
	if (tmout > 20 * HZ)
		tmout = 20 * HZ;

	audio_devs[dev]->dmap_out->flags |= DMA_SYNCING;

	audio_devs[dev]->dmap_out->underrun_count = 0;
	if (!signal_pending(current)
	    && audio_devs[dev]->dmap_out->qlen
	    && audio_devs[dev]->dmap_out->underrun_count == 0)
	  {

		  {
			  unsigned long   tlimit;

			  if (tmout)
				  current->timeout = tlimit = jiffies + (tmout);
			  else
				  tlimit = (unsigned long) -1;
			  out_sleep_flag[dev].opts = WK_SLEEP;
			  interruptible_sleep_on(&out_sleeper[dev]);
			  if (!(out_sleep_flag[dev].opts & WK_WAKEUP))
			    {
				    if (jiffies >= tlimit)
					    out_sleep_flag[dev].opts |= WK_TIMEOUT;
			    }
			  out_sleep_flag[dev].opts &= ~WK_SLEEP;
		  };
	  }
	audio_devs[dev]->dmap_out->flags &= ~(DMA_SYNCING | DMA_ACTIVE);

/*
 * Finally shut the device off
 */

	if (!(audio_devs[dev]->flags & DMA_DUPLEX) ||
	    !audio_devs[dev]->d->halt_output)
		audio_devs[dev]->d->halt_io(dev);
	else
		audio_devs[dev]->d->halt_output(dev);
	audio_devs[dev]->dmap_out->flags &= ~DMA_STARTED;
	restore_flags(flags);

	clear_dma_ff(dmap->dma);
	disable_dma(dmap->dma);
	dmap->byte_counter = 0;
	reorganize_buffers(dev, audio_devs[dev]->dmap_out, 0);
	dmap->qlen = dmap->qhead = dmap->qtail = dmap->user_counter = 0;
}

static void
dma_reset_input(int dev)
{
	unsigned long   flags;
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_in;

	save_flags(flags);
	cli();
	if (!(audio_devs[dev]->flags & DMA_DUPLEX) ||
	    !audio_devs[dev]->d->halt_input)
		audio_devs[dev]->d->halt_io(dev);
	else
		audio_devs[dev]->d->halt_input(dev);
	audio_devs[dev]->dmap_in->flags &= ~DMA_STARTED;
	restore_flags(flags);

	dmap->qlen = dmap->qhead = dmap->qtail = dmap->user_counter = 0;
	dmap->byte_counter = 0;
	reorganize_buffers(dev, audio_devs[dev]->dmap_in, 1);
}

void
DMAbuf_launch_output(int dev, struct dma_buffparms *dmap)
{
	if (!((audio_devs[dev]->enable_bits * audio_devs[dev]->go) & PCM_ENABLE_OUTPUT))
		return;		/* Don't start DMA yet */

	dmap->dma_mode = DMODE_OUTPUT;

	if (!(dmap->flags & DMA_ACTIVE) || !(audio_devs[dev]->flags & DMA_AUTOMODE) || dmap->flags & DMA_NODMA)
	  {
		  if (!(dmap->flags & DMA_STARTED))
		    {
			    reorganize_buffers(dev, dmap, 0);

			    if (audio_devs[dev]->d->prepare_for_output(dev,
				       dmap->fragment_size, dmap->nbufs))
				    return;

			    if (!(dmap->flags & DMA_NODMA))
			      {
				      local_start_dma(dev, dmap->raw_buf_phys, dmap->bytes_in_use,
						      DMA_MODE_WRITE);
			      }
			    dmap->flags |= DMA_STARTED;
		    }
		  if (dmap->counts[dmap->qhead] == 0)
			  dmap->counts[dmap->qhead] = dmap->fragment_size;

		  dmap->dma_mode = DMODE_OUTPUT;
		  audio_devs[dev]->d->output_block(dev, dmap->raw_buf_phys +
				       dmap->qhead * dmap->fragment_size,
					   dmap->counts[dmap->qhead], 1);
		  if (audio_devs[dev]->d->trigger)
			  audio_devs[dev]->d->trigger(dev,
						      audio_devs[dev]->enable_bits * audio_devs[dev]->go);
	  }
	dmap->flags |= DMA_ACTIVE;
}

int
DMAbuf_sync(int dev)
{
	unsigned long   flags;
	int             tmout, n = 0;

	if (!audio_devs[dev]->go && (!audio_devs[dev]->enable_bits & PCM_ENABLE_OUTPUT))
		return 0;

	if (audio_devs[dev]->dmap_out->dma_mode == DMODE_OUTPUT)
	  {

		  struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

		  save_flags(flags);
		  cli();

		  tmout =
		      (dmap->fragment_size * HZ) / dmap->data_rate;

		  tmout += HZ / 5;	/* Some safety distance */

		  if (tmout < (HZ / 2))
			  tmout = HZ / 2;
		  if (tmout > 20 * HZ)
			  tmout = 20 * HZ;

		  ;
		  if (dmap->qlen > 0)
			  if (!(dmap->flags & DMA_ACTIVE))
				  DMAbuf_launch_output(dev, dmap);
		  ;

		  audio_devs[dev]->dmap_out->flags |= DMA_SYNCING;

		  audio_devs[dev]->dmap_out->underrun_count = 0;
		  while (!signal_pending(current)
			 && n++ <= audio_devs[dev]->dmap_out->nbufs
			 && audio_devs[dev]->dmap_out->qlen
		       && audio_devs[dev]->dmap_out->underrun_count == 0)
		    {

			    {
				    unsigned long   tlimit;

				    if (tmout)
					    current->timeout = tlimit = jiffies + (tmout);
				    else
					    tlimit = (unsigned long) -1;
				    out_sleep_flag[dev].opts = WK_SLEEP;
				    interruptible_sleep_on(&out_sleeper[dev]);
				    if (!(out_sleep_flag[dev].opts & WK_WAKEUP))
				      {
					      if (jiffies >= tlimit)
						      out_sleep_flag[dev].opts |= WK_TIMEOUT;
				      }
				    out_sleep_flag[dev].opts &= ~WK_SLEEP;
			    };
			    if ((out_sleep_flag[dev].opts & WK_TIMEOUT))
			      {
				      audio_devs[dev]->dmap_out->flags &= ~DMA_SYNCING;
				      restore_flags(flags);
				      return audio_devs[dev]->dmap_out->qlen;
			      }
		    }
		  audio_devs[dev]->dmap_out->flags &= ~(DMA_SYNCING | DMA_ACTIVE);
		  restore_flags(flags);
		  /*
		   * Some devices such as GUS have huge amount of on board RAM for the
		   * audio data. We have to wait until the device has finished playing.
		   */

		  save_flags(flags);
		  cli();
		  if (audio_devs[dev]->d->local_qlen)	/* Device has hidden buffers */
		    {
			    while (!signal_pending(current)
				   && audio_devs[dev]->d->local_qlen(dev))
			      {

				      {
					      unsigned long   tlimit;

					      if (tmout)
						      current->timeout = tlimit = jiffies + (tmout);
					      else
						      tlimit = (unsigned long) -1;
					      out_sleep_flag[dev].opts = WK_SLEEP;
					      interruptible_sleep_on(&out_sleeper[dev]);
					      if (!(out_sleep_flag[dev].opts & WK_WAKEUP))
						{
							if (jiffies >= tlimit)
								out_sleep_flag[dev].opts |= WK_TIMEOUT;
						}
					      out_sleep_flag[dev].opts &= ~WK_SLEEP;
				      };
			      }
		    }
		  restore_flags(flags);
	  }
	audio_devs[dev]->dmap_out->dma_mode = DMODE_NONE;
	return audio_devs[dev]->dmap_out->qlen;
}

int
DMAbuf_release(int dev, int mode)
{
	unsigned long   flags;

	if (audio_devs[dev]->open_mode & OPEN_WRITE)
		audio_devs[dev]->dmap_out->closing = 1;
	if (audio_devs[dev]->open_mode & OPEN_READ)
		audio_devs[dev]->dmap_in->closing = 1;

	if (audio_devs[dev]->open_mode & OPEN_WRITE)
		if (!(audio_devs[dev]->dmap_in->mapping_flags & DMA_MAP_MAPPED))
			if (!signal_pending(current)
			    && (audio_devs[dev]->dmap_out->dma_mode == DMODE_OUTPUT))
			  {
				  DMAbuf_sync(dev);
			  }
	if (audio_devs[dev]->dmap_out->dma_mode == DMODE_OUTPUT)
	  {
		  memset(audio_devs[dev]->dmap_out->raw_buf,
			 audio_devs[dev]->dmap_out->neutral_byte,
			 audio_devs[dev]->dmap_out->bytes_in_use);
	  }
	save_flags(flags);
	cli();

	DMAbuf_reset(dev);
	audio_devs[dev]->d->close(dev);

	if (audio_devs[dev]->open_mode & OPEN_WRITE)
		close_dmap(dev, audio_devs[dev]->dmap_out, audio_devs[dev]->dmap_out->dma);

	if (audio_devs[dev]->open_mode == OPEN_READ ||
	    (audio_devs[dev]->open_mode != OPEN_WRITE &&
	     audio_devs[dev]->flags & DMA_DUPLEX))
		close_dmap(dev, audio_devs[dev]->dmap_in, audio_devs[dev]->dmap_in->dma);
	audio_devs[dev]->open_mode = 0;

	restore_flags(flags);

	return 0;
}

int
DMAbuf_activate_recording(int dev, struct dma_buffparms *dmap)
{
	if (!(audio_devs[dev]->open_mode & OPEN_READ))
		return 0;

	if (!(audio_devs[dev]->enable_bits & PCM_ENABLE_INPUT))
		return 0;

	if (dmap->dma_mode == DMODE_OUTPUT)	/* Direction change */
	  {
		  DMAbuf_sync(dev);
		  DMAbuf_reset(dev);
		  dmap->dma_mode = DMODE_NONE;
	  }
	if (!dmap->dma_mode)
	  {
		  int             err;

		  reorganize_buffers(dev, dmap, 1);
		  if ((err = audio_devs[dev]->d->prepare_for_input(dev,
				  dmap->fragment_size, dmap->nbufs)) < 0)
		    {
			    return err;
		    }
		  dmap->dma_mode = DMODE_INPUT;
	  }
	if (!(dmap->flags & DMA_ACTIVE))
	  {
		  if (dmap->needs_reorg)
			  reorganize_buffers(dev, dmap, 0);
		  local_start_dma(dev, dmap->raw_buf_phys, dmap->bytes_in_use,
				  DMA_MODE_READ);
		  audio_devs[dev]->d->start_input(dev, dmap->raw_buf_phys +
				       dmap->qtail * dmap->fragment_size,
						  dmap->fragment_size, 0);
		  dmap->flags |= DMA_ACTIVE;
		  if (audio_devs[dev]->d->trigger)
			  audio_devs[dev]->d->trigger(dev,
						      audio_devs[dev]->enable_bits * audio_devs[dev]->go);
	  }
	return 0;
}

int
DMAbuf_getrdbuffer(int dev, char **buf, int *len, int dontblock)
{
	unsigned long   flags;
	int             err = 0, n = 0;
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_in;

	if (!(audio_devs[dev]->open_mode & OPEN_READ))
		return -EIO;
	if (dmap->needs_reorg)
		reorganize_buffers(dev, dmap, 0);

	save_flags(flags);
	cli();
	if (audio_devs[dev]->dmap_in->mapping_flags & DMA_MAP_MAPPED)
	  {
		  printk("Sound: Can't read from mmapped device (1)\n");
		  restore_flags(flags);
		  return -EINVAL;
	} else
		while (dmap->qlen <= 0 && n++ < 10)
		  {
			  int             tmout;

			  if (!(audio_devs[dev]->enable_bits & PCM_ENABLE_INPUT) ||
			      !audio_devs[dev]->go)
			    {
				    restore_flags(flags);
				    return -EAGAIN;
			    }
			  if ((err = DMAbuf_activate_recording(dev, dmap)) < 0)
			    {
				    restore_flags(flags);
				    return err;
			    }
			  /* Wait for the next block */

			  if (dontblock)
			    {
				    restore_flags(flags);
				    return -EAGAIN;
			    }
			  if (!audio_devs[dev]->go)
				  tmout = 0;
			  else
			    {
				    tmout =
					(dmap->fragment_size * HZ) / dmap->data_rate;

				    tmout += HZ / 5;	/* Some safety distance */

				    if (tmout < (HZ / 2))
					    tmout = HZ / 2;
				    if (tmout > 20 * HZ)
					    tmout = 20 * HZ;
			    }


			  {
				  unsigned long   tlimit;

				  if (tmout)
					  current->timeout = tlimit = jiffies + (tmout);
				  else
					  tlimit = (unsigned long) -1;
				  in_sleep_flag[dev].opts = WK_SLEEP;
				  interruptible_sleep_on(&in_sleeper[dev]);
				  if (!(in_sleep_flag[dev].opts & WK_WAKEUP))
				    {
					    if (jiffies >= tlimit)
						    in_sleep_flag[dev].opts |= WK_TIMEOUT;
				    }
				  in_sleep_flag[dev].opts &= ~WK_SLEEP;
			  };
			  if ((in_sleep_flag[dev].opts & WK_TIMEOUT))
			    {
				    err = -EIO;
				    printk("Sound: DMA (input) timed out - IRQ/DRQ config error?\n");
				    dma_reset_input(dev);
				    ;
			  } else
				  err = -EINTR;
		  }
	restore_flags(flags);

	if (dmap->qlen <= 0)
	  {
		  if (err == 0)
			  err = -EINTR;
		  return err;
	  }
	*buf = &dmap->raw_buf[dmap->qhead * dmap->fragment_size + dmap->counts[dmap->qhead]];
	*len = dmap->fragment_size - dmap->counts[dmap->qhead];

	return dmap->qhead;
}

int
DMAbuf_rmchars(int dev, int buff_no, int c)
{
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_in;

	int             p = dmap->counts[dmap->qhead] + c;

	if (dmap->mapping_flags & DMA_MAP_MAPPED)
	  {
		  printk("Sound: Can't read from mmapped device (2)\n");
		  return -EINVAL;
	} else if (dmap->qlen <= 0)
		return -EIO;
	else if (p >= dmap->fragment_size)
	  {			/* This buffer is completely empty */
		  dmap->counts[dmap->qhead] = 0;
		  dmap->qlen--;
		  dmap->qhead = (dmap->qhead + 1) % dmap->nbufs;
	} else
		dmap->counts[dmap->qhead] = p;

	return 0;
}

int
DMAbuf_get_buffer_pointer(int dev, struct dma_buffparms *dmap, int direction)
{
/*
 * Try to approximate the active byte position of the DMA pointer within the
 * buffer area as well as possible.
 */
	int             pos;
	unsigned long   flags;

	save_flags(flags);
	cli();
	if (!(dmap->flags & DMA_ACTIVE))
		pos = 0;
	else
	  {
		  int             chan = dmap->dma;

		  clear_dma_ff(chan);
		  disable_dma(dmap->dma);
		  pos = get_dma_residue(chan);
		  pos = dmap->bytes_in_use - pos;

		  if (!(dmap->mapping_flags & DMA_MAP_MAPPED))
			  if (direction == DMODE_OUTPUT)
			    {
				    if (dmap->qhead == 0)
					    if (pos > dmap->fragment_size)
						    pos = 0;
			  } else
			    {
				    if (dmap->qtail == 0)
					    if (pos > dmap->fragment_size)
						    pos = 0;
			    }
		  if (pos < 0)
			  pos = 0;
		  if (pos >= dmap->bytes_in_use)
			  pos = 0;
		  enable_dma(dmap->dma);
	  }
	restore_flags(flags);
	/* printk( "%04x ",  pos); */

	return pos;
}

/*
 * DMAbuf_start_devices() is called by the /dev/music driver to start
 * one or more audio devices at desired moment.
 */
static void
DMAbuf_start_device(int dev)
{
	if (audio_devs[dev]->open_mode != 0)
		if (!audio_devs[dev]->go)
		  {
			  /* OK to start the device */
			  audio_devs[dev]->go = 1;

			  if (audio_devs[dev]->d->trigger)
				  audio_devs[dev]->d->trigger(dev,
							      audio_devs[dev]->enable_bits * audio_devs[dev]->go);
		  }
}

void
DMAbuf_start_devices(unsigned int devmask)
{
	int             dev;

	for (dev = 0; dev < num_audiodevs; dev++)
		if ((devmask & (1 << dev)) && audio_devs[dev] != NULL)
			DMAbuf_start_device(dev);
}

int
DMAbuf_space_in_queue(int dev)
{
	int             len, max, tmp;
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

	int             lim = dmap->nbufs;


	if (lim < 2)
		lim = 2;

	if (dmap->qlen >= lim)	/* No space at all */
		return 0;

	/*
	 * Verify that there are no more pending buffers than the limit
	 * defined by the process.
	 */

	max = dmap->max_fragments;
	if (max > lim)
		max = lim;
	len = dmap->qlen;

	if (audio_devs[dev]->d->local_qlen)
	  {
		  tmp = audio_devs[dev]->d->local_qlen(dev);
		  if (tmp && len)
			  tmp--;	/*
					 * This buffer has been counted twice
					 */
		  len += tmp;
	  }
	if (dmap->byte_counter % dmap->fragment_size)	/* There is a partial fragment */
		len = len + 1;

	if (len >= max)
		return 0;
	return max - len;
}

static int
output_sleep(int dev, int dontblock)
{
	int             tmout;
	int             err = 0;
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

	if (dontblock)
	  {
		  return -EAGAIN;
	  }
	if (!(audio_devs[dev]->enable_bits & PCM_ENABLE_OUTPUT))
	  {
		  return -EAGAIN;
	  }
	/*
	 * Wait for free space
	 */
	if (!audio_devs[dev]->go || dmap->flags & DMA_NOTIMEOUT)
		tmout = 0;
	else
	  {
		  tmout =
		      (dmap->fragment_size * HZ) / dmap->data_rate;

		  tmout += HZ / 5;	/* Some safety distance */

		  if (tmout < (HZ / 2))
			  tmout = HZ / 2;
		  if (tmout > 20 * HZ)
			  tmout = 20 * HZ;
	  }

	if (signal_pending(current))
		return -EIO;


	{
		unsigned long   tlimit;

		if (tmout)
			current->timeout = tlimit = jiffies + (tmout);
		else
			tlimit = (unsigned long) -1;
		out_sleep_flag[dev].opts = WK_SLEEP;
		interruptible_sleep_on(&out_sleeper[dev]);
		if (!(out_sleep_flag[dev].opts & WK_WAKEUP))
		  {
			  if (jiffies >= tlimit)
				  out_sleep_flag[dev].opts |= WK_TIMEOUT;
		  }
		out_sleep_flag[dev].opts &= ~WK_SLEEP;
	};
	if ((out_sleep_flag[dev].opts & WK_TIMEOUT))
	  {
		  printk("Sound: DMA (output) timed out - IRQ/DRQ config error?\n");
		  ;
		  dma_reset_output(dev);
	} else if (signal_pending(current))
	  {
		  err = -EINTR;
	  }
	return err;
}

static int
find_output_space(int dev, char **buf, int *size)
{
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;
	unsigned long   flags;
	unsigned long   active_offs;
	long            len, offs;
	int             maxfrags;
	int             occupied_bytes = (dmap->user_counter % dmap->fragment_size);

	*buf = dmap->raw_buf;

	if (!(maxfrags = DMAbuf_space_in_queue(dev)) && !occupied_bytes)
	  {
		  return 0;
	  }
	save_flags(flags);
	cli();

#ifdef BE_CONSERVATIVE
	active_offs = dmap->byte_counter + dmap->qhead * dmap->fragment_size;
#else
	active_offs = DMAbuf_get_buffer_pointer(dev, dmap, DMODE_OUTPUT);
	/* Check for pointer wrapping situation */
	if (active_offs < 0 || active_offs >= dmap->bytes_in_use)
		active_offs = 0;
	active_offs += dmap->byte_counter;
#endif

	offs = (dmap->user_counter % dmap->bytes_in_use) & ~3;
	if (offs < 0 || offs >= dmap->bytes_in_use)
	  {
		  printk("OSS: Got unexpected offs %ld. Giving up.\n", offs);
		  printk("Counter = %ld, bytes=%d\n", dmap->user_counter, dmap->bytes_in_use);
		  return 0;
	  }
	*buf = dmap->raw_buf + offs;

	len = active_offs + dmap->bytes_in_use - dmap->user_counter;	/* Number of unused bytes in buffer */

	if ((offs + len) > dmap->bytes_in_use)
	  {
		  len = dmap->bytes_in_use - offs;
	  }
	if (len < 0)
	  {
		  restore_flags(flags);
		  return 0;
	  }
	if (len > ((maxfrags * dmap->fragment_size) - occupied_bytes))
	  {
		  len = (maxfrags * dmap->fragment_size) - occupied_bytes;
	  }
	*size = len & ~3;

	restore_flags(flags);
	return (len > 0);
}

int
DMAbuf_getwrbuffer(int dev, char **buf, int *size, int dontblock)
{
	unsigned long   flags;
	int             err = -EIO;
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

	if (dmap->needs_reorg)
		reorganize_buffers(dev, dmap, 0);

	if (dmap->mapping_flags & DMA_MAP_MAPPED)
	  {
		  printk("Sound: Can't write to mmapped device (3)\n");
		  return -EINVAL;
	  }
	if (dmap->dma_mode == DMODE_INPUT)	/* Direction change */
	  {
		  DMAbuf_reset(dev);
		  dmap->dma_mode = DMODE_NONE;
	  }
	dmap->dma_mode = DMODE_OUTPUT;

	save_flags(flags);
	cli();

	while (find_output_space(dev, buf, size) <= 0)
	  {
		  if ((err = output_sleep(dev, dontblock)) < 0)
		    {
			    restore_flags(flags);
			    return err;
		    }
	  }

	restore_flags(flags);

	return 0;
}

int
DMAbuf_move_wrpointer(int dev, int l)
{
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;
	unsigned long   ptr = (dmap->user_counter / dmap->fragment_size)
	* dmap->fragment_size;

	unsigned long   end_ptr, p;
	int             post = (dmap->flags & DMA_POST);

	;

	dmap->flags &= ~DMA_POST;

	dmap->cfrag = -1;

	dmap->user_counter += l;
	dmap->flags |= DMA_DIRTY;

	if (dmap->user_counter >= dmap->max_byte_counter)
	  {			/* Wrap the byte counters */
		  long            decr = dmap->user_counter;

		  dmap->user_counter = (dmap->user_counter % dmap->bytes_in_use) + dmap->bytes_in_use;
		  decr -= dmap->user_counter;
		  dmap->byte_counter -= decr;
	  }
	end_ptr = (dmap->user_counter / dmap->fragment_size) * dmap->fragment_size;

	p = (dmap->user_counter - 1) % dmap->bytes_in_use;
	dmap->neutral_byte = dmap->raw_buf[p];

	/* Update the fragment based bookkeeping too */
	while (ptr < end_ptr)
	  {
		  dmap->counts[dmap->qtail] = dmap->fragment_size;
		  dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
		  dmap->qlen++;
		  ptr += dmap->fragment_size;
	  }

	dmap->counts[dmap->qtail] = dmap->user_counter - ptr;

/*
 * Let the low level driver to perform some postprocessing to
 * the written data.
 */
	if (audio_devs[dev]->d->postprocess_write)
		audio_devs[dev]->d->postprocess_write(dev);

	if (!(dmap->flags & DMA_ACTIVE))
		if (dmap->qlen > 1 ||
		    (dmap->qlen > 0 && (post || dmap->qlen >= dmap->nbufs - 1)))
		  {
			  DMAbuf_launch_output(dev, dmap);
		  };
	return 0;
}

int
DMAbuf_start_dma(int dev, unsigned long physaddr, int count, int dma_mode)
{
	int             chan;
	struct dma_buffparms *dmap;

	if (dma_mode == DMA_MODE_WRITE)
	  {
		  chan = audio_devs[dev]->dmap_out->dma;
		  dmap = audio_devs[dev]->dmap_out;
	} else
	  {
		  chan = audio_devs[dev]->dmap_in->dma;
		  dmap = audio_devs[dev]->dmap_in;
	  }

	if (dmap->raw_buf == NULL)
	  {
		  printk("sound: DMA buffer(1) == NULL\n");
		  printk("Device %d, chn=%s\n", dev, (dmap == audio_devs[dev]->dmap_out) ? "out" : "in");
		  return 0;
	  }
	if (chan < 0)
		return 0;

	sound_start_dma(dev, dmap, chan, physaddr, count, dma_mode, 0);

	return count;
}

static int
local_start_dma(int dev, unsigned long physaddr, int count, int dma_mode)
{
	int             chan;
	struct dma_buffparms *dmap;

	if (dma_mode == DMA_MODE_WRITE)
	  {
		  chan = audio_devs[dev]->dmap_out->dma;
		  dmap = audio_devs[dev]->dmap_out;
	} else
	  {
		  chan = audio_devs[dev]->dmap_in->dma;
		  dmap = audio_devs[dev]->dmap_in;
	  }

	if (dmap->raw_buf == NULL)
	  {
		  printk("sound: DMA buffer(2) == NULL\n");
		  printk("Device %d, chn=%s\n", dev, (dmap == audio_devs[dev]->dmap_out) ? "out" : "in");
		  return 0;
	  }
	if (dmap->flags & DMA_NODMA)
	  {
		  return 1;
	  }
	if (chan < 0)
		return 0;

	sound_start_dma(dev, dmap, chan, dmap->raw_buf_phys, dmap->bytes_in_use, dma_mode, 1);
	dmap->flags |= DMA_STARTED;

	return count;
}

static void
finish_output_interrupt(int dev, struct dma_buffparms *dmap)
{
	unsigned long   flags;

	if (dmap->audio_callback != NULL)
		dmap->audio_callback(dev, dmap->callback_parm);

	save_flags(flags);
	cli();
	if ((out_sleep_flag[dev].opts & WK_SLEEP))
	  {
		  {
			  out_sleep_flag[dev].opts = WK_WAKEUP;
			  wake_up(&out_sleeper[dev]);
		  };
	  }
	restore_flags(flags);
}

static void
do_outputintr(int dev, int dummy)
{
	unsigned long   flags;
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;
	int             this_fragment;

#ifdef OS_DMA_INTR
	if (audio_devs[dev]->dmap_out->dma >= 0)
		sound_dma_intr(dev, audio_devs[dev]->dmap_out, audio_devs[dev]->dmap_out->dma);
#endif

	if (dmap->raw_buf == NULL)
	  {
		  printk("Sound: Fatal error. Audio interrupt (%d) after freeing buffers.\n", dev);
		  return;
	  }
	if (dmap->mapping_flags & DMA_MAP_MAPPED)	/* Virtual memory mapped access */
	  {
		  /* mmapped access */
		  dmap->qhead = (dmap->qhead + 1) % dmap->nbufs;
		  if (dmap->qhead == 0)		/* Wrapped */
		    {
			    dmap->byte_counter += dmap->bytes_in_use;
			    if (dmap->byte_counter >= dmap->max_byte_counter)	/* Overflow */
			      {
				      long            decr = dmap->byte_counter;

				      dmap->byte_counter = (dmap->byte_counter % dmap->bytes_in_use) + dmap->bytes_in_use;
				      decr -= dmap->byte_counter;
				      dmap->user_counter -= decr;
			      }
		    }
		  dmap->qlen++;	/* Yes increment it (don't decrement) */
		  if (!(audio_devs[dev]->flags & DMA_AUTOMODE))
			  dmap->flags &= ~DMA_ACTIVE;
		  dmap->counts[dmap->qhead] = dmap->fragment_size;

		  DMAbuf_launch_output(dev, dmap);
		  finish_output_interrupt(dev, dmap);
		  return;
	  }
	save_flags(flags);
	cli();

	dmap->qlen--;
	this_fragment = dmap->qhead;
	dmap->qhead = (dmap->qhead + 1) % dmap->nbufs;

	if (dmap->qhead == 0)	/* Wrapped */
	  {
		  dmap->byte_counter += dmap->bytes_in_use;
		  if (dmap->byte_counter >= dmap->max_byte_counter)	/* Overflow */
		    {
			    long            decr = dmap->byte_counter;

			    dmap->byte_counter = (dmap->byte_counter % dmap->bytes_in_use) + dmap->bytes_in_use;
			    decr -= dmap->byte_counter;
			    dmap->user_counter -= decr;
		    }
	  }
	if (!(audio_devs[dev]->flags & DMA_AUTOMODE))
		dmap->flags &= ~DMA_ACTIVE;

	while (dmap->qlen < 0)
	  {
		  dmap->underrun_count++;

		  dmap->qlen++;
		  if (dmap->flags & DMA_DIRTY && dmap->applic_profile != APF_CPUINTENS)
		    {
			    dmap->flags &= ~DMA_DIRTY;
			    memset(audio_devs[dev]->dmap_out->raw_buf,
				 audio_devs[dev]->dmap_out->neutral_byte,
				   audio_devs[dev]->dmap_out->buffsize);
		    }
		  dmap->user_counter += dmap->fragment_size;
		  dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
	  }

	if (dmap->qlen > 0)
		DMAbuf_launch_output(dev, dmap);

	restore_flags(flags);
	finish_output_interrupt(dev, dmap);
}

void
DMAbuf_outputintr(int dev, int notify_only)
{
	unsigned long   flags;
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

	save_flags(flags);
	cli();

	if (!(dmap->flags & DMA_NODMA))
	  {
		  int             chan = dmap->dma, pos, n;

		  clear_dma_ff(chan);
		  disable_dma(dmap->dma);
		  pos = dmap->bytes_in_use - get_dma_residue(chan);
		  enable_dma(dmap->dma);

		  pos = pos / dmap->fragment_size;	/* Actual qhead */
		  if (pos < 0 || pos >= dmap->nbufs)
			  pos = 0;

		  n = 0;
		  while (dmap->qhead != pos && n++ < dmap->nbufs)
		    {
			    do_outputintr(dev, notify_only);
		    }
	} else
		do_outputintr(dev, notify_only);
	restore_flags(flags);
}

static void
do_inputintr(int dev)
{
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_in;
	unsigned long   flags;

#ifdef OS_DMA_INTR
	if (audio_devs[dev]->dmap_in->dma >= 0)
		sound_dma_intr(dev, audio_devs[dev]->dmap_in, audio_devs[dev]->dmap_in->dma);
#endif

	if (dmap->raw_buf == NULL)
	  {
		  printk("Sound: Fatal error. Audio interrupt after freeing buffers.\n");
		  return;
	  }
	if (dmap->mapping_flags & DMA_MAP_MAPPED)
	  {
		  dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
		  if (dmap->qtail == 0)		/* Wrapped */
		    {
			    dmap->byte_counter += dmap->bytes_in_use;
			    if (dmap->byte_counter >= dmap->max_byte_counter)	/* Overflow */
			      {
				      long            decr = dmap->byte_counter;

				      dmap->byte_counter = (dmap->byte_counter % dmap->bytes_in_use) + dmap->bytes_in_use;
				      decr -= dmap->byte_counter;
				      dmap->user_counter -= decr;
			      }
		    }
		  dmap->qlen++;

		  if (!(audio_devs[dev]->flags & DMA_AUTOMODE))
		    {
			    if (dmap->needs_reorg)
				    reorganize_buffers(dev, dmap, 0);
			    local_start_dma(dev, dmap->raw_buf_phys, dmap->bytes_in_use,
					    DMA_MODE_READ);
			    audio_devs[dev]->d->start_input(dev, dmap->raw_buf_phys +
				       dmap->qtail * dmap->fragment_size,
						 dmap->fragment_size, 1);
			    if (audio_devs[dev]->d->trigger)
				    audio_devs[dev]->d->trigger(dev,
								audio_devs[dev]->enable_bits * audio_devs[dev]->go);
		    }
		  dmap->flags |= DMA_ACTIVE;
	} else if (dmap->qlen >= (dmap->nbufs - 1))
	  {
		  printk("Sound: Recording overrun\n");
		  dmap->underrun_count++;

		  /* Just throw away the oldest fragment but keep the engine running */
		  dmap->qhead = (dmap->qhead + 1) % dmap->nbufs;
		  dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
	} else if (dmap->qlen >= 0 && dmap->qlen < dmap->nbufs)
	  {
		  dmap->qlen++;
		  dmap->qtail = (dmap->qtail + 1) % dmap->nbufs;
		  if (dmap->qtail == 0)		/* Wrapped */
		    {
			    dmap->byte_counter += dmap->bytes_in_use;
			    if (dmap->byte_counter >= dmap->max_byte_counter)	/* Overflow */
			      {
				      long            decr = dmap->byte_counter;

				      dmap->byte_counter = (dmap->byte_counter % dmap->bytes_in_use) + dmap->bytes_in_use;
				      decr -= dmap->byte_counter;
				      dmap->user_counter -= decr;
			      }
		    }
	  }
	if (!(audio_devs[dev]->flags & DMA_AUTOMODE) || dmap->flags & DMA_NODMA)
	  {
		  local_start_dma(dev, dmap->raw_buf_phys, dmap->bytes_in_use,
				  DMA_MODE_READ);
		  audio_devs[dev]->d->start_input(dev, dmap->raw_buf_phys +
				       dmap->qtail * dmap->fragment_size,
						  dmap->fragment_size, 1);
		  if (audio_devs[dev]->d->trigger)
			  audio_devs[dev]->d->trigger(dev,
						      audio_devs[dev]->enable_bits * audio_devs[dev]->go);
	  }
	dmap->flags |= DMA_ACTIVE;

	save_flags(flags);
	cli();
	if (dmap->qlen > 0)
		if ((in_sleep_flag[dev].opts & WK_SLEEP))
		  {
			  {
				  in_sleep_flag[dev].opts = WK_WAKEUP;
				  wake_up(&in_sleeper[dev]);
			  };
		  }
	restore_flags(flags);
}

void
DMAbuf_inputintr(int dev)
{
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_in;
	unsigned long   flags;

	save_flags(flags);
	cli();

	if (!(dmap->flags & DMA_NODMA))
	  {
		  int             chan = dmap->dma, pos, n;

		  clear_dma_ff(chan);
		  disable_dma(dmap->dma);
		  pos = dmap->bytes_in_use - get_dma_residue(chan);
		  enable_dma(dmap->dma);

		  pos = pos / dmap->fragment_size;	/* Actual qhead */
		  if (pos < 0 || pos >= dmap->nbufs)
			  pos = 0;

		  n = 0;
		  while (dmap->qtail != pos && ++n < dmap->nbufs)
		    {
			    do_inputintr(dev);
		    }
	} else
		do_inputintr(dev);
	restore_flags(flags);
}

int
DMAbuf_open_dma(int dev)
{
/*
 *    NOTE!  This routine opens only the primary DMA channel (output).
 */

	int             chan = audio_devs[dev]->dmap_out->dma;
	int             err;

	if ((err = open_dmap(dev, OPEN_READWRITE, audio_devs[dev]->dmap_out, chan)) < 0)
	  {
		  return -EBUSY;
	  }
	dma_init_buffers(dev, audio_devs[dev]->dmap_out);
	out_sleep_flag[dev].opts = WK_NONE;
	audio_devs[dev]->dmap_out->flags |= DMA_ALLOC_DONE;
	audio_devs[dev]->dmap_out->fragment_size = audio_devs[dev]->dmap_out->buffsize;

	if (chan >= 0)
	  {
		  unsigned long   flags;

		  save_flags(flags);
		  cli();
		  disable_dma(audio_devs[dev]->dmap_out->dma);
		  clear_dma_ff(chan);
		  restore_flags(flags);
	  }
	return 0;
}

void
DMAbuf_close_dma(int dev)
{
	close_dmap(dev, audio_devs[dev]->dmap_out, audio_devs[dev]->dmap_out->dma);
}

void
DMAbuf_init(int dev, int dma1, int dma2)
{
	/*
	   * NOTE! This routine could be called several times.
	 */

	if (audio_devs[dev] && audio_devs[dev]->dmap_out == NULL)
	  {
		  if (audio_devs[dev]->d == NULL)
			  panic("OSS: audio_devs[%d]->d == NULL\n", dev);

		  if (audio_devs[dev]->parent_dev)
		    {		/* Use DMA map of the parent dev */
			    int             parent = audio_devs[dev]->parent_dev - 1;

			    audio_devs[dev]->dmap_out = audio_devs[parent]->dmap_out;
			    audio_devs[dev]->dmap_in = audio_devs[parent]->dmap_in;
		  } else
		    {
			    audio_devs[dev]->dmap_out =
				audio_devs[dev]->dmap_in =
				&dmaps[ndmaps++];
			    audio_devs[dev]->dmap_out->dma = dma1;

			    if (audio_devs[dev]->flags & DMA_DUPLEX)
			      {
				      audio_devs[dev]->dmap_in =
					  &dmaps[ndmaps++];
				      audio_devs[dev]->dmap_in->dma = dma2;
			      }
		    }
	  }
}

int
DMAbuf_select(int dev, struct fileinfo *file, int sel_type, poll_table * wait)
{
	struct dma_buffparms *dmap;
	unsigned long   flags;

	switch (sel_type)
	  {
	  case SEL_IN:
		  if (!(audio_devs[dev]->open_mode & OPEN_READ))
			  return 0;

		  dmap = audio_devs[dev]->dmap_in;

		  if (dmap->mapping_flags & DMA_MAP_MAPPED)
		    {
			    if (dmap->qlen)
				    return 1;

			    save_flags(flags);
			    cli();

			    in_sleep_flag[dev].opts = WK_SLEEP;
			    poll_wait(&in_sleeper[dev], wait);
			    restore_flags(flags);
			    return 0;
		    }
		  if (dmap->dma_mode != DMODE_INPUT)
		    {
			    if (dmap->dma_mode == DMODE_NONE &&
				audio_devs[dev]->enable_bits & PCM_ENABLE_INPUT &&
				!dmap->qlen &&
				audio_devs[dev]->go)
			      {
				      unsigned long   flags;

				      save_flags(flags);
				      cli();
				      DMAbuf_activate_recording(dev, dmap);
				      restore_flags(flags);
			      }
			    return 0;
		    }
		  if (!dmap->qlen)
		    {
			    save_flags(flags);
			    cli();

			    in_sleep_flag[dev].opts = WK_SLEEP;
			    poll_wait(&in_sleeper[dev], wait);
			    restore_flags(flags);
			    return 0;
		    }
		  return 1;
		  break;

	  case SEL_OUT:
		  dmap = audio_devs[dev]->dmap_out;

		  if (!(audio_devs[dev]->open_mode & OPEN_WRITE))
			  return 0;

		  if (dmap->mapping_flags & DMA_MAP_MAPPED)
		    {
			    if (dmap->qlen)
				    return 1;

			    save_flags(flags);
			    cli();

			    out_sleep_flag[dev].opts = WK_SLEEP;
			    poll_wait(&out_sleeper[dev], wait);
			    restore_flags(flags);
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
		  if (!DMAbuf_space_in_queue(dev))
		    {
			    save_flags(flags);
			    cli();

			    out_sleep_flag[dev].opts = WK_SLEEP;
			    poll_wait(&out_sleeper[dev], wait);
			    restore_flags(flags);
			    return 0;
		    }
		  return 1;
		  break;

	  case SEL_EX:
		  return 0;
	  }

	return 0;
}

void
DMAbuf_deinit(int dev)
{
/* This routine is called when driver is being unloaded */
#ifdef RUNTIME_DMA_ALLOC
	if (audio_devs[dev])
		sound_free_dmap (dev, audio_devs[dev]->dmap_out, 
				 audio_devs[dev]->dmap_out->dma);

	if (audio_devs[dev] && audio_devs[dev]->flags & DMA_DUPLEX)
		sound_free_dmap (dev, audio_devs[dev]->dmap_in, 
				 audio_devs[dev]->dmap_in->dma);
#endif
}

#endif
