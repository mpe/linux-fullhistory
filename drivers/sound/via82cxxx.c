/*
 * Support for VIA 82Cxxx Audio Codecs
 * Copyright 1999 Jeff Garzik <jgarzik@pobox.com>
 *
 * Distributed under the GNU GENERAL PUBLIC LICENSE (GPL) Version 2.
 * See the "COPYING" file distributed with this software for more info.
 *
 ********************************************************************
 *
 * TODO:
 *
 *	- Integrate AC'97 support, when AC'97 interface released
 *
 */
 
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>

#include <asm/io.h>

#include "sound_config.h"
#include "soundmodule.h"
#include "sb.h"

#ifndef SOUND_LOCK
#define SOUND_LOCK do {} while (0)
#define SOUND_LOCK_END do {} while (0)
#endif

#define MAX_CARDS	2

#define PFX		"via82cxxx: "

#define VIA_VERSION	"1.0.0"
#define VIA_CARD_NAME	"VIA 82Cxxx Audio driver " VIA_VERSION

#define VIA_FUNC_ENABLE		0x42
#define VIA_PNP_CONTROL		0x43

#define VIA_CR42_SB_ENABLE	0x01
#define VIA_CR42_MIDI_ENABLE	0x02
#define VIA_CR42_FM_ENABLE	0x04

#define via_probe_midi probe_uart401
#define via_attach_midi attach_uart401
#define via_unload_midi unload_uart401

static struct address_info	sb_data[MAX_CARDS];
static struct address_info	opl3_data[MAX_CARDS];
static unsigned			cards = 0;


static void __init via_attach_sb(struct address_info *hw_config)
{
	if(!sb_dsp_init(hw_config))
		hw_config->slots[0] = -1;
}


static int __init via_probe_sb(struct address_info *hw_config)
{
	if (check_region(hw_config->io_base, 16))
	{
		printk(KERN_DEBUG PFX "SBPro port 0x%x is already in use\n",
		       hw_config->io_base);
		return 0;
	}
	return sb_dsp_detect(hw_config, 0, 0);
}


static void __exit via_unload_sb(struct address_info *hw_config, int unload_mpu)
{
	if(hw_config->slots[0]!=-1)
		sb_dsp_unload(hw_config, unload_mpu);
}


static int __init via82cxxx_install (struct pci_dev *pcidev)
{
	int sb_io_base = 0;
	int sb_irq = 0;
	int sb_dma = 0;
	int midi_base = 0;
	u8 tmp8;
	
	memset (&sb_data[cards], 0, sizeof (struct address_info));
	memset (&opl3_data[cards], 0, sizeof (struct address_info));

	sb_data[cards].name = opl3_data[cards].name = VIA_CARD_NAME;
	opl3_data[cards].irq = -1;

	/* turn on features, if not already */
	pci_read_config_byte (pcidev, VIA_FUNC_ENABLE, &tmp8);
	tmp8 |= VIA_CR42_SB_ENABLE | VIA_CR42_MIDI_ENABLE |
		VIA_CR42_FM_ENABLE;
	pci_write_config_byte (pcidev, VIA_FUNC_ENABLE, tmp8);

	/* read legacy PNP info byte */
	pci_read_config_byte (pcidev, VIA_PNP_CONTROL, &tmp8);
	pci_write_config_byte (pcidev, VIA_PNP_CONTROL, tmp8);
	
	switch ((tmp8 >> 6) & 0x03) {
		case 0: sb_irq = 5; break;
		case 1: sb_irq = 7; break;
		case 2: sb_irq = 9; break;
		case 3: sb_irq = 10; break;
		default: /* do nothing */ break;
	}
	switch ((tmp8 >> 4) & 0x03) {
		case 0: sb_dma = 0; break;
		case 1: sb_dma = 1; break;
		case 2: sb_dma = 2; break;
		case 3: sb_dma = 3; break;
		default: /* do nothing */ break;
	}
	switch ((tmp8 >> 2) & 0x03) {
		case 0: midi_base = 0x300; break;
		case 1: midi_base = 0x310; break;
		case 2: midi_base = 0x320; break;
		case 3: midi_base = 0x330; break;
		default: /* do nothing */ break;
	}
	switch (tmp8 & 0x03) {
		case 0: sb_io_base = 0x220; break;
		case 1: sb_io_base = 0x240; break;
		case 2: sb_io_base = 0x260; break;
		case 3: sb_io_base = 0x280; break;
		default: /* do nothing */ break;
	}

	udelay(100);
	
	printk(KERN_INFO PFX "legacy "
	       "MIDI: 0x%X, SB: 0x%X / %d IRQ / %d DMA\n",
		midi_base, sb_io_base, sb_irq, sb_dma);
		
	sb_data[cards].card_subtype = MDL_SBPRO;
	sb_data[cards].io_base = sb_io_base;
	sb_data[cards].irq = sb_irq;
	sb_data[cards].dma = sb_dma;
	
	opl3_data[cards].io_base = midi_base;
	
	/* register legacy SoundBlaster Pro */
	if (!via_probe_sb (&sb_data[cards])) {
		printk (KERN_ERR PFX
			"SB probe @ 0x%X failed, aborting\n",
			sb_io_base);
		return -1;
	}
	via_attach_sb (&sb_data[cards]);

	/* register legacy MIDI */
	if (!via_probe_midi (&opl3_data[cards])) {
		printk (KERN_ERR PFX
			"MIDI probe @ 0x%X failed, aborting\n",
			midi_base);
		via_unload_sb (&sb_data[cards], 0);
		return -1;
	}
	via_attach_midi (&opl3_data[cards]);

	cards++;	
	return 0;
}


/*
 * 	This loop walks the PCI configuration database and finds where
 *	the sound cards are.
 */
 
static int __init probe_via82cxxx (void)
{
	struct pci_dev *pcidev = NULL;

	while ((pcidev = pci_find_device (PCI_VENDOR_ID_VIA,
					  PCI_DEVICE_ID_VIA_82C686_5,
					  pcidev)) != NULL) {

		  if (via82cxxx_install (pcidev) != 0) {
			  printk (KERN_ERR PFX "audio init failed\n");
			  return -1;
		  }

		  if (cards == MAX_CARDS) {
		  	  printk (KERN_DEBUG PFX "maximum number of cards reached\n");
			  break;
		  }
	}

	return 0;
}


/*
 *	This function is called when the user or kernel loads the 
 *	module into memory.
 */


static int __init init_via82cxxx_module(void)
{
	if (!pci_present ()) {
		printk (KERN_DEBUG PFX "PCI not present, exiting\n");
		return -ENODEV;
	}

	if (probe_via82cxxx() != 0) {
		printk(KERN_ERR PFX "probe failed, aborting\n");
		/* XXX unload cards registered so far, if any */
		return -ENODEV;
	}

	if (cards == 0) {
		printk(KERN_DEBUG PFX "No chips found, aborting\n");
		return -ENODEV;
	}

	printk (KERN_INFO PFX VIA_CARD_NAME " loaded\n");
	
	/*
	 *	Binds us to the sound subsystem	
	 */
	SOUND_LOCK;
	return 0;
}

/*
 *	This is called when it is removed. It will only be removed 
 *	when its use count is 0. For sound the SOUND_LOCK/SOUND_UNLOCK
 *	macros hide the entire work for this.
 */
 
static void __exit cleanup_via82cxxx_module(void)
{
	int i;
	
	for (i = 0; i < cards; i++)
		via_unload_sb (&sb_data[i], 1);

	/*
	 *	Final clean up with the sound layer
	 */
	SOUND_LOCK_END;
}

module_init(init_via82cxxx_module);
module_exit(cleanup_via82cxxx_module);

