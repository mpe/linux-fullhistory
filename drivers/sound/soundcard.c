/*
 * linux/drivers/sound/soundcard.c
 *
 * Sound card driver for Linux
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
/*
 * Thomas Sailer   : ioctl code reworked (vmalloc/vfree removed)
 *                   integrated sound_switch.c
 * Stefan Reinauer : integrated /proc/sound (equals to /dev/sndstat,
 *                   which should disappear in the near future)
 *
 * Rob Riggs		Added persistent DMA buffers support (1998/10/17)
 */

#include <linux/config.h>

#include "sound_config.h"
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/ctype.h>
#include <linux/stddef.h>
#include <linux/kmod.h>
#ifdef __KERNEL__
#include <asm/dma.h>
#include <asm/io.h>
#include <asm/segment.h>
#include <linux/wait.h>
#include <linux/malloc.h>
#include <linux/ioport.h>
#endif				/* __KERNEL__ */
#include <linux/delay.h>
#include <linux/proc_fs.h>

#include "soundmodule.h"
struct notifier_block *sound_locker=(struct notifier_block *)0;
static int lock_depth = 0;

#ifdef MODULE
#define modular 1
#else
#define modular 0
#endif

/*
 * This ought to be moved into include/asm/dma.h
 */
#ifndef valid_dma
#define valid_dma(n) ((n) >= 0 && (n) < MAX_DMA_CHANNELS && (n) != 4)
#endif

static int      chrdev_registered = 0;
static int      is_unloading = 0;

/*
 * Table for permanently allocated memory (used when unloading the module)
 */
caddr_t         sound_mem_blocks[1024];
int             sound_nblocks = 0;

/* Persistent DMA buffers */
int		sound_dmap_flag = 0;	/* Off by default */

static int      soundcard_configured = 0;

static char     dma_alloc_map[MAX_DMA_CHANNELS] =
{0};

#define DMA_MAP_UNAVAIL		0
#define DMA_MAP_FREE		1
#define DMA_MAP_BUSY		2


static int in_use = 0;	        /* Total # of open devices */
unsigned long seq_time = 0;	/* Time for /dev/sequencer */

/*
 * Table for configurable mixer volume handling
 */
static mixer_vol_table mixer_vols[MAX_MIXER_DEV];
static int num_mixer_volumes = 0;


int *load_mixer_volumes(char *name, int *levels, int present)
{
	int             i, n;

	for (i = 0; i < num_mixer_volumes; i++)
	{
		if (strcmp(name, mixer_vols[i].name) == 0)
		{
			if (present)
				mixer_vols[i].num = i;
			return mixer_vols[i].levels;
		}
	}
	if (num_mixer_volumes >= MAX_MIXER_DEV)
	{
		printk(KERN_ERR "Sound: Too many mixers (%s)\n", name);
		return levels;
	}
	n = num_mixer_volumes++;

	strcpy(mixer_vols[n].name, name);

	if (present)
		mixer_vols[n].num = n;
	else
		mixer_vols[n].num = -1;

	for (i = 0; i < 32; i++)
		mixer_vols[n].levels[i] = levels[i];
	return mixer_vols[n].levels;
}

static int set_mixer_levels(caddr_t arg)
{
        /* mixer_vol_table is 174 bytes, so IMHO no reason to not allocate it on the stack */
	mixer_vol_table buf;   

	if (__copy_from_user(&buf, arg, sizeof(buf)))
		return -EFAULT;
	load_mixer_volumes(buf.name, buf.levels, 0);
	if (__copy_to_user(arg, &buf, sizeof(buf)))
		return -EFAULT;
	return 0;
}

static int get_mixer_levels(caddr_t arg)
{
	int n;

	if (__get_user(n, (int *)(&(((mixer_vol_table *)arg)->num))))
		return -EFAULT;
	if (n < 0 || n >= num_mixer_volumes)
		return -EINVAL;
	if (__copy_to_user(arg, &mixer_vols[n], sizeof(mixer_vol_table)))
		return -EFAULT;
	return 0;
}

static int sound_proc_get_info(char *buffer, char **start, off_t offset, int length, int inout)
{
	int len, i, drv;
        off_t pos = 0;
        off_t begin = 0;

#ifdef MODULE
#define MODULEPROCSTRING "Driver loaded as a module"
#else
#define MODULEPROCSTRING "Driver compiled into kernel"
#endif

	down(&uts_sem);	

	len = sprintf(buffer, "OSS/Free:" SOUND_VERSION_STRING "\n"
		      "Load type: " MODULEPROCSTRING "\n"
		      "Kernel: %s %s %s %s %s\n"
		      "Config options: %x\n\nInstalled drivers: \n", 
		      system_utsname.sysname, system_utsname.nodename, system_utsname.release, 
		      system_utsname.version, system_utsname.machine, SELECTED_SOUND_OPTIONS);
	up(&uts_sem);
	
	for (i = 0; (i < num_sound_drivers) && (pos <= offset + length); i++) {
		if (!sound_drivers[i].card_type)
			continue;
		len += sprintf(buffer + len, "Type %d: %s\n", 
			       sound_drivers[i].card_type, sound_drivers[i].name);
		pos = begin + len;
		if (pos < offset) {
			len = 0;
			begin = pos;
		}
	}
	len += sprintf(buffer + len, "\nCard config: \n");

	for (i = 0; (i < num_sound_cards) && (pos <= offset + length); i++) 
	{
		if (!snd_installed_cards[i].card_type)
			continue;
		if (!snd_installed_cards[i].enabled)
			len += sprintf(buffer + len, "(");
		if ((drv = snd_find_driver(snd_installed_cards[i].card_type)) != -1)
			len += sprintf(buffer + len, "%s", sound_drivers[drv].name);
		if (snd_installed_cards[i].config.io_base)
			len += sprintf(buffer + len, " at 0x%x", snd_installed_cards[i].config.io_base);
		if (snd_installed_cards[i].config.irq != 0)
			len += sprintf(buffer + len, " irq %d", abs(snd_installed_cards[i].config.irq));
		if (snd_installed_cards[i].config.dma != -1) {
			len += sprintf(buffer + len, " drq %d", snd_installed_cards[i].config.dma);
			if (snd_installed_cards[i].config.dma2 != -1)
				len += sprintf(buffer + len, ",%d", snd_installed_cards[i].config.dma2);
		}
		if (!snd_installed_cards[i].enabled)
			len += sprintf(buffer + len, ")");
		len += sprintf(buffer + len, "\n");
		pos = begin + len;
		if (pos < offset) {
			len = 0;
			begin = pos;
		}
	}
	if (!sound_started)
		len += sprintf(buffer + len, "\n\n***** Sound driver not started *****\n\n");
#ifndef CONFIG_AUDIO
	len += sprintf(buffer + len, "\nAudio devices: NOT ENABLED IN CONFIG\n");
#else
	len += sprintf(buffer + len, "\nAudio devices:\n");
	for (i = 0; (i < num_audiodevs) && (pos <= offset + length); i++) {
		if (audio_devs[i] == NULL)
			continue;
		len += sprintf(buffer + len, "%d: %s%s\n", i, audio_devs[i]->name, 
			       audio_devs[i]->flags & DMA_DUPLEX ? " (DUPLEX)" : "");
		pos = begin + len;
		if (pos < offset) {
			len = 0;
			begin = pos;
		}
	}
#endif

#ifndef CONFIG_SEQUENCER
	len += sprintf(buffer + len, "\nSynth devices: NOT ENABLED IN CONFIG\n");
#else
	len += sprintf(buffer + len, "\nSynth devices:\n");
	for (i = 0; (i < num_synths) && (pos <= offset + length); i++) {
		if (synth_devs[i] == NULL)
			continue;
		len += sprintf(buffer + len, "%d: %s\n", i, synth_devs[i]->info->name);
		pos = begin + len;
		if (pos < offset) {
			len = 0;
			begin = pos;
		}
	}
#endif

#ifndef CONFIG_MIDI
	len += sprintf(buffer + len, "\nMidi devices: NOT ENABLED IN CONFIG\n");
#else
	len += sprintf(buffer + len, "\nMidi devices:\n");
	for (i = 0; (i < num_midis) && (pos <= offset + length); i++) {
		if (midi_devs[i] == NULL)
			continue;
		len += sprintf(buffer + len, "%d: %s\n", i, midi_devs[i]->info.name);
		pos = begin + len;
		if (pos < offset) {
			len = 0;
			begin = pos;
		}
	}
#endif

#ifdef CONFIG_SEQUENCER
	len += sprintf(buffer + len, "\nTimers:\n");

	for (i = 0; (i < num_sound_timers) && (pos <= offset + length); i++) {
		if (sound_timer_devs[i] == NULL)
			continue;
		len += sprintf(buffer + len, "%d: %s\n", i, sound_timer_devs[i]->info.name);
		pos = begin + len;
		if (pos < offset) {
			len = 0;
			begin = pos;
		}
	}
#endif

	len += sprintf(buffer + len, "\nMixers:\n");
	for (i = 0; (i < num_mixers) && (pos <= offset + length); i++) {
		if (mixer_devs[i] == NULL)
			continue;
		len += sprintf(buffer + len, "%d: %s\n", i, mixer_devs[i]->name);
		pos = begin + len;
		if (pos < offset) {
			len = 0;
			begin = pos;
		}
	}
	*start = buffer + (offset - begin);
	len -= (offset - begin);
        if (len > length) 
		len = length;
        return len;
}

#ifdef CONFIG_PROC_FS
static struct proc_dir_entry proc_root_sound = {
        PROC_SOUND, 5, "sound",
        S_IFREG | S_IRUGO, 1, 0, 0,
        0, NULL, sound_proc_get_info
};
#endif

#ifndef MIN
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

/* 4K page size but our output routines use some slack for overruns */
#define PROC_BLOCK_SIZE (3*1024)

/*
 * basically copied from fs/proc/generic.c:proc_file_read 
 * should be removed sometime in the future together with /dev/sndstat
 * (a symlink /dev/sndstat -> /proc/sound will do as well)
 */
static ssize_t sndstat_file_read(struct file * file, char * buf, size_t nbytes, loff_t *ppos)
{
        char    *page;
        ssize_t retval=0;
        int     eof=0;
        ssize_t n, count;
        char    *start;

        if (!(page = (char*) __get_free_page(GFP_KERNEL)))
                return -ENOMEM;

        while ((nbytes > 0) && !eof)
        {
                count = MIN(PROC_BLOCK_SIZE, nbytes);

                start = NULL;
		n = sound_proc_get_info(page, &start, *ppos, count, 0);
		if (n < count)
			eof = 1;
                        
                if (!start) {
                        /*
                         * For proc files that are less than 4k
                         */
                        start = page + *ppos;
                        n -= *ppos;
                        if (n <= 0)
                                break;
                        if (n > count)
                                n = count;
                }
                if (n == 0)
                        break;  /* End of file */
                if (n < 0) {
                        if (retval == 0)
                                retval = n;
                        break;
                }
                
                n -= copy_to_user(buf, start, n);       /* BUG ??? */
                if (n == 0) {
                        if (retval == 0)
                                retval = -EFAULT;
                        break;
                }
                
                *ppos += n;     /* Move down the file */
                nbytes -= n;
                buf += n;
                retval += n;
        }
        free_page((unsigned long) page);
        return retval;
}


static ssize_t sound_read(struct file *file, char *buf, size_t count, loff_t *ppos)
{
	int dev = MINOR(file->f_dentry->d_inode->i_rdev);

	DEB(printk("sound_read(dev=%d, count=%d)\n", dev, count));
	switch (dev & 0x0f) {
	case SND_DEV_STATUS:
		return sndstat_file_read(file, buf, count, ppos);

#ifdef CONFIG_AUDIO
	case SND_DEV_DSP:
	case SND_DEV_DSP16:
	case SND_DEV_AUDIO:
		return audio_read(dev, file, buf, count);
#endif

#ifdef CONFIG_SEQUENCER
	case SND_DEV_SEQ:
	case SND_DEV_SEQ2:
		return sequencer_read(dev, file, buf, count);
#endif

#ifdef CONFIG_MIDI
	case SND_DEV_MIDIN:
		return MIDIbuf_read(dev, file, buf, count);
#endif

	default:;
	}
	return -EINVAL;
}

static ssize_t sound_write(struct file *file, const char *buf, size_t count, loff_t *ppos)
{
	int dev = MINOR(file->f_dentry->d_inode->i_rdev);

	DEB(printk("sound_write(dev=%d, count=%d)\n", dev, count));
	switch (dev & 0x0f) {
#ifdef CONFIG_SEQUENCER
	case SND_DEV_SEQ:
	case SND_DEV_SEQ2:
		return sequencer_write(dev, file, buf, count);
#endif

#ifdef CONFIG_AUDIO
	case SND_DEV_DSP:
	case SND_DEV_DSP16:
	case SND_DEV_AUDIO:
		return audio_write(dev, file, buf, count);
#endif

#ifdef CONFIG_MIDI
	case SND_DEV_MIDIN:
		return MIDIbuf_write(dev, file, buf, count);
#endif
	}
	return -EINVAL;
}

static long long sound_lseek(struct file *file, long long offset, int orig)
{
	return -ESPIPE;
}

static int sound_open(struct inode *inode, struct file *file)
{
	int dev, retval;

	if (is_unloading) {
		/* printk(KERN_ERR "Sound: Driver partially removed. Can't open device\n");*/
		return -EBUSY;
	}
	dev = MINOR(inode->i_rdev);
	if (!soundcard_configured && dev != SND_DEV_CTL && dev != SND_DEV_STATUS) {
		/* printk("SoundCard Error: The sound system has not been configured\n");*/
		return -ENXIO;
	}
	DEB(printk("sound_open(dev=%d)\n", dev));
	if ((dev >= SND_NDEVS) || (dev < 0)) {
		/* printk(KERN_ERR "Invalid minor device %d\n", dev);*/
		return -ENXIO;
	}
	switch (dev & 0x0f) {
	case SND_DEV_STATUS:
		break;

	case SND_DEV_CTL:
		dev >>= 4;
		if (dev >= 0 && dev < MAX_MIXER_DEV && mixer_devs[dev] == NULL) {
			char modname[20];
			sprintf(modname, "mixer%d", dev);
			request_module(modname);
		}
		if (dev && (dev >= num_mixers || mixer_devs[dev] == NULL))
			return -ENXIO;
		break;

#ifdef CONFIG_SEQUENCER
	case SND_DEV_SEQ:
	case SND_DEV_SEQ2:
		if ((retval = sequencer_open(dev, file)) < 0)
			return retval;
		break;
#endif

#ifdef CONFIG_MIDI
	case SND_DEV_MIDIN:
		if ((retval = MIDIbuf_open(dev, file)) < 0)
			return retval;
		break;
#endif

#ifdef CONFIG_AUDIO
	case SND_DEV_DSP:
	case SND_DEV_DSP16:
	case SND_DEV_AUDIO:
		if ((retval = audio_open(dev, file)) < 0)
			return retval;
		break;
#endif

	default:
		printk(KERN_ERR "Invalid minor device %d\n", dev);
		return -ENXIO;
	}
	in_use++;

#ifdef CONFIG_MODULES
	notifier_call_chain(&sound_locker, 1, 0);
	lock_depth++;
#endif

	return 0;
}

static int sound_release(struct inode *inode, struct file *file)
{
	int dev = MINOR(inode->i_rdev);

	DEB(printk("sound_release(dev=%d)\n", dev));
	switch (dev & 0x0f) {
	case SND_DEV_STATUS:
	case SND_DEV_CTL:
		break;
		
#ifdef CONFIG_SEQUENCER
	case SND_DEV_SEQ:
	case SND_DEV_SEQ2:
		sequencer_release(dev, file);
		break;
#endif

#ifdef CONFIG_MIDI
	case SND_DEV_MIDIN:
		MIDIbuf_release(dev, file);
		break;
#endif

#ifdef CONFIG_AUDIO
	case SND_DEV_DSP:
	case SND_DEV_DSP16:
	case SND_DEV_AUDIO:
		audio_release(dev, file);
		break;
#endif

	default:
		printk(KERN_ERR "Sound error: Releasing unknown device 0x%02x\n", dev);
	}
	in_use--;

#ifdef CONFIG_MODULES
	notifier_call_chain(&sound_locker, 0, 0);
	lock_depth--;
#endif

	return 0;
}

static int get_mixer_info(int dev, caddr_t arg)
{
	mixer_info info;

	strncpy(info.id, mixer_devs[dev]->id, sizeof(info.id));
	strncpy(info.name, mixer_devs[dev]->name, sizeof(info.name));
	info.name[sizeof(info.name)-1] = 0;
	info.modify_counter = mixer_devs[dev]->modify_counter;
	if (__copy_to_user(arg, &info,  sizeof(info)))
		return -EFAULT;
	return 0;
}

static int get_old_mixer_info(int dev, caddr_t arg)
{
	_old_mixer_info info;

 	strncpy(info.id, mixer_devs[dev]->id, sizeof(info.id));
 	strncpy(info.name, mixer_devs[dev]->name, sizeof(info.name));
 	info.name[sizeof(info.name)-1] = 0;	
 	if (copy_to_user(arg, &info,  sizeof(info)))
		return -EFAULT;
	return 0;
}

static int sound_mixer_ioctl(int mixdev, unsigned int cmd, caddr_t arg)
{
 	if (mixdev < 0 || mixdev >= MAX_MIXER_DEV)
 		return -ENXIO;
 	/* Try to load the mixer... */
 	if (mixer_devs[mixdev] == NULL) {
 		char modname[20];
 		sprintf(modname, "mixer%d", mixdev);
 		request_module(modname);
 	}
 	if (mixdev >= num_mixers || !mixer_devs[mixdev])
 		return -ENXIO;
	if (cmd == SOUND_MIXER_INFO)
		return get_mixer_info(mixdev, arg);
	if (cmd == SOUND_OLD_MIXER_INFO)
		return get_old_mixer_info(mixdev, arg);
	if (_SIOC_DIR(cmd) & _SIOC_WRITE)
		mixer_devs[mixdev]->modify_counter++;
	if (!mixer_devs[mixdev]->ioctl)
		return -EINVAL;
	return mixer_devs[mixdev]->ioctl(mixdev, cmd, arg);
}

static int sound_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	int err, len = 0, dtype;
	int dev = MINOR(inode->i_rdev);

	if (_SIOC_DIR(cmd) != _SIOC_NONE && _SIOC_DIR(cmd) != 0) {
		/*
		 * Have to validate the address given by the process.
		 */
		len = _SIOC_SIZE(cmd);
		if (len < 1 || len > 65536 || arg == 0)
			return -EFAULT;
		if (_SIOC_DIR(cmd) & _SIOC_WRITE)
			if ((err = verify_area(VERIFY_READ, (void *)arg, len)) < 0)
				return err;
		if (_SIOC_DIR(cmd) & _SIOC_READ)
			if ((err = verify_area(VERIFY_WRITE, (void *)arg, len)) < 0)
				return err;
	}
	DEB(printk("sound_ioctl(dev=%d, cmd=0x%x, arg=0x%x)\n", dev, cmd, arg));
	if (cmd == OSS_GETVERSION)
		return __put_user(SOUND_VERSION, (int *)arg);
	
	if (_IOC_TYPE(cmd) == 'M' && num_mixers > 0 &&   /* Mixer ioctl */
	    (dev & 0x0f) != SND_DEV_CTL) {              
		dtype = dev & 0x0f;
		switch (dtype) {
#ifdef CONFIG_AUDIO
		case SND_DEV_DSP:
		case SND_DEV_DSP16:
		case SND_DEV_AUDIO:
			return sound_mixer_ioctl(audio_devs[dev >> 4]->mixer_dev,
						 cmd, (caddr_t)arg);
#endif
			
		default:
			return sound_mixer_ioctl(dev >> 4, cmd, (caddr_t)arg);
		}
	}
	switch (dev & 0x0f) {
	case SND_DEV_CTL:
		if (cmd == SOUND_MIXER_GETLEVELS)
			return get_mixer_levels((caddr_t)arg);
		if (cmd == SOUND_MIXER_SETLEVELS)
			return set_mixer_levels((caddr_t)arg);
		return sound_mixer_ioctl(dev >> 4, cmd, (caddr_t)arg);

#ifdef CONFIG_SEQUENCER
	case SND_DEV_SEQ:
	case SND_DEV_SEQ2:
		return sequencer_ioctl(dev, file, cmd, (caddr_t)arg);
#endif

#ifdef CONFIG_AUDIO
	case SND_DEV_DSP:
	case SND_DEV_DSP16:
	case SND_DEV_AUDIO:
		return audio_ioctl(dev, file, cmd, (caddr_t)arg);
		break;
#endif

#ifdef CONFIG_MIDI
	case SND_DEV_MIDIN:
		return MIDIbuf_ioctl(dev, file, cmd, (caddr_t)arg);
		break;
#endif

	}
	return -EINVAL;
}

static unsigned int sound_poll(struct file *file, poll_table * wait)
{
	struct inode *inode = file->f_dentry->d_inode;
	int dev = MINOR(inode->i_rdev);

	DEB(printk("sound_poll(dev=%d)\n", dev));
	switch (dev & 0x0f) {
#ifdef CONFIG_SEQUENCER
	case SND_DEV_SEQ:
	case SND_DEV_SEQ2:
		return sequencer_poll(dev, file, wait);
#endif

#ifdef CONFIG_MIDI
	case SND_DEV_MIDIN:
		return MIDIbuf_poll(dev, file, wait);
#endif

#ifdef CONFIG_AUDIO
	case SND_DEV_DSP:
	case SND_DEV_DSP16:
	case SND_DEV_AUDIO:
		return DMAbuf_poll(file, dev >> 4, wait);
#endif
	}
	return 0;
}

static int sound_mmap(struct file *file, struct vm_area_struct *vma)
{
	int dev_class;
	unsigned long size;
	struct dma_buffparms *dmap = NULL;
	int dev = MINOR(file->f_dentry->d_inode->i_rdev);

	dev_class = dev & 0x0f;
	dev >>= 4;

	if (dev_class != SND_DEV_DSP && dev_class != SND_DEV_DSP16 && dev_class != SND_DEV_AUDIO)
	{
/*		printk("Sound: mmap() not supported for other than audio devices\n");*/
		return -EINVAL;
	}
	if (vma->vm_flags & VM_WRITE)	/* Map write and read/write to the output buf */
		dmap = audio_devs[dev]->dmap_out;
	else if (vma->vm_flags & VM_READ)
		dmap = audio_devs[dev]->dmap_in;
	else
	{
/*		printk("Sound: Undefined mmap() access\n");*/
		return -EINVAL;
	}

	if (dmap == NULL)
	{
/*		printk("Sound: mmap() error. dmap == NULL\n");*/
		return -EIO;
	}
	if (dmap->raw_buf == NULL)
	{
/*		printk("Sound: mmap() called when raw_buf == NULL\n");*/
		return -EIO;
	}
	if (dmap->mapping_flags)
	{
/*		printk("Sound: mmap() called twice for the same DMA buffer\n");*/
		return -EIO;
	}
	if (vma->vm_offset != 0)
	{
/*		printk("Sound: mmap() offset must be 0.\n");*/
		return -EINVAL;
	}
	size = vma->vm_end - vma->vm_start;

	if (size != dmap->bytes_in_use)
	{
		printk(KERN_WARNING "Sound: mmap() size = %ld. Should be %d\n", size, dmap->bytes_in_use);
	}
	if (remap_page_range(vma->vm_start, virt_to_phys(dmap->raw_buf),
		vma->vm_end - vma->vm_start,
		vma->vm_page_prot))
		return -EAGAIN;

	vma->vm_file = file;
	file->f_count++;

	dmap->mapping_flags |= DMA_MAP_MAPPED;

	if( audio_devs[dev]->d->mmap)
		audio_devs[dev]->d->mmap(dev);

	memset(dmap->raw_buf,
	       dmap->neutral_byte,
	       dmap->bytes_in_use);
	return 0;
}

struct file_operations oss_sound_fops =
{
	sound_lseek,
	sound_read,
	sound_write,
	NULL,			/* sound_readdir */
	sound_poll,
	sound_ioctl,
	sound_mmap,
	sound_open,
	NULL,			/* flush */
	sound_release
};

/*
 *	Create the required special subdevices
 */
 
static int create_special_devices(void)
{
	int seq1,seq2;
	int sndstat=register_sound_special(&oss_sound_fops, 6);
	if(sndstat==-1)
		goto bad1;
	seq1=register_sound_special(&oss_sound_fops, 1);
	if(seq1==-1)
		goto bad2;
	seq2=register_sound_special(&oss_sound_fops, 8);
	if(seq2!=-1)
		return 0;
	unregister_sound_special(1);
bad2:
	unregister_sound_special(6);		
bad1:	
	return -1;
}

static void destroy_special_devices(void)
{
	unregister_sound_special(6);
	unregister_sound_special(1);
	unregister_sound_special(8);
}

#ifdef MODULE
static void
#else
void
#endif
soundcard_init(void)
{
	/* drag in sound_syms.o */
	{
		extern char sound_syms_symbol;
		sound_syms_symbol = 0;
	}

#ifndef MODULE
	create_special_devices();
	chrdev_registered = 1;
#endif

	soundcard_configured = 1;

	sndtable_init();	/* Initialize call tables and detect cards */


#ifdef FIXME
	if (sndtable_get_cardcount() == 0)
		return;		/* No cards detected */
#endif

#ifdef CONFIG_AUDIO
	if (num_audiodevs || modular)	/* Audio devices present */
	{
		audio_init_devices();
	}
#endif
#ifdef CONFIG_PROC_FS
	if (proc_register(&proc_root, &proc_root_sound))
		printk(KERN_ERR "sound: registering /proc/sound failed\n");
#endif		
}

static int      sound[20] = {
	0
};

#ifdef MODULE

int traceinit = 0;
static int dmabuf = 0;
MODULE_PARM(traceinit, "i");
MODULE_PARM(dmabuf, "i");

int init_module(void)
{
	int             err;
	int             ints[21];
	int             i;

	trace_init=traceinit;
	/*
	 * "sound=" command line handling by Harald Milz.
	 */
	i = 0;
	while (i < 20 && sound[i])
		ints[i + 1] = sound[i++];
	ints[0] = i;

	if (i)
		sound_setup("sound=", ints);

	err = create_special_devices();
	if (err)
	{
		printk(KERN_ERR "sound: driver already loaded/included in kernel\n");
		return err;
	}

	/* Protecting the innocent */
	sound_dmap_flag = (dmabuf > 0 ? 1 : 0);

	chrdev_registered = 1;
	soundcard_init();

	if (sound_nblocks >= 1024)
		printk(KERN_ERR "Sound warning: Deallocation table was too small.\n");
	
	return 0;
}

void cleanup_module(void)
{
	int i;

	if (MOD_IN_USE)
	{
		return;
	}
#ifdef CONFIG_PROC_FS	
        if (proc_unregister(&proc_root, PROC_SOUND))
		printk(KERN_ERR "sound: unregistering /proc/sound failed\n");
#endif		
	if (chrdev_registered)
		destroy_special_devices();

#ifdef CONFIG_SEQUENCER
	sound_stop_timer();
#endif

#ifdef CONFIG_LOWLEVEL_SOUND
	{
		extern void     sound_unload_lowlevel_drivers(void);

		sound_unload_lowlevel_drivers();
	}
#endif
	sound_unload_drivers();
	sequencer_unload();

	for (i = 0; i < MAX_DMA_CHANNELS; i++)
	{
		if (dma_alloc_map[i] != DMA_MAP_UNAVAIL)
		{
			printk(KERN_ERR "Sound: Hmm, DMA%d was left allocated - fixed\n", i);
			sound_free_dma(i);
		}
	}
	for (i = 0; i < sound_nblocks; i++)
	{
		vfree(sound_mem_blocks[i]);
	}

}
#endif

int sound_alloc_dma(int chn, char *deviceID)
{
	int err;

	if ((err = request_dma(chn, deviceID)) != 0)
		return err;

	dma_alloc_map[chn] = DMA_MAP_FREE;

	return 0;
}

int sound_open_dma(int chn, char *deviceID)
{
	unsigned long   flags;

	if (!valid_dma(chn))
	{
		printk(KERN_ERR "sound_open_dma: Invalid DMA channel %d\n", chn);
		return 1;
	}
	save_flags(flags);
	cli();

	if (dma_alloc_map[chn] != DMA_MAP_FREE)
	{
		printk("sound_open_dma: DMA channel %d busy or not allocated (%d)\n", chn, dma_alloc_map[chn]);
		restore_flags(flags);
		return 1;
	}
	dma_alloc_map[chn] = DMA_MAP_BUSY;
	restore_flags(flags);
	return 0;
}

void sound_free_dma(int chn)
{
	if (dma_alloc_map[chn] == DMA_MAP_UNAVAIL)
	{
		/* printk( "sound_free_dma: Bad access to DMA channel %d\n",  chn); */
		return;
	}
	free_dma(chn);
	dma_alloc_map[chn] = DMA_MAP_UNAVAIL;
}

void sound_close_dma(int chn)
{
	unsigned long   flags;

	save_flags(flags);
	cli();

	if (dma_alloc_map[chn] != DMA_MAP_BUSY)
	{
		printk(KERN_ERR "sound_close_dma: Bad access to DMA channel %d\n", chn);
		restore_flags(flags);
		return;
	}
	dma_alloc_map[chn] = DMA_MAP_FREE;
	restore_flags(flags);
}

#ifdef CONFIG_SEQUENCER

static void do_sequencer_timer(unsigned long dummy)
{
	sequencer_timer(0);
}


static struct timer_list seq_timer =
{NULL, NULL, 0, 0, do_sequencer_timer};

void request_sound_timer(int count)
{
	extern unsigned long seq_time;

	if (count < 0)
	{
		seq_timer.expires = (-count) + jiffies;
		add_timer(&seq_timer);
		return;
	}
	count += seq_time;

	count -= jiffies;

	if (count < 1)
		count = 1;

	seq_timer.expires = (count) + jiffies;
	add_timer(&seq_timer);
}

void sound_stop_timer(void)
{
	del_timer(&seq_timer);;
}
#endif

void conf_printf(char *name, struct address_info *hw_config)
{
	if (!trace_init)
		return;

	printk("<%s> at 0x%03x", name, hw_config->io_base);

	if (hw_config->irq)
		printk(" irq %d", (hw_config->irq > 0) ? hw_config->irq : -hw_config->irq);

	if (hw_config->dma != -1 || hw_config->dma2 != -1)
	{
		printk(" dma %d", hw_config->dma);
		if (hw_config->dma2 != -1)
			printk(",%d", hw_config->dma2);
	}
	printk("\n");
}

void conf_printf2(char *name, int base, int irq, int dma, int dma2)
{
	if (!trace_init)
		return;

	printk("<%s> at 0x%03x", name, base);

	if (irq)
		printk(" irq %d", (irq > 0) ? irq : -irq);

	if (dma != -1 || dma2 != -1)
	{
		  printk(" dma %d", dma);
		  if (dma2 != -1)
			  printk(",%d", dma2);
	}
	printk("\n");
}

/*
 *	Module and lock management
 */
 
/*
 *	When a sound module is registered we need to bring it to the current
 *	lock level...
 */
 
void sound_notifier_chain_register(struct notifier_block *bl)
{
	int ct=0;
	
	notifier_chain_register(&sound_locker, bl);
	/*
	 *	Normalise the lock count by calling the entry directly. We
	 *	have to call the module as it owns its own use counter
	 */
	while(ct<lock_depth)
	{
		bl->notifier_call(bl, 1, 0);
		ct++;
	}
}
