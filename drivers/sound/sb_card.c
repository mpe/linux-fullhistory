/*
 * sound/sb_card.c
 *
 * Detection routine for the Sound Blaster cards.
 *
 *
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 *
 *
 * 26-11-1999 Patched to compile without ISA PnP support in the
 * kernel - Daniel Stone (tamriel@ductape.net) 
 *
 * 06-01-2000 Refined and bugfixed ISA PnP support, added
 *  CMI 8330 support - Alessandro Zummo <azummo@ita.flashnet.it>
 *
 * 18-01-2000 Separated sb_card and sb_common
 *  Jeff Garzik <jgarzik@mandrakesoft.com>
 *
 * 04-02-2000 Added Soundblaster AWE 64 PnP support, isapnpjump
 *  Alessandro Zummo <azummo@ita.flashnet.it>
 *
 * 11-02-2000 Added Soundblaster AWE 32 PnP support, refined PnP code
 *  Alessandro Zummo <azummo@ita.flashnet.it>
 *
 * 13-02-2000 Hopefully fixed awe/sb16 related bugs, code cleanup
 *  Alessandro Zummo <azummo@ita.flashnet.it>
 *
 */

#include <linux/config.h>
#include <linux/mca.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/isapnp.h>

#include "sound_config.h"
#include "soundmodule.h"

#include "sb_mixer.h"
#include "sb.h"

static int sbmpu = 0;

extern void *smw_free;

static void __init attach_sb_card(struct address_info *hw_config)
{
	if(!sb_dsp_init(hw_config))
		hw_config->slots[0] = -1;
	SOUND_LOCK;
}

static int __init probe_sb(struct address_info *hw_config)
{
	if (hw_config->io_base == -1 || hw_config->dma == -1 || hw_config->irq == -1)
	{
		printk(KERN_ERR "sb_card: I/O, IRQ, and DMA are mandatory\n");
		return -EINVAL;
	}

#ifdef CONFIG_MCA
	/* MCA code added by ZP Gu (zpg@castle.net) */
	if (MCA_bus) {               /* no multiple REPLY card probing */
		int slot;
		u8 pos2, pos3, pos4;

		slot = mca_find_adapter( 0x5138, 0 );
		if( slot == MCA_NOTFOUND ) 
		{
			slot = mca_find_adapter( 0x5137, 0 );

			if (slot != MCA_NOTFOUND)
				mca_set_adapter_name( slot, "REPLY SB16 & SCSI Adapter" );
		}
		else
		{
			mca_set_adapter_name( slot, "REPLY SB16 Adapter" );
		}

		if (slot != MCA_NOTFOUND) 
		{
			mca_mark_as_used(slot);
			pos2 = mca_read_stored_pos( slot, 2 );
			pos3 = mca_read_stored_pos( slot, 3 );
			pos4 = mca_read_stored_pos( slot, 4 );

			if (pos2 & 0x4) 
			{
				/* enabled? */
				static unsigned short irq[] = { 0, 5, 7, 10 };
				/*
				static unsigned short midiaddr[] = {0, 0x330, 0, 0x300 };
       				*/

				hw_config->io_base = 0x220 + 0x20 * (pos2 >> 6);
				hw_config->irq = irq[(pos4 >> 5) & 0x3];
				hw_config->dma = pos3 & 0xf;
				/* Reply ADF wrong on High DMA, pos[1] should start w/ 00 */
				hw_config->dma2 = (pos3 >> 4) & 0x3;
				if (hw_config->dma2 == 0)
					hw_config->dma2 = hw_config->dma;
				else
					hw_config->dma2 += 4;
				/*
					hw_config->driver_use_2 = midiaddr[(pos2 >> 3) & 0x3];
				*/
	
				printk(KERN_INFO "sb: Reply MCA SB at slot=%d \
iobase=0x%x irq=%d lo_dma=%d hi_dma=%d\n",
						slot+1,
				        	hw_config->io_base, hw_config->irq,
	        				hw_config->dma, hw_config->dma2);
			}
			else
			{
				printk (KERN_INFO "sb: Reply SB Base I/O address disabled\n");
			}
		}
	}
#endif

	/* This is useless since is done by sb_dsp_detect - azummo */
	
	if (check_region(hw_config->io_base, 16))
	{
		printk(KERN_ERR "sb_card: I/O port 0x%x is already in use\n\n", hw_config->io_base);
		return 0;
	}
	return sb_dsp_detect(hw_config, 0, 0);
}

static void __exit unload_sb(struct address_info *hw_config)
{
	if(hw_config->slots[0]!=-1)
		sb_dsp_unload(hw_config, sbmpu);
}

extern int esstype;	/* ESS chip type */

static struct address_info cfg;
static struct address_info cfg_mpu;

struct pci_dev 	*sb_dev 	= NULL, 
		*wss_dev	= NULL, 
		*jp_dev		= NULL,
		*mpu_dev	= NULL, 
		*wt_dev		= NULL;
/*
 *    Note DMA2 of -1 has the right meaning in the SB16 driver as well
 *    as here. It will cause either an error if it is needed or a fallback
 *    to the 8bit channel.
 */

static int __initdata mpu_io	= 0;
static int __initdata io	= -1;
static int __initdata irq	= -1;
static int __initdata dma	= -1;
static int __initdata dma16	= -1;   /* Set this for modules that need it */
static int __initdata type	= 0;    /* Can set this to a specific card type */


#if defined CONFIG_ISAPNP || defined CONFIG_ISAPNP_MODULE
static int isapnp	= 1;
static int isapnpjump	= 0;
static int nosbwave	= 0;	/* This option will be removed when the new awe_wave driver will be
				   in the kernel tree */
#else
int isapnp	= 0;
#endif

MODULE_DESCRIPTION("Soundblaster driver");

MODULE_PARM(io,		"i");
MODULE_PARM(irq,	"i");
MODULE_PARM(dma,	"i");
MODULE_PARM(dma16,	"i");
MODULE_PARM(mpu_io,	"i");
MODULE_PARM(type,	"i");
MODULE_PARM(sm_games,	"i");
MODULE_PARM(esstype,	"i");
MODULE_PARM(acer,	"i");

#if defined CONFIG_ISAPNP || defined CONFIG_ISAPNP_MODULE
MODULE_PARM(isapnp,	"i");
MODULE_PARM(isapnpjump,	"i");
MODULE_PARM(nosbwave,	"i");
MODULE_PARM_DESC(isapnp,	"When set to 0, Plug & Play support will be disabled");
MODULE_PARM_DESC(isapnpjump,	"Jumps to a specific slot in the driver's PnP table. Use the source, Luke.");
MODULE_PARM_DESC(nosbwave,	"Disable SB AWE 32/64 Wavetable initialization. Use this option with the new awe_wave driver.");
#endif

MODULE_PARM_DESC(io,		"Soundblaster i/o base address (0x220,0x240,0x260,0x280)");
MODULE_PARM_DESC(irq,		"IRQ (5,7,9,10)");
MODULE_PARM_DESC(dma,		"8-bit DMA channel (0,1,3)");
MODULE_PARM_DESC(dma16,		"16-bit DMA channel (5,6,7)");
MODULE_PARM_DESC(mpu_io,	"Mpu base address");
MODULE_PARM_DESC(type,		"You can set this to specific card type");
MODULE_PARM_DESC(sm_games,	"Enable support for Logitech soundman games");
MODULE_PARM_DESC(esstype,	"ESS chip type");
MODULE_PARM_DESC(acer,		"Set this to detect cards in some ACER notebooks");

#if defined CONFIG_ISAPNP || defined CONFIG_ISAPNP_MODULE

/* That's useful. */

#define show_base(devname, resname, resptr) printk(KERN_INFO "sb: %s %s base located at %#lx\n", devname, resname, (resptr)->start)

static struct pci_dev *activate_dev(char *devname, char *resname, struct pci_dev *dev)
{
	int err;

	if(dev->active)
	{
		printk(KERN_INFO "sb: %s %s already in use\n", devname, resname);
		return(NULL);
	}

	if((err = dev->activate(dev)) < 0)
	{
		printk(KERN_ERR "sb: %s %s config failed (out of resources?)[%d]\n", devname, resname, err);

		dev->deactivate(dev);

		return(NULL);
	}
	return(dev);
}

/* Card's specific initialization functions
 */

static struct pci_dev *sb_init_generic(struct pci_bus *bus, struct pci_dev *card, struct address_info *hw_config, struct address_info *mpu_config)
{
	if((sb_dev = isapnp_find_dev(bus, card->vendor, card->device, NULL)))
	{
		sb_dev->prepare(sb_dev);

		if((sb_dev = activate_dev("Soundblaster", "sb", sb_dev)))
		{
			hw_config->io_base 	= sb_dev->resource[0].start;
			hw_config->irq 		= sb_dev->irq_resource[0].start;
			hw_config->dma 		= sb_dev->dma_resource[0].start;
			hw_config->dma2 	= sb_dev->dma_resource[1].start;
			mpu_config->io_base	= sb_dev->resource[1].start;
		}
	}
	return(sb_dev);
}

static struct pci_dev *sb_init_ess(struct pci_bus *bus, struct pci_dev *card, struct address_info *hw_config, struct address_info *mpu_config)
{
	if((sb_dev = isapnp_find_dev(bus, card->vendor, card->device, NULL)))
	{
		sb_dev->prepare(sb_dev);

		if((sb_dev = activate_dev("ESS", "sb", sb_dev)))
		{
			hw_config->io_base 	= sb_dev->resource[0].start;
			hw_config->irq 		= sb_dev->irq_resource[0].start;
			hw_config->dma 		= sb_dev->dma_resource[0].start;
			hw_config->dma2		= sb_dev->dma_resource[1].start;
			mpu_config->io_base	= sb_dev->resource[2].start;
		}
	}
	return(sb_dev);
}

static struct pci_dev *sb_init_cmi(struct pci_bus *bus, struct pci_dev *card, struct address_info *hw_config, struct address_info *mpu_config)
{
	/* 
	 *  The CMI8330/C3D is a very 'stupid' chip... where did they get al those @@@ ?
	 *  It's ISAPnP section is badly designed and has many flaws, i'll do my best
	 *  to workaround them. I strongly suggest you to buy a real soundcard.
	 *  The CMI8330 on my motherboard has also the bad habit to activate 
	 *  the rear channel of my amplifier instead of the front one.
	 */

	/*  @X@0001:Soundblaster.
	 */

	if((sb_dev = isapnp_find_dev(bus,
		ISAPNP_VENDOR('@','X','@'), ISAPNP_FUNCTION(0x0001), NULL)))
	{
#ifdef CMI8330_DMA0BAD
		int dmahack = 0;
#endif
		sb_dev->prepare(sb_dev);
		
		/*  This device doesn't work with DMA 0, so we must allocate
		 *  it to prevent PnP routines to assign it to the card.
		 *
		 *  I know i could have inlined the following lines, but it's cleaner
		 *  this way.
		 */
	
#ifdef CMI8330_DMA0BAD
		if(sb_dev->dma_resource[0].start == 0)
		{
			if(!request_dma(0, "cmi8330 dma hack"))
			{
				/* DMA was free, we now have it */
				dmahack = 1;
			}
		}
#endif

		if((sb_dev = activate_dev("CMI8330", "sb", sb_dev)))
		{
			hw_config->io_base 	= sb_dev->resource[0].start;
			hw_config->irq 		= sb_dev->irq_resource[0].start;
			hw_config->dma 		= sb_dev->dma_resource[0].start;
			hw_config->dma2 	= sb_dev->dma_resource[1].start;

			show_base("CMI8330", "sb", &sb_dev->resource[0]);
		}

#ifdef CMI8330_DMA0BAD
		if(dmahack) free_dma(0);
#endif
		if(!sb_dev) return(NULL);
	}
	else
		printk(KERN_ERR "sb: CMI8330 panic: sb base not found\n");

	/*  @H@0001:mpu
	 */

	if((mpu_dev = isapnp_find_dev(bus,
		ISAPNP_VENDOR('@','H','@'), ISAPNP_FUNCTION(0x0001), NULL)))
	{
		mpu_dev->prepare(mpu_dev);

		/*  This disables the interrupt on this resource. Do we need it ?
		 */

		mpu_dev->irq_resource[0].flags = 0;

		if((mpu_dev = activate_dev("CMI8330", "mpu", mpu_dev)))
		{
			show_base("CMI8330", "mpu", &mpu_dev->resource[0]);
			mpu_config->io_base = mpu_dev->resource[0].start;
		}
	}
	else
		printk(KERN_ERR "sb: CMI8330 panic: mpu not found\n");


	/*  @P@:Gameport
	 */

	if((jp_dev = isapnp_find_dev(bus,
		ISAPNP_VENDOR('@','P','@'), ISAPNP_FUNCTION(0x0001), NULL)))
	{
		jp_dev->prepare(jp_dev);

		if((jp_dev = activate_dev("CMI8330", "gameport", jp_dev)))
			show_base("CMI8330", "gameport", &jp_dev->resource[0]);
	}
	else
		printk(KERN_ERR "sb: CMI8330 panic: gameport not found\n");

	/*  @@@0001:OPL3 
	 */

#if defined(CONFIG_SOUND_YM3812) || defined(CONFIG_SOUND_YM3812_MODULE)
	if((wss_dev = isapnp_find_dev(bus,
		ISAPNP_VENDOR('@','@','@'), ISAPNP_FUNCTION(0x0001), NULL)))
	{
		wss_dev->prepare(wss_dev);

		/* Let's disable IRQ and DMA for WSS device */

		wss_dev->irq_resource[0].flags = 0;
		wss_dev->dma_resource[0].flags = 0;

		if((wss_dev = activate_dev("CMI8330", "opl3", wss_dev)))
			show_base("CMI8330", "opl3", &wss_dev->resource[1]);
	}
	else
		printk(KERN_ERR "sb: CMI8330 panic: opl3 not found\n");
#endif

	printk(KERN_INFO "sb: CMI8330 mail reports to Alessandro Zummo <azummo@ita.flashnet.it>\n");

	return(sb_dev);
}

static struct pci_dev *sb_init_diamond(struct pci_bus *bus, struct pci_dev *card, struct address_info *hw_config, struct address_info *mpu_config)
{
	/* 
	 * Diamonds DT0197H
	 * very similar to the CMI8330 above
	 */

	/*  @@@0001:Soundblaster.
	 */

	if((sb_dev = isapnp_find_dev(bus,
				ISAPNP_VENDOR('@','@','@'), ISAPNP_FUNCTION(0x0001), NULL)))
	{
		sb_dev->prepare(sb_dev);
		
		if((sb_dev = activate_dev("DT0197H", "sb", sb_dev)))
		{
			hw_config->io_base 	= sb_dev->resource[0].start;
			hw_config->irq 		= sb_dev->irq_resource[0].start;
			hw_config->dma 		= sb_dev->dma_resource[0].start;
			hw_config->dma2 	= -1;

			show_base("DT0197H", "sb", &sb_dev->resource[0]);
		}

		if(!sb_dev) return(NULL);

	}
	else
		printk(KERN_ERR "sb: DT0197H panic: sb base not found\n");

	/*  @X@0001:mpu
	 */

#ifdef CONFIG_MIDI
	if((mpu_dev = isapnp_find_dev(bus,
				ISAPNP_VENDOR('@','X','@'), ISAPNP_FUNCTION(0x0001), NULL)))
	{
		mpu_dev->prepare(mpu_dev);

		if((mpu_dev = activate_dev("DT0197H", "mpu", mpu_dev)))
		{
			show_base("DT0197H", "mpu", &mpu_dev->resource[0]);
			mpu_config->io_base = mpu_dev->resource[0].start;
		}
	}
	else
		printk(KERN_ERR "sb: DT0197H panic: mpu not found\n");
#endif


	/*  @P@:Gameport
	 */

	if((jp_dev = isapnp_find_dev(bus,
				ISAPNP_VENDOR('@','P','@'), ISAPNP_FUNCTION(0x0001), NULL)))
	{
		jp_dev->prepare(jp_dev);

		if((jp_dev = activate_dev("DT0197H", "gameport", jp_dev)))
			show_base("DT0197H", "gameport", &jp_dev->resource[0]);
	}
	else
		printk(KERN_ERR "sb: DT0197H panic: gameport not found\n");

	/*  @H@0001:OPL3 
	 */

#if defined(CONFIG_SOUND_YM3812) || defined(CONFIG_SOUND_YM3812_MODULE)
	if((wss_dev = isapnp_find_dev(bus,
				ISAPNP_VENDOR('@','H','@'), ISAPNP_FUNCTION(0x0001), NULL)))
	{
		wss_dev->prepare(wss_dev);

		/* Let's disable IRQ and DMA for WSS device */

		wss_dev->irq_resource[0].flags = 0;
		wss_dev->dma_resource[0].flags = 0;

		if((wss_dev = activate_dev("DT0197H", "opl3", wss_dev)))
			show_base("DT0197H", "opl3", &wss_dev->resource[0]);
	}
	else
		printk(KERN_ERR "sb: DT0197H panic: opl3 not found\n");
#endif

	printk(KERN_INFO "sb: DT0197H mail reports to Torsten Werner <twerner@intercomm.de>\n");

	return(sb_dev);
}

/* Specific support for awe will be dropped when:
 * a) The new awe_wawe driver with PnP support will be introduced in the kernel
 * b) The joystick driver will support PnP - a little patch is available from me....hint, hint :-)
 */

static struct pci_dev *sb_init_awe(struct pci_bus *bus, struct pci_dev *card, struct address_info *hw_config, struct address_info *mpu_config)
{
	/*  CTL0042:Audio SB64
	 *  CTL0031:Audio SB32
	 *  CTL0045:Audio SB64
	 */

	if(	(sb_dev = isapnp_find_dev(bus, ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0042), NULL)) || 
		(sb_dev = isapnp_find_dev(bus, ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0031), NULL)) ||
		(sb_dev = isapnp_find_dev(bus, ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0045), NULL))	)
	{
		sb_dev->prepare(sb_dev);
		
		if((sb_dev = activate_dev("AWE", "sb", sb_dev)))
		{
			hw_config->io_base 	= sb_dev->resource[0].start;
			hw_config->irq 		= sb_dev->irq_resource[0].start;
			hw_config->dma 		= sb_dev->dma_resource[0].start;
			hw_config->dma2 	= sb_dev->dma_resource[1].start;

			mpu_config->io_base	= sb_dev->resource[1].start;

			show_base("AWE", "sb",		&sb_dev->resource[0]);
			show_base("AWE", "mpu", 	&sb_dev->resource[1]);
			show_base("AWE", "opl3",	&sb_dev->resource[2]);
		}
		else
			return(NULL);
	}
	else
		printk(KERN_ERR "sb: AWE panic: sb base not found\n");


	/*  CTL7002:Game SB64
	 *  CTL7001:Game SB32
	 */

	if(	(jp_dev = isapnp_find_dev(bus, ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x7002), NULL)) ||
		(jp_dev = isapnp_find_dev(bus, ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x7001), NULL)) )
	{
		jp_dev->prepare(jp_dev);
		
		if((jp_dev = activate_dev("AWE", "gameport", jp_dev)))
			show_base("AWE", "gameport", &jp_dev->resource[0]);
	}
	else
		printk(KERN_ERR "sb: AWE panic: gameport not found\n");


	/*  CTL0022:WaveTable SB64
	 *  CTL0021:WaveTable SB32
	 *  CTL0023:WaveTable Sb64
	 */

	if( nosbwave == 0 &&
   	( ( wt_dev = isapnp_find_dev(bus, ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0023), NULL)) ||
	  ( wt_dev = isapnp_find_dev(bus, ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0022), NULL)) ||
	  ( wt_dev = isapnp_find_dev(bus, ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0021), NULL)) ))
	{
		wt_dev->prepare(wt_dev);
		
		if((wt_dev = activate_dev("AWE", "wavetable", wt_dev)))
		{
			show_base("AWE", "wavetable", &wt_dev->resource[0]);
			show_base("AWE", "wavetable", &wt_dev->resource[1]);
			show_base("AWE", "wavetable", &wt_dev->resource[2]);
		}
	}
	else
		printk(KERN_ERR "sb: AWE panic: wavetable not found\n");

	printk(KERN_INFO "sb: AWE mail reports to Alessandro Zummo <azummo@ita.flashnet.it>\n");

	return(sb_dev);
}

#define SBF_DEV	0x01


static struct { unsigned short vendor, function, flags; struct pci_dev * (*initfunc)(struct pci_bus *, struct pci_dev *, struct address_info *, struct address_info *); char *name; }
isapnp_sb_list[] __initdata = {
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0001), SBF_DEV,	&sb_init_generic,	"Sound Blaster 16" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0031), SBF_DEV,	&sb_init_generic,	"Sound Blaster 16" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0041), SBF_DEV,	&sb_init_generic,	"Sound Blaster 16" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0042), SBF_DEV,	&sb_init_generic,	"Sound Blaster 16" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0043), SBF_DEV,	&sb_init_generic,	"Sound Blaster 16" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0045), SBF_DEV,	&sb_init_generic,	"Sound Blaster 16" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0044), 0,	&sb_init_awe,		"Sound Blaster 32" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0039), 0,	&sb_init_awe,		"Sound Blaster AWE 32" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x009D), 0,	&sb_init_awe,		"Sound Blaster AWE 64" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x00C5), 0,	&sb_init_awe,		"Sound Blaster AWE 64" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x00E4), 0,	&sb_init_awe,		"Sound Blaster AWE 64" },
	{ISAPNP_VENDOR('E','S','S'), ISAPNP_FUNCTION(0x0968), SBF_DEV,	&sb_init_ess,		"ESS 1688" },
	{ISAPNP_VENDOR('E','S','S'), ISAPNP_FUNCTION(0x1868), SBF_DEV,	&sb_init_ess,		"ESS 1868" },
	{ISAPNP_VENDOR('E','S','S'), ISAPNP_FUNCTION(0x8611), SBF_DEV,	&sb_init_ess,		"ESS 1868" },
	{ISAPNP_VENDOR('E','S','S'), ISAPNP_FUNCTION(0x1869), SBF_DEV,	&sb_init_ess,		"ESS 1869" },
	{ISAPNP_VENDOR('E','S','S'), ISAPNP_FUNCTION(0x1878), SBF_DEV,	&sb_init_ess,		"ESS 1878" },
	{ISAPNP_VENDOR('E','S','S'), ISAPNP_FUNCTION(0x1879), SBF_DEV,	&sb_init_ess,		"ESS 1879" },
	{ISAPNP_VENDOR('C','M','I'), ISAPNP_FUNCTION(0x0001), 0,	&sb_init_cmi,		"CMI 8330 SoundPRO" },
	{ISAPNP_VENDOR('R','W','B'), ISAPNP_FUNCTION(0x1688), 0,	&sb_init_diamond,	"Diamond DT0197H" },
	{0}
};

static int __init sb_init_isapnp(struct address_info *hw_config, struct address_info *mpu_config, struct pci_bus *bus, struct pci_dev *card, int slot)
{
	struct pci_dev *idev = NULL;

	/* You missed the init func? That's bad. */
	if(isapnp_sb_list[slot].initfunc)
	{
		char *busname = bus->name[0] ? bus->name : isapnp_sb_list[slot].name;

		printk(KERN_INFO "sb: %s detected\n", busname); 

		/* Initialize this baby. */

		if((idev = isapnp_sb_list[slot].initfunc(bus, card, hw_config, mpu_config)))
		{
			/* We got it. */

			printk(KERN_NOTICE "sb: ISAPnP reports '%s' at i/o %#x, irq %d, dma %d, %d\n",
			       busname,
			       hw_config->io_base, hw_config->irq, hw_config->dma,
			       hw_config->dma2);
			return 1;
		}
		else
			printk(KERN_INFO "sb: Failed to initialize %s\n", busname);
	}
	else
		printk(KERN_ERR "sb: Bad entry in sb_card.c PnP table\n");

	return 0;
}

/* Actually this routine will detect and configure only the first card with successful
   initialization. isapnpjump could be used to jump to a specific entry.
   Please always add entries at the end of the array.
   Should this be fixed? - azummo
*/

int __init sb_probe_isapnp(struct address_info *hw_config, struct address_info *mpu_config) 
{
	int i;

	/* Count entries in isapnp_sb_list */
	for (i = 0; isapnp_sb_list[i].vendor != 0; i++);

	/* Check and adjust isapnpjump */
	if( isapnpjump < 0 || isapnpjump > ( i - 1 ) )
	{
		printk(KERN_ERR "sb: Valid range for isapnpjump is 0-%d. Adjusted to 0.\n", i-1);
		isapnpjump = 0;
	}
	
	for (i = isapnpjump; isapnp_sb_list[i].vendor != 0; i++) {

		if(!(isapnp_sb_list[i].flags & SBF_DEV))
		{
			struct pci_bus *bus = NULL;
				
			while ((bus = isapnp_find_card(
					isapnp_sb_list[i].vendor,
					isapnp_sb_list[i].function,
					bus))) {
	
				if(sb_init_isapnp(hw_config, mpu_config, bus, NULL, i))
					return 0;
			}
		}
	}

	/*  No cards found. I'll try now to search inside every card for a logical device
	 *  that matches any entry marked with SBF_DEV in the table.
	 */

	for (i = isapnpjump; isapnp_sb_list[i].vendor != 0; i++) {

		if(isapnp_sb_list[i].flags & SBF_DEV)
		{
			struct pci_dev *card = NULL;

			while ((card = isapnp_find_dev(NULL,
					isapnp_sb_list[i].vendor,
					isapnp_sb_list[i].function,
					card))) {

				if(sb_init_isapnp(hw_config, mpu_config, card->bus, card, i))
					return 0;
			}
		}
	}

	return -ENODEV;
}
#endif

static int __init init_sb(void)
{
	printk(KERN_INFO "Soundblaster audio driver Copyright (C) by Hannu Savolainen 1993-1996\n");

	/* Please remember that even with CONFIG_ISAPNP defined one should still be
		able to disable PNP support for this single driver!
	*/

#if defined CONFIG_ISAPNP || defined CONFIG_ISAPNP_MODULE			
	if(isapnp && (sb_probe_isapnp(&cfg, &cfg_mpu) < 0) ) {
		printk(KERN_NOTICE "sb_card: No ISAPnP cards found, trying standard ones...\n");
		isapnp = 0;
	}
#endif

	if( isapnp == 0 ) {
		cfg.io_base	= io;
		cfg.irq		= irq;
		cfg.dma		= dma;
		cfg.dma2	= dma16;
	}

	cfg.card_subtype = type;

	if (!probe_sb(&cfg))
		return -ENODEV;
	attach_sb_card(&cfg);

	if(cfg.slots[0]==-1)
		return -ENODEV;
		
	if (isapnp == 0) 
		cfg_mpu.io_base = mpu_io;
	if (probe_sbmpu(&cfg_mpu))
		sbmpu = 1;
	if (sbmpu)
		attach_sbmpu(&cfg_mpu);
	return 0;
}

static void __exit cleanup_sb(void)
{
	if (smw_free) {
		vfree(smw_free);
		smw_free = NULL;
	}
	unload_sb(&cfg);
	if (sbmpu)
		unload_sbmpu(&cfg_mpu);
	SOUND_LOCK_END;

	if(sb_dev)	sb_dev->deactivate(sb_dev);
	if(jp_dev)	jp_dev->deactivate(jp_dev);
	if(wt_dev)	wt_dev->deactivate(wt_dev);
	if(mpu_dev)	mpu_dev->deactivate(mpu_dev);
	if(wss_dev)	wss_dev->deactivate(wss_dev);
}

module_init(init_sb);
module_exit(cleanup_sb);

#ifndef MODULE
static int __init setup_sb(char *str)
{
	/* io, irq, dma, dma2 */
	int ints[5];
	
	str = get_options(str, ARRAY_SIZE(ints), ints);
	
	io	= ints[1];
	irq	= ints[2];
	dma	= ints[3];
	dma16	= ints[4];

	return 1;
}
__setup("sb=", setup_sb);
#endif
