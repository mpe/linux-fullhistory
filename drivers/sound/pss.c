/*
 * sound/pss.c
 *
 * The low level driver for the Personal Sound System (ECHO ESC614).
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *
 * Thomas Sailer	ioctl code reworked (vmalloc/vfree removed)
 * Alan Cox		modularisation, clean up.
 *
 * 98-02-21: Vladimir Michl <vladimir.michl@upol.cz>
 *          Added mixer device for Beethoven ADSP-16 (master volume,
 *	    bass, treble, synth), only for speakers.
 *          Fixed bug in pss_write (exchange parameters)
 *          Fixed config port of SB
 *          Requested two regions for PSS (PSS mixer, PSS config)
 *          Modified pss_download_boot
 *          To probe_pss_mss added test for initialize AD1848
 * 98-05-28: Vladimir Michl <vladimir.michl@upol.cz>
 *          Fixed computation of mixer volumes
 */


#include <linux/config.h>
#include <linux/module.h>

#include "sound_config.h"
#include "sound_firmware.h"
#include "soundmodule.h"

#ifdef CONFIG_PSS
#ifdef CONFIG_AUDIO

/*
 * PSS registers.
 */
#define REG(x)	(devc->base+x)
#define	PSS_DATA	0
#define	PSS_STATUS	2
#define PSS_CONTROL	2
#define	PSS_ID		4
#define	PSS_IRQACK	4
#define	PSS_PIO		0x1a

/*
 * Config registers
 */
#define CONF_PSS	0x10
#define CONF_WSS	0x12
#define CONF_SB		0x14
#define CONF_CDROM	0x16
#define CONF_MIDI	0x18

/*
 * Status bits.
 */
#define PSS_FLAG3     0x0800
#define PSS_FLAG2     0x0400
#define PSS_FLAG1     0x1000
#define PSS_FLAG0     0x0800
#define PSS_WRITE_EMPTY  0x8000
#define PSS_READ_FULL    0x4000

/*
 * WSS registers
 */
#define WSS_INDEX 4
#define WSS_DATA 5

/*
 * WSS status bits
 */
#define WSS_INITIALIZING 0x80
#define WSS_AUTOCALIBRATION 0x20

#define NO_WSS_MIXER	-1

#include "coproc.h"

#ifdef PSS_HAVE_LD
#include "pss_boot.h"
#else
static int pss_synthLen = 0;
static unsigned char *pss_synth =
NULL;
#endif

/* If compiled into kernel, it enable or disable pss mixer */
#ifdef CONFIG_PSS_MIXER
static unsigned char pss_mixer = 1;
#else
static unsigned char pss_mixer = 0;
#endif


typedef struct pss_mixerdata {
	unsigned int volume_l;
	unsigned int volume_r;
	unsigned int bass;
	unsigned int treble;
	unsigned int synth;
} pss_mixerdata;

typedef struct pss_confdata {
	int             base;
	int             irq;
	int             dma;
	int            *osp;
	pss_mixerdata   mixer;
	int             ad_mixer_dev;
} pss_confdata;
  
static pss_confdata pss_data;
static pss_confdata *devc = &pss_data;

static int      pss_initialized = 0;
static int      nonstandard_microcode = 0;

static void pss_write(pss_confdata *devc, int data)
{
	int i, limit;

	limit = jiffies + HZ/10;	/* The timeout is 0.1 seconds */
	/*
	 * Note! the i<5000000 is an emergency exit. The dsp_command() is sometimes
	 * called while interrupts are disabled. This means that the timer is
	 * disabled also. However the timeout situation is a abnormal condition.
	 * Normally the DSP should be ready to accept commands after just couple of
	 * loops.
	 */

	for (i = 0; i < 5000000 && time_before(jiffies, limit); i++)
 	{
 		if (inw(REG(PSS_STATUS)) & PSS_WRITE_EMPTY)
 		{
 			outw(data, REG(PSS_DATA));
 			return;
 		}
 	}
 	printk(KERN_WARNING "PSS: DSP Command (%04x) Timeout.\n", data);
}

int probe_pss(struct address_info *hw_config)
{
	unsigned short id;
	int irq, dma;

	devc->base = hw_config->io_base;
	irq = devc->irq = hw_config->irq;
	dma = devc->dma = hw_config->dma;
	devc->osp = hw_config->osp;

	if (devc->base != 0x220 && devc->base != 0x240)
		if (devc->base != 0x230 && devc->base != 0x250)		/* Some cards use these */
			return 0;

	if (check_region(devc->base, 0x19 /*16*/)) { 
		printk(KERN_ERR "PSS: I/O port conflict\n");
		return 0;
	}
	id = inw(REG(PSS_ID));
	if ((id >> 8) != 'E') {
		printk(KERN_ERR "No PSS signature detected at 0x%x (0x%x)\n",  devc->base,  id); 
		return 0;
	}
	return 1;
}

static int set_irq(pss_confdata * devc, int dev, int irq)
{
	static unsigned short irq_bits[16] =
	{
		0x0000, 0x0000, 0x0000, 0x0008,
		0x0000, 0x0010, 0x0000, 0x0018,
		0x0000, 0x0020, 0x0028, 0x0030,
		0x0038, 0x0000, 0x0000, 0x0000
	};

	unsigned short  tmp, bits;

	if (irq < 0 || irq > 15)
		return 0;

	tmp = inw(REG(dev)) & ~0x38;	/* Load confreg, mask IRQ bits out */

	if ((bits = irq_bits[irq]) == 0 && irq != 0)
	{
		printk(KERN_ERR "PSS: Invalid IRQ %d\n", irq);
		return 0;
	}
	outw(tmp | bits, REG(dev));
	return 1;
}

static int set_io_base(pss_confdata * devc, int dev, int base)
{
	unsigned short  tmp = inw(REG(dev)) & 0x003f;
	unsigned short  bits = (base & 0x0ffc) << 4;

	outw(bits | tmp, REG(dev));

	return 1;
}

static int set_dma(pss_confdata * devc, int dev, int dma)
{
	static unsigned short dma_bits[8] =
	{
		0x0001, 0x0002, 0x0000, 0x0003,
		0x0000, 0x0005, 0x0006, 0x0007
	};

	unsigned short  tmp, bits;

	if (dma < 0 || dma > 7)
		return 0;

	tmp = inw(REG(dev)) & ~0x07;	/* Load confreg, mask DMA bits out */

	if ((bits = dma_bits[dma]) == 0 && dma != 4)
	{
		  printk(KERN_ERR "PSS: Invalid DMA %d\n", dma);
		  return 0;
	}
	outw(tmp | bits, REG(dev));
	return 1;
}

static int pss_reset_dsp(pss_confdata * devc)
{
	unsigned long   i, limit = jiffies + HZ/10;

	outw(0x2000, REG(PSS_CONTROL));
	for (i = 0; i < 32768 && (limit-jiffies >= 0); i++)
		inw(REG(PSS_CONTROL));
	outw(0x0000, REG(PSS_CONTROL));
	return 1;
}

static int pss_put_dspword(pss_confdata * devc, unsigned short word)
{
	int i, val;

	for (i = 0; i < 327680; i++)
	{
		val = inw(REG(PSS_STATUS));
		if (val & PSS_WRITE_EMPTY)
		{
			outw(word, REG(PSS_DATA));
			return 1;
		}
	}
	return 0;
}

static int pss_get_dspword(pss_confdata * devc, unsigned short *word)
{
	int i, val;

	for (i = 0; i < 327680; i++)
	{
		val = inw(REG(PSS_STATUS));
		if (val & PSS_READ_FULL)
		{
			*word = inw(REG(PSS_DATA));
			return 1;
		}
	}
	return 0;
}

static int pss_download_boot(pss_confdata * devc, unsigned char *block, int size, int flags)
{
	int i, limit, val, count;

	if (flags & CPF_FIRST)
	{
/*_____ Warn DSP software that a boot is coming */
		outw(0x00fe, REG(PSS_DATA));

		limit = jiffies + HZ/10;
		for (i = 0; i < 32768 && time_before(jiffies, limit); i++)
			if (inw(REG(PSS_DATA)) == 0x5500)
				break;

		outw(*block++, REG(PSS_DATA));
		pss_reset_dsp(devc);
	}
	count = 1;
	while ((flags&CPF_LAST) || count<size )
	{
		int j;

		for (j = 0; j < 327670; j++)
		{
/*_____ Wait for BG to appear */
			if (inw(REG(PSS_STATUS)) & PSS_FLAG3)
				break;
		}

		if (j == 327670)
		{
			/* It's ok we timed out when the file was empty */
			if (count >= size && flags & CPF_LAST)
				break;
			else
			{
				printk("\n");
				printk(KERN_ERR "PSS: Download timeout problems, byte %d=%d\n", count, size);
				return 0;
			}
		}
/*_____ Send the next byte */
		if (count >= size) 
		{
			/* If not data in block send 0xffff */
			outw (0xffff, REG (PSS_DATA));
		}
		else
		{
			/*_____ Send the next byte */
			outw (*block++, REG (PSS_DATA));
		};
		count++;
	}

	if (flags & CPF_LAST)
	{
/*_____ Why */
		outw(0, REG(PSS_DATA));

		limit = jiffies + HZ/10;
		for (i = 0; i < 32768 && (limit - jiffies >= 0); i++)
			val = inw(REG(PSS_STATUS));

		limit = jiffies + HZ/10;
		for (i = 0; i < 32768 && (limit-jiffies >= 0); i++)
		{
			val = inw(REG(PSS_STATUS));
			if (val & 0x4000)
				break;
		}

		/* now read the version */
		for (i = 0; i < 32000; i++)
		{
			val = inw(REG(PSS_STATUS));
			if (val & PSS_READ_FULL)
				break;
		}
		if (i == 32000)
			return 0;

		val = inw(REG(PSS_DATA));
		/* printk( "<PSS: microcode version %d.%d loaded>",  val/16,  val % 16); */
	}
	return 1;
}

/* Mixer */
static void set_master_volume(pss_confdata *devc, int left, int right)
{
	static unsigned char log_scale[101] =  {
		0xdb, 0xe0, 0xe3, 0xe5, 0xe7, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xed, 0xee,
		0xef, 0xef, 0xf0, 0xf0, 0xf1, 0xf1, 0xf2, 0xf2, 0xf2, 0xf3, 0xf3, 0xf3,
		0xf4, 0xf4, 0xf4, 0xf5, 0xf5, 0xf5, 0xf5, 0xf6, 0xf6, 0xf6, 0xf6, 0xf7,
		0xf7, 0xf7, 0xf7, 0xf7, 0xf8, 0xf8, 0xf8, 0xf8, 0xf8, 0xf9, 0xf9, 0xf9,
		0xf9, 0xf9, 0xf9, 0xfa, 0xfa, 0xfa, 0xfa, 0xfa, 0xfa, 0xfa, 0xfb, 0xfb,
		0xfb, 0xfb, 0xfb, 0xfb, 0xfb, 0xfb, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc, 0xfc,
		0xfc, 0xfc, 0xfc, 0xfc, 0xfd, 0xfd, 0xfd, 0xfd, 0xfd, 0xfd, 0xfd, 0xfd,
		0xfd, 0xfd, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe, 0xfe,
		0xfe, 0xfe, 0xff, 0xff, 0xff
	};
	pss_write(devc, 0x0010);
	pss_write(devc, log_scale[left] | 0x0000);
	pss_write(devc, 0x0010);
	pss_write(devc, log_scale[right] | 0x0100);
}

static void set_synth_volume(pss_confdata *devc, int volume)
{
	int vol = ((0x8000*volume)/100L);
	pss_write(devc, 0x0080);
	pss_write(devc, vol);
	pss_write(devc, 0x0081);
	pss_write(devc, vol);
}

static void set_bass(pss_confdata *devc, int level)
{
	int vol = (int)(((0xfd - 0xf0) * level)/100L) + 0xf0;
	pss_write(devc, 0x0010);
	pss_write(devc, vol | 0x0200);
};

static void set_treble(pss_confdata *devc, int level)
{	
	int vol = (((0xfd - 0xf0) * level)/100L) + 0xf0;
	pss_write(devc, 0x0010);
	pss_write(devc, vol | 0x0300);
};

static void pss_mixer_reset(pss_confdata *devc)
{
	set_master_volume(devc, 33, 33);
	set_bass(devc, 50);
	set_treble(devc, 50);
	set_synth_volume(devc, 30);
	pss_write (devc, 0x0010);
	pss_write (devc, 0x0800 | 0xce);	/* Stereo */
	
	if(pss_mixer)
	{
		devc->mixer.volume_l = devc->mixer.volume_r = 33;
		devc->mixer.bass = 50;
		devc->mixer.treble = 50;
		devc->mixer.synth = 30;
	}
}

static void arg_to_volume_mono(unsigned int volume, int *aleft)
{
	int left;
	
	left = volume & 0x00ff;
	if (left > 100)
		left = 100;
	*aleft = left;
}

static void arg_to_volume_stereo(unsigned int volume, int *aleft, int *aright)
{
	arg_to_volume_mono(volume, aleft);
	arg_to_volume_mono(volume >> 8, aright);
}

static int ret_vol_mono(int left)
{
	return ((left << 8) | left);
}

static int ret_vol_stereo(int left, int right)
{
	return ((right << 8) | left);
}

static int call_ad_mixer(pss_confdata *devc,unsigned int cmd, caddr_t arg)
{
	if (devc->ad_mixer_dev != NO_WSS_MIXER) 
		return mixer_devs[devc->ad_mixer_dev]->ioctl(devc->ad_mixer_dev, cmd, arg);
	else 
		return -EINVAL;
}

static int pss_mixer_ioctl (int dev, unsigned int cmd, caddr_t arg)
{
	pss_confdata *devc = mixer_devs[dev]->devc;
	int cmdf = cmd & 0xff;
	
	if ((cmdf != SOUND_MIXER_VOLUME) && (cmdf != SOUND_MIXER_BASS) &&
		(cmdf != SOUND_MIXER_TREBLE) && (cmdf != SOUND_MIXER_SYNTH) &&
		(cmdf != SOUND_MIXER_DEVMASK) && (cmdf != SOUND_MIXER_STEREODEVS) &&
		(cmdf != SOUND_MIXER_RECMASK) && (cmdf != SOUND_MIXER_CAPS) &&
		(cmdf != SOUND_MIXER_RECSRC)) 
	{
		return call_ad_mixer(devc, cmd, arg);
	}
	
	if (((cmd >> 8) & 0xff) != 'M')	
		return -EINVAL;
		
	if (_SIOC_DIR (cmd) & _SIOC_WRITE)
	{
		switch (cmdf)	
		{
			case SOUND_MIXER_RECSRC:
				if (devc->ad_mixer_dev != NO_WSS_MIXER)
					return call_ad_mixer(devc, cmd, arg);
				else
				{
					if (*(int *)arg != 0)
						return -EINVAL;
					return 0;
				}
			case SOUND_MIXER_VOLUME:
				arg_to_volume_stereo(*(unsigned int *)arg, &devc->mixer.volume_l,
					&devc->mixer.volume_r); 
				set_master_volume(devc, devc->mixer.volume_l,
					devc->mixer.volume_r);
				return ret_vol_stereo(devc->mixer.volume_l,
					devc->mixer.volume_r);
		  
			case SOUND_MIXER_BASS:
				arg_to_volume_mono(*(unsigned int *)arg,
					&devc->mixer.bass);
				set_bass(devc, devc->mixer.bass);
				return ret_vol_mono(devc->mixer.bass);
		  
			case SOUND_MIXER_TREBLE:
				arg_to_volume_mono(*(unsigned int *)arg,
					&devc->mixer.treble);
				set_treble(devc, devc->mixer.treble);
				return ret_vol_mono(devc->mixer.treble);
		  
			case SOUND_MIXER_SYNTH:
				arg_to_volume_mono(*(unsigned int *)arg,
					&devc->mixer.synth);
				set_synth_volume(devc, devc->mixer.synth);
				return ret_vol_mono(devc->mixer.synth);
		  
			default:
				return -EINVAL;
		}
	}
	else			
	{
		/*
		 * Return parameters
		 */
		switch (cmdf)
		{

			case SOUND_MIXER_DEVMASK:
				if (call_ad_mixer(devc, cmd, arg) == -EINVAL)
					*(int *)arg = 0; /* no mixer devices */
				return (*(int *)arg |= SOUND_MASK_VOLUME | SOUND_MASK_BASS | SOUND_MASK_TREBLE | SOUND_MASK_SYNTH);
		  
			case SOUND_MIXER_STEREODEVS:
				if (call_ad_mixer(devc, cmd, arg) == -EINVAL)
					*(int *)arg = 0; /* no stereo devices */
				return (*(int *)arg |= SOUND_MASK_VOLUME);
		  
			case SOUND_MIXER_RECMASK:
				if (devc->ad_mixer_dev != NO_WSS_MIXER)
					return call_ad_mixer(devc, cmd, arg);
				else
					return (*(int *)arg = 0); /* no record devices */

			case SOUND_MIXER_CAPS:
				if (devc->ad_mixer_dev != NO_WSS_MIXER)
					return call_ad_mixer(devc, cmd, arg);
				else
					return (*(int *)arg = SOUND_CAP_EXCL_INPUT);

			case SOUND_MIXER_RECSRC:
				if (devc->ad_mixer_dev != NO_WSS_MIXER)
					return call_ad_mixer(devc, cmd, arg);
				else
					return (*(int *)arg = 0); /* no record source */

			case SOUND_MIXER_VOLUME:
				return (*(int *)arg = ret_vol_stereo(devc->mixer.volume_l, devc->mixer.volume_r));
			  
			case SOUND_MIXER_BASS:
				return (*(int *)arg = ret_vol_mono(devc->mixer.bass));
			  
			case SOUND_MIXER_TREBLE:
				return (*(int *)arg = ret_vol_mono(devc->mixer.treble));
			  
			case SOUND_MIXER_SYNTH:
				return (*(int *)arg = ret_vol_mono(devc->mixer.synth));
			default:
				return -EINVAL;
		}
	}
}

static struct mixer_operations pss_mixer_operations =
{
	"SOUNDPORT",
	"PSS-AD1848",
	 pss_mixer_ioctl
};

void attach_pss(struct address_info *hw_config)
{
	unsigned short  id;
	char tmp[100];

	devc->base = hw_config->io_base;
	devc->irq = hw_config->irq;
	devc->dma = hw_config->dma;
	devc->osp = hw_config->osp;
	devc->ad_mixer_dev = NO_WSS_MIXER;

	if (!probe_pss(hw_config))
		return;

	request_region(hw_config->io_base, 0x10, "PSS mixer, SB emulation");
	request_region(hw_config->io_base + 0x10, 0x9, "PSS config");

	id = inw(REG(PSS_ID)) & 0x00ff;

	/*
	   * Disable all emulations. Will be enabled later (if required).
	 */
	outw(0x0000, REG(CONF_PSS));	/* 0x0400 enables joystick */
	outw(0x0000, REG(CONF_WSS));
	outw(0x0000, REG(CONF_SB));
	outw(0x0000, REG(CONF_MIDI));
	outw(0x0000, REG(CONF_CDROM));

#if YOU_REALLY_WANT_TO_ALLOCATE_THESE_RESOURCES
	if (sound_alloc_dma(hw_config->dma, "PSS"))
	{
		printk("pss.c: Can't allocate DMA channel.\n");
		return;
	}
	if (!set_irq(devc, CONF_PSS, devc->irq))
	{
		printk("PSS: IRQ allocation error.\n");
		return;
	}
	if (!set_dma(devc, CONF_PSS, devc->dma))
	{
		printk(KERN_ERR "PSS: DMA allocation error\n");
		return;
	}
#endif

	pss_initialized = 1;
	sprintf(tmp, "ECHO-PSS  Rev. %d", id);
	conf_printf(tmp, hw_config);
}

int probe_pss_mpu(struct address_info *hw_config)
{
	int timeout;

	if (!pss_initialized)
		return 0;

	if (check_region(hw_config->io_base, 2))
	{
		printk(KERN_ERR "PSS: MPU I/O port conflict\n");
		return 0;
	}
	if (!set_io_base(devc, CONF_MIDI, hw_config->io_base))
	{
		  printk(KERN_ERR "PSS: MIDI base could not be set.\n");
		  return 0;
	}
	if (!set_irq(devc, CONF_MIDI, hw_config->irq))
	{
		  printk(KERN_ERR "PSS: MIDI IRQ allocation error.\n");
		  return 0;
	}
	if (!pss_synthLen)
	{
		printk(KERN_ERR "PSS: Can't enable MPU. MIDI synth microcode not available.\n");
		return 0;
	}
	if (!pss_download_boot(devc, pss_synth, pss_synthLen, CPF_FIRST | CPF_LAST))
	{
		printk(KERN_ERR "PSS: Unable to load MIDI synth microcode to DSP.\n");
		return 0;
	}

	/*
	 * Finally wait until the DSP algorithm has initialized itself and
	 * deactivates receive interrupt.
	 */

	for (timeout = 900000; timeout > 0; timeout--)
	{
		if ((inb(hw_config->io_base + 1) & 0x80) == 0)	/* Input data avail */
			inb(hw_config->io_base);	/* Discard it */
		else
			break;	/* No more input */
	}

#if (defined(CONFIG_MPU401) || defined(CONFIG_MPU_EMU)) && defined(CONFIG_MIDI)
	return probe_mpu401(hw_config);
#else
	return 0;
#endif
}

static int pss_coproc_open(void *dev_info, int sub_device)
{
	switch (sub_device)
	{
		case COPR_MIDI:
			if (pss_synthLen == 0)
			{
				printk(KERN_ERR "PSS: MIDI synth microcode not available.\n");
				return -EIO;
			}
			if (nonstandard_microcode)
				if (!pss_download_boot(devc, pss_synth, pss_synthLen, CPF_FIRST | CPF_LAST))
			{
				printk(KERN_ERR "PSS: Unable to load MIDI synth microcode to DSP.\n");
				return -EIO;
			}
			nonstandard_microcode = 0;
			break;

		default:
	}
	return 0;
}

static void pss_coproc_close(void *dev_info, int sub_device)
{
	return;
}

static void pss_coproc_reset(void *dev_info)
{
	if (pss_synthLen)
		if (!pss_download_boot(devc, pss_synth, pss_synthLen, CPF_FIRST | CPF_LAST))
		{
			printk(KERN_ERR "PSS: Unable to load MIDI synth microcode to DSP.\n");
		}
	nonstandard_microcode = 0;
}

static int download_boot_block(void *dev_info, copr_buffer * buf)
{
	if (buf->len <= 0 || buf->len > sizeof(buf->data))
		return -EINVAL;

	if (!pss_download_boot(devc, buf->data, buf->len, buf->flags))
	{
		printk(KERN_ERR "PSS: Unable to load microcode block to DSP.\n");
		return -EIO;
	}
	nonstandard_microcode = 1;	/* The MIDI microcode has been overwritten */
	return 0;
}

static int pss_coproc_ioctl(void *dev_info, unsigned int cmd, caddr_t arg, int local)
{
	copr_buffer *buf;
	copr_msg *mbuf;
	copr_debug_buf dbuf;
	unsigned short tmp;
	unsigned long flags;
	unsigned short *data;
	int i, err;
	/* printk( "PSS coproc ioctl %x %x %d\n",  cmd,  arg,  local); */
	
	switch (cmd) 
	{
		case SNDCTL_COPR_RESET:
			pss_coproc_reset(dev_info);
			return 0;

		case SNDCTL_COPR_LOAD:
			buf = (copr_buffer *) vmalloc(sizeof(copr_buffer));
			if (buf == NULL)
				return -ENOSPC;
			if (copy_from_user(buf, arg, sizeof(copr_buffer))) {
				vfree(buf);
				return -EFAULT;
			}
			err = download_boot_block(dev_info, buf);
			vfree(buf);
			return err;
		
		case SNDCTL_COPR_SENDMSG:
			mbuf = (copr_msg *)vmalloc(sizeof(copr_msg));
			if (mbuf == NULL)
				return -ENOSPC;
			if (copy_from_user(mbuf, arg, sizeof(copr_msg))) {
				vfree(mbuf);
				return -EFAULT;
			}
			data = (unsigned short *)(mbuf->data);
			save_flags(flags);
			cli();
			for (i = 0; i < mbuf->len; i++) {
				if (!pss_put_dspword(devc, *data++)) {
					restore_flags(flags);
					mbuf->len = i;	/* feed back number of WORDs sent */
					err = copy_to_user(arg, mbuf, sizeof(copr_msg));
					vfree(mbuf);
					return err ? -EFAULT : -EIO;
				}
			}
			restore_flags(flags);
			vfree(mbuf);
			return 0;

		case SNDCTL_COPR_RCVMSG:
			err = 0;
			mbuf = (copr_msg *)vmalloc(sizeof(copr_msg));
			if (mbuf == NULL)
				return -ENOSPC;
			data = (unsigned short *)mbuf->data;
			save_flags(flags);
			cli();
			for (i = 0; i < sizeof(mbuf->data)/sizeof(unsigned short); i++) {
				mbuf->len = i;	/* feed back number of WORDs read */
				if (!pss_get_dspword(devc, data++)) {
					if (i == 0)
						err = -EIO;
					break;
				}
			}
			restore_flags(flags);
			if (copy_to_user(arg, mbuf, sizeof(copr_msg)))
				err = -EFAULT;
			vfree(mbuf);
			return err;
		
		case SNDCTL_COPR_RDATA:
			if (copy_from_user(&dbuf, arg, sizeof(dbuf)))
				return -EFAULT;
			save_flags(flags);
			cli();
			if (!pss_put_dspword(devc, 0x00d0)) {
				restore_flags(flags);
				return -EIO;
			}
			if (!pss_put_dspword(devc, (unsigned short)(dbuf.parm1 & 0xffff))) {
				restore_flags(flags);
				return -EIO;
			}
			if (!pss_get_dspword(devc, &tmp)) {
				restore_flags(flags);
				return -EIO;
			}
			dbuf.parm1 = tmp;
			restore_flags(flags);
			if (copy_to_user(arg, &dbuf, sizeof(dbuf)))
				return -EFAULT;
			return 0;
		
		case SNDCTL_COPR_WDATA:
			if (copy_from_user(&dbuf, arg, sizeof(dbuf)))
				return -EFAULT;
			save_flags(flags);
			cli();
			if (!pss_put_dspword(devc, 0x00d1)) {
				restore_flags(flags);
				return -EIO;
			}
			if (!pss_put_dspword(devc, (unsigned short) (dbuf.parm1 & 0xffff))) {
				restore_flags(flags);
				return -EIO;
			}
			tmp = (unsigned int)dbuf.parm2 & 0xffff;
			if (!pss_put_dspword(devc, tmp)) {
				restore_flags(flags);
				return -EIO;
			}
			restore_flags(flags);
			return 0;
		
		case SNDCTL_COPR_WCODE:
			if (copy_from_user(&dbuf, arg, sizeof(dbuf)))
				return -EFAULT;
			save_flags(flags);
			cli();
			if (!pss_put_dspword(devc, 0x00d3)) {
				restore_flags(flags);
				return -EIO;
			}
			if (!pss_put_dspword(devc, (unsigned short)(dbuf.parm1 & 0xffff))) {
				restore_flags(flags);
				return -EIO;
			}
			tmp = (unsigned int)dbuf.parm2 & 0x00ff;
			if (!pss_put_dspword(devc, tmp)) {
				restore_flags(flags);
				return -EIO;
			}
			tmp = ((unsigned int)dbuf.parm2 >> 8) & 0xffff;
			if (!pss_put_dspword(devc, tmp)) {
				restore_flags(flags);
				return -EIO;
			}
			restore_flags(flags);
			return 0;
		
		case SNDCTL_COPR_RCODE:
			if (copy_from_user(&dbuf, arg, sizeof(dbuf)))
				return -EFAULT;
			save_flags(flags);
			cli();
			if (!pss_put_dspword(devc, 0x00d2)) {
				restore_flags(flags);
				return -EIO;
			}
			if (!pss_put_dspword(devc, (unsigned short)(dbuf.parm1 & 0xffff))) {
				restore_flags(flags);
				return -EIO;
			}
			if (!pss_get_dspword(devc, &tmp)) { /* Read MSB */
				restore_flags(flags);
				return -EIO;
			}
			dbuf.parm1 = tmp << 8;
			if (!pss_get_dspword(devc, &tmp)) { /* Read LSB */
				restore_flags(flags);
				return -EIO;
			}
			dbuf.parm1 |= tmp & 0x00ff;
			restore_flags(flags);
			if (copy_to_user(arg, &dbuf, sizeof(dbuf)))
				return -EFAULT;
			return 0;

		default:
			return -EINVAL;
	}
	return -EINVAL;
}

static coproc_operations pss_coproc_operations =
{
	"ADSP-2115",
	pss_coproc_open,
	pss_coproc_close,
	pss_coproc_ioctl,
	pss_coproc_reset,
	&pss_data
};

void attach_pss_mpu(struct address_info *hw_config)
{
#if (defined(CONFIG_MPU401) || defined(CONFIG_MPU_EMU)) && defined(CONFIG_MIDI)
	attach_mpu401(hw_config);	/* Slot 1 */
	if (hw_config->slots[1] != -1)	/* The MPU driver installed itself */
		midi_devs[hw_config->slots[1]]->coproc = &pss_coproc_operations;
#endif
}

int probe_pss_mss(struct address_info *hw_config)
{
	volatile int timeout;

	if (!pss_initialized)
		return 0;

	if (check_region(hw_config->io_base, 8))
	{
		  printk(KERN_ERR "PSS: WSS I/O port conflicts.\n");
		  return 0;
	}
	if (!set_io_base(devc, CONF_WSS, hw_config->io_base))
	{
		printk("PSS: WSS base not settable.\n");
		return 0;
	}
	if (!set_irq(devc, CONF_WSS, hw_config->irq))
	{
		printk("PSS: WSS IRQ allocation error.\n");
		return 0;
	}
	if (!set_dma(devc, CONF_WSS, hw_config->dma))
	{
		printk(KERN_ERR "PSS: WSS DMA allocation error\n");
		return 0;
	}
	/*
	 * For some reason the card returns 0xff in the WSS status register
	 * immediately after boot. Probably MIDI+SB emulation algorithm
	 * downloaded to the ADSP2115 spends some time initializing the card.
	 * Let's try to wait until it finishes this task.
	 */
	for (timeout = 0; timeout < 100000 && (inb(hw_config->io_base + WSS_INDEX) & WSS_INITIALIZING); timeout++);

	outb((0x0b), hw_config->io_base + WSS_INDEX);	/* Required by some cards */

	for (timeout = 0; (inb(hw_config->io_base + WSS_DATA) & WSS_AUTOCALIBRATION) && (timeout < 100000); timeout++);

	return probe_ms_sound(hw_config);
}

void attach_pss_mss(struct address_info *hw_config)
{
	int        my_mix = -999;	/* gcc shut up */
	
	devc->ad_mixer_dev = NO_WSS_MIXER;
	if (pss_mixer) 
	{
		if ((my_mix = sound_install_mixer (MIXER_DRIVER_VERSION,
			"PSS-SPEAKERS and AD1848 (through MSS audio codec)",
			&pss_mixer_operations,
			sizeof (struct mixer_operations),
			devc)) < 0) 
		{
			printk(KERN_ERR "Could not install PSS mixer\n");
			return;
		}
	}
	pss_mixer_reset(devc);
	attach_ms_sound(hw_config);	/* Slot 0 */

	if (hw_config->slots[0] != -1)
	{
		/* The MSS driver installed itself */
		audio_devs[hw_config->slots[0]]->coproc = &pss_coproc_operations;
		if (pss_mixer && (num_mixers == (my_mix + 2)))
		{
			/* The MSS mixer installed */
			devc->ad_mixer_dev = audio_devs[hw_config->slots[0]]->mixer_dev;
		}
	}
}

void unload_pss(struct address_info *hw_config)
{
	release_region(hw_config->io_base, 0x10);
	release_region(hw_config->io_base+0x10, 0x9);
}

void unload_pss_mpu(struct address_info *hw_config)
{
#if (defined(CONFIG_MPU401) || defined(CONFIG_MPU_EMU)) && defined(CONFIG_MIDI)
	unload_mpu401(hw_config);
#endif
}

void unload_pss_mss(struct address_info *hw_config)
{
	unload_ms_sound(hw_config);
}

#ifdef MODULE

int pss_io = -1;

int mss_io = -1;
int mss_irq = -1;
int mss_dma = -1;

int mpu_io = -1;
int mpu_irq = -1;

struct address_info cfgpss = { 0 /* pss_io */, 0, -1, -1 };
struct address_info cfgmpu = { 0 /* mpu_io */, 0 /* mpu_irq */, 0, -1 };
struct address_info cfgmss = { 0 /* mss_io */, 0 /* mss_irq */, 0 /* mss_dma */, -1 };

MODULE_PARM(pss_io, "i");
MODULE_PARM_DESC(pss_io, "Set i/o base of PSS card (probably 0x220 or 0x240)");
MODULE_PARM(mss_io, "i");
MODULE_PARM_DESC(mss_io, "Set WSS (audio) i/o base (0x530, 0x604, 0xE80, 0xF40, or other. Address must end in 0 or 4 and must be from 0x100 to 0xFF4)");
MODULE_PARM(mss_irq, "i");
MODULE_PARM_DESC(mss_irq, "Set WSS (audio) IRQ (3, 5, 7, 9, 10, 11, 12)");
MODULE_PARM(mss_dma, "i");
MODULE_PARM_DESC(mss_dma, "Set WSS (audio) DMA (0, 1, 3)");
MODULE_PARM(mpu_io, "i");
MODULE_PARM_DESC(mpu_io, "Set MIDI i/o base (0x330 or other. Address must be on 4 location boundaries and must be from 0x100 to 0xFFC)");
MODULE_PARM(mpu_irq, "i");
MODULE_PARM_DESC(mpu_irq, "Set MIDI IRQ (3, 5, 7, 9, 10, 11, 12)");
MODULE_PARM(pss_mixer, "b");
MODULE_PARM_DESC(pss_mixer, "Enable (1) or disable (0) PSS mixer (controlling of output volume, bass, treble, synth volume). The mixer is not available on all PSS cards.");
MODULE_AUTHOR("Hannu Savolainen, Vladimir Michl");
MODULE_DESCRIPTION("Module for PSS sound cards (based on AD1848, ADSP-2115 and ESC614). This module includes control of output amplifier and synth volume of the Beethoven ADSP-16 card (this may work with other PSS cards).\n");

static int fw_load = 0;
static int pssmpu = 0, pssmss = 0;

/*
 *    Load a PSS sound card module
 */

int init_module(void)
{
	if (pss_io == -1 || mss_io == -1 || mss_irq == -1 || mss_dma == -1) {
		printk(KERN_INFO "pss: mss_io, mss_dma, mss_irq and pss_io must be set.\n");
		return -EINVAL;
	}

	cfgpss.io_base = pss_io;

	cfgmss.io_base = mss_io;
	cfgmss.irq = mss_irq;
	cfgmss.dma = mss_dma;

	cfgmpu.io_base = mpu_io;
	cfgmpu.irq = mpu_irq;

	if (!pss_synth) 
	{
		fw_load = 1;
		pss_synthLen = mod_firmware_load("/etc/sound/pss_synth", (void *) &pss_synth);
	}
	if (!probe_pss(&cfgpss))
		return -ENODEV;
	attach_pss(&cfgpss);
	/*
	 *    Attach stuff
	 */
	if (probe_pss_mpu(&cfgmpu)) {
		pssmpu = 1;
		attach_pss_mpu(&cfgmpu);
	}
	if (probe_pss_mss(&cfgmss)) {
		pssmss = 1;
		attach_pss_mss(&cfgmss);
	}
	SOUND_LOCK;
	return 0;
}

void cleanup_module(void)
{
	if (fw_load && pss_synth)
		vfree(pss_synth);
	if (pssmss)
		unload_pss_mss(&cfgmss);
	if (pssmpu)
		unload_pss_mpu(&cfgmpu);
	unload_pss(&cfgpss);
	SOUND_LOCK_END;
}
#endif
#endif
#endif
