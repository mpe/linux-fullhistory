/*
 * sound/trix.c
 *
 * Low level driver for the MediaTrix AudioTrix Pro
 * (MT-0002-PC Control Chip)
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 * Changes
 *	Alan Cox		Modularisation, cleanup.
 */
 
#include <linux/config.h>
#include <linux/module.h>

#include "sound_config.h"
#include "soundmodule.h"
#include "sb.h"
#include "sound_firmware.h"

#ifdef CONFIG_TRIX

#ifdef INCLUDE_TRIX_BOOT
#include <linux/init.h>
#include "trix_boot.h"
#else
static unsigned char *trix_boot = NULL;
static int trix_boot_len = 0;
#endif


static int kilroy_was_here = 0;	/* Don't detect twice */
static int sb_initialized = 0;
static int mpu_initialized = 0;

static int *trix_osp = NULL;

static int mpu = 0;

static unsigned char trix_read(int addr)
{
	outb(((unsigned char) addr), 0x390);	/* MT-0002-PC ASIC address */
	return inb(0x391);	/* MT-0002-PC ASIC data */
}

static void trix_write(int addr, int data)
{
	outb(((unsigned char) addr), 0x390);	/* MT-0002-PC ASIC address */
	outb(((unsigned char) data), 0x391);	/* MT-0002-PC ASIC data */
}

static void download_boot(int base)
{
	int i = 0, n = trix_boot_len;

	if (trix_boot_len == 0)
		return;

	trix_write(0xf8, 0x00);	/* ??????? */
	outb((0x01), base + 6);	/* Clear the internal data pointer */
	outb((0x00), base + 6);	/* Restart */

	/*
	   *  Write the boot code to the RAM upload/download register.
	   *  Each write increments the internal data pointer.
	 */
	outb((0x01), base + 6);	/* Clear the internal data pointer */
	outb((0x1A), 0x390);	/* Select RAM download/upload port */

	for (i = 0; i < n; i++)
		outb((trix_boot[i]), 0x391);
	for (i = n; i < 10016; i++)	/* Clear up to first 16 bytes of data RAM */
		outb((0x00), 0x391);
	outb((0x00), base + 6);	/* Reset */
	outb((0x50), 0x390);	/* ?????? */

}

static int trix_set_wss_port(struct address_info *hw_config)
{
	unsigned char   addr_bits;

	if (check_region(0x390, 2))
	{
		printk(KERN_ERR "AudioTrix: Config port I/O conflict\n");
		return 0;
	}
	if (kilroy_was_here)	/* Already initialized */
		return 0;

	if (trix_read(0x15) != 0x71)	/* No ASIC signature */
	{
		MDB(printk(KERN_ERR "No AudioTrix ASIC signature found\n"));
		return 0;
	}
	kilroy_was_here = 1;

	/*
	 * Reset some registers.
	 */

	trix_write(0x13, 0);
	trix_write(0x14, 0);

	/*
	 * Configure the ASIC to place the codec to the proper I/O location
	 */

	switch (hw_config->io_base)
	{
		case 0x530:
			addr_bits = 0;
			break;
		case 0x604:
			addr_bits = 1;
			break;
		case 0xE80:
			addr_bits = 2;
			break;
		case 0xF40:
			addr_bits = 3;
			break;
		default:
			return 0;
	}

	trix_write(0x19, (trix_read(0x19) & 0x03) | addr_bits);
	return 1;
}

/*
 *    Probe and attach routines for the Windows Sound System mode of
 *      AudioTrix Pro
 */

int probe_trix_wss(struct address_info *hw_config)
{
	int ret;

	/*
	 * Check if the IO port returns valid signature. The original MS Sound
	 * system returns 0x04 while some cards (AudioTrix Pro for example)
	 * return 0x00.
	 */
	if (check_region(hw_config->io_base, 8))
	{
		printk(KERN_ERR "AudioTrix: MSS I/O port conflict (%x)\n", hw_config->io_base);
		return 0;
	}
	trix_osp = hw_config->osp;

	if (!trix_set_wss_port(hw_config))
		return 0;

	if ((inb(hw_config->io_base + 3) & 0x3f) != 0x00)
	{
		MDB(printk(KERN_ERR "No MSS signature detected on port 0x%x\n", hw_config->io_base));
		return 0;
	}
	if (hw_config->irq > 11)
	{
		printk(KERN_ERR "AudioTrix: Bad WSS IRQ %d\n", hw_config->irq);
		return 0;
	}
	if (hw_config->dma != 0 && hw_config->dma != 1 && hw_config->dma != 3)
	{
		printk(KERN_ERR "AudioTrix: Bad WSS DMA %d\n", hw_config->dma);
		return 0;
	}
	if (hw_config->dma2 != -1 && hw_config->dma2 != hw_config->dma)
		if (hw_config->dma2 != 0 && hw_config->dma2 != 1 && hw_config->dma2 != 3)
		{
			  printk(KERN_ERR "AudioTrix: Bad capture DMA %d\n", hw_config->dma2);
			  return 0;
		}
	/*
	 * Check that DMA0 is not in use with a 8 bit board.
	 */

	if (hw_config->dma == 0 && inb(hw_config->io_base + 3) & 0x80)
	{
		printk(KERN_ERR "AudioTrix: Can't use DMA0 with a 8 bit card slot\n");
		return 0;
	}
	if (hw_config->irq > 7 && hw_config->irq != 9 && inb(hw_config->io_base + 3) & 0x80)
	{
		printk(KERN_ERR "AudioTrix: Can't use IRQ%d with a 8 bit card slot\n", hw_config->irq);
		return 0;
	}
	ret = ad1848_detect(hw_config->io_base + 4, NULL, hw_config->osp);

	if (ret)
	{
#ifdef TRIX_ENABLE_JOYSTICK
		trix_write(0x15, 0x80);
#endif
		request_region(0x390, 2, "AudioTrix");
	}
	return ret;
}

void
attach_trix_wss(struct address_info *hw_config)
{
	static unsigned char interrupt_bits[12] = {
		0, 0, 0, 0, 0, 0, 0, 0x08, 0, 0x10, 0x18, 0x20
	};
	char bits;

	static unsigned char dma_bits[4] = {
		1, 2, 0, 3
	};

	int config_port = hw_config->io_base + 0;
	int dma1 = hw_config->dma, dma2 = hw_config->dma2;
	int old_num_mixers = num_mixers;

	trix_osp = hw_config->osp;

	if (!kilroy_was_here)
	{
		DDB(printk("AudioTrix: Attach called but not probed yet???\n"));
		return;
	}
	
	/*
	 * Set the IRQ and DMA addresses.
	 */

	bits = interrupt_bits[hw_config->irq];
	if (bits == 0)
	{
		printk("AudioTrix: Bad IRQ (%d)\n", hw_config->irq);
		return;
	}
	outb((bits | 0x40), config_port);

	if (hw_config->dma2 == -1 || hw_config->dma2 == hw_config->dma)
	{
		  bits |= dma_bits[dma1];
		  dma2 = dma1;
	}
	else
	{
		unsigned char tmp;

		tmp = trix_read(0x13) & ~30;
		trix_write(0x13, tmp | 0x80 | (dma1 << 4));

		tmp = trix_read(0x14) & ~30;
		trix_write(0x14, tmp | 0x80 | (dma2 << 4));
	}

	outb((bits), config_port);	/* Write IRQ+DMA setup */

	hw_config->slots[0] = ad1848_init("AudioTrix Pro", hw_config->io_base + 4,
					  hw_config->irq,
					  dma1,
					  dma2,
					  0,
					  hw_config->osp);
	request_region(hw_config->io_base, 4, "MSS config");

	if (num_mixers > old_num_mixers)	/* Mixer got installed */
	{
		AD1848_REROUTE(SOUND_MIXER_LINE1, SOUND_MIXER_LINE);	/* Line in */
		AD1848_REROUTE(SOUND_MIXER_LINE2, SOUND_MIXER_CD);
		AD1848_REROUTE(SOUND_MIXER_LINE3, SOUND_MIXER_SYNTH);		/* OPL4 */
		AD1848_REROUTE(SOUND_MIXER_SPEAKER, SOUND_MIXER_ALTPCM);	/* SB */
	}
}

int probe_trix_sb(struct address_info *hw_config)
{

	int tmp;
	unsigned char conf;
	static char irq_translate[] = {
		-1, -1, -1, 0, 1, 2, -1, 3
	};

	if (trix_boot_len == 0)
		return 0;	/* No boot code -> no fun */

	if (!kilroy_was_here)
		return 0;	/* AudioTrix Pro has not been detected earlier */

	if (sb_initialized)
		return 0;

	if (check_region(hw_config->io_base, 16))
	{
		printk(KERN_ERR "AudioTrix: SB I/O port conflict (%x)\n", hw_config->io_base);
		return 0;
	}
	if ((hw_config->io_base & 0xffffff8f) != 0x200)
		return 0;

	tmp = hw_config->irq;
	if (tmp > 7)
		return 0;
	if (irq_translate[tmp] == -1)
		return 0;

	tmp = hw_config->dma;
	if (tmp != 1 && tmp != 3)
		return 0;

	conf = 0x84;		/* DMA and IRQ enable */
	conf |= hw_config->io_base & 0x70;	/* I/O address bits */
	conf |= irq_translate[hw_config->irq];
	if (hw_config->dma == 3)
		conf |= 0x08;
	trix_write(0x1b, conf);

	download_boot(hw_config->io_base);
	sb_initialized = 1;

	hw_config->name = "AudioTrix SB";
#ifdef CONFIG_SBDSP
	return sb_dsp_detect(hw_config);
#else
	return 0;
#endif
}

void attach_trix_sb(struct address_info *hw_config)
{
	extern int sb_be_quiet;
	int old_quiet;

#ifdef CONFIG_SBDSP
	hw_config->driver_use_1 = SB_NO_MIDI | SB_NO_MIXER | SB_NO_RECORDING;

	/* Prevent false alarms */
	old_quiet = sb_be_quiet;
	sb_be_quiet = 1;

	sb_dsp_init(hw_config);

	sb_be_quiet = old_quiet;
#endif
}

void attach_trix_mpu(struct address_info *hw_config)
{
#if defined(CONFIG_UART401) && defined(CONFIG_MIDI)
	hw_config->name = "AudioTrix Pro";
	attach_uart401(hw_config);
#endif
}

int probe_trix_mpu(struct address_info *hw_config)
{
#if defined(CONFIG_UART401) && defined(CONFIG_MIDI)
	unsigned char conf;
	static char irq_bits[] = {
		-1, -1, -1, 1, 2, 3, -1, 4, -1, 5
	};

	if (!kilroy_was_here)
	{
		DDB(printk("Trix: WSS and SB modes must be initialized before MPU\n"));
		return 0;	/* AudioTrix Pro has not been detected earlier */
	}
	if (!sb_initialized)
	{
		DDB(printk("Trix: SB mode must be initialized before MPU\n"));
		return 0;
	}
	if (mpu_initialized)
	{
		DDB(printk("Trix: MPU mode already initialized\n"));
		return 0;
	}
	if (check_region(hw_config->io_base, 4))
	{
		printk(KERN_ERR "AudioTrix: MPU I/O port conflict (%x)\n", hw_config->io_base);
		return 0;
	}
	if (hw_config->irq > 9)
	{
		printk(KERN_ERR "AudioTrix: Bad MPU IRQ %d\n", hw_config->irq);
		return 0;
	}
	if (irq_bits[hw_config->irq] == -1)
	{
		printk(KERN_ERR "AudioTrix: Bad MPU IRQ %d\n", hw_config->irq);
		return 0;
	}
	switch (hw_config->io_base)
	{
		case 0x330:
			conf = 0x00;
			break;
		case 0x370:
			conf = 0x04;
			break;
		case 0x3b0:
			conf = 0x08;
			break;
		case 0x3f0:
			conf = 0x0c;
			break;
		default:
			return 0;	/* Invalid port */
	}

	conf |= irq_bits[hw_config->irq] << 4;
	trix_write(0x19, (trix_read(0x19) & 0x83) | conf);
	mpu_initialized = 1;
	return probe_uart401(hw_config);
#else
	return 0;
#endif
}

void unload_trix_wss(struct address_info *hw_config)
{
	int dma2 = hw_config->dma2;

	if (dma2 == -1)
		dma2 = hw_config->dma;

	release_region(0x390, 2);
	release_region(hw_config->io_base, 4);

	ad1848_unload(hw_config->io_base + 4,
		      hw_config->irq,
		      hw_config->dma,
		      dma2,
		      0);
	sound_unload_audiodev(hw_config->slots[0]);
}

void unload_trix_mpu(struct address_info *hw_config)
{
#if defined(CONFIG_UART401) && defined(CONFIG_MIDI)
	unload_uart401(hw_config);
#endif
}

void unload_trix_sb(struct address_info *hw_config)
{
#ifdef CONFIG_SBDSP
	sb_dsp_unload(hw_config, mpu);
#endif
}

#ifdef MODULE

int             io = -1;
int             irq = -1;
int             dma = -1;
int             dma2 = -1;	/* Set this for modules that need it */

int             sb_io = -1;
int             sb_dma = -1;
int             sb_irq = -1;

int             mpu_io = -1;
int             mpu_irq = -1;

EXPORT_NO_SYMBOLS;

MODULE_PARM(io,"i");
MODULE_PARM(irq,"i");
MODULE_PARM(dma,"i");
MODULE_PARM(dma2,"i");
MODULE_PARM(sb_io,"i");
MODULE_PARM(sb_dma,"i");
MODULE_PARM(sb_irq,"i");
MODULE_PARM(mpu_io,"i");
MODULE_PARM(mpu_irq,"i");

struct address_info config;
struct address_info sb_config;
struct address_info mpu_config;

static int      sb = 0;

static int      fw_load;

int init_module(void)
{
	printk(KERN_INFO "MediaTrix audio driver Copyright (C) by Hannu Savolainen 1993-1996\n");

	if (io == -1 || dma == -1 || irq == -1)
	{
		printk(KERN_INFO "I/O, IRQ, DMA and type are mandatory\n");
		return -EINVAL;
	}
	config.io_base = io;
	config.irq = irq;
	config.dma = dma;
	config.dma2 = dma2;

	sb_config.io_base = sb_io;
	sb_config.irq = sb_irq;
	sb_config.dma = sb_dma;

	mpu_config.io_base = mpu_io;
	mpu_config.irq = mpu_irq;

	if (sb_io != -1 && (sb_irq == -1 || sb_dma == -1))
	{
		printk(KERN_INFO "CONFIG_SB_IRQ and CONFIG_SB_DMA must be specified if SB_IO is set.\n");
		return -EINVAL;
	}
	if (mpu_io != -1 && mpu_irq == -1)
	{
		printk(KERN_INFO "CONFIG_MPU_IRQ must be specified if MPU_IO is set.\n");
		return -EINVAL;
	}
	if (!trix_boot)
	{
		fw_load = 1;
		trix_boot_len = mod_firmware_load("/etc/sound/trxpro.bin",
						    (char **) &trix_boot);
	}
	if (!probe_trix_wss(&config))
		return -ENODEV;
	attach_trix_wss(&config);

	/*
	 *    We must attach in the right order to get the firmware
	 *      loaded up in time.
	 */

	if (sb_io != -1)
	{
		sb = probe_trix_sb(&sb_config);
		if (sb)
			attach_trix_sb(&sb_config);
	}
	
	if (mpu_io != -1)
	{
		mpu = probe_trix_mpu(&mpu_config);
		if (mpu)
			attach_trix_mpu(&mpu_config);
	}
	SOUND_LOCK;
	return 0;
}

void cleanup_module(void)
{
	if (fw_load && trix_boot)
		vfree(trix_boot);
	if (sb)
		unload_trix_sb(&sb_config);
	if (mpu)
		unload_trix_mpu(&mpu_config);
	unload_trix_wss(&config);
	SOUND_LOCK_END;
}

#endif
#endif
