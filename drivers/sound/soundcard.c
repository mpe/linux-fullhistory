/*
 * linux/kernel/chr_drv/sound/soundcard.c
 *
 * Soundcard driver for Linux
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1996
 *
 * USS/Lite for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
#include <linux/config.h>


#include "sound_config.h"

#include <linux/major.h>


int            *sound_global_osp = NULL;
static int      chrdev_registered = 0;
static int      sound_major = SOUND_MAJOR;

static int      is_unloading = 0;

/*
 * Table for permanently allocated memory (used when unloading the module)
 */
caddr_t         sound_mem_blocks[1024];
int             sound_nblocks = 0;

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
sound_read (inode_handle * inode, file_handle * file, char *buf, int count)
{
  int             dev;

  dev = MINOR (inode_get_rdev (inode));

  files[dev].flags = file_get_flags (file);

  return sound_read_sw (dev, &files[dev], buf, count);
}

static int
sound_write (inode_handle * inode, file_handle * file, const char *buf, int count)
{
  int             dev;

  dev = MINOR (inode_get_rdev (inode));

  files[dev].flags = file_get_flags (file);

  return sound_write_sw (dev, &files[dev], buf, count);
}

static int
sound_lseek (inode_handle * inode, file_handle * file, off_t offset, int orig)
{
  return -(EPERM);
}

static int
sound_open (inode_handle * inode, file_handle * file)
{
  int             dev, retval;
  struct fileinfo tmp_file;

  if (is_unloading)
    {
      printk ("Sound: Driver partially removed. Can't open device\n");
      return -(EBUSY);
    }

  dev = MINOR (inode_get_rdev (inode));

  if (!soundcard_configured && dev != SND_DEV_CTL && dev != SND_DEV_STATUS)
    {
      printk ("SoundCard Error: The soundcard system has not been configured\n");
      return -(ENXIO);
    }

  tmp_file.mode = 0;
  tmp_file.flags = file_get_flags (file);

  if ((tmp_file.flags & O_ACCMODE) == O_RDWR)
    tmp_file.mode = OPEN_READWRITE;
  if ((tmp_file.flags & O_ACCMODE) == O_RDONLY)
    tmp_file.mode = OPEN_READ;
  if ((tmp_file.flags & O_ACCMODE) == O_WRONLY)
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
sound_release (inode_handle * inode, file_handle * file)
{
  int             dev;

  dev = MINOR (inode_get_rdev (inode));

  files[dev].flags = file_get_flags (file);

  sound_release_sw (dev, &files[dev]);
#ifdef MODULE
  MOD_DEC_USE_COUNT;
#endif
}

static int
sound_ioctl (inode_handle * inode, file_handle * file,
	     unsigned int cmd, unsigned long arg)
{
  int             dev, err;

  dev = MINOR (inode_get_rdev (inode));

  files[dev].flags = file_get_flags (file);

  if (_IOC_DIR (cmd) != _IOC_NONE)
    {
      /*
         * Have to validate the address given by the process.
       */
      int             len;

      len = _IOC_SIZE (cmd);

      if (_IOC_DIR (cmd) & _IOC_WRITE)
	{
	  if ((err = verify_area (VERIFY_READ, (void *) arg, len)) < 0)
	    return err;
	}

      if (_IOC_DIR (cmd) & _IOC_READ)
	{
	  if ((err = verify_area (VERIFY_WRITE, (void *) arg, len)) < 0)
	    return err;
	}

    }

  err = sound_ioctl_sw (dev, &files[dev], cmd, (caddr_t) arg);

  return err;
}

static int
sound_select (inode_handle * inode, file_handle * file, int sel_type, select_table_handle * wait)
{
  int             dev;

  dev = MINOR (inode_get_rdev (inode));

  files[dev].flags = file_get_flags (file);

  DEB (printk ("sound_select(dev=%d, type=0x%x)\n", dev, sel_type));

  switch (dev & 0x0f)
    {
#ifdef CONFIG_SEQUENCER
    case SND_DEV_SEQ:
    case SND_DEV_SEQ2:
      return sequencer_select (dev, &files[dev], sel_type, wait);
      break;
#endif

#ifdef CONFIG_MIDI
    case SND_DEV_MIDIN:
      return MIDIbuf_select (dev, &files[dev], sel_type, wait);
      break;
#endif

#ifdef CONFIG_AUDIO
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
sound_mmap (inode_handle * inode, file_handle * file, vm_area_handle * vma)
{
  int             dev, dev_class;
  unsigned long   size;
  struct dma_buffparms *dmap = NULL;

  dev = MINOR (inode_get_rdev (inode));

  files[dev].flags = file_get_flags (file);

  dev_class = dev & 0x0f;
  dev >>= 4;

  if (dev_class != SND_DEV_DSP && dev_class != SND_DEV_DSP16 && dev_class != SND_DEV_AUDIO)
    {
      printk ("Sound: mmap() not supported for other than audio devices\n");
      return -EINVAL;
    }

  if ((vma_get_flags (vma) & (VM_READ | VM_WRITE)) == (VM_READ | VM_WRITE))
    {
      printk ("Sound: Cannot do read/write mmap()\n");
      return -EINVAL;
    }

  if (vma_get_flags (vma) & VM_READ)
    {
      dmap = audio_devs[dev]->dmap_in;
    }
  else if (vma_get_flags (vma) & VM_WRITE)
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

  if (vma_get_offset (vma) != 0)
    {
      printk ("Sound: mmap() offset must be 0.\n");
      return -EINVAL;
    }

  size = vma_get_end (vma) - vma_get_start (vma);

  if (size != dmap->bytes_in_use)
    {
      printk ("Sound: mmap() size = %ld. Should be %d\n",
	      size, dmap->bytes_in_use);
    }

  if (remap_page_range (vma_get_start (vma), dmap->raw_buf_phys,
			vma_get_end (vma) - vma_get_start (vma),
			vma_get_page_prot (vma)))
    return -EAGAIN;

  vma_set_inode (vma, inode);
  inode_inc_count (inode);

  dmap->mapping_flags |= DMA_MAP_MAPPED;

  memset (dmap->raw_buf,
	  dmap->neutral_byte,
	  dmap->bytes_in_use);
  return 0;
}

static struct file_operation_handle sound_fops =
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
  module_register_chrdev (sound_major, "sound", &sound_fops);
  chrdev_registered = 1;
#endif

  soundcard_configured = 1;

  sndtable_init ();		/* Initialize call tables and detect cards */



#ifdef CONFIG_LOWLEVEL_SOUND
  {
    extern void     sound_init_lowlevel_drivers (void);

    sound_init_lowlevel_drivers ();
  }
#endif

  if (sndtable_get_cardcount () == 0)
    return;			/* No cards detected */

#ifdef CONFIG_AUDIO
  if (num_audiodevs)		/* Audio devices present */
    {
      DMAbuf_init ();
      audio_init ();
    }
#endif

#ifdef CONFIG_MIDI
  if (num_midis)
    MIDIbuf_init ();
#endif

#ifdef CONFIG_SEQUENCER
  if (num_midis + num_synths)
    sequencer_init ();
#endif

}

static unsigned int irqs = 0;

#ifdef MODULE
static void
free_all_irqs (void)
{
  int             i;

  for (i = 0; i < 31; i++)
    if (irqs & (1ul << i))
      {
	printk ("Sound warning: IRQ%d was left allocated - fixed.\n", i);
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

  if (connect_wrapper (WRAPPER_VERSION) < 0)
    {
      printk ("Sound: Incompatible kernel (wrapper) version\n");
      return -EINVAL;
    }

  /*
     * "sound=" command line handling by Harald Milz.
   */
  i = 0;
  while (i < 20 && sound[i])
    ints[i + 1] = sound[i++];
  ints[0] = i;

  if (i)
    sound_setup ("sound=", ints);

  err = module_register_chrdev (sound_major, "sound", &sound_fops);
  if (err)
    {
      printk ("sound: driver already loaded/included in kernel\n");
      return err;
    }

  chrdev_registered = 1;
  soundcard_init ();

  if (sound_nblocks >= 1024)
    printk ("Sound warning: Deallocation table was too small.\n");

  return 0;
}

#ifdef MODULE


void
cleanup_module (void)
{
  int             i;

  if (MOD_IN_USE)
    {
      return;
    }

  if (chrdev_registered)
    module_unregister_chrdev (sound_major, "sound");

#ifdef CONFIG_SEQUENCER
  sound_stop_timer ();
#endif

#ifdef CONFIG_LOWLEVEL_SOUND
  {
    extern void     sound_unload_lowlevel_drivers (void);

    sound_unload_lowlevel_drivers ();
  }
#endif
  sound_unload_drivers ();

  for (i = 0; i < sound_nblocks; i++)
    vfree (sound_mem_blocks[i]);

  free_all_irqs ();		/* If something was left allocated by accident */

  for (i = 0; i < 8; i++)
    if (dma_alloc_map[i] != DMA_MAP_UNAVAIL)
      {
	printk ("Sound: Hmm, DMA%d was left allocated - fixed\n", i);
	sound_free_dma (i);
      }


}
#endif

void
tenmicrosec (int *osp)
{
  int             i;

  for (i = 0; i < 16; i++)
    inb (0x80);
}

int
snd_set_irq_handler (int interrupt_level, void (*iproc) (int, void *, struct pt_regs *), char *name, int *osp)
{
  int             retcode;

  retcode = request_irq (interrupt_level, iproc, 0 /* SA_INTERRUPT */ , name, NULL);
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
  if (!(irqs & (1ul << vect)))
    return;

  irqs &= ~(1ul << vect);
  free_irq (vect, NULL);
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

  if (chn < 0 || chn > 7 || chn == 4)
    {
      printk ("sound_open_dma: Invalid DMA channel %d\n", chn);
      return 1;
    }

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
      /* printk ("sound_free_dma: Bad access to DMA channel %d\n", chn); */
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

#ifdef CONFIG_SEQUENCER


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

  ;

  {
    seq_timer.expires = ((count - jiffies)) + jiffies;
    add_timer (&seq_timer);
  };
}

void
sound_stop_timer (void)
{
  del_timer (&seq_timer);;
}
#endif

#ifdef CONFIG_AUDIO

#ifdef KMALLOC_DMA_BROKEN
fatal_error__This_version_is_not_compatible_with_this_kernel;
#endif

static int      dma_buffsize = DSP_BUFFSIZE;

int
sound_alloc_dmap (int dev, struct dma_buffparms *dmap, int chan)
{
  char           *start_addr, *end_addr;
  int             i, dma_pagesize;

  dmap->mapping_flags &= ~DMA_MAP_MAPPED;

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
    printk ("sound: buffsize[%d] = %lu\n", dev, audio_devs[dev]->buffsize);

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
      return -(ENOMEM);
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
	  || end_addr >= (char *) (MAX_DMA_ADDRESS))
	{
	  printk (
		   "sound: Got invalid address 0x%lx for %ldb DMA-buffer\n",
		   (long) start_addr,
		   audio_devs[dev]->buffsize);
	  return -(EFAULT);
	}
    }
  dmap->raw_buf = start_addr;
  dmap->raw_buf_phys = virt_to_bus (start_addr);

  for (i = MAP_NR (start_addr); i <= MAP_NR (end_addr); i++)
    {
      mem_map_reserve (i);
    }

  return 0;
}

void
sound_free_dmap (int dev, struct dma_buffparms *dmap)
{
  int             sz, size, i;
  unsigned long   start_addr, end_addr;

  if (dmap->raw_buf == NULL)
    return;

  if (dmap->mapping_flags & DMA_MAP_MAPPED)
    return;			/* Don't free mmapped buffer. Will use it next time */

  for (sz = 0, size = PAGE_SIZE;
       size < audio_devs[dev]->buffsize;
       sz++, size <<= 1);

  start_addr = (unsigned long) dmap->raw_buf;
  end_addr = start_addr + audio_devs[dev]->buffsize;

  for (i = MAP_NR (start_addr); i <= MAP_NR (end_addr); i++)
    {
      mem_map_unreserve (i);
    }

  free_pages ((unsigned long) dmap->raw_buf, sz);
  dmap->raw_buf = NULL;
}

int
soud_map_buffer (int dev, struct dma_buffparms *dmap, buffmem_desc * info)
{
  printk ("Entered sound_map_buffer()\n");
  printk ("Exited sound_map_buffer()\n");
  return -(EINVAL);
}
#endif

void
conf_printf (char *name, struct address_info *hw_config)
{
  if (!trace_init)
    return;

  printk ("<%s> at 0x%03x", name, hw_config->io_base);

  if (hw_config->irq)
    printk (" irq %d", (hw_config->irq > 0) ? hw_config->irq : -hw_config->irq);

  if (hw_config->dma != -1 || hw_config->dma2 != -1)
    {
      printk (" dma %d", hw_config->dma);
      if (hw_config->dma2 != -1)
	printk (",%d", hw_config->dma2);
    }

  printk ("\n");
}

void
conf_printf2 (char *name, int base, int irq, int dma, int dma2)
{
  if (!trace_init)
    return;

  printk ("<%s> at 0x%03x", name, base);

  if (irq)
    printk (" irq %d", (irq > 0) ? irq : -irq);

  if (dma != -1 || dma2 != -1)
    {
      printk (" dma %d", dma);
      if (dma2 != -1)
	printk (",%d", dma2);
    }

  printk ("\n");
}
