/*
 * sound/opl3sa.c
 *
 * Low level driver for Yamaha YMF701B aka OPL3-SA chip
 * 
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Changes:
 *	Alan Cox		Modularisation
 *
 * FIXME:
 * 	Check for install of mpu etc is wrong, should check result of the mss stuff
 */
 
#include <linux/config.h>
#include <linux/module.h>

#undef  SB_OK

#include "sound_config.h"
#include "soundmodule.h"
#ifdef SB_OK
#include "sb.h"
static int sb_initialized = 0;

#endif

#ifdef CONFIG_OPL3SA1

static int kilroy_was_here = 0;	/* Don't detect twice */
static int mpu_initialized = 0;

static int *opl3sa_osp = NULL;

static unsigned char opl3sa_read(int addr)
{
	unsigned long flags;
	unsigned char tmp;

	save_flags(flags);
	cli();
	outb((0x1d), 0xf86);	/* password */
	outb(((unsigned char) addr), 0xf86);	/* address */
	tmp = inb(0xf87);	/* data */
	restore_flags(flags);

	return tmp;
}

static void opl3sa_write(int addr, int data)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	outb((0x1d), 0xf86);	/* password */
	outb(((unsigned char) addr), 0xf86);	/* address */
	outb(((unsigned char) data), 0xf87);	/* data */
	restore_flags(flags);
}

static int opl3sa_detect(void)
{
	int tmp;

	if (((tmp = opl3sa_read(0x01)) & 0xc4) != 0x04)
	{
		DDB(printk("OPL3-SA detect error 1 (%x)\n", opl3sa_read(0x01)));
		/* return 0; */
	}

	/*
	 * Check that the password feature has any effect
	 */
	
	if (inb(0xf87) == tmp)
	{
		DDB(printk("OPL3-SA detect failed 2 (%x/%x)\n", tmp, inb(0xf87)));
		return 0;
	}
	tmp = (opl3sa_read(0x04) & 0xe0) >> 5;

	if (tmp != 0 && tmp != 1)
	{
		DDB(printk("OPL3-SA detect failed 3 (%d)\n", tmp));
		return 0;
	}
	DDB(printk("OPL3-SA mode %x detected\n", tmp));

	opl3sa_write(0x01, 0x00);	/* Disable MSS */
	opl3sa_write(0x02, 0x00);	/* Disable SB */
	opl3sa_write(0x03, 0x00);	/* Disable MPU */

	return 1;
}

/*
 *    Probe and attach routines for the Windows Sound System mode of
 *     OPL3-SA
 */

int probe_opl3sa_wss(struct address_info *hw_config)
{
	int ret;
	unsigned char tmp = 0x24;	/* WSS enable */

	if (check_region(0xf86, 2))	/* Control port is busy */
		return 0;
	/*
	 * Check if the IO port returns valid signature. The original MS Sound
	 * system returns 0x04 while some cards (OPL3-SA for example)
	 * return 0x00.
	 */

	if (check_region(hw_config->io_base, 8))
	{
		printk(KERN_ERR "OPL3-SA: MSS I/O port conflict (%x)\n", hw_config->io_base);
		return 0;
	}
	opl3sa_osp = hw_config->osp;

	if (!opl3sa_detect())
	{
		printk(KERN_ERR "OSS: OPL3-SA chip not found\n");
		return 0;
	}
	
	switch (hw_config->io_base)
	{
		case 0x530:
			tmp |= 0x00;
			break;
		case 0xe80:
			tmp |= 0x08;
			break;
		case 0xf40:
			tmp |= 0x10;
			break;
		case 0x604:
			tmp |= 0x18;
			break;
		default:
			printk(KERN_ERR "OSS: Unsupported OPL3-SA/WSS base %x\n", hw_config->io_base);
		  return 0;
	}

	opl3sa_write(0x01, tmp);	/* WSS setup register */
	kilroy_was_here = 1;

	ret = probe_ms_sound(hw_config);
	if (ret)
		request_region(0xf86, 2, "OPL3-SA");

	return ret;
}

void attach_opl3sa_wss(struct address_info *hw_config)
{
	int nm = num_mixers;

	/* FIXME */
	attach_ms_sound(hw_config);
	if (num_mixers > nm)	/* A mixer was installed */
	{
		AD1848_REROUTE(SOUND_MIXER_LINE1, SOUND_MIXER_CD);
		AD1848_REROUTE(SOUND_MIXER_LINE2, SOUND_MIXER_SYNTH);
		AD1848_REROUTE(SOUND_MIXER_LINE3, SOUND_MIXER_LINE);
	}
}


void attach_opl3sa_mpu(struct address_info *hw_config)
{
#if defined(CONFIG_UART401) && defined(CONFIG_MIDI)
	hw_config->name = "OPL3-SA (MPU401)";
	attach_uart401(hw_config);
#endif
}

int probe_opl3sa_mpu(struct address_info *hw_config)
{
#if defined(CONFIG_UART401) && defined(CONFIG_MIDI)
	unsigned char conf;
	static char irq_bits[] = {
		-1, -1, -1, -1, -1, 1, -1, 2, -1, 3, 4
	};

	if (!kilroy_was_here)
		return 0;	/* OPL3-SA has not been detected earlier */

	if (mpu_initialized)
	{
		DDB(printk("OPL3-SA: MPU mode already initialized\n"));
		return 0;
	}
	if (check_region(hw_config->io_base, 4))
	{
		printk(KERN_ERR "OPL3-SA: MPU I/O port conflict (%x)\n", hw_config->io_base);
		return 0;
	}
	if (hw_config->irq > 10)
	{
		printk(KERN_ERR "OPL3-SA: Bad MPU IRQ %d\n", hw_config->irq);
		return 0;
	}
	if (irq_bits[hw_config->irq] == -1)
	{
		printk(KERN_ERR "OPL3-SA: Bad MPU IRQ %d\n", hw_config->irq);
		return 0;
	}
	switch (hw_config->io_base)
	{
		case 0x330:
			conf = 0x00;
			break;
		case 0x332:
			conf = 0x20;
			break;
		case 0x334:
			conf = 0x40;
			break;
		case 0x300:
			conf = 0x60;
			break;
		default:
			return 0;	/* Invalid port */
	}

	conf |= 0x83;		/* MPU & OPL3 (synth) & game port enable */
	conf |= irq_bits[hw_config->irq] << 2;

	opl3sa_write(0x03, conf);

	mpu_initialized = 1;

	return probe_uart401(hw_config);
#else
	return 0;
#endif
}

void unload_opl3sa_wss(struct address_info *hw_config)
{
	int dma2 = hw_config->dma2;

	if (dma2 == -1)
		dma2 = hw_config->dma;

	release_region(0xf86, 2);
	release_region(hw_config->io_base, 4);

	ad1848_unload(hw_config->io_base + 4,
		      hw_config->irq,
		      hw_config->dma,
		      dma2,
		      0);
	sound_unload_audiodev(hw_config->slots[0]);
}

void unload_opl3sa_mpu(struct address_info *hw_config)
{
#if defined(CONFIG_UART401) && defined(CONFIG_MIDI)
	unload_uart401(hw_config);
#endif
}

#ifdef SB_OK
void unload_opl3sa_sb(struct address_info *hw_config)
{
#ifdef CONFIG_SBDSP
	sb_dsp_unload(hw_config);
#endif
}
#endif

#ifdef MODULE
int             io = -1;
int             irq = -1;
int             dma = -1;
int             dma2 = -1;

int		mpu_io = -1;
int 		mpu_irq = -1;

MODULE_PARM(io,"i");
MODULE_PARM(irq,"i");
MODULE_PARM(dma,"i");
MODULE_PARM(dma2,"i");
MODULE_PARM(mpu_io,"i");
MODULE_PARM(mpu_irq,"i");

struct address_info cfg;
struct address_info mpu_cfg;
static int found_mpu;

int init_module(void)
{
	if (io == -1 || irq == -1 || dma == -1)
	{
		printk(KERN_ERR "opl3sa: dma, irq and io must be set.\n");
		return -EINVAL;
	}
	cfg.io_base = io;
	cfg.irq = irq;
	cfg.dma = dma;
	cfg.dma2 = dma2;
	
	mpu_cfg.io_base = mpu_io;
	mpu_cfg.irq = mpu_irq;

	if (probe_opl3sa_wss(&cfg) == 0)
		return -ENODEV;

	found_mpu=probe_opl3sa_mpu(&mpu_cfg);

	attach_opl3sa_wss(&cfg);
	if(found_mpu)
		attach_opl3sa_mpu(&mpu_cfg);
	SOUND_LOCK;
	return 0;
}

void cleanup_module(void)
{
	if(found_mpu)
		unload_opl3sa_mpu(&mpu_cfg);
	unload_opl3sa_wss(&cfg);
	SOUND_LOCK_END;
}

#endif

#endif
