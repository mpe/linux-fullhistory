/*
 * sound/sound_switch.c
 *
 * The system call switch handler
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

static int      in_use = 0;	/* Total # of open devices */
unsigned long   seq_time = 0;	/* Time for /dev/sequencer */

/*
 * Table for configurable mixer volume handling
 */
static mixer_vol_table mixer_vols[MAX_MIXER_DEV];
static int      num_mixer_volumes = 0;

/*
 * /dev/sndstatus -device
 */
static char    *status_buf = NULL;
static int      status_len, status_ptr;
static int      status_busy = 0;

int
               *
load_mixer_volumes(char *name, int *levels, int present)
{
	int             i, n;

	for (i = 0; i < num_mixer_volumes; i++)
		if (strcmp(name, mixer_vols[i].name) == 0)
		  {
			  if (present)
				  mixer_vols[i].num = i;
			  return mixer_vols[i].levels;
		  }
	if (num_mixer_volumes >= MAX_MIXER_DEV)
	  {
		  printk("Sound: Too many mixers (%s)\n", name);
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

static int
set_mixer_levels(caddr_t arg)
{
	mixer_vol_table *buf = NULL;
	int             err = 0;

	if ((buf = (mixer_vol_table *) vmalloc(sizeof(mixer_vol_table))) == NULL)
		return -ENOSPC;

	memcpy((char *) buf, (&((char *) arg)[0]), sizeof(*buf));

	load_mixer_volumes(buf->name, buf->levels, 0);

	memcpy((&((char *) arg)[0]), (char *) buf, sizeof(*buf));
	vfree(buf);

	return err;
}

static int
get_mixer_levels(caddr_t arg)
{
	mixer_vol_table *buf = NULL;
	int             n, err = 0;

	if ((buf = (mixer_vol_table *) vmalloc(sizeof(mixer_vol_table))) == NULL)
		return -ENOSPC;

	memcpy((char *) buf, (&((char *) arg)[0]), sizeof(*buf));

	n = buf->num;
	if (n < 0 || n >= num_mixer_volumes)
		err = -EINVAL;
	else
	  {
		  memcpy((char *) buf, (char *) &mixer_vols[n], sizeof(*buf));
	  }

	memcpy((&((char *) arg)[0]), (char *) buf, sizeof(*buf));
	vfree(buf);

	return err;
}

static int
put_status(char *s)
{
	int             l = strlen(s);

	if (status_len + l >= 4000)
		return 0;

	memcpy(&status_buf[status_len], s, l);
	status_len += l;

	return 1;
}

static int
put_status_int(unsigned int val, int radix)
{
	int             l, v;

	static char     hx[] = "0123456789abcdef";
	char            buf[11];

	if (!val)
		return put_status("0");

	l = 0;
	buf[10] = 0;

	while (val)
	  {
		  v = val % radix;
		  val = val / radix;

		  buf[9 - l] = hx[v];
		  l++;
	  }

	if (status_len + l >= 4000)
		return 0;

	memcpy(&status_buf[status_len], &buf[10 - l], l);
	status_len += l;

	return 1;
}

static void
init_status(void)
{
	/*
	 * Write the status information to the status_buf and update status_len.
	 * There is a limit of 4000 bytes for the data.
	 */

	int             i;

	status_ptr = 0;

#ifdef SOUND_UNAME_A
	put_status("OSS/Free" SOUND_VERSION_STRING
		   " (" SOUND_CONFIG_DATE " " SOUND_CONFIG_BY ",\n"
		   SOUND_UNAME_A ")"
		   "\n");
#else
	put_status("OSS/Free:" SOUND_VERSION_STRING
#if 0
		   " (" SOUND_CONFIG_DATE " " SOUND_CONFIG_BY "@"
		   SOUND_CONFIG_HOST "." SOUND_CONFIG_DOMAIN ")"
#endif
		   "\n");
#endif

#ifdef MODULE
	put_status("Load type: Driver loaded as a module.\n");
#else
	put_status("Load type: Driver compiled into kernel\n");
#endif
	put_status("Kernel: ");
	put_status(system_utsname.sysname);
	put_status(" ");
	put_status(system_utsname.nodename);
	put_status(" ");
	put_status(system_utsname.release);
	put_status(" ");
	put_status(system_utsname.version);
	put_status(" ");
	put_status(system_utsname.machine);
	put_status("\n");


	if (!put_status("Config options: "))
		return;
	if (!put_status_int(SELECTED_SOUND_OPTIONS, 16))
		return;

	if (!put_status("\n\nInstalled drivers: \n"))
		return;

	for (i = 0; i < num_sound_drivers; i++)
		if (sound_drivers[i].card_type != 0)
		  {
			  if (!put_status("Type "))
				  return;
			  if (!put_status_int(sound_drivers[i].card_type, 10))
				  return;
			  if (!put_status(": "))
				  return;
			  if (!put_status(sound_drivers[i].name))
				  return;

			  if (!put_status("\n"))
				  return;
		  }
	if (!put_status("\nCard config: \n"))
		return;

	for (i = 0; i < num_sound_cards; i++)
		if (snd_installed_cards[i].card_type != 0)
		  {
			  int             drv, tmp;

			  if (!snd_installed_cards[i].enabled)
				  if (!put_status("("))
					  return;

			  /*
			   * if (!put_status_int(snd_installed_cards[i].card_type, 10)) return;
			   * if (!put_status (": ")) return;
			   */

			  if ((drv = snd_find_driver(snd_installed_cards[i].card_type)) != -1)
				  if (!put_status(sound_drivers[drv].name))
					  return;

			  if (snd_installed_cards[i].config.io_base)
			    {
				    if (!put_status(" at 0x"))
					    return;
				    if (!put_status_int(snd_installed_cards[i].config.io_base, 16))
					    return;
			    }
			  tmp = snd_installed_cards[i].config.irq;
			  if (tmp != 0)
			    {
				    if (!put_status(" irq "))
					    return;
				    if (tmp < 0)
					    tmp = -tmp;
				    if (!put_status_int(tmp, 10))
					    return;
			    }
			  if (snd_installed_cards[i].config.dma != -1)
			    {
				    if (!put_status(" drq "))
					    return;
				    if (!put_status_int(snd_installed_cards[i].config.dma, 10))
					    return;
				    if (snd_installed_cards[i].config.dma2 != -1)
				      {
					      if (!put_status(","))
						      return;
					      if (!put_status_int(snd_installed_cards[i].config.dma2, 10))
						      return;
				      }
			    }
			  if (!snd_installed_cards[i].enabled)
				  if (!put_status(")"))
					  return;

			  if (!put_status("\n"))
				  return;
		  }
	if (!sound_started)
	  {
		  put_status("\n\n***** Sound driver not started *****\n\n");
		  return;
	  }
#ifndef CONFIG_AUDIO
	if (!put_status("\nAudio devices: NOT ENABLED IN CONFIG\n"))
		return;
#else
	if (!put_status("\nAudio devices:\n"))
		return;

	for (i = 0; i < num_audiodevs; i++)
	  {
		  if (audio_devs[i] == NULL)
			  continue;
		  if (!put_status_int(i, 10))
			  return;
		  if (!put_status(": "))
			  return;
		  if (!put_status(audio_devs[i]->name))
			  return;

		  if (audio_devs[i]->flags & DMA_DUPLEX)
			  if (!put_status(" (DUPLEX)"))
				  return;

		  if (!put_status("\n"))
			  return;
	  }
#endif

#ifndef CONFIG_SEQUENCER
	if (!put_status("\nSynth devices: NOT ENABLED IN CONFIG\n"))
		return;
#else
	if (!put_status("\nSynth devices:\n"))
		return;

	for (i = 0; i < num_synths; i++)
	  {
		  if (synth_devs[i] == NULL)
			  continue;
		  if (!put_status_int(i, 10))
			  return;
		  if (!put_status(": "))
			  return;
		  if (!put_status(synth_devs[i]->info->name))
			  return;
		  if (!put_status("\n"))
			  return;
	  }
#endif

#ifndef CONFIG_MIDI
	if (!put_status("\nMidi devices: NOT ENABLED IN CONFIG\n"))
		return;
#else
	if (!put_status("\nMidi devices:\n"))
		return;

	for (i = 0; i < num_midis; i++)
	  {
		  if (midi_devs[i] == NULL)
			  continue;
		  if (!put_status_int(i, 10))
			  return;
		  if (!put_status(": "))
			  return;
		  if (!put_status(midi_devs[i]->info.name))
			  return;
		  if (!put_status("\n"))
			  return;
	  }
#endif

#ifdef CONFIG_SEQUENCER
	if (!put_status("\nTimers:\n"))
		return;

	for (i = 0; i < num_sound_timers; i++)
	  {
		  if (sound_timer_devs[i] == NULL)
			  continue;
		  if (!put_status_int(i, 10))
			  return;
		  if (!put_status(": "))
			  return;
		  if (!put_status(sound_timer_devs[i]->info.name))
			  return;
		  if (!put_status("\n"))
			  return;
	  }
#endif

	if (!put_status("\nMixers:\n"))
		return;

	for (i = 0; i < num_mixers; i++)
	  {
		  if (mixer_devs[i] == NULL)
			  continue;
		  if (!put_status_int(i, 10))
			  return;
		  if (!put_status(": "))
			  return;
		  if (!put_status(mixer_devs[i]->name))
			  return;
		  if (!put_status("\n"))
			  return;
	  }
}

static int
read_status(char *buf, int count)
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

	{
		char           *fixit = &status_buf[status_ptr];

		copy_to_user(&(buf)[0], fixit, l);
	};
	status_ptr += l;

	return l;
}

int
sound_read_sw(int dev, struct fileinfo *file, char *buf, int count)
{
	DEB(printk("sound_read_sw(dev=%d, count=%d)\n", dev, count));

	switch (dev & 0x0f)
	  {
	  case SND_DEV_STATUS:
		  return read_status(buf, count);
		  break;

#ifdef CONFIG_AUDIO
	  case SND_DEV_DSP:
	  case SND_DEV_DSP16:
	  case SND_DEV_AUDIO:
		  return audio_read(dev, file, buf, count);
		  break;
#endif

#ifdef CONFIG_SEQUENCER
	  case SND_DEV_SEQ:
	  case SND_DEV_SEQ2:
		  return sequencer_read(dev, file, buf, count);
		  break;
#endif

#ifdef CONFIG_MIDI
	  case SND_DEV_MIDIN:
		  return MIDIbuf_read(dev, file, buf, count);
#endif

	  default:;
	  }

	return -EINVAL;
}

int
sound_write_sw(int dev, struct fileinfo *file, const char *buf, int count)
{

	DEB(printk("sound_write_sw(dev=%d, count=%d)\n", dev, count));


	switch (dev & 0x0f)
	  {

#ifdef CONFIG_SEQUENCER
	  case SND_DEV_SEQ:
	  case SND_DEV_SEQ2:
		  return sequencer_write(dev, file, buf, count);
		  break;
#endif

#ifdef CONFIG_AUDIO
	  case SND_DEV_DSP:
	  case SND_DEV_DSP16:
	  case SND_DEV_AUDIO:
		  return audio_write(dev, file, buf, count);
		  break;
#endif

#ifdef CONFIG_MIDI
	  case SND_DEV_MIDIN:
		  return MIDIbuf_write(dev, file, buf, count);
#endif

	  }

	return -EINVAL;
}

int
sound_open_sw(int dev, struct fileinfo *file)
{
	int             retval;

	DEB(printk("sound_open_sw(dev=%d)\n", dev));

	if ((dev >= SND_NDEVS) || (dev < 0))
	  {
		  printk("Invalid minor device %d\n", dev);
		  return -ENXIO;
	  }
	switch (dev & 0x0f)
	  {
	  case SND_DEV_STATUS:
		  if (status_busy)
			  return -EBUSY;
		  status_busy = 1;
		  if ((status_buf = (char *) vmalloc(4000)) == NULL)
		    {
			    status_busy = 0;
			    return -EIO;
		    }
		  status_len = status_ptr = 0;
		  init_status();
		  break;

	  case SND_DEV_CTL:
		  if ((dev & 0xf0) && ((dev & 0xf0) >> 4) >= num_mixers)
			  return -ENXIO;
		  return 0;
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
		  printk("Invalid minor device %d\n", dev);
		  return -ENXIO;
	  }

	in_use++;

	return 0;
}

void
sound_release_sw(int dev, struct fileinfo *file)
{

	DEB(printk("sound_release_sw(dev=%d)\n", dev));

	switch (dev & 0x0f)
	  {
	  case SND_DEV_STATUS:
		  if (status_buf)
			  vfree(status_buf);
		  status_buf = NULL;
		  status_busy = 0;
		  break;

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
		  printk("Sound error: Releasing unknown device 0x%02x\n", dev);
	  }
	in_use--;
}

static int
get_mixer_info(int dev, caddr_t arg)
{
	mixer_info      info;
	int             i;

	if (dev < 0 || dev >= num_mixers)
		return -ENXIO;

	strcpy(info.id, mixer_devs[dev]->id);
	for (i = 0; i < 32 && mixer_devs[dev]->name; i++)
		info.name[i] = mixer_devs[dev]->name[i];
	info.name[i] = 0;
	info.modify_counter = mixer_devs[dev]->modify_counter;

	memcpy((&((char *) arg)[0]), (char *) &info, sizeof(info));
	return 0;
}

static int
get_old_mixer_info(int dev, caddr_t arg)
{
	_old_mixer_info info;
	int             i;

	if (dev < 0 || dev >= num_mixers)
		return -ENXIO;

	strcpy(info.id, mixer_devs[dev]->id);
	for (i = 0; i < 32 && mixer_devs[dev]->name; i++)
		info.name[i] = mixer_devs[dev]->name[i];
	info.name[i] = 0;

	memcpy((&((char *) arg)[0]), (char *) &info, sizeof(info));
	return 0;
}

static int
sound_mixer_ioctl(int mixdev,
		  unsigned int cmd, caddr_t arg)
{
	if (cmd == SOUND_MIXER_INFO)
		return get_mixer_info(mixdev, arg);
	if (cmd == SOUND_OLD_MIXER_INFO)
		return get_old_mixer_info(mixdev, arg);

	if (_SIOC_DIR(cmd) & _SIOC_WRITE)
		mixer_devs[mixdev]->modify_counter++;

	return mixer_devs[mixdev]->ioctl(mixdev, cmd, arg);
}

int
sound_ioctl_sw(int dev, struct fileinfo *file,
	       unsigned int cmd, caddr_t arg)
{
	DEB(printk("sound_ioctl_sw(dev=%d, cmd=0x%x, arg=0x%x)\n", dev, cmd, arg));

	if (cmd == OSS_GETVERSION)
		return (*(int *) arg = SOUND_VERSION);


	if (((cmd >> 8) & 0xff) == 'M' && num_mixers > 0)	/* Mixer ioctl */
		if ((dev & 0x0f) != SND_DEV_CTL)
		  {
			  int             dtype = dev & 0x0f;
			  int             mixdev;

			  switch (dtype)
			    {
#ifdef CONFIG_AUDIO
			    case SND_DEV_DSP:
			    case SND_DEV_DSP16:
			    case SND_DEV_AUDIO:
				    mixdev = audio_devs[dev >> 4]->mixer_dev;
				    if (mixdev < 0 || mixdev >= num_mixers)
					    return -ENXIO;
				    return sound_mixer_ioctl(mixdev, cmd, arg);
				    break;
#endif

			    default:
				    return sound_mixer_ioctl(dev, cmd, arg);
			    }
		  }
	switch (dev & 0x0f)
	  {

	  case SND_DEV_CTL:
		  if (cmd == SOUND_MIXER_GETLEVELS)
			  return get_mixer_levels(arg);
		  if (cmd == SOUND_MIXER_SETLEVELS)
			  return set_mixer_levels(arg);

		  if (!num_mixers)
			  return -ENXIO;

		  dev = dev >> 4;

		  if (dev >= num_mixers)
			  return -ENXIO;

		  return sound_mixer_ioctl(dev, cmd, arg);
		  break;

#ifdef CONFIG_SEQUENCER
	  case SND_DEV_SEQ:
	  case SND_DEV_SEQ2:
		  return sequencer_ioctl(dev, file, cmd, arg);
		  break;
#endif

#ifdef CONFIG_AUDIO
	  case SND_DEV_DSP:
	  case SND_DEV_DSP16:
	  case SND_DEV_AUDIO:
		  return audio_ioctl(dev, file, cmd, arg);
		  break;
#endif

#ifdef CONFIG_MIDI
	  case SND_DEV_MIDIN:
		  return MIDIbuf_ioctl(dev, file, cmd, arg);
		  break;
#endif

	  }

	return -EINVAL;
}
