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

static int      soundcards_installed = 0;	/* Number of installed cards */
static int      chrdev_registered = 0;

/*
 * Table for permanently allocated memory (used when unloading the module)
 */
caddr_t         sound_mem_blocks[1024];
int             sound_num_blocks = 0;

static int      soundcard_configured = 0;

static struct fileinfo files[SND_NDEVS];

static char     dma_alloc_map[8] =
{0};

#define DMA_MAP_UNAVAIL		0
#define DMA_MAP_FREE		1
#define DMA_MAP_BUSY		2

int
snd_ioctl_return (int *addr, int value)
{
  if (value < 0)
    return value;

  put_fs_long (value, (long *) &((addr)[0]));
  return 0;
}

static int
sound_read (struct inode *inode, struct file *file, char *buf, int count)
{
  int             dev;

  dev = inode->i_rdev;
  dev = MINOR (dev);

  return sound_read_sw (dev, &files[dev], buf, count);
}

static int
sound_write (struct inode *inode, struct file *file, const char *buf, int count)
{
  int             dev;

  dev = inode->i_rdev;
  dev = MINOR (dev);

  return sound_write_sw (dev, &files[dev], buf, count);
}

static int
sound_lseek (struct inode *inode, struct file *file, off_t offset, int orig)
{
  return -EPERM;
}

static int
sound_open (struct inode *inode, struct file *file)
{
  int             dev, retval;
  struct fileinfo tmp_file;

  dev = inode->i_rdev;
  dev = MINOR (dev);

  if (!soundcard_configured && dev != SND_DEV_CTL && dev != SND_DEV_STATUS)
    {
      printk ("SoundCard Error: The soundcard system has not been configured\n");
      return -ENXIO;
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

  dev = inode->i_rdev;
  dev = MINOR (dev);

  sound_release_sw (dev, &files[dev]);
#ifdef MODULE
  MOD_DEC_USE_COUNT;
#endif
}

static int
sound_ioctl (struct inode *inode, struct file *file,
	     unsigned int cmd, unsigned long arg)
{
  int             dev, err;

  dev = inode->i_rdev;
  dev = MINOR (dev);

  if (cmd == 1)
    {
      int             i;

      unsigned char  *p;


      if (!audio_devs[dev >> 4]->dmap_out)
	return 0;
      if (!audio_devs[dev >> 4]->dmap_out->raw_buf)
	return 0;

      p = audio_devs[dev >> 4]->dmap_out->raw_buf;

      for (i = 0; i < 256; i++)
	printk ("%02x ", p[i]);
      printk ("\n");
      return 0;
    }

  if (cmd & IOC_INOUT)
    {
      /*
         * Have to validate the address given by the process.
       */
      int             len;

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

  err = sound_ioctl_sw (dev, &files[dev], cmd, (caddr_t) arg);

  return err;
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

static int
sound_mmap (struct inode *inode, struct file *file, struct vm_area_struct *vma)
{
  int             dev, dev_class;
  unsigned long   size;
  struct dma_buffparms *dmap = NULL;

  dev = inode->i_rdev;
  dev = MINOR (dev);

  dev_class = dev & 0x0f;
  dev >>= 4;

  if (dev_class != SND_DEV_DSP && dev_class != SND_DEV_DSP16 && dev_class != SND_DEV_AUDIO)
    {
      printk ("Sound: mmap() not supported for other than audio devices\n");
      return -EINVAL;
    }

  if ((vma->vm_flags & (VM_READ | VM_WRITE)) == (VM_READ | VM_WRITE))
    {
      printk ("Sound: Cannot do read/write mmap()\n");
      return -EINVAL;
    }

  if (vma->vm_flags & VM_READ)
    {
      dmap = audio_devs[dev]->dmap_in;
    }
  else if (vma->vm_flags & VM_WRITE)
    {
      dmap = audio_devs[dev]->dmap_out;
    }
  else
    {
      printk ("Sound: Undefined mmap() access\n");
      return -EINVAL;
    }

  if (dmap == NULL)
    {
      printk ("Sound: mmap() error. dmap == NULL\n");
      return -EIO;
    }

  if (dmap->raw_buf == NULL)
    {
      printk ("Sound: mmap() called when raw_buf == NULL\n");
      return -EIO;
    }

  if (dmap->mapping_flags)
    {
      printk ("Sound: mmap() called twice for the same DMA buffer\n");
      return -EIO;
    }

  if (vma->vm_offset != 0)
    {
      printk ("Sound: mmap() offset must be 0.\n");
      return -EINVAL;
    }

  size = vma->vm_end - vma->vm_start;

  if (size != dmap->bytes_in_use)
    {
      printk ("Sound: mmap() size = %ld. Should be %d\n",
	      size, dmap->bytes_in_use);
    }

  if (remap_page_range (vma->vm_start, dmap->raw_buf_phys, vma->vm_end - vma->vm_start, vma->vm_page_prot))
    return -EAGAIN;


  vma->vm_inode = inode;
  inode->i_count++;

  dmap->mapping_flags |= DMA_MAP_MAPPED;

  memset (dmap->raw_buf,
	  dmap->neutral_byte,
	  dmap->bytes_in_use);
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
  sound_mmap,
  sound_open,
  sound_release
};

void
soundcard_init (void)
{
#ifndef MODULE
  register_chrdev (SOUND_MAJOR, "sound", &sound_fops);
  chrdev_registered = 1;
#endif

  soundcard_configured = 1;

  sndtable_init (0);		/* Initialize call tables and
				   * detect cards */
  if (!(soundcards_installed = sndtable_get_cardcount ()))
    return;			/* No cards detected */

#ifndef EXCLUDE_AUDIO
  if (num_audiodevs)		/* Audio devices present */
    {
      DMAbuf_init (0);
      audio_init (0);
    }
#endif

#ifndef EXCLUDE_MIDI
  if (num_midis)
    MIDIbuf_init (0);
#endif

#ifndef EXCLUDE_SEQUENCER
  if (num_midis + num_synths)
    sequencer_init (0);
#endif

}

static unsigned long irqs = 0;

#ifdef MODULE
static void
free_all_irqs (void)
{
  int             i;

  for (i = 0; i < 31; i++)
    if (irqs & (1ul << i))
      {
	printk ("Sound warning: IRQ%d was left allocated. Fixed.\n", i);
	snd_release_irq (i);
      }
  irqs = 0;
}

char            kernel_version[] = UTS_RELEASE;

#endif

static int      debugmem = 0;	/* switched off by default */

static int      sound[20] =
{0};

int
init_module (void)
{
  int             err;
  int             ints[21];
  int             i;

  /*
     * "sound=" command line handling by Harald Milz.
   */
  i = 0;
  while (i < 20 && sound[i])
    ints[i + 1] = sound[i++];
  ints[0] = i;

  if (i)
    sound_setup ("sound=", ints);

  err = register_chrdev (SOUND_MAJOR, "sound", &sound_fops);
  if (err)
    {
      printk ("sound: driver already loaded/included in kernel\n");
      return err;
    }

  chrdev_registered = 1;
  soundcard_init ();

  if (sound_num_blocks >= 1024)
    printk ("Sound warning: Deallocation table was too small.\n");

  return 0;
}

#ifdef MODULE
void
cleanup_module (void)
{
  if (MOD_IN_USE)
    printk ("sound: module busy -- remove delayed\n");
  else
    {
      int             i;

      if (chrdev_registered)
	unregister_chrdev (SOUND_MAJOR, "sound");

      sound_stop_timer ();
      sound_unload_drivers ();

      for (i = 0; i < sound_num_blocks; i++)
	kfree (sound_mem_blocks[i]);

      free_all_irqs ();		/* If something was left allocated by accident */

      for (i = 0; i < 8; i++)
	if (dma_alloc_map[i] != DMA_MAP_UNAVAIL)
	  {
	    printk ("Sound: Hmm, DMA%d was left allocated\n", i);
	    sound_free_dma (i);
	  }

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
snd_set_irq_handler (int interrupt_level, void (*hndlr) (int, struct pt_regs *), char *name, sound_os_info * osp)
{
  int             retcode;

  retcode = request_irq (interrupt_level, hndlr, 0 /* SA_INTERRUPT */ , name);
  if (retcode < 0)
    {
      printk ("Sound: IRQ%d already in use\n", interrupt_level);
    }
  else
    irqs |= (1ul << interrupt_level);

  return retcode;
}

void
snd_release_irq (int vect)
{
  irqs &= ~(1ul << vect);
  free_irq (vect);
}

int
sound_alloc_dma (int chn, char *deviceID)
{
  int             err;

  if ((err = request_dma (chn, deviceID)) != 0)
    return err;

  dma_alloc_map[chn] = DMA_MAP_FREE;

  return 0;
}

int
sound_open_dma (int chn, char *deviceID)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();

  if (dma_alloc_map[chn] != DMA_MAP_FREE)
    {
      printk ("sound_open_dma: DMA channel %d busy or not allocated\n", chn);
      restore_flags (flags);
      return 1;
    }

  dma_alloc_map[chn] = DMA_MAP_BUSY;
  restore_flags (flags);
  return 0;
}

void
sound_free_dma (int chn)
{
  if (dma_alloc_map[chn] != DMA_MAP_FREE)
    {
      printk ("sound_free_dma: Bad access to DMA channel %d\n", chn);
      return;
    }
  free_dma (chn);
  dma_alloc_map[chn] = DMA_MAP_UNAVAIL;
}

void
sound_close_dma (int chn)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();

  if (dma_alloc_map[chn] != DMA_MAP_BUSY)
    {
      printk ("sound_close_dma: Bad access to DMA channel %d\n", chn);
      restore_flags (flags);
      return;
    }
  dma_alloc_map[chn] = DMA_MAP_FREE;
  restore_flags (flags);
}

#ifndef EXCLUDE_SEQUENCER


static struct timer_list seq_timer =
{NULL, NULL, 0, 0, sequencer_timer};

void
request_sound_timer (int count)
{
  extern unsigned long seq_time;

  if (count < 0)
    count = jiffies + (-count);
  else
    count += seq_time;


  {
    seq_timer.expires = ((count - jiffies)) + jiffies;
    add_timer (&seq_timer);
  };
}

#endif

void
sound_stop_timer (void)
{
  del_timer (&seq_timer);;
}

#ifndef EXCLUDE_AUDIO

#ifdef KMALLOC_DMA_BROKEN
fatal_error__This_version_is_not_compatible_with_this_kernel;
#endif

static int      dma_buffsize = DSP_BUFFSIZE;

int
sound_alloc_dmap (int dev, struct dma_buffparms *dmap, int chan)
{
  char           *start_addr, *end_addr;
  int             i, dma_pagesize;

  if (dmap->raw_buf != NULL)
    return 0;			/* Already done */

  if (dma_buffsize < 4096)
    dma_buffsize = 4096;

  if (chan < 4)
    dma_pagesize = 64 * 1024;
  else
    dma_pagesize = 128 * 1024;

  dmap->raw_buf = NULL;

  if (debugmem)
    printk ("sound: buffsize%d %lu\n", dev, audio_devs[dev]->buffsize);

  audio_devs[dev]->buffsize = dma_buffsize;

  if (audio_devs[dev]->buffsize > dma_pagesize)
    audio_devs[dev]->buffsize = dma_pagesize;

  start_addr = NULL;

/*
 * Now loop until we get a free buffer. Try to get smaller buffer if
 * it fails.
 */

  while (start_addr == NULL && audio_devs[dev]->buffsize > PAGE_SIZE)
    {
      int             sz, size;

      for (sz = 0, size = PAGE_SIZE;
	   size < audio_devs[dev]->buffsize;
	   sz++, size <<= 1);

      audio_devs[dev]->buffsize = PAGE_SIZE * (1 << sz);

      if ((start_addr = (char *) __get_free_pages (GFP_ATOMIC, sz, MAX_DMA_ADDRESS)) == NULL)
	audio_devs[dev]->buffsize /= 2;
    }

  if (start_addr == NULL)
    {
      printk ("Sound error: Couldn't allocate DMA buffer\n");
      return -ENOMEM;
    }
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
	  return -EFAULT;
	}
    }
  dmap->raw_buf = start_addr;
  dmap->raw_buf_phys = (unsigned long) start_addr;

  for (i = MAP_NR (start_addr); i <= MAP_NR (end_addr); i++)
    {
#ifdef MAP_PAGE_RESERVED
      mem_map[i] |= MAP_PAGE_RESERVED;
#else
      mem_map[i].reserved = 1;
#endif
    }

  return 0;
}

void
sound_free_dmap (int dev, struct dma_buffparms *dmap)
{
  if (dmap->raw_buf == NULL)
    return;
  {
    int             sz, size, i;
    unsigned long   start_addr, end_addr;

    for (sz = 0, size = PAGE_SIZE;
	 size < audio_devs[dev]->buffsize;
	 sz++, size <<= 1);

    start_addr = (unsigned long) dmap->raw_buf;
    end_addr = start_addr + audio_devs[dev]->buffsize;

    for (i = MAP_NR (start_addr); i <= MAP_NR (end_addr); i++)
      {
#ifdef MAP_PAGE_RESERVED
	mem_map[i] &= ~MAP_PAGE_RESERVED;
#else
	mem_map[i].reserved = 0;
#endif
      }
    free_pages ((unsigned long) dmap->raw_buf, sz);
  }
  dmap->raw_buf = NULL;
}

int
soud_map_buffer (int dev, struct dma_buffparms *dmap, buffmem_desc * info)
{
  printk ("Entered sound_map_buffer()\n");
  printk ("Exited sound_map_buffer()\n");
  return -EINVAL;
}

#else

void
soundcard_init (void)		/* Dummy version */
{
}

#endif

#endif
