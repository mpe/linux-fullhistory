/*
 *	Sound core handling. Breaks out sound functions to submodules
 *	
 *	Author:		Alan Cox <alan.cox@linux.org>
 *
 *	Fixes:
 *
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *                         --------------------
 * 
 *	Top level handler for the sound subsystem. Various devices can
 *	plug into this. The fact they dont all go via OSS doesn't mean 
 *	they don't have to implement the OSS API. There is a lot of logic
 *	to keeping much of the OSS weight out of the code in a compatibility
 *	module, but its up to the driver to rember to load it...
 *
 *	The code provides a set of functions for registration of devices
 *	by type. This is done rather than providing a single call so that
 *	we can hide any future changes in the internals (eg when we go to
 *	32bit dev_t) from the modules and their interface.
 *
 *	Secondly we need to allocate the dsp, dsp16 and audio devices as
 *	one. Thus we misuse the chains a bit to simplify this.
 *
 *	Thirdly to make it more fun and for 2.3.x and above we do all
 *	of this using fine grained locking.
 *
 *	FIXME: we have to resolve modules and fine grained load/unload
 *	locking at some point in 2.3.x.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/malloc.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/sound.h>
#include <linux/major.h>
#include <linux/kmod.h>
#include <linux/devfs_fs_kernel.h>

#define SOUND_STEP 16


struct sound_unit
{
	int unit_minor;
	struct file_operations *unit_fops;
	struct sound_unit *next;
	devfs_handle_t de;
};

#ifdef CONFIG_SOUND_MSNDCLAS
extern int msnd_classic_init(void);
#endif
#ifdef CONFIG_SOUND_MSNDPIN
extern int msnd_pinnacle_init(void);
#endif
#ifdef CONFIG_SOUND_CMPCI
extern init_cmpci(void);
#endif

/*
 *	Low level list operator. Scan the ordered list, find a hole and
 *	join into it. Called with the lock asserted
 */

static int __sound_insert_unit(struct sound_unit * s, struct sound_unit **list, struct file_operations *fops, int index, int low, int top)
{
	int n=low;

	if (index < 0) {	/* first free */

		while (*list && (*list)->unit_minor<n)
			list=&((*list)->next);

		while(n<top)
		{
			/* Found a hole ? */
			if(*list==NULL || (*list)->unit_minor>n)
				break;
			list=&((*list)->next);
			n+=SOUND_STEP;
		}

		if(n>=top)
			return -ENOENT;
	} else {
		n = low+(index*16);
		while (*list) {
			if ((*list)->unit_minor==n)
				return -EBUSY;
			if ((*list)->unit_minor>n)
				break;
			list=&((*list)->next);
		}
	}	
		
	/*
	 *	Fill it in
	 */
	 
	s->unit_minor=n;
	s->unit_fops=fops;
	
	/*
	 *	Link it
	 */
	 
	s->next=*list;
	*list=s;
	
	
	MOD_INC_USE_COUNT;
	return n;
}

/*
 *	Remove a node from the chain. Called with the lock asserted
 */
 
static void __sound_remove_unit(struct sound_unit **list, int unit)
{
	while(*list)
	{
		struct sound_unit *p=*list;
		if(p->unit_minor==unit)
		{
			*list=p->next;
			devfs_unregister (p->de);
			kfree(p);
			MOD_DEC_USE_COUNT;
			return;
		}
		list=&(p->next);
	}
	printk(KERN_ERR "Sound device %d went missing!\n", unit);
}

/*
 *	This lock guards the sound loader list.
 */

spinlock_t sound_loader_lock = SPIN_LOCK_UNLOCKED;

/*
 *	Allocate the controlling structure and add it to the sound driver
 *	list. Acquires locks as needed
 */

static devfs_handle_t devfs_handle = NULL;
 
static int sound_insert_unit(struct sound_unit **list, struct file_operations *fops, int index, int low, int top, const char *name, umode_t mode)
{
	int r;
	struct sound_unit *s=(struct sound_unit *)kmalloc(sizeof(struct sound_unit), GFP_KERNEL);
	char name_buf[16];

	if(s==NULL)
		return -ENOMEM;
		
	spin_lock(&sound_loader_lock);
	r=__sound_insert_unit(s,list,fops,index,low,top);
	spin_unlock(&sound_loader_lock);
	
	if(r<0)
		kfree(s);
	if (r == low)
		sprintf (name_buf, "%s", name);
	else
		sprintf (name_buf, "%s%d", name, (r - low) / SOUND_STEP);
	s->de = devfs_register (devfs_handle, name_buf, 0,
				DEVFS_FL_NONE, SOUND_MAJOR, s->unit_minor,
				S_IFCHR | mode, 0, 0, fops, NULL);
	return r;
}

/*
 *	Remove a unit. Acquires locks as needed. The drivers MUST have
 *	completed the removal before their file operations become
 *	invalid.
 */
 	
static void sound_remove_unit(struct sound_unit **list, int unit)
{
	spin_lock(&sound_loader_lock);
	__sound_remove_unit(list, unit);
	spin_unlock(&sound_loader_lock);
}

/*
 *	Allocations
 *
 *	0	*16		Mixers
 *	1	*8		Sequencers
 *	2	*16		Midi
 *	3	*16		DSP
 *	4	*16		SunDSP
 *	5	*16		DSP16
 *	6	--		sndstat (obsolete)
 *	7	*16		unused
 *	8	--		alternate sequencer (see above)
 *	9	*16		raw synthesizer access
 *	10	*16		unused
 *	11	*16		unused
 *	12	*16		unused
 *	13	*16		unused
 *	14	*16		unused
 *	15	*16		unused
 */

static struct sound_unit *chains[16];

int register_sound_special(struct file_operations *fops, int unit)
{
	char *name;

	switch (unit) {
	    case 0:
		name = "mixer";
		break;
	    case 1:
		name = "sequencer";
		break;
	    case 2:
		name = "midi00";
		break;
	    case 3:
		name = "dsp";
		break;
	    case 4:
		name = "audio";
		break;
	    case 5:
		name = "unknown5";
		break;
	    case 6:		/* Was once sndstat */
		name = "unknown6";
		break;
	    case 7:
		name = "unknown7";
		break;
	    case 8:
		name = "sequencer2";
		break;
	    case 9:
		name = "dmmidi";
		break;
	    case 10:
		name = "dmfm";
		break;
	    case 11:
		name = "unknown11";
		break;
	    case 12:
		name = "adsp";
		break;
	    case 13:
		name = "amidi";
		break;
	    case 14:
		name = "admmidi";
		break;
	    default:
		name = "unknown";
		break;
	}
	return sound_insert_unit(&chains[unit&15], fops, -1, unit, unit+1,
				 name, S_IRUSR | S_IWUSR);
}
 
EXPORT_SYMBOL(register_sound_special);

int register_sound_mixer(struct file_operations *fops, int dev)
{
	return sound_insert_unit(&chains[0], fops, dev, 0, 128,
				 "mixer", S_IRUSR | S_IWUSR);
}

EXPORT_SYMBOL(register_sound_mixer);

int register_sound_midi(struct file_operations *fops, int dev)
{
	return sound_insert_unit(&chains[2], fops, dev, 2, 130,
				 "midi", S_IRUSR | S_IWUSR);
}

EXPORT_SYMBOL(register_sound_midi);

/*
 *	DSP's are registered as a triple. Register only one and cheat
 *	in open - see below.
 */
 
int register_sound_dsp(struct file_operations *fops, int dev)
{
	return sound_insert_unit(&chains[3], fops, dev, 3, 131,
				 "dsp", S_IWUSR | S_IRUSR);
}

EXPORT_SYMBOL(register_sound_dsp);

int register_sound_synth(struct file_operations *fops, int dev)
{
	return sound_insert_unit(&chains[9], fops, dev, 9, 137,
				 "synth", S_IRUSR | S_IWUSR);
}

EXPORT_SYMBOL(register_sound_synth);

void unregister_sound_special(int unit)
{
	sound_remove_unit(&chains[unit&15], unit);
}
 
EXPORT_SYMBOL(unregister_sound_special);

void unregister_sound_mixer(int unit)
{
	sound_remove_unit(&chains[0], unit);
}

EXPORT_SYMBOL(unregister_sound_mixer);

void unregister_sound_midi(int unit)
{
	return sound_remove_unit(&chains[2], unit);
}

EXPORT_SYMBOL(unregister_sound_midi);

void unregister_sound_dsp(int unit)
{
	return sound_remove_unit(&chains[3], unit);
}

EXPORT_SYMBOL(unregister_sound_dsp);

void unregister_sound_synth(int unit)
{
	return sound_remove_unit(&chains[9], unit);
}

EXPORT_SYMBOL(unregister_sound_synth);

/*
 *	Now our file operations
 */

static int soundcore_open(struct inode *, struct file *);

static struct file_operations soundcore_fops=
{
	open:	soundcore_open,
};

static struct sound_unit *__look_for_unit(int chain, int unit)
{
	struct sound_unit *s;
	
	s=chains[chain];
	while(s && s->unit_minor <= unit)
	{
		if(s->unit_minor==unit)
			return s;
		s=s->next;
	}
	return NULL;
}

int soundcore_open(struct inode *inode, struct file *file)
{
	int chain;
	int unit=MINOR(inode->i_rdev);
	struct sound_unit *s;

	chain=unit&0x0F;
	if(chain==4 || chain==5)	/* dsp/audio/dsp16 */
	{
		unit&=0xF0;
		unit|=3;
		chain=3;
	}
	
	spin_lock(&sound_loader_lock);
	s = __look_for_unit(chain, unit);
	if (s == NULL) {
		char mod[32];
	
		spin_unlock(&sound_loader_lock);
		/*
		 *  Please, don't change this order or code.
		 *  For ALSA slot means soundcard and OSS emulation code
		 *  comes as add-on modules which aren't depend on
		 *  ALSA toplevel modules for soundcards, thus we need
		 *  load them at first.	  [Jaroslav Kysela <perex@jcu.cz>]
		 */
		sprintf(mod, "sound-slot-%i", unit>>4);
		request_module(mod);
		sprintf(mod, "sound-service-%i-%i", unit>>4, chain);
		request_module(mod);
		spin_lock(&sound_loader_lock);
		s = __look_for_unit(chain, unit);
	}
	if (s) {
		file->f_op=s->unit_fops;
		spin_unlock(&sound_loader_lock);
		if(file->f_op->open)
			return file->f_op->open(inode,file);
		else
			return 0;
	}
	spin_unlock(&sound_loader_lock);
	return -ENODEV;
}

extern int mod_firmware_load(const char *, char **);
EXPORT_SYMBOL(mod_firmware_load);

#ifdef MODULE

MODULE_DESCRIPTION("Core sound module");
MODULE_AUTHOR("Alan Cox");

void cleanup_module(void)
{
	/* We have nothing to really do here - we know the lists must be
	   empty */
	devfs_unregister_chrdev(SOUND_MAJOR, "sound");
	devfs_unregister (devfs_handle);
}

int init_module(void)
#else
int soundcore_init(void)
#endif
{
	if(devfs_register_chrdev(SOUND_MAJOR, "sound", &soundcore_fops)==-1)
	{
		printk(KERN_ERR "soundcore: sound device already in use.\n");
		return -EBUSY;
	}
	devfs_handle = devfs_mk_dir (NULL, "sound", 0, NULL);
	/*
	 *	Now init non OSS drivers
	 */
#ifdef CONFIG_SOUND_CMPCI
	init_cmpci();
#endif
#ifdef CONFIG_SOUND_MSNDCLAS
	msnd_classic_init();
#endif
#ifdef CONFIG_SOUND_MSNDPIN
	msnd_pinnacle_init();
#endif
#ifdef CONFIG_SOUND_VWSND
	init_vwsnd();
#endif
	return 0;
}
