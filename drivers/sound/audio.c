/*
 * sound/audio.c
 *
 * Device file manager for /dev/audio
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

#if defined(CONFIG_AUDIO) || defined(MODULE)

#include "ulaw.h"
#include "coproc.h"

#define NEUTRAL8	0x80
#define NEUTRAL16	0x00

static int      audio_mode[MAX_AUDIO_DEV];
static int      dev_nblock[MAX_AUDIO_DEV];	/* 1 if in nonblocking mode */

#define		AM_NONE		0
#define		AM_WRITE	OPEN_WRITE
#define 	AM_READ		OPEN_READ
int             dma_ioctl(int dev, unsigned int cmd, caddr_t arg);


static int      local_format[MAX_AUDIO_DEV], audio_format[MAX_AUDIO_DEV];
static int      local_conversion[MAX_AUDIO_DEV];

#define CNV_MU_LAW	0x00000001
static int
set_format(int dev, int fmt)
{
	if (fmt != AFMT_QUERY)
	  {
		  local_conversion[dev] = 0;

		  if (!(audio_devs[dev]->format_mask & fmt))	/* Not supported */
			  if (fmt == AFMT_MU_LAW)
			    {
				    fmt = AFMT_U8;
				    local_conversion[dev] = CNV_MU_LAW;
			  } else
				  fmt = AFMT_U8;	/* This is always supported */

		  audio_format[dev] = audio_devs[dev]->d->set_bits(dev, fmt);
		  local_format[dev] = fmt;
	} else
		return local_format[dev];

	return local_format[dev];
}

int
audio_open(int dev, struct fileinfo *file)
{
	int             ret;
	int             bits;
	int             dev_type = dev & 0x0f;
	int             mode = file->mode & O_ACCMODE;

	dev = dev >> 4;

	if (dev_type == SND_DEV_DSP16)
		bits = 16;
	else
		bits = 8;

	if (dev < 0 || dev >= num_audiodevs)
		return -ENXIO;

	if ((ret = DMAbuf_open(dev, mode)) < 0)
		return ret;

	if (audio_devs[dev]->coproc)
		if ((ret = audio_devs[dev]->coproc->
		     open(audio_devs[dev]->coproc->devc, COPR_PCM)) < 0)
		  {
			  audio_release(dev, file);
			  printk("Sound: Can't access coprocessor device\n");

			  return ret;
		  }
	local_conversion[dev] = 0;

	if (dev_type == SND_DEV_AUDIO)
	  {
		  set_format(dev, AFMT_MU_LAW);
	} else
		set_format(dev, bits);

	audio_mode[dev] = AM_NONE;
	dev_nblock[dev] = 0;


	return ret;
}

static void
sync_output(int dev)
{
	int             p, i;
	int             l;
	struct dma_buffparms *dmap = audio_devs[dev]->dmap_out;

	if (dmap->fragment_size <= 0)
		return;
	dmap->flags |= DMA_POST;

	/* Align the write pointer with fragment boundaries */
	if ((l = dmap->user_counter % dmap->fragment_size) > 0)
	  {
		  int             len;
		  unsigned long   offs = dmap->user_counter % dmap->bytes_in_use;

		  len = dmap->fragment_size - l;
		  memset(dmap->raw_buf + offs, dmap->neutral_byte, len);
		  DMAbuf_move_wrpointer(dev, len);
	  }
/*
 * Clean all unused buffer fragments.
 */

	p = dmap->qtail;
	dmap->flags |= DMA_POST;

	for (i = dmap->qlen + 1; i < dmap->nbufs; i++)
	  {
		  p = (p + 1) % dmap->nbufs;
		  if (((dmap->raw_buf + p * dmap->fragment_size) + dmap->fragment_size) >
		      (dmap->raw_buf + dmap->buffsize))
			  printk("audio: Buffer error 2\n");

		  memset(dmap->raw_buf + p * dmap->fragment_size,
			 dmap->neutral_byte,
			 dmap->fragment_size);
	  }

	dmap->flags |= DMA_DIRTY;
}

void
audio_release(int dev, struct fileinfo *file)
{
	int             mode;

	dev = dev >> 4;
	mode = file->mode & O_ACCMODE;

	audio_devs[dev]->dmap_out->closing = 1;
	audio_devs[dev]->dmap_in->closing = 1;

	sync_output(dev);

	if (audio_devs[dev]->coproc)
		audio_devs[dev]->coproc->close(audio_devs[dev]->coproc->devc, COPR_PCM);
	DMAbuf_release(dev, mode);
}

#if defined(NO_INLINE_ASM) || !defined(i386)
static void
translate_bytes(const unsigned char *table, unsigned char *buff, int n)
{
	unsigned long   i;

	if (n <= 0)
		return;

	for (i = 0; i < n; ++i)
		buff[i] = table[buff[i]];
}

#else
extern inline void
translate_bytes(const void *table, void *buff, int n)
{
	if (n > 0)
	  {
		  __asm__("cld\n"
			  "1:\tlodsb\n\t"
			  "xlatb\n\t"
			  "stosb\n\t"
		"loop 1b\n\t":
		:	  "b"((long) table), "c"(n), "D"((long) buff), "S"((long) buff)
		:	  "bx", "cx", "di", "si", "ax");
	  }
}

#endif

int
audio_write(int dev, struct fileinfo *file, const char *buf, int count)
{
	int             c, p, l, buf_size;
	int             err;
	char           *dma_buf;

	dev = dev >> 4;

	p = 0;
	c = count;

	if (!(audio_devs[dev]->open_mode & OPEN_WRITE))
		return -EPERM;

	if (audio_devs[dev]->flags & DMA_DUPLEX)
		audio_mode[dev] |= AM_WRITE;
	else
		audio_mode[dev] = AM_WRITE;

	if (!count)		/* Flush output */
	  {
		  sync_output(dev);
		  return 0;
	  }
	while (c)
	  {
		  if ((err = DMAbuf_getwrbuffer(dev, &dma_buf, &buf_size, dev_nblock[dev])) < 0)
		    {
			    /* Handle nonblocking mode */
			    if (dev_nblock[dev] && err == -EAGAIN)
				    return p;	/* No more space. Return # of accepted bytes */
			    return err;
		    }
		  l = c;

		  if (l > buf_size)
			  l = buf_size;

		  if (!audio_devs[dev]->d->copy_user)
		    {
			    if ((dma_buf + l) >
				(audio_devs[dev]->dmap_out->raw_buf + audio_devs[dev]->dmap_out->buffsize))
			      {
				      printk("audio: Buffer error 3 (%lx,%d), (%lx, %d)\n", (long) dma_buf, l, (long) audio_devs[dev]->dmap_out->raw_buf, (int) audio_devs[dev]->dmap_out->buffsize);
				      return -EDOM;
			      }
			    if (dma_buf < audio_devs[dev]->dmap_out->raw_buf)
			      {
				      printk("audio: Buffer error 13 (%lx<%lx)\n", (long) dma_buf, (long) audio_devs[dev]->dmap_out->raw_buf);
				      return -EDOM;
			      }
			    copy_from_user(dma_buf, &(buf)[p], l);
		  } else
			  audio_devs[dev]->d->copy_user(dev,
						  dma_buf, 0, buf, p, l);

		  if (local_conversion[dev] & CNV_MU_LAW)
		    {
			    /*
			     * This just allows interrupts while the conversion is running
			     */
			    sti();
			    translate_bytes(ulaw_dsp, (unsigned char *) dma_buf, l);
		    }
		  c -= l;
		  p += l;
		  DMAbuf_move_wrpointer(dev, l);

	  }

	return count;
}

int
audio_read(int dev, struct fileinfo *file, char *buf, int count)
{
	int             c, p, l;
	char           *dmabuf;
	int             buf_no;

	dev = dev >> 4;
	p = 0;
	c = count;

	if (!(audio_devs[dev]->open_mode & OPEN_READ))
		return -EPERM;

	if ((audio_mode[dev] & AM_WRITE) && !(audio_devs[dev]->flags & DMA_DUPLEX))
	  {
		  sync_output(dev);
	  }
	if (audio_devs[dev]->flags & DMA_DUPLEX)
		audio_mode[dev] |= AM_READ;
	else
		audio_mode[dev] = AM_READ;

	while (c)
	  {
		  if ((buf_no = DMAbuf_getrdbuffer(dev, &dmabuf, &l,
						   dev_nblock[dev])) < 0)
		    {
			    /* Nonblocking mode handling. Return current # of bytes */

			    if (dev_nblock[dev] && buf_no == -EAGAIN)
				    return p;

			    return buf_no;
		    }
		  if (l > c)
			  l = c;

		  /*
		   * Insert any local processing here.
		   */

		  if (local_conversion[dev] & CNV_MU_LAW)
		    {
			    /*
			     * This just allows interrupts while the conversion is running
			     */
			    sti();

			    translate_bytes(dsp_ulaw, (unsigned char *) dmabuf, l);
		    }
		  {
			  char           *fixit = dmabuf;

			  copy_to_user(&(buf)[p], fixit, l);
		  };

		  DMAbuf_rmchars(dev, buf_no, l);

		  p += l;
		  c -= l;
	  }

	return count - c;
}

int
audio_ioctl(int dev, struct fileinfo *file_must_not_be_used,
	    unsigned int cmd, caddr_t arg)
{
	int             val;

	/* printk( "audio_ioctl(%x, %x)\n",  (int)cmd,  (int)arg); */

	dev = dev >> 4;

	if (((cmd >> 8) & 0xff) == 'C')
	  {
		  if (audio_devs[dev]->coproc)	/* Coprocessor ioctl */
			  return audio_devs[dev]->coproc->ioctl(audio_devs[dev]->coproc->devc, cmd, arg, 0);
		  else
			  printk("/dev/dsp%d: No coprocessor for this device\n", dev);

		  return -ENXIO;
	} else
		switch (cmd)
		  {
		  case SNDCTL_DSP_SYNC:
			  if (!(audio_devs[dev]->open_mode & OPEN_WRITE))
				  return 0;

			  if (audio_devs[dev]->dmap_out->fragment_size == 0)
				  return 0;
			  sync_output(dev);
			  DMAbuf_sync(dev);
			  DMAbuf_reset(dev);
			  return 0;
			  break;

		  case SNDCTL_DSP_POST:
			  if (!(audio_devs[dev]->open_mode & OPEN_WRITE))
				  return 0;
			  if (audio_devs[dev]->dmap_out->fragment_size == 0)
				  return 0;
			  audio_devs[dev]->dmap_out->flags |= DMA_POST | DMA_DIRTY;
			  sync_output(dev);
			  dma_ioctl(dev, SNDCTL_DSP_POST, (caddr_t) 0);
			  return 0;
			  break;

		  case SNDCTL_DSP_RESET:
			  audio_mode[dev] = AM_NONE;
			  DMAbuf_reset(dev);
			  return 0;
			  break;

		  case SNDCTL_DSP_GETFMTS:
			  return (*(int *) arg = audio_devs[dev]->format_mask);
			  break;

		  case SNDCTL_DSP_SETFMT:
			  val = *(int *) arg;
			  return (*(int *) arg = set_format(dev, val));

		  case SNDCTL_DSP_GETISPACE:
			  if (!(audio_devs[dev]->open_mode & OPEN_READ))
				  return 0;
			  if ((audio_mode[dev] & AM_WRITE) && !(audio_devs[dev]->flags & DMA_DUPLEX))
				  return -EBUSY;

			  {
				  audio_buf_info  info;

				  int             err = dma_ioctl(dev, cmd, (caddr_t) & info);

				  if (err < 0)
					  return err;

				  memcpy((&((char *) arg)[0]), (char *) &info, sizeof(info));
				  return 0;
			  }

		  case SNDCTL_DSP_GETOSPACE:
			  if (!(audio_devs[dev]->open_mode & OPEN_WRITE))
				  return -EPERM;
			  if ((audio_mode[dev] & AM_READ) && !(audio_devs[dev]->flags & DMA_DUPLEX))
				  return -EBUSY;

			  {
				  audio_buf_info  info;

				  int             err = dma_ioctl(dev, cmd, (caddr_t) & info);

				  if (err < 0)
					  return err;

				  memcpy((&((char *) arg)[0]), (char *) &info, sizeof(info));
				  return 0;
			  }

		  case SNDCTL_DSP_NONBLOCK:
			  dev_nblock[dev] = 1;
			  return 0;
			  break;

		  case SNDCTL_DSP_GETCAPS:
			  {
				  int             info = 1;	/* Revision level of this ioctl() */

				  if (audio_devs[dev]->flags & DMA_DUPLEX &&
				      audio_devs[dev]->open_mode == OPEN_READWRITE)
					  info |= DSP_CAP_DUPLEX;

				  if (audio_devs[dev]->coproc)
					  info |= DSP_CAP_COPROC;

				  if (audio_devs[dev]->d->local_qlen)	/* Device has hidden buffers */
					  info |= DSP_CAP_BATCH;

				  if (audio_devs[dev]->d->trigger)	/* Supports SETTRIGGER */
					  info |= DSP_CAP_TRIGGER;

				  info |= DSP_CAP_MMAP;

				  memcpy((&((char *) arg)[0]), (char *) &info, sizeof(info));
				  return 0;
			  }
			  break;

		  case SOUND_PCM_WRITE_RATE:
			  val = *(int *) arg;
			  return (*(int *) arg = audio_devs[dev]->d->set_speed(dev, val));

		  case SOUND_PCM_READ_RATE:
			  return (*(int *) arg = audio_devs[dev]->d->set_speed(dev, 0));

		  case SNDCTL_DSP_STEREO:
			  {
				  int             n;

				  n = *(int *) arg;
				  if (n > 1)
				    {
					    printk("sound: SNDCTL_DSP_STEREO called with invalid argument %d\n", n);
					    return -EINVAL;
				    }
				  if (n < 0)
					  return -EINVAL;

				  return (*(int *) arg = audio_devs[dev]->d->set_channels(dev, n + 1) - 1);
			  }

		  case SOUND_PCM_WRITE_CHANNELS:
			  val = *(int *) arg;
			  return (*(int *) arg = audio_devs[dev]->d->set_channels(dev, val));

		  case SOUND_PCM_READ_CHANNELS:
			  return (*(int *) arg = audio_devs[dev]->d->set_channels(dev, 0));

		  case SOUND_PCM_READ_BITS:
			  return (*(int *) arg = audio_devs[dev]->d->set_bits(dev, 0));

		  case SNDCTL_DSP_SETDUPLEX:
			  if (audio_devs[dev]->open_mode != OPEN_READWRITE)
				  return -EPERM;
			  if (audio_devs[dev]->flags & DMA_DUPLEX)
				  return 0;
			  else
				  return -EIO;
			  break;

		  case SNDCTL_DSP_PROFILE:
			  if (audio_devs[dev]->open_mode & OPEN_WRITE)
				  audio_devs[dev]->dmap_out->applic_profile = *(int *) arg;
			  if (audio_devs[dev]->open_mode & OPEN_READ)
				  audio_devs[dev]->dmap_in->applic_profile = *(int *) arg;
			  return 0;
			  break;

		  default:
			  return dma_ioctl(dev, cmd, arg);
		  }
}

void
audio_init_devices(void)
{
	/*
	 * NOTE! This routine could be called several times during boot.
	 */
}


#endif

void
reorganize_buffers(int dev, struct dma_buffparms *dmap, int recording)
{
	/*
	 * This routine breaks the physical device buffers to logical ones.
	 */

	struct audio_operations *dsp_dev = audio_devs[dev];

	unsigned        i, n;
	unsigned        sr, nc, sz, bsz;

	sr = dsp_dev->d->set_speed(dev, 0);
	nc = dsp_dev->d->set_channels(dev, 0);
	sz = dsp_dev->d->set_bits(dev, 0);

	if (sz == 8)
		dmap->neutral_byte = NEUTRAL8;
	else
		dmap->neutral_byte = NEUTRAL16;

	if (sr < 1 || nc < 1 || sz < 1)
	  {
		  printk("Warning: Invalid PCM parameters[%d] sr=%d, nc=%d, sz=%d\n", dev, sr, nc, sz);
		  sr = DSP_DEFAULT_SPEED;
		  nc = 1;
		  sz = 8;
	  }
	sz = sr * nc * sz;

	sz /= 8;		/* #bits -> #bytes */
	dmap->data_rate = sz;

	if (!dmap->needs_reorg)
		return;
	dmap->needs_reorg = 0;

	if (dmap->fragment_size == 0)
	  {			/* Compute the fragment size using the default algorithm */

		  /*
		     * Compute a buffer size for time not exceeding 1 second.
		     * Usually this algorithm gives a buffer size for 0.5 to 1.0 seconds
		     * of sound (using the current speed, sample size and #channels).
		   */

		  bsz = dmap->buffsize;
		  while (bsz > sz)
			  bsz /= 2;

		  if (bsz == dmap->buffsize)
			  bsz /= 2;	/* Needs at least 2 buffers */

/*
 *    Split the computed fragment to smaller parts. After 3.5a9
 *      the default subdivision is 4 which should give better
 *      results when recording.
 */

		  if (dmap->subdivision == 0)	/* Not already set */
		    {
			    dmap->subdivision = 4;	/* Init to the default value */

			    if ((bsz / dmap->subdivision) > 4096)
				    dmap->subdivision *= 2;
			    if ((bsz / dmap->subdivision) < 4096)
				    dmap->subdivision = 1;
		    }
		  bsz /= dmap->subdivision;

		  if (bsz < 16)
			  bsz = 16;	/* Just a sanity check */

		  dmap->fragment_size = bsz;
	} else
	  {
		  /*
		     * The process has specified the buffer size with SNDCTL_DSP_SETFRAGMENT or
		     * the buffer size computation has already been done.
		   */
		  if (dmap->fragment_size > (dmap->buffsize / 2))
			  dmap->fragment_size = (dmap->buffsize / 2);
		  bsz = dmap->fragment_size;
	  }

	if (audio_devs[dev]->min_fragment)
		if (bsz < (1 << audio_devs[dev]->min_fragment))
			bsz = 1 << audio_devs[dev]->min_fragment;
	if (audio_devs[dev]->max_fragment)
		if (bsz > (1 << audio_devs[dev]->max_fragment))
			bsz = 1 << audio_devs[dev]->max_fragment;
	bsz &= ~0x07;		/* Force size which is multiple of 8 bytes */
#ifdef OS_DMA_ALIGN_CHECK
	OS_DMA_ALIGN_CHECK(bsz);
#endif

	n = dmap->buffsize / bsz;
	if (n > MAX_SUB_BUFFERS)
		n = MAX_SUB_BUFFERS;
	if (n > dmap->max_fragments)
		n = dmap->max_fragments;

	if (n < 2)
	  {
		  n = 2;
		  bsz /= 2;
	  }
	dmap->nbufs = n;
	dmap->bytes_in_use = n * bsz;
	dmap->fragment_size = bsz;
	dmap->max_byte_counter = (dmap->data_rate * 60 * 60) +
	    dmap->bytes_in_use;	/* Approximately one hour */

	if (dmap->raw_buf)
	  {
		  memset(dmap->raw_buf,
			 dmap->neutral_byte,
			 dmap->bytes_in_use);
	  }
	for (i = 0; i < dmap->nbufs; i++)
	  {
		  dmap->counts[i] = 0;
	  }

	dmap->flags |= DMA_ALLOC_DONE | DMA_EMPTY;
}

static int
dma_subdivide(int dev, struct dma_buffparms *dmap, caddr_t arg, int fact)
{
	if (fact == 0)
	  {
		  fact = dmap->subdivision;
		  if (fact == 0)
			  fact = 1;
		  return (*(int *) arg = fact);
	  }
	if (dmap->subdivision != 0 ||
	    dmap->fragment_size)	/* Too late to change */
		return -EINVAL;

	if (fact > MAX_REALTIME_FACTOR)
		return -EINVAL;

	if (fact != 1 && fact != 2 && fact != 4 && fact != 8 && fact != 16)
		return -EINVAL;

	dmap->subdivision = fact;
	return (*(int *) arg = fact);
}

static int
dma_set_fragment(int dev, struct dma_buffparms *dmap, caddr_t arg, int fact)
{
	int             bytes, count;

	if (fact == 0)
		return -EIO;

	if (dmap->subdivision != 0 ||
	    dmap->fragment_size)	/* Too late to change */
		return -EINVAL;

	bytes = fact & 0xffff;
	count = (fact >> 16) & 0x7fff;

	if (count == 0)
		count = MAX_SUB_BUFFERS;
	else if (count < MAX_SUB_BUFFERS)
		count++;

	if (bytes < 4 || bytes > 17)	/* <16 || > 512k */
		return -EINVAL;

	if (count < 2)
		return -EINVAL;

	if (audio_devs[dev]->min_fragment > 0)
		if (bytes < audio_devs[dev]->min_fragment)
			bytes = audio_devs[dev]->min_fragment;

	if (audio_devs[dev]->max_fragment > 0)
		if (bytes > audio_devs[dev]->max_fragment)
			bytes = audio_devs[dev]->max_fragment;

#ifdef OS_DMA_MINBITS
	if (bytes < OS_DMA_MINBITS)
		bytes = OS_DMA_MINBITS;
#endif

	dmap->fragment_size = (1 << bytes);
	dmap->max_fragments = count;

	if (dmap->fragment_size > dmap->buffsize)
		dmap->fragment_size = dmap->buffsize;

	if (dmap->fragment_size == dmap->buffsize &&
	    audio_devs[dev]->flags & DMA_AUTOMODE)
		dmap->fragment_size /= 2;	/* Needs at least 2 buffers */

	dmap->subdivision = 1;	/* Disable SNDCTL_DSP_SUBDIVIDE */
	if (arg)
		return (*(int *) arg = bytes | ((count - 1) << 16));
	else
		return 0;
}

int
dma_ioctl(int dev, unsigned int cmd, caddr_t arg)
{

	struct dma_buffparms *dmap_out = audio_devs[dev]->dmap_out;
	struct dma_buffparms *dmap_in = audio_devs[dev]->dmap_in;

	switch (cmd)
	  {

	  case SNDCTL_DSP_SUBDIVIDE:
		  {
			  int             fact;
			  int             ret = 0;

			  fact = *(int *) arg;

			  if (audio_devs[dev]->open_mode & OPEN_WRITE)
				  ret = dma_subdivide(dev, dmap_out, arg, fact);
			  if (ret < 0)
				  return ret;

			  if (audio_devs[dev]->open_mode != OPEN_WRITE ||
			      (audio_devs[dev]->flags & DMA_DUPLEX &&
			       audio_devs[dev]->open_mode & OPEN_READ))
				  ret = dma_subdivide(dev, dmap_in, arg, fact);

			  return ret;
		  }
		  break;

	  case SNDCTL_DSP_GETISPACE:
	  case SNDCTL_DSP_GETOSPACE:
		  {
			  struct dma_buffparms *dmap = dmap_out;

			  audio_buf_info *info = (audio_buf_info *) arg;

			  if (cmd == SNDCTL_DSP_GETISPACE &&
			      !(audio_devs[dev]->open_mode & OPEN_READ))
				  return -EINVAL;

			  if (cmd == SNDCTL_DSP_GETOSPACE &&
			      !(audio_devs[dev]->open_mode & OPEN_WRITE))
				  return -EINVAL;

			  if (cmd == SNDCTL_DSP_GETISPACE && audio_devs[dev]->flags & DMA_DUPLEX)
				  dmap = dmap_in;

			  if (dmap->mapping_flags & DMA_MAP_MAPPED)
				  return -EINVAL;

			  if (!(dmap->flags & DMA_ALLOC_DONE))
				  reorganize_buffers(dev, dmap, (cmd == SNDCTL_DSP_GETISPACE));

			  info->fragstotal = dmap->nbufs;

			  if (cmd == SNDCTL_DSP_GETISPACE)
				  info->fragments = dmap->qlen;
			  else
			    {
				    if (!DMAbuf_space_in_queue(dev))
					    info->fragments = 0;
				    else
				      {
					      info->fragments = DMAbuf_space_in_queue(dev);
					      if (audio_devs[dev]->d->local_qlen)
						{
							int             tmp = audio_devs[dev]->d->local_qlen(dev);

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
			  else
			    {
				    info->fragments = info->bytes / dmap->fragment_size;
				    info->bytes -= dmap->user_counter % dmap->fragment_size;
			    }
		  }
		  return 0;

	  case SNDCTL_DSP_SETTRIGGER:
		  {
			  unsigned long   flags;

			  int             bits;
			  int             changed;

			  bits = *(int *) arg;
			  bits &= audio_devs[dev]->open_mode;

			  if (audio_devs[dev]->d->trigger == NULL)
				  return -EINVAL;

			  if (!(audio_devs[dev]->flags & DMA_DUPLEX))
				  if ((bits & PCM_ENABLE_INPUT) && (bits & PCM_ENABLE_OUTPUT))
				    {
					    printk("Sound: Device doesn't have full duplex capability\n");
					    return -EINVAL;
				    }
			  save_flags(flags);
			  cli();
			  changed = audio_devs[dev]->enable_bits ^ bits;

			  if ((changed & bits) & PCM_ENABLE_INPUT && audio_devs[dev]->go)
			    {
				    int             err;

				    reorganize_buffers(dev, dmap_in, 1);

				    if ((err = audio_devs[dev]->d->prepare_for_input(dev,
										     dmap_in->fragment_size, dmap_in->nbufs)) < 0)
					    return -err;

				    dmap_in->dma_mode = DMODE_INPUT;
				    audio_devs[dev]->enable_bits = bits;
				    DMAbuf_activate_recording(dev, dmap_in);
			    }
			  if ((changed & bits) & PCM_ENABLE_OUTPUT &&
			      (dmap_out->mapping_flags & DMA_MAP_MAPPED || dmap_out->qlen > 0) &&
			      audio_devs[dev]->go)
			    {

				    if (!(dmap_out->flags & DMA_ALLOC_DONE))
				      {
					      reorganize_buffers(dev, dmap_out, 0);
				      }
				    dmap_out->dma_mode = DMODE_OUTPUT;
				    ;
				    audio_devs[dev]->enable_bits = bits;
				    dmap_out->counts[dmap_out->qhead] = dmap_out->fragment_size;
				    DMAbuf_launch_output(dev, dmap_out);
				    ;
			    }
			  audio_devs[dev]->enable_bits = bits;
			  if (changed && audio_devs[dev]->d->trigger)
			    {
				    audio_devs[dev]->d->trigger(dev, bits * audio_devs[dev]->go);
			    }
			  restore_flags(flags);
		  }
	  case SNDCTL_DSP_GETTRIGGER:
		  return (*(int *) arg = audio_devs[dev]->enable_bits);
		  break;

	  case SNDCTL_DSP_SETSYNCRO:

		  if (!audio_devs[dev]->d->trigger)
			  return -EINVAL;

		  audio_devs[dev]->d->trigger(dev, 0);
		  audio_devs[dev]->go = 0;
		  return 0;
		  break;

	  case SNDCTL_DSP_GETIPTR:
		  {
			  count_info      info;
			  unsigned long   flags;
			  struct dma_buffparms *dmap = dmap_in;

			  if (!(audio_devs[dev]->open_mode & OPEN_READ))
				  return -EINVAL;

			  save_flags(flags);
			  cli();
			  info.bytes = dmap->byte_counter;
			  info.ptr = DMAbuf_get_buffer_pointer(dev, dmap, DMODE_INPUT) & ~3;
			  if (info.ptr < dmap->fragment_size && dmap->qtail != 0)
				  info.bytes += dmap->bytes_in_use;	/* Pointer wrap not handled yet */

			  info.blocks = dmap->qlen;
			  info.bytes += info.ptr;
			  memcpy((&((char *) arg)[0]), (char *) &info, sizeof(info));

			  if (dmap->mapping_flags & DMA_MAP_MAPPED)
				  dmap->qlen = 0;	/* Reset interrupt counter */
			  restore_flags(flags);
			  return 0;
		  }
		  break;

	  case SNDCTL_DSP_GETOPTR:
		  {
			  count_info      info;
			  unsigned long   flags;
			  struct dma_buffparms *dmap = dmap_out;

			  if (!(audio_devs[dev]->open_mode & OPEN_WRITE))
				  return -EINVAL;

			  save_flags(flags);
			  cli();
			  info.bytes = dmap->byte_counter;
			  info.ptr = DMAbuf_get_buffer_pointer(dev, dmap, DMODE_OUTPUT) & ~3;
			  if (info.ptr < dmap->fragment_size && dmap->qhead != 0)
				  info.bytes += dmap->bytes_in_use;	/* Pointer wrap not handled yet */
			  info.blocks = dmap->qlen;
			  info.bytes += info.ptr;
			  memcpy((&((char *) arg)[0]), (char *) &info, sizeof(info));

			  if (dmap->mapping_flags & DMA_MAP_MAPPED)
				  dmap->qlen = 0;	/* Reset interrupt counter */

			  restore_flags(flags);
			  return 0;
		  }
		  break;


	  case SNDCTL_DSP_POST:
		  ;
		  if (audio_devs[dev]->dmap_out->qlen > 0)
			  if (!(audio_devs[dev]->dmap_out->flags & DMA_ACTIVE))
				  DMAbuf_launch_output(dev, audio_devs[dev]->dmap_out);
		  ;
		  return 0;
		  break;

	  case SNDCTL_DSP_GETBLKSIZE:
		  {
			  int             fragment_size;
			  struct dma_buffparms *dmap = dmap_out;

			  if (audio_devs[dev]->open_mode & OPEN_WRITE)
				  reorganize_buffers(dev, dmap_out,
						     (audio_devs[dev]->open_mode == OPEN_READ));
			  if (audio_devs[dev]->open_mode != OPEN_WRITE ||
			      (audio_devs[dev]->flags & DMA_DUPLEX &&
			       audio_devs[dev]->open_mode & OPEN_READ))
				  reorganize_buffers(dev, dmap_in,
						     (audio_devs[dev]->open_mode == OPEN_READ));
			  if (audio_devs[dev]->open_mode == OPEN_READ)
				  dmap = dmap_in;
			  fragment_size = dmap->fragment_size;
			  return (*(int *) arg = fragment_size);
		  }
		  break;

	  case SNDCTL_DSP_SETFRAGMENT:
		  {
			  int             fact;
			  int             ret;

			  fact = *(int *) arg;
			  ret = dma_set_fragment(dev, dmap_out, arg, fact);
			  if (ret < 0)
				  return ret;

			  if (audio_devs[dev]->flags & DMA_DUPLEX &&
			      audio_devs[dev]->open_mode & OPEN_READ)
				  ret = dma_set_fragment(dev, dmap_in, arg, fact);

			  return ret;
		  }
		  break;

	  default:
		  return audio_devs[dev]->d->ioctl(dev, cmd, arg);
	  }

}
