/*
 * linux/kernel/chr_drv/sound/soundcard.c
 * 
 * Soundcard driver for Linux
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

#ifdef CONFIGURE_SOUNDCARD

#include <linux/major.h>

struct sbc_device
{
  int             usecount;
};

static struct sbc_device sbc_devices[SND_NDEVS];
extern long     seq_time;

static int      in_use = 0;	/* Total # of open device files (excluding
				 * minor 0) */

static int      soundcards_installed = 0;	/* Number of installed
						 * soundcards */
static int      soundcard_configured = 0;

static struct fileinfo files[SND_NDEVS];

extern char    *snd_raw_buf[MAX_DSP_DEV][DSP_BUFFCOUNT];
extern unsigned long snd_raw_buf_phys[MAX_DSP_DEV][DSP_BUFFCOUNT];
extern int      snd_raw_count[MAX_DSP_DEV];

/*
 * /dev/sndstatus -device
 */
static char    *status_buf = NULL;
static int      status_len, status_ptr;
static int      status_busy = 0;

static int
put_status (char *s)
{
  int             l = strlen (s);

  if (status_len + l >= 4000)
    return 0;

  memcpy (&status_buf[status_len], s, l);
  status_len += l;

  return 1;
}

static void
init_status (void)
{
  /*
   * Write the status information to the status_buf and update status_len.
   * There is a limit of 4000 bytes for the data.
   */

  char            tmp_buf[256];	/* Line buffer */
  int             i;

  status_ptr = 0;

  put_status ("Sound Driver:" SOUND_VERSION_STRING
	      " (" SOUND_CONFIG_DATE " " SOUND_CONFIG_BY "@"
	      SOUND_CONFIG_HOST "." SOUND_CONFIG_DOMAIN ")"
	      "\n");

  sprintf (tmp_buf, "Config options: 0x%08x\n\n", SELECTED_SOUND_OPTIONS);
  if (!put_status (tmp_buf))
    return;

  sprintf (tmp_buf, "Major number: %d\n", SOUND_MAJOR);
  if (!put_status (tmp_buf))
    return;

  if (!put_status ("HW config: \n"))
    return;

  for (i = 0; i < (num_sound_drivers - 1); i++)
    {
      sprintf (tmp_buf, "Type %d: %s ",
	       supported_drivers[i].card_type,
	       supported_drivers[i].name);
      if (!put_status (tmp_buf))
	return;

      sprintf (tmp_buf, " at 0x%03x irq %d drq %d\n",
	       supported_drivers[i].config.io_base,
	       supported_drivers[i].config.irq,
	       supported_drivers[i].config.dma);
      if (!put_status (tmp_buf))
	return;
    }

  if (!put_status ("\nPCM devices:\n"))
    return;

  for (i = 0; i < num_dspdevs; i++)
    {
      sprintf (tmp_buf, "%02d: %s\n", i, dsp_devs[i]->name);
      if (!put_status (tmp_buf))
	return;
    }

  if (!put_status ("\nSynth devices:\n"))
    return;

  for (i = 0; i < num_synths; i++)
    {
      sprintf (tmp_buf, "%02d: %s\n", i, synth_devs[i]->info->name);
      if (!put_status (tmp_buf))
	return;
    }

  if (!put_status ("\nMidi devices:\n"))
    return;

  for (i = 0; i < num_midis; i++)
    {
      sprintf (tmp_buf, "%02d: %s\n", i, midi_devs[i]->info.name);
      if (!put_status (tmp_buf))
	return;
    }

  if (num_mixers)
    {
      if (!put_status ("\nMixer(s) installed\n"))
	return;
    }
  else
    {
      if (!put_status ("\nNo mixers installed\n"))
	return;
    }
}

static int
read_status (char *buf, int count)
{
  /*
   * Return at most 'count' bytes from the status_buf.
   */
  int             l, c;

  l = count;
  c = status_len - status_ptr;

  if (l > c)
    l = c;
  if (l <= 0)
    return 0;

  memcpy_tofs (buf, &status_buf[status_ptr], l);
  status_ptr += l;

  return l;
}

int
snd_ioctl_return (int *addr, int value)
{
  if (value < 0)
    return value;

  PUT_WORD_TO_USER (addr, 0, value);
  return 0;
}

static int
sound_read (struct inode *inode, struct file *file, char *buf, int count)
{
  int             dev;

  dev = inode->i_rdev;
  dev = MINOR (dev);

  DEB (printk ("sound_read(dev=%d, count=%d)\n", dev, count));

  switch (dev & 0x0f)
    {
    case SND_DEV_STATUS:
      return read_status (buf, count);
      break;

    case SND_DEV_AUDIO:
      return audio_read (dev, &files[dev], buf, count);
      break;

    case SND_DEV_DSP:
    case SND_DEV_DSP16:
      return dsp_read (dev, &files[dev], buf, count);
      break;

    case SND_DEV_SEQ:
      return sequencer_read (dev, &files[dev], buf, count);
      break;

#ifndef EXCLUDE_MPU401
    case SND_DEV_MIDIN:
      return MIDIbuf_read (dev, &files[dev], buf, count);
#endif

    default:
      printk ("Sound: Undefined minor device %d\n", dev);
    }

  return RET_ERROR (EPERM);
}

static int
sound_write (struct inode *inode, struct file *file, char *buf, int count)
{
  int             dev;

  dev = inode->i_rdev;
  dev = MINOR (dev);

  DEB (printk ("sound_write(dev=%d, count=%d)\n", dev, count));

  switch (dev & 0x0f)
    {

    case SND_DEV_SEQ:
      return sequencer_write (dev, &files[dev], buf, count);
      break;

    case SND_DEV_AUDIO:
      return audio_write (dev, &files[dev], buf, count);
      break;

    case SND_DEV_DSP:
    case SND_DEV_DSP16:
      return dsp_write (dev, &files[dev], buf, count);
      break;

    default:
      return RET_ERROR (EPERM);
    }

  return count;
}

static int
sound_lseek (struct inode *inode, struct file *file, off_t offset, int orig)
{
  return RET_ERROR (EPERM);
}

static int
sound_open (struct inode *inode, struct file *file)
{
  int             dev, retval;

  dev = inode->i_rdev;
  dev = MINOR (dev);

  DEB (printk ("sound_open(dev=%d) : usecount=%d\n", dev, sbc_devices[dev].usecount));

  if ((dev >= SND_NDEVS) || (dev < 0))
    {
      printk ("Invalid minor device %d\n", dev);
      return RET_ERROR (ENODEV);
    }

  if (!soundcard_configured && dev != SND_DEV_CTL && dev != SND_DEV_STATUS)
    {
      printk ("SoundCard Error: The soundcard system has not been configured\n");
      return RET_ERROR (ENODEV);
    }

  files[dev].mode = 0;

  if ((file->f_flags & O_ACCMODE) == O_RDWR)
    files[dev].mode = OPEN_READWRITE;
  if ((file->f_flags & O_ACCMODE) == O_RDONLY)
    files[dev].mode = OPEN_READ;
  if ((file->f_flags & O_ACCMODE) == O_WRONLY)
    files[dev].mode = OPEN_WRITE;

  switch (dev & 0x0f)
    {
    case SND_DEV_STATUS:
      if (status_busy)
	return RET_ERROR (EBUSY);
      status_busy = 1;
      if ((status_buf = (char *) KERNEL_MALLOC (4000)) == NULL)
	return RET_ERROR (EIO);
      status_len = status_ptr = 0;
      init_status ();
      break;

    case SND_DEV_CTL:
      if (!soundcards_installed)
	if (soundcard_configured)
	  {
	    printk ("Soundcard not installed\n");
	    return RET_ERROR (ENODEV);
	  }
      break;

    case SND_DEV_SEQ:
      if ((retval = sequencer_open (dev, &files[dev])) < 0)
	return retval;
      break;

#ifndef EXCLUDE_MPU401
    case SND_DEV_MIDIN:
      if ((retval = MIDIbuf_open (dev, &files[dev])) < 0)
	return retval;
      break;
#endif

    case SND_DEV_AUDIO:
      if ((retval = audio_open (dev, &files[dev])) < 0)
	return retval;
      break;

    case SND_DEV_DSP:
      if ((retval = dsp_open (dev, &files[dev], 8)) < 0)
	return retval;
      break;

    case SND_DEV_DSP16:
      if ((retval = dsp_open (dev, &files[dev], 16)) < 0)
	return retval;
      break;

    default:
      printk ("Invalid minor device %d\n", dev);
      return RET_ERROR (ENODEV);
    }

  sbc_devices[dev].usecount++;
  in_use++;

  return 0;
}

static void
sound_release (struct inode *inode, struct file *file)
{
  int             dev;

  dev = inode->i_rdev;
  dev = MINOR (dev);

  DEB (printk ("sound_release(dev=%d)\n", dev));

  switch (dev & 0x0f)
    {
    case SND_DEV_STATUS:
      if (status_buf)
	KERNEL_FREE (status_buf);
      status_buf = NULL;
      status_busy = 0;
      break;

    case SND_DEV_CTL:
      break;

    case SND_DEV_SEQ:
      sequencer_release (dev, &files[dev]);
      break;

#ifndef EXCLUDE_MPU401
    case SND_DEV_MIDIN:
      MIDIbuf_release (dev, &files[dev]);
      break;
#endif

    case SND_DEV_AUDIO:
      audio_release (dev, &files[dev]);
      break;

    case SND_DEV_DSP:
    case SND_DEV_DSP16:
      dsp_release (dev, &files[dev]);
      break;

    default:
      printk ("Sound error: Releasing unknown device 0x%02x\n", dev);
    }

  sbc_devices[dev].usecount--;
  in_use--;
}

static int
sound_ioctl (struct inode *inode, struct file *file,
	     unsigned int cmd, unsigned long arg)
{
  int             dev;

  dev = inode->i_rdev;
  dev = MINOR (dev);

  DEB (printk ("sound_ioctl(dev=%d, cmd=0x%x, arg=0x%x)\n", dev, cmd, arg));

  switch (dev & 0x0f)
    {

    case SND_DEV_CTL:

      if (!num_mixers)
	return RET_ERROR (ENODEV);

      if (dev >= num_mixers)
	return RET_ERROR (ENODEV);

      return mixer_devs[dev]->ioctl (dev, cmd, arg);
      break;

    case SND_DEV_SEQ:
      return sequencer_ioctl (dev, &files[dev], cmd, arg);
      break;

    case SND_DEV_AUDIO:
      return audio_ioctl (dev, &files[dev], cmd, arg);
      break;

    case SND_DEV_DSP:
    case SND_DEV_DSP16:
      return dsp_ioctl (dev, &files[dev], cmd, arg);
      break;

#ifndef EXCLUDE_MPU401
    case SND_DEV_MIDIN:
      return MIDIbuf_ioctl (dev, &files[dev], cmd, arg);
      break;
#endif

    default:
      return RET_ERROR (EPERM);
      break;
    }

  return RET_ERROR (EPERM);
}

static int
sound_select (struct inode *inode, struct file *file, int sel_type, select_table * wait)
{
  int             dev;

  dev = inode->i_rdev;
  dev = MINOR (dev);

  DEB (printk ("sound_select(dev=%d, type=0x%x)\n", dev, sel_type));

  switch (dev & 0x0f)
    {
    case SND_DEV_SEQ:
      return sequencer_select (dev, &files[dev], sel_type, wait);
      break;

    default:
      return 0;
    }

  return 0;
}

static struct file_operations sound_fops =
{
  sound_lseek,
  sound_read,
  sound_write,
  NULL,				/* sound_readdir */
  sound_select,
  sound_ioctl,
  NULL,
  sound_open,
  sound_release
};

long
soundcard_init (long mem_start)
{
  int             i;

  register_chrdev (SOUND_MAJOR, "sound", &sound_fops);

  soundcard_configured = 1;

  mem_start = sndtable_init (mem_start);	/* Initialize call tables and
						 * detect cards */

  if (!(soundcards_installed = sndtable_get_cardcount ()))
    return mem_start;		/* No cards detected */

  if (num_dspdevs)		/* Audio devices present */
    {
      mem_start = DMAbuf_init (mem_start);
      mem_start = audio_init (mem_start);
      mem_start = dsp_init (mem_start);
    }

#ifndef EXCLUDE_MPU401
  if (num_midis)
    mem_start = MIDIbuf_init (mem_start);
#endif

  if (num_midis + num_synths)
    mem_start = sequencer_init (mem_start);

  for (i = 0; i < SND_NDEVS; i++)
    {
      sbc_devices[i].usecount = 0;
    }

  return mem_start;
}

void
tenmicrosec (void)
{
  int             i;

  for (i = 0; i < 16; i++)
    inb (0x80);
}

void
request_sound_timer (int count)
{
  if (count < 0)
    count = jiffies + (-count);
  else
    count += seq_time;
  timer_table[SOUND_TIMER].fn = sequencer_timer;
  timer_table[SOUND_TIMER].expires = count;
  timer_active |= 1 << SOUND_TIMER;
}

void
sound_stop_timer (void)
{
  timer_table[SOUND_TIMER].expires = 0;
  timer_active &= ~(1 << SOUND_TIMER);
}

#ifndef EXCLUDE_AUDIO
static int
valid_dma_page (unsigned long addr, unsigned long dev_buffsize, unsigned long dma_pagesize)
{
  if (((addr & (dma_pagesize - 1)) + dev_buffsize) <= dma_pagesize)
    return 1;
  else
    return 0;
}

void
sound_mem_init (void)
{
  int             i, dev;
  unsigned long   start_addr, end_addr, mem_ptr, dma_pagesize;

  mem_ptr = high_memory;

  /* Some sanity checks */

  if (mem_ptr > (16 * 1024 * 1024))
    mem_ptr = 16 * 1024 * 1024;	/* Limit to 16M */

  for (dev = 0; dev < num_dspdevs; dev++)	/* Enumerate devices */
    if (sound_buffcounts[dev] > 0 && sound_dsp_dmachan[dev] > 0)
      {
	if (sound_dma_automode[dev])
	  sound_buffcounts[dev] = 1;

	if (sound_dsp_dmachan[dev] > 3 && sound_buffsizes[dev] > 65536)
	  dma_pagesize = 131072;/* 128k */
	else
	  dma_pagesize = 65536;

	/* More sanity checks */

	if (sound_buffsizes[dev] > dma_pagesize)
	  sound_buffsizes[dev] = dma_pagesize;
	sound_buffsizes[dev] &= 0xfffff000;	/* Truncate to n*4k */
	if (sound_buffsizes[dev] < 4096)
	  sound_buffsizes[dev] = 4096;

	/* Now allocate the buffers */

	for (snd_raw_count[dev] = 0; snd_raw_count[dev] < sound_buffcounts[dev]; snd_raw_count[dev]++)
	  {
	    start_addr = mem_ptr - sound_buffsizes[dev];
	    if (!valid_dma_page (start_addr, sound_buffsizes[dev], dma_pagesize))
	      start_addr &= ~(dma_pagesize - 1);	/* Align address to
							 * dma_pagesize */

	    end_addr = start_addr + sound_buffsizes[dev] - 1;

	    snd_raw_buf[dev][snd_raw_count[dev]] = (char *) start_addr;
	    snd_raw_buf_phys[dev][snd_raw_count[dev]] = start_addr;
	    mem_ptr = start_addr;

	    for (i = MAP_NR (start_addr); i <= MAP_NR (end_addr); i++)
	      {
		if (mem_map[i])
		  panic ("sound_mem_init: Page not free (driver incompatible with kernel).\n");

		mem_map[i] = MAP_PAGE_RESERVED;
	      }
	  }
      }				/* for dev */
}

#endif

#else

long
soundcard_init (long mem_start)	/* Dummy version */
{
  return mem_start;
}

#endif

#if !defined(CONFIGURE_SOUNDCARD) || defined(EXCLUDE_AUDIO)
void
sound_mem_init (void)
{
  /* Dummy version */
}

#endif
