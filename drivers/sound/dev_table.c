/*
 * sound/dev_table.c
 *
 * Device call tables.
 */

/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */

#include <linux/config.h>


#define _DEV_TABLE_C_
#include "sound_config.h"

int softoss_dev = 0;
int sound_started = 0;
int sndtable_get_cardcount(void);

int snd_find_driver(int type)
{
	int i, n = num_sound_drivers;

	for (i = 0; i < n; i++)
		if (sound_drivers[i].card_type == type)
			return i;

	return -1;
}

static void start_services(void)
{
#ifdef FIXME
	int soundcards_installed;

	if (!(soundcards_installed = sndtable_get_cardcount()))
		return;		/* No cards detected */
#endif

#ifdef CONFIG_AUDIO
	if (num_audiodevs)	/* Audio devices present */
	{
		int             dev;
		for (dev = 0; dev < num_audiodevs; dev++)
		{
		}
		audio_init_devices();
	  }
#endif

	return;
}

static void
start_cards(void)
{
	int i, n = num_sound_cards;
	int drv;

	sound_started = 1;
	if (trace_init)
		printk(KERN_DEBUG "Sound initialization started\n");

#ifdef CONFIG_LOWLEVEL_SOUND
	{
		extern void sound_preinit_lowlevel_drivers(void);
		sound_preinit_lowlevel_drivers();
	}
#endif

/*
 * Check the number of cards actually defined in the table
 */

	for (i = 0; i < n && snd_installed_cards[i].card_type; i++)
		num_sound_cards = i + 1;

	for (i = 0; i < n && snd_installed_cards[i].card_type; i++)
	{
		if (snd_installed_cards[i].enabled)
		{
			snd_installed_cards[i].for_driver_use = NULL;

			if ((drv = snd_find_driver(snd_installed_cards[i].card_type)) == -1)
			{
				snd_installed_cards[i].enabled = 0;	/*
									 * Mark as not detected
									 */
				continue;
			}
			snd_installed_cards[i].config.card_subtype =
					sound_drivers[drv].card_subtype;

			if (sound_drivers[drv].probe(&snd_installed_cards[i].config))
				sound_drivers[drv].attach(&snd_installed_cards[i].config);
			else
				snd_installed_cards[i].enabled = 0;	/*
									 * Mark as not detected
									 */
		}
	}
#ifdef CONFIG_LOWLEVEL_SOUND
	{
		extern void     sound_init_lowlevel_drivers(void);
		sound_init_lowlevel_drivers();
	}
#endif
	if (trace_init)
		printk(KERN_DEBUG "Sound initialization complete\n");
}

void sndtable_init(void)
{
	start_cards();
}


void sound_unload_drivers(void)
{
	int i, n = num_sound_cards;
	int drv;

	if (!sound_started)
		return;

	if (trace_init)
		printk(KERN_DEBUG "Sound unload started\n");


	for (i = 0; i < n && snd_installed_cards[i].card_type; i++)
	{
		if (snd_installed_cards[i].enabled)
		{
			if ((drv = snd_find_driver(snd_installed_cards[i].card_type)) != -1)
			{
				if (sound_drivers[drv].unload)
				{
					sound_drivers[drv].unload(&snd_installed_cards[i].config);
					snd_installed_cards[i].enabled = 0;
				}
			}
		}
	}

        for (i=0;i<num_audiodevs;i++)
		DMAbuf_deinit(i);

	if (trace_init)
		printk(KERN_DEBUG "Sound unload complete\n");
}

void sound_unload_driver(int type)
{
	int i, drv = -1, n = num_sound_cards;


	DEB(printk("unload driver %d: ", type));

	for (i = 0; i < n && snd_installed_cards[i].card_type; i++)
	{
		if (snd_installed_cards[i].card_type == type)
		{
			if (snd_installed_cards[i].enabled)
			{
				if ((drv = snd_find_driver(type)) != -1)
				{
					DEB(printk(" card %d", i));
					if (sound_drivers[drv].unload)
					{
						sound_drivers[drv].unload(&snd_installed_cards[i].config);
						snd_installed_cards[i].enabled = 0;
					}
				}
			}
		}
	}
	DEB(printk("\n"));
}


int sndtable_probe(int unit, struct address_info *hw_config)
{
	int             sel = -1;

	DEB(printk(KERN_DEBUG "sndtable_probe(%d)\n", unit));

	if (!unit)
		return 1;


	if (sel == -1 && num_sound_cards < max_sound_cards)
	{
		int i;
		i = sel = (num_sound_cards++);
		snd_installed_cards[sel].card_type = unit;
		snd_installed_cards[sel].enabled = 1;
	}
	if (sel != -1)
	{
		int             drv;

		snd_installed_cards[sel].for_driver_use = NULL;
		snd_installed_cards[sel].config.io_base = hw_config->io_base;
		snd_installed_cards[sel].config.irq = hw_config->irq;
		snd_installed_cards[sel].config.dma = hw_config->dma;
		snd_installed_cards[sel].config.dma2 = hw_config->dma2;
		snd_installed_cards[sel].config.name = hw_config->name;
		snd_installed_cards[sel].config.always_detect = hw_config->always_detect;
		snd_installed_cards[sel].config.driver_use_1 = hw_config->driver_use_1;
		snd_installed_cards[sel].config.driver_use_2 = hw_config->driver_use_2;
		snd_installed_cards[sel].config.card_subtype = hw_config->card_subtype;

		if ((drv = snd_find_driver(snd_installed_cards[sel].card_type)) == -1)
		{
			snd_installed_cards[sel].enabled = 0;
			DEB(printk(KERN_DEBUG "Failed to find driver\n"));
			return 0;
		}
		DEB(printk(KERN_DEBUG "Driver name '%s'\n", sound_drivers[drv].name));

		hw_config->card_subtype = snd_installed_cards[sel].config.card_subtype = sound_drivers[drv].card_subtype;

		if (sound_drivers[drv].probe(hw_config))
		{
			DEB(printk(KERN_DEBUG "Hardware probed OK\n"));
			return 1;
		}
		DEB(printk("Failed to find hardware\n"));
		snd_installed_cards[sel].enabled = 0;		/*
								 * Mark as not detected
								 */
		return 0;
	}
	return 0;
}


int sndtable_init_card(int unit, struct address_info *hw_config)
{
	int i, n = num_sound_cards;

	DEB(printk("sndtable_init_card(%d) entered\n", unit));

	if (!unit)
	{
		sndtable_init();
		return 1;
	}
	for (i = 0; i < n && snd_installed_cards[i].card_type; i++)
	{
		if (snd_installed_cards[i].card_type == unit)
		{
			int drv;

			snd_installed_cards[i].config.io_base = hw_config->io_base;
			snd_installed_cards[i].config.irq = hw_config->irq;
			snd_installed_cards[i].config.dma = hw_config->dma;
			snd_installed_cards[i].config.dma2 = hw_config->dma2;
			snd_installed_cards[i].config.name = hw_config->name;
			snd_installed_cards[i].config.always_detect = hw_config->always_detect;
			snd_installed_cards[i].config.driver_use_1 = hw_config->driver_use_1;
			snd_installed_cards[i].config.driver_use_2 = hw_config->driver_use_2;
			snd_installed_cards[i].config.card_subtype = hw_config->card_subtype;

			if ((drv = snd_find_driver(snd_installed_cards[i].card_type)) == -1)
				snd_installed_cards[i].enabled = 0;	/*
									 * Mark as not detected
									 */
			else
			{
				DEB(printk(KERN_DEBUG "Located card - calling attach routine\n"));
				sound_drivers[drv].attach(hw_config);

				DEB(printk("attach routine finished\n"));
			}
			start_services();
			return 1;
		}
	}
	DEB(printk("sndtable_init_card: No card defined with type=%d, num cards: %d\n", unit, num_sound_cards));
	return 0;
}

int sndtable_get_cardcount(void)
{
	return num_audiodevs + num_mixers + num_synths + num_midis;
}

int sndtable_identify_card(char *name)
{
	int i, n = num_sound_drivers;

	if (name == NULL)
		return 0;

	for (i = 0; i < n; i++)
	{
		if (sound_drivers[i].driver_id != NULL)
		{
			char *id = sound_drivers[i].driver_id;
			int j;

			for (j = 0; j < 80 && name[j] == id[j]; j++)
				if (id[j] == 0 && name[j] == 0)	/* Match */
					return sound_drivers[i].card_type;
		}
	}
	return 0;
}

void sound_setup(char *str, int *ints)
{
	int i, n = num_sound_cards;

	/*
	 *	First disable all drivers
	 */

	for (i = 0; i < n && snd_installed_cards[i].card_type; i++)
		snd_installed_cards[i].enabled = 0;

	if (ints[0] == 0 || ints[1] == 0)
		return;
	/*
	 *	Then enable them one by time
	 */

	for (i = 1; i <= ints[0]; i++)
	{
		int card_type, ioaddr, irq, dma, dma2, ptr, j;
		unsigned int val;

		val = (unsigned int) ints[i];
		card_type = (val & 0x0ff00000) >> 20;

		if (card_type > 127)
		{
			/*
			 * Add any future extensions here
			 */
			return;
		}
		ioaddr = (val & 0x000fff00) >> 8;
		irq = (val & 0x000000f0) >> 4;
		dma = (val & 0x0000000f);
		dma2 = (val & 0xf0000000) >> 28;

		ptr = -1;
		for (j = 0; j < n && ptr == -1; j++)
		{
			if (snd_installed_cards[j].card_type == card_type &&
				!snd_installed_cards[j].enabled)/*
								 * Not already found
								 */
				ptr = j;
		}

		if (ptr == -1)
			printk(KERN_ERR "Sound: Invalid setup parameter 0x%08x\n", val);
		else
		{
			snd_installed_cards[ptr].enabled = 1;
			snd_installed_cards[ptr].config.io_base = ioaddr;
			snd_installed_cards[ptr].config.irq = irq;
			snd_installed_cards[ptr].config.dma = dma;
			snd_installed_cards[ptr].config.dma2 = dma2;
			snd_installed_cards[ptr].config.name = NULL;
			snd_installed_cards[ptr].config.always_detect = 0;
			snd_installed_cards[ptr].config.driver_use_1 = 0;
			snd_installed_cards[ptr].config.driver_use_2 = 0;
			snd_installed_cards[ptr].config.card_subtype = 0;
		}
	}
}


struct address_info * sound_getconf(int card_type)
{
	int j, ptr;
	int n = num_sound_cards;

	ptr = -1;
	for (j = 0; j < n && ptr == -1 && snd_installed_cards[j].card_type; j++)
	{
		if (snd_installed_cards[j].card_type == card_type)
			ptr = j;
	}
	if (ptr == -1)
		return (struct address_info *) NULL;

	return &snd_installed_cards[ptr].config;
}



int sound_install_audiodrv(int vers, char *name, struct audio_driver *driver,
			int driver_size, int flags, unsigned int format_mask,
			void *devc, int dma1, int dma2)
{
#ifdef CONFIG_AUDIO
	struct audio_driver *d;
	struct audio_operations *op;
	int l, num;

	if (vers != AUDIO_DRIVER_VERSION || driver_size > sizeof(struct audio_driver))
	{
		printk(KERN_ERR "Sound: Incompatible audio driver for %s\n", name);
		return -(EINVAL);
	}
	num = sound_alloc_audiodev();

	if (num == -1)
	{
		printk(KERN_ERR "sound: Too many audio drivers\n");
		return -(EBUSY);
	}
	d = (struct audio_driver *) (sound_mem_blocks[sound_nblocks] = vmalloc(sizeof(struct audio_driver)));

	if (sound_nblocks < 1024)
		sound_nblocks++;

	op = (struct audio_operations *) (sound_mem_blocks[sound_nblocks] = vmalloc(sizeof(struct audio_operations)));

	if (sound_nblocks < 1024)
		sound_nblocks++;
	if (d == NULL || op == NULL)
	{
		printk(KERN_ERR "Sound: Can't allocate driver for (%s)\n", name);
		sound_unload_audiodev(num);
		return -(ENOMEM);
	}
	memset((char *) op, 0, sizeof(struct audio_operations));
	init_waitqueue(&op->in_sleeper);
	init_waitqueue(&op->out_sleeper);	
	if (driver_size < sizeof(struct audio_driver))
		memset((char *) d, 0, sizeof(struct audio_driver));

	memcpy((char *) d, (char *) driver, driver_size);

	op->d = d;
	l = strlen(name) + 1;
	if (l > sizeof(op->name))
		l = sizeof(op->name);
	strncpy(op->name, name, l);
	op->name[l - 1] = 0;
	op->flags = flags;
	op->format_mask = format_mask;
	op->devc = devc;

	/*
	 *    Hardcoded defaults
	 */
	audio_devs[num] = op;

	DMAbuf_init(num, dma1, dma2);

	audio_init_devices();
	return num;
#else
	return -EINVAL;
#endif
}

int sound_install_mixer(int vers, char *name, struct mixer_operations *driver,
	int driver_size, void *devc)
{
	struct mixer_operations *op;
	int l;

	int n = sound_alloc_mixerdev();

	if (n == -1)
	{
		printk(KERN_ERR "Sound: Too many mixer drivers\n");
		return -EBUSY;
	}
	if (vers != MIXER_DRIVER_VERSION ||
		driver_size > sizeof(struct mixer_operations))
	{
		printk(KERN_ERR "Sound: Incompatible mixer driver for %s\n", name);
		return -EINVAL;
	}
	
	/* FIXME: This leaks a mixer_operations struct every time its called
	   until you unload sound! */
	   
	op = (struct mixer_operations *) (sound_mem_blocks[sound_nblocks] = vmalloc(sizeof(struct mixer_operations)));

	if (sound_nblocks < 1024)
		sound_nblocks++;
	if (op == NULL)
	{
		printk(KERN_ERR "Sound: Can't allocate mixer driver for (%s)\n", name);
		return -ENOMEM;
	}
	memset((char *) op, 0, sizeof(struct mixer_operations));
	memcpy((char *) op, (char *) driver, driver_size);

	l = strlen(name) + 1;
	if (l > sizeof(op->name))
		l = sizeof(op->name);
	strncpy(op->name, name, l);
	op->name[l - 1] = 0;
	op->devc = devc;

	mixer_devs[n] = op;
	return n;
}

void sound_unload_audiodev(int dev)
{
	if (dev != -1)
	{
		DMAbuf_deinit(dev);
		audio_devs[dev] = NULL;
		unregister_sound_dsp((dev<<4)+3);
	}
}

int sound_alloc_audiodev(void)
{ 
	int i = register_sound_dsp(&oss_sound_fops, -1);
	if(i==-1)
		return i;
	i>>=4;
	if(i>=num_audiodevs)
		num_audiodevs = i + 1;
	return i;
}

int sound_alloc_mididev(void)
{
#ifdef CONFIG_MIDI
	int i = register_sound_midi(&oss_sound_fops, -1);
	if(i==-1)
		return i;
	i>>=4;
	if(i>=num_midis)
		num_midis = i + 1;
	return i;
#else
	return (-1);
#endif
}

int sound_alloc_synthdev(void)
{
	int i;

	for (i = 0; i < MAX_SYNTH_DEV; i++)
	{
		if (synth_devs[i] == NULL)
		{
			if (i >= num_synths)
				num_synths++;
			return i;
		}
	}
	return -1;
}

int sound_alloc_mixerdev(void)
{
	int i = register_sound_mixer(&oss_sound_fops, -1);
	if(i==-1)
		return -1;
	i>>=4;
	if(i>=num_mixers)
		num_mixers = i + 1;
	return i;
}

int sound_alloc_timerdev(void)
{
	int i;

	for (i = 0; i < MAX_TIMER_DEV; i++)
	{
		if (sound_timer_devs[i] == NULL)
		{
			if (i >= num_sound_timers)
				num_sound_timers++;
			return i;
		}
	}
	return -1;
}

void sound_unload_mixerdev(int dev)
{
	if (dev != -1)
	{
		mixer_devs[dev] = NULL;
		unregister_sound_mixer(dev<<4);
	}
}

void sound_unload_mididev(int dev)
{
#ifdef CONFIG_MIDI
	if (dev != -1)
	{
		midi_devs[dev] = NULL;
		unregister_sound_midi((dev<<4)+2);
	}
#endif
}

void sound_unload_synthdev(int dev)
{
	if (dev != -1)
		synth_devs[dev] = NULL;
}

void sound_unload_timerdev(int dev)
{
	if (dev != -1)
		sound_timer_devs[dev] = NULL;
}

