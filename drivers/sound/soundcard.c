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
/*
 * Created modular version by Peter Trattler (peter@sbox.tu-graz.ac.at)
 */

#include "sound_config.h"

#ifdef CONFIGURE_SOUNDCARD

#include <linux/major.h>

static int      soundcards_installed = 0;	/* Number of installed

						 * soundcards */
static int      soundcard_configured = 0;

static struct fileinfo files[SND_NDEVS];

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

  dev = MINOR(inode->i_rdev);

  return sound_read_sw (dev, &files[dev], buf, count);
}

static int
sound_write (struct inode *inode, struct file *file, const char *buf, int count)
{
  int             dev;

#ifdef MODULE
  int             err;

#endif

  dev = MINOR(inode->i_rdev);

  return sound_write_sw (dev, &files[dev], buf, count);
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
  struct fileinfo tmp_file;

  dev = MINOR(inode->i_rdev);

  if (!soundcard_configured && dev != SND_DEV_CTL && dev != SND_DEV_STATUS)
    {
      printk ("SoundCard Error: The soundcard system has not been configured\n");
      return RET_ERROR (ENXIO);
    }

  tmp_file.mode = 0;
  tmp_file.filp = file;

  if ((file->f_flags & O_ACCMODE) == O_RDWR)
    tmp_file.mode = OPEN_READWRITE;
  if ((file->f_flags & O_ACCMODE) == O_RDONLY)
    tmp_file.mode = OPEN_READ;
  if ((file->f_flags & O_ACCMODE) == O_WRONLY)
    tmp_file.mode = OPEN_WRITE;

  if ((retval = sound_open_sw (dev, &tmp_file)) < 0)
    return retval;

#ifdef MODULE
  MOD_INC_USE_COUNT;
#endif

  memcpy ((char *) &files[dev], (char *) &tmp_file, sizeof (tmp_file));
  return retval;
}

static void
sound_release (struct inode *inode, struct file *file)
{
  int             dev;

  dev = MINOR(inode->i_rdev);

  sound_release_sw (dev, &files[dev]);
#ifdef MODULE
  MOD_DEC_USE_COUNT;
#endif
}

static int
sound_ioctl (struct inode *inode, struct file *file,
	     unsigned int cmd, unsigned long arg)
{
  int             dev;

  dev = MINOR(inode->i_rdev);

  if (cmd & IOC_INOUT)
    {
      /*
         * Have to validate the address given by the process.
       */
      int             len, err;

      len = (cmd & IOCSIZE_MASK) >> IOCSIZE_SHIFT;

      if (cmd & IOC_IN)
	{
	  if ((err = verify_area (VERIFY_READ, (void *) arg, len)) < 0)
	    return err;
	}

      if (cmd & IOC_OUT)
	{
	  if ((err = verify_area (VERIFY_WRITE, (void *) arg, len)) < 0)
	    return err;
	}

    }

  return sound_ioctl_sw (dev, &files[dev], cmd, arg);
}

static int
sound_select (struct inode *inode, struct file *file, int sel_type, select_table * wait)
{
  int             dev;

  dev = MINOR(inode->i_rdev);

  DEB (printk ("sound_select(dev=%d, type=0x%x)\n", dev, sel_type));

  switch (dev & 0x0f)
    {
#ifndef EXCLUDE_SEQUENCER
    case SND_DEV_SEQ:
    case SND_DEV_SEQ2:
      return sequencer_select (dev, &files[dev], sel_type, wait);
      break;
#endif

#ifndef EXCLUDE_MIDI
    case SND_DEV_MIDIN:
      return MIDIbuf_select (dev, &files[dev], sel_type, wait);
      break;
#endif

#ifndef EXCLUDE_AUDIO
    case SND_DEV_DSP:
    case SND_DEV_DSP16:
    case SND_DEV_AUDIO:
      return audio_select (dev, &files[dev], sel_type, wait);
      break;
#endif

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
#ifndef MODULE
  register_chrdev (SOUND_MAJOR, "sound", &sound_fops);
#endif

  soundcard_configured = 1;

  mem_start = sndtable_init (mem_start);	/* Initialize call tables and
						 * detect cards */

  if (!(soundcards_installed = sndtable_get_cardcount ()))
    return mem_start;		/* No cards detected */

#ifndef EXCLUDE_AUDIO
  if (num_audiodevs)		/* Audio devices present */
    {
      mem_start = DMAbuf_init (mem_start);
      mem_start = audio_init (mem_start);
    }
#endif

#ifndef EXCLUDE_MIDI
  if (num_midis)
    mem_start = MIDIbuf_init (mem_start);
#endif

#ifndef EXCLUDE_SEQUENCER
  if (num_midis + num_synths)
    mem_start = sequencer_init (mem_start);
#endif

  return mem_start;
}

#ifdef MODULE
static unsigned long irqs = 0;
void            snd_release_irq (int);
static int      module_sound_mem_init (void);
static void     module_sound_mem_release (void);

static void
free_all_irqs (void)
{
  int             i;

  for (i = 0; i < 31; i++)
    if (irqs & (1ul << i))
      snd_release_irq (i);
  irqs = 0;
}

char            kernel_version[] = UTS_RELEASE;

static long     memory_pool = 0;
static int      memsize = 70 * 1024;
static int      debugmem = 0;	/* switched off by default */

int
init_module (void)
{
  long            lastbyte;
  int             err;

  printk ("sound: made modular by Peter Trattler (peter@sbox.tu-graz.ac.at)\n");
  err = register_chrdev (SOUND_MAJOR, "sound", &sound_fops);
  if (err)
    {
      printk ("sound: driver already loaded/included in kernel\n");
      return err;
    }
  memory_pool = (long) kmalloc (memsize, GFP_KERNEL);
  if (memory_pool == 0l)
    {
      unregister_chrdev (SOUND_MAJOR, "sound");
      return -ENOMEM;
    }
  lastbyte = soundcard_init (memory_pool);
  if (lastbyte > memory_pool + memsize)
    {
      printk ("sound: Not enough memory; use : 'insmod sound.o memsize=%ld'\n",
	      lastbyte - memory_pool);
      kfree ((void *) memory_pool);
      unregister_chrdev (SOUND_MAJOR, "sound");
      free_all_irqs ();
      return -ENOMEM;
    }
  err = module_sound_mem_init ();
  if (err)
    {
      module_sound_mem_release ();
      kfree ((void *) memory_pool);
      unregister_chrdev (SOUND_MAJOR, "sound");
      free_all_irqs ();
      return err;
    }
  if (lastbyte < memory_pool + memsize)
    printk ("sound: (Suggestion) too much memory; use : 'insmod sound.o memsize=%ld'\n",
	    lastbyte - memory_pool);
  return 0;
}

void
cleanup_module (void)
{
  if (MOD_IN_USE)
    printk ("sound: module busy -- remove delayed\n");
  else
    {
      kfree ((void *) memory_pool);
      unregister_chrdev (SOUND_MAJOR, "sound");
      free_all_irqs ();
      module_sound_mem_release ();
    }
}

#endif

void
tenmicrosec (void)
{
  int             i;

  for (i = 0; i < 16; i++)
    inb (0x80);
}

int
snd_set_irq_handler (int interrupt_level, INT_HANDLER_PROTO (), char *name)
{
  int             retcode;

  retcode = request_irq (interrupt_level, hndlr, SA_INTERRUPT, name);
  if (retcode < 0)
    {
      printk ("Sound: IRQ%d already in use\n", interrupt_level);
    }
#ifdef MODULE
  else
    irqs |= (1ul << interrupt_level);
#endif

  return retcode;
}

void
snd_release_irq (int vect)
{
#ifdef MODULE
  irqs &= ~(1ul << vect);
#endif
  free_irq (vect);
}

#ifndef EXCLUDE_SEQUENCER
void
request_sound_timer (int count)
{
  extern unsigned long seq_time;

#if 1
  if (count < 0)
    count = jiffies + (-count);
  else
    count += seq_time;
  timer_table[SOUND_TIMER].fn = sequencer_timer;
  timer_table[SOUND_TIMER].expires = count;
  timer_active |= 1 << SOUND_TIMER;
#endif
}

#endif

void
sound_stop_timer (void)
{
#if 1
  timer_table[SOUND_TIMER].expires = 0;
  timer_active &= ~(1 << SOUND_TIMER);
#endif
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

#ifdef MODULE

#ifdef KMALLOC_DMA_BROKEN
#define KMALLOC_MEM_REGIONS 20

static char    *dma_list[KMALLOC_MEM_REGIONS];
static int      dma_last = 0;
inline void
add_to_dma_list (char *adr)
{
  dma_list[dma_last++] = adr;
}

#endif

static int
module_sound_mem_init (void)
{
  int             dev, ret = 0;
  unsigned long   dma_pagesize;
  char           *start_addr, *end_addr;
  int		  order, size;
  struct dma_buffparms *dmap;

  for (dev = 0; dev < num_audiodevs; dev++)
    if (audio_devs[dev]->buffcount > 0 && audio_devs[dev]->dmachan >= 0)
      {
	dmap = audio_devs[dev]->dmap;
	if (audio_devs[dev]->flags & DMA_AUTOMODE)
	  audio_devs[dev]->buffcount = 1;

	if (audio_devs[dev]->dmachan > 3)
	  dma_pagesize = 131072;	/* 16bit dma: 128k */
	else
	  dma_pagesize = 65536;	/* 8bit dma: 64k */
	if (debugmem)
	  printk ("sound: dma-page-size %lu\n", dma_pagesize);
	/* More sanity checks */

	if (audio_devs[dev]->buffsize > dma_pagesize)
	  audio_devs[dev]->buffsize = dma_pagesize;
	audio_devs[dev]->buffsize &= 0xfffff000;	/* Truncate to n*4k */
	if (audio_devs[dev]->buffsize < 4096)
	  audio_devs[dev]->buffsize = 4096;
	if (debugmem)
	  printk ("sound: buffsize %lu\n", audio_devs[dev]->buffsize);
	/* Now allocate the buffers */
	for (dmap->raw_count = 0; dmap->raw_count < audio_devs[dev]->buffcount;
	     dmap->raw_count++)
	  {
#ifdef KMALLOC_DMA_BROKEN
	    start_addr = kmalloc (audio_devs[dev]->buffsize, GFP_KERNEL);
	    if (start_addr)
	      {
		if (debugmem)
		  printk ("sound: trying 0x%lx for DMA\n", (long) start_addr);
		if (valid_dma_page ((unsigned long) start_addr,
				    audio_devs[dev]->buffsize,
				    dma_pagesize))
		  add_to_dma_list (start_addr);
		else
		  {
		    kfree (start_addr);
		    start_addr = kmalloc (audio_devs[dev]->buffsize * 2,
					  GFP_KERNEL);	/* what a waste :-( */
		    if (start_addr)
		      {
			if (debugmem)
			  printk ("sound: failed; trying 0x%lx aligned to",
				  (long) start_addr);
			add_to_dma_list (start_addr);
			/* now align it to the next dma-page boundary */
			start_addr = (char *) (((long) start_addr
						+ dma_pagesize - 1)
					       & ~(dma_pagesize - 1));
			if (debugmem)
			  printk (" 0x%lx\n", (long) start_addr);
		      }
		  }
	      }
#else
	    for (order = 0, size = PAGE_SIZE;
		 size < audio_devs[dev]->buffsize;
		 order++, size <<= 1);
	    start_addr = (char *) __get_free_pages(GFP_KERNEL, order, MAX_DMA_ADDRESS);
#endif
	    if (start_addr == NULL)
	      ret = -ENOMEM;	/* Can't stop the loop in this case, because
				   * ...->raw_buf [...] must be initilized
				   * to valid values (at least to NULL)
				 */
	    else
	      {
		/* make some checks */
		end_addr = start_addr + audio_devs[dev]->buffsize - 1;
		if (debugmem)
		  printk ("sound: start 0x%lx, end 0x%lx\n",
			  (long) start_addr, (long) end_addr);
		/* now check if it fits into the same dma-pagesize */
		if (((long) start_addr & ~(dma_pagesize - 1))
		    != ((long) end_addr & ~(dma_pagesize - 1))
		    || end_addr >= (char *) (16 * 1024 * 1024))
		  {
		    printk (
			     "sound: kmalloc returned invalid address 0x%lx for %ld Bytes DMA-buffer\n",
			     (long) start_addr,
			     audio_devs[dev]->buffsize);
		    ret = -EFAULT;
		  }
	      }
	    dmap->raw_buf[dmap->raw_count] = start_addr;
	    dmap->raw_buf_phys[dmap->raw_count] = (unsigned long) start_addr;
	  }
      }
  return ret;
}

static void
module_sound_mem_release (void)
{
#ifdef KMALLOC_DMA_BROKEN
  int             i;

  for (i = 0; i < dma_last; i++)
    {
      if (debugmem)
	printk ("sound: freeing 0x%lx\n", (long) dma_list[i]);
      kfree (dma_list[i]);
    }
#else
  int             dev, i;
  int		  order, size;

  for (dev = 0; dev < num_audiodevs; dev++) {
    for (order = 0, size = PAGE_SIZE;
	 size < audio_devs[dev]->buffsize;
	 order++, size <<= 1);
    if (audio_devs[dev]->buffcount > 0 && audio_devs[dev]->dmachan >= 0)
      {
	for (i = 0; i < audio_devs[dev]->buffcount; i++)
	  if (audio_devs[dev]->dmap->raw_buf[i])
	    {
	      if (debugmem)
		printk ("sound: freeing 0x%lx\n",
			(long) (audio_devs[dev]->dmap->raw_buf[i]));
	      free_pages((unsigned long) audio_devs[dev]->dmap->raw_buf[i], 
			 order);
	    }
      }
  }
#endif
}

#else /* !MODULE */

void
sound_mem_init (void)
{
  int             i, dev;
  unsigned long   start_addr, end_addr, mem_ptr, dma_pagesize;
  struct dma_buffparms *dmap;

  mem_ptr = high_memory;

  /* Some sanity checks */

  if (mem_ptr > (16 * 1024 * 1024))
    mem_ptr = 16 * 1024 * 1024;	/* Limit to 16M */

  for (dev = 0; dev < num_audiodevs; dev++)	/* Enumerate devices */
    if (audio_devs[dev]->buffcount > 0 && audio_devs[dev]->dmachan >= 0)
      {
	dmap = audio_devs[dev]->dmap;

	if (audio_devs[dev]->flags & DMA_AUTOMODE)
	  audio_devs[dev]->buffcount = 1;

	if (audio_devs[dev]->dmachan > 3 && audio_devs[dev]->buffsize > 65536)
	  dma_pagesize = 131072;	/* 128k */
	else
	  dma_pagesize = 65536;

	/* More sanity checks */

	if (audio_devs[dev]->buffsize > dma_pagesize)
	  audio_devs[dev]->buffsize = dma_pagesize;
	audio_devs[dev]->buffsize &= 0xfffff000;	/* Truncate to n*4k */
	if (audio_devs[dev]->buffsize < 4096)
	  audio_devs[dev]->buffsize = 4096;

	/* Now allocate the buffers */

	for (dmap->raw_count = 0; dmap->raw_count < audio_devs[dev]->buffcount; dmap->raw_count++)
	  {
	    start_addr = mem_ptr - audio_devs[dev]->buffsize;
	    if (!valid_dma_page (start_addr, audio_devs[dev]->buffsize, dma_pagesize))
	      start_addr &= ~(dma_pagesize - 1);	/* Align address to
							 * dma_pagesize */

	    end_addr = start_addr + audio_devs[dev]->buffsize - 1;

	    dmap->raw_buf[dmap->raw_count] = (char *) start_addr;
	    dmap->raw_buf_phys[dmap->raw_count] = start_addr;
	    mem_ptr = start_addr;

	    for (i = MAP_NR (start_addr); i <= MAP_NR (end_addr); i++)
	      {
		if (mem_map[i].reserved || mem_map[i].count)
		  panic ("sound_mem_init: Page not free (driver incompatible with kernel).\n");

		mem_map[i].reserved = 1;
	      }
	  }
      }				/* for dev */
}

#endif /* !MODULE */

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

#ifdef MODULE
static int
module_sound_mem_init (void)
{
  return 0;			/* no error */
}

#endif

#endif
