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
 * 26th November 1999 - patched to compile without ISA PnP support in the
 * kernel. -Daniel Stone (tamriel@ductape.net) 
 *
 * 06-01-2000 Refined and bugfixed ISA PnP support, added
 *  CMI 8330 support - Alessandro Zummo <azummo@ita.flashnet.it>
 *
 *
 * 04-02-2000 Added Soundblaster AWE 64 PnP support, isapnpjump
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

#ifdef CONFIG_SBDSP

#include "sb_mixer.h"
#include "sb.h"

static int sbmpu = 0;

void attach_sb_card(struct address_info *hw_config)
{
	if(!sb_dsp_init(hw_config))
		hw_config->slots[0] = -1;
}

int probe_sb(struct address_info *hw_config)
{
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

	/* This is useless since is done by sb_dsp_detect - azummo*/
	
	if (check_region(hw_config->io_base, 16))
	{
		printk(KERN_ERR "sb_card: I/O port %x is already in use\n\n", hw_config->io_base);
		return 0;
	}
	return sb_dsp_detect(hw_config, 0, 0);
}

void unload_sb(struct address_info *hw_config)
{
	if(hw_config->slots[0]!=-1)
		sb_dsp_unload(hw_config, sbmpu);
}

int sb_be_quiet=0;
extern int esstype;	/* ESS chip type */

#ifdef MODULE

static struct address_info config;
static struct address_info config_mpu;

struct pci_dev 	*sb_dev 	= NULL, 
				*wss_dev 	= NULL, 
				*jp_dev 	= NULL, 
				*mpu_dev 	= NULL, 
				*wt_dev 	= NULL;
/*
 *    Note DMA2 of -1 has the right meaning in the SB16 driver as well
 *    as here. It will cause either an error if it is needed or a fallback
 *    to the 8bit channel.
 */

int mpu_io 	= 0;
int io 		= -1;
int irq 	= -1;
int dma 	= -1;
int dma16 	= -1;		/* Set this for modules that need it */
int type 	= 0;		/* Can set this to a specific card type */
int mad16 	= 0;		/* Set mad16=1 to load this as support for mad16 */
int trix 	= 0;		/* Set trix=1 to load this as support for trix */
int pas2 	= 0;		/* Set pas2=1 to load this as support for pas2 */
int support 	= 0;		/* Set support to load this as a support module */
int sm_games	= 0;		/* Mixer - see sb_mixer.c */
int acer 	= 0;		/* Do acer notebook init */

#ifdef CONFIG_ISAPNP
int isapnp 		= 1;
int isapnpjump 		= 0;
#else
int isapnp 		= 0;
#endif

MODULE_DESCRIPTION("Soundblaster driver");

MODULE_PARM(io, 	"i");
MODULE_PARM(irq, 	"i");
MODULE_PARM(dma, 	"i");
MODULE_PARM(dma16, 	"i");
MODULE_PARM(mpu_io, 	"i");
MODULE_PARM(type, 	"i");
MODULE_PARM(mad16, 	"i");
MODULE_PARM(support, 	"i");
MODULE_PARM(trix, 	"i");
MODULE_PARM(pas2, 	"i");
MODULE_PARM(sm_games, 	"i");
MODULE_PARM(esstype, 	"i");
MODULE_PARM(acer, 	"i");

#ifdef CONFIG_ISAPNP
MODULE_PARM(isapnp, 	"i");
MODULE_PARM(isapnpjump, "i");
MODULE_PARM_DESC(isapnp,	"When set to 0, Plug & Play support will be disabled");
MODULE_PARM_DESC(isapnpjump, 	"Jumps to a specific slot in the driver's PnP table. Use the source, Luke.");
#endif

MODULE_PARM_DESC(io, 		"Soundblaster i/o base address (0x220,0x240,0x260,0x280)");
MODULE_PARM_DESC(irq,		"IRQ (5,7,9,10)");
MODULE_PARM_DESC(dma,		"8-bit DMA channel (0,1,3)");
MODULE_PARM_DESC(dma16,		"16-bit DMA channel (5,6,7)");
MODULE_PARM_DESC(mpu_io,	"Mpu base address");
MODULE_PARM_DESC(type,		"You can set this to specific card type");
MODULE_PARM_DESC(mad16,		"Enable MAD16 support");
MODULE_PARM_DESC(trix,		"Enable Audiotrix support");
MODULE_PARM_DESC(pas2,		"Enable Pas2 support");
MODULE_PARM_DESC(support,	"Set this to load as generic support module");
MODULE_PARM_DESC(sm_games,	"Enable support for Logitech soundman games");
MODULE_PARM_DESC(esstype,	"ESS chip type");
MODULE_PARM_DESC(acer,		"Set this to detect cards in some ACER notebooks");

void *smw_free = NULL;

#ifdef CONFIG_ISAPNP

/* That's useful. */

static int check_base(char *devname, char *resname, struct resource *res)
{
	if (check_region(res->start, res->end - res->start))
	{
		printk(KERN_ERR "sb: %s %s error, i/o at %#lx already in use\n", devname, resname, res->start);
		return 0;
	}

	printk(KERN_INFO "sb: %s %s base located at %#lx\n", devname, resname, res->start);
	return 1;
}


/* Card's specific initialization functions
 */

static struct pci_dev *sb_init_generic(struct pci_bus *card, struct address_info *hw_config, struct address_info *mpu_config)
{
	if((sb_dev = isapnp_find_dev(card,
				card->vendor,
				card->device,
				NULL)))
	{
		sb_dev->prepare(sb_dev);
		sb_dev->activate(sb_dev);

		if (!sb_dev->resource[0].start)
			return(NULL);

		hw_config->io_base 	= sb_dev->resource[0].start;
		hw_config->irq 		= sb_dev->irq_resource[0].start;
		hw_config->dma 		= sb_dev->dma_resource[0].start;
		hw_config->dma2 	= sb_dev->dma_resource[1].start;
		mpu_config->io_base     = sb_dev->resource[1].start;
	}
	return(sb_dev);
}

static struct pci_dev *sb_init_ess(struct pci_bus *card, struct address_info *hw_config, struct address_info *mpu_config)
{
	if((sb_dev = isapnp_find_dev(card,
				card->vendor,
				card->device,
				NULL)))
	{
		sb_dev->prepare(sb_dev);
		sb_dev->activate(sb_dev);

		if (!sb_dev->resource[0].start)
			return(NULL);

		hw_config->io_base 	= sb_dev->resource[0].start;
		hw_config->irq 		= sb_dev->irq_resource[0].start;
		hw_config->dma 		= sb_dev->dma_resource[0].start;
		hw_config->dma2 	= sb_dev->dma_resource[1].start;
		mpu_config->io_base     = sb_dev->resource[2].start;
	}
	return(sb_dev);
}

static struct pci_dev *sb_init_cmi(struct pci_bus *card, struct address_info *hw_config, struct address_info *mpu_config)
{
	/* What a stupid chip... where did they get all those @@@ ?*/

	printk(KERN_INFO "sb: CMI8330 detected\n");

	/* Soundblaster compatible logical device. */

	if((sb_dev = isapnp_find_dev(card,
				ISAPNP_VENDOR('@','X','@'), ISAPNP_FUNCTION(0x0001), NULL)))
	{
#ifdef CMI8330_DMA0BAD
		int dmahack = 0;
#endif
		sb_dev->prepare(sb_dev);
		
		/*  This device doesn't work with DMA 0, so we must allocate
			it to prevent PnP routines to assign it to the card.

			I know i could have inlined the following lines, but it's cleaner
			this way.
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

		if(sb_dev->activate(sb_dev) >= 0)
		{
			hw_config->io_base 	= sb_dev->resource[0].start;
			hw_config->irq 		= sb_dev->irq_resource[0].start;
			hw_config->dma 		= sb_dev->dma_resource[0].start;
			hw_config->dma2 	= sb_dev->dma_resource[1].start;

			check_base("CMI8330", "sb", &sb_dev->resource[0]);
		}
		else
			printk(KERN_ERR "sb: CMI8330 sb config failed (out of resources?)\n");

#ifdef CMI8330_DMA0BAD
		if(dmahack)
			free_dma(0);
#endif
	}
	else
		printk(KERN_ERR "sb: CMI8330 panic! sb base not found\n");

	if((mpu_dev = isapnp_find_dev(card,
				ISAPNP_VENDOR('@','H','@'), ISAPNP_FUNCTION(0x0001), NULL)))
	{
		mpu_dev->prepare(mpu_dev);

		/* This disables the interrupt on this resource. Do we need it ? */

		mpu_dev->irq_resource[0].flags = 0;

		if(mpu_dev->activate(mpu_dev) >= 0)
		{
			if( check_base("CMI8330", "mpu", &mpu_dev->resource[0]) ) 
				mpu_config->io_base = mpu_dev->resource[0].start;
		}
		else
			printk(KERN_ERR "sb: CMI8330 mpu config failed (out of resources?)\n");
	}
	else
		printk(KERN_ERR "sb: CMI8330 panic! mpu not found\n");


	/* Gameport. */

	if((jp_dev = isapnp_find_dev(card,
				ISAPNP_VENDOR('@','P','@'), ISAPNP_FUNCTION(0x0001), NULL)))
	{
		jp_dev->prepare(jp_dev);
		
		if(jp_dev->activate(jp_dev) >= 0)
		{
			check_base("CMI8330", "gameport", &jp_dev->resource[0]);
		}
		else
			printk(KERN_ERR "sb: CMI8330 gameport config failed (out of resources?)\n");
	}
	else
		printk(KERN_ERR "sb: CMI8330 panic! gameport not found\n");


	/* OPL3 support */

#if defined(CONFIG_SOUND_YM3812) || defined(CONFIG_SOUND_YM3812_MODULE)
	if((wss_dev = isapnp_find_dev(card,
				ISAPNP_VENDOR('@','@','@'), ISAPNP_FUNCTION(0x0001), NULL)))
	{
		wss_dev->prepare(wss_dev);

		/* Let's disable IRQ and DMA for WSS device */

		wss_dev->irq_resource[0].flags = 0;
		wss_dev->dma_resource[0].flags = 0;

		if(wss_dev->activate(wss_dev) >= 0)
		{
			check_base("CMI8330", "opl3", &wss_dev->resource[1]);
		}
		else
			printk(KERN_ERR "sb: CMI8330 opl3 config failed (out of resources?)\n");
	}
	else
		printk(KERN_ERR "sb: CMI8330 panic! opl3 not found\n");
#endif

	printk(KERN_INFO "sb: CMI8330 mail reports to Alessandro Zummo <azummo@ita.flashnet.it>\n");

	return(sb_dev);
}

static struct pci_dev *sb_init_awe64(struct pci_bus *card, struct address_info *hw_config, struct address_info *mpu_config)
{
	printk(KERN_INFO "sb: SoundBlaster AWE 64 detected\n");

	/* CTL0042:Audio. */

	if((sb_dev = isapnp_find_dev(card,
				ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0042), NULL)))
	{
		sb_dev->prepare(sb_dev);
		
		if(sb_dev->activate(sb_dev) >= 0)
		{
			hw_config->io_base 	= sb_dev->resource[0].start;
			hw_config->irq 		= sb_dev->irq_resource[0].start;
			hw_config->dma 		= sb_dev->dma_resource[0].start;
			hw_config->dma2 	= sb_dev->dma_resource[1].start;

			mpu_config->io_base	= sb_dev->resource[1].start;

			check_base("AWE64", "sb", 	&sb_dev->resource[0]);
			check_base("AWE64", "mpu", 	&sb_dev->resource[1]);
			check_base("AWE64", "opl3",	&sb_dev->resource[2]);
		}
		else
			printk(KERN_ERR "sb: AWE64 sb config failed (out of resources?)\n");
	}
	else
		printk(KERN_ERR "sb: AWE64 panic! sb base not found\n");


	/* CTL7002:Game */

	if((jp_dev = isapnp_find_dev(card,
				ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x7002), NULL)))
	{
		jp_dev->prepare(jp_dev);
		
		if(jp_dev->activate(jp_dev) >= 0)
		{
			check_base("AWE64", "gameport", &jp_dev->resource[0]);
		}
		else
			printk(KERN_ERR "sb: AWE64 gameport config failed (out of resources?)\n");
	}
	else
		printk(KERN_ERR "sb: AWE64 panic! gameport not found\n");


	/* CTL0022:WaveTable */

	if((wt_dev = isapnp_find_dev(card,
				ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0022), NULL)))
	{
		wt_dev->prepare(wt_dev);
		
		if(wt_dev->activate(wt_dev) >= 0)
		{
			check_base("AWE64", "wavetable", &wt_dev->resource[0]);
			check_base("AWE64", "wavetable", &wt_dev->resource[1]);
			check_base("AWE64", "wavetable", &wt_dev->resource[2]);
		}
		else
			printk(KERN_ERR "sb: AWE64 wavetable config failed (out of resources?)\n");
	}
	else
		printk(KERN_ERR "sb: AWE64 panic! wavetable not found\n");

	printk(KERN_INFO "sb: AWE64 mail reports to Alessandro Zummo <azummo@ita.flashnet.it>\n");

	return(sb_dev);
}


static struct { unsigned short vendor, function; struct pci_dev * (*initfunc)(struct pci_bus *, struct address_info *, struct address_info *); char *name; }
isapnp_sb_list[] __initdata = {
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0001), &sb_init_generic,	"Sound Blaster 16" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0031), &sb_init_generic,	"Sound Blaster 16" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0041), &sb_init_generic,	"Sound Blaster 16" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0042), &sb_init_generic,	"Sound Blaster 16" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0043), &sb_init_generic,	"Sound Blaster 16" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0044), &sb_init_generic,	"Sound Blaster 16" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x0045), &sb_init_generic,	"Sound Blaster 16" },
	{ISAPNP_VENDOR('C','T','L'), ISAPNP_FUNCTION(0x009D), &sb_init_awe64,	"Sound Blaster AWE 64" },
	{ISAPNP_VENDOR('E','S','S'), ISAPNP_FUNCTION(0x1868), &sb_init_ess,		"ESS 1868" },
	{ISAPNP_VENDOR('E','S','S'), ISAPNP_FUNCTION(0x8611), &sb_init_ess,		"ESS 1868" },
	{ISAPNP_VENDOR('E','S','S'), ISAPNP_FUNCTION(0x1869), &sb_init_ess,		"ESS 1869" },
	{ISAPNP_VENDOR('E','S','S'), ISAPNP_FUNCTION(0x1878), &sb_init_ess,		"ESS 1878" },
	{ISAPNP_VENDOR('E','S','S'), ISAPNP_FUNCTION(0x1879), &sb_init_ess,		"ESS 1879" },
	{ISAPNP_VENDOR('C','M','I'), ISAPNP_FUNCTION(0x0001), &sb_init_cmi, 	"CMI 8330 SoundPRO" },
    {0}
};

/* Actually this routine will detect and configure only the first card with successful
   initalization. isapnpjump could be used to jump to a specific entry.
   Please always add entries at the end of the array.
   Should this be fixed? - azummo
*/

static int __init sb_probe_isapnp(struct address_info *hw_config, struct address_info *mpu_config) {

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
		struct pci_bus *card = NULL;
			
		while ((card = isapnp_find_card(
					       isapnp_sb_list[i].vendor,
					       isapnp_sb_list[i].function,
					       card))) {

       			/* You missed the init func? That's bad. */

			if(isapnp_sb_list[i].initfunc)
			{
				struct pci_dev *idev = NULL;

				/* Initialize this baby. */

				if((idev = isapnp_sb_list[i].initfunc(card, hw_config, mpu_config)))
				{
					/* We got it. */

					printk(KERN_INFO "sb: ISAPnP reports %s at i/o %#x, irq %d, dma %d, %d\n",
					       isapnp_sb_list[i].name,
					       hw_config->io_base, hw_config->irq, hw_config->dma,
					       hw_config->dma2);
					return 0;
				}
			}
		}
	}
	return -ENODEV;
}
#endif

int init_module(void)
{
	printk(KERN_INFO "Soundblaster audio driver Copyright (C) by Hannu Savolainen 1993-1996\n");

	if (mad16 == 0 && trix == 0 && pas2 == 0 && support == 0)
	{
		/* Please remember that even with CONFIG_ISAPNP defined one should still be
			able to disable PNP support for this single driver!
		*/

#ifdef CONFIG_ISAPNP			
		if (isapnp)
		{
			if(sb_probe_isapnp(&config, &config_mpu) < 0 )
			{
				printk(KERN_ERR "sb_card: No ISAPnP cards found\n");
				return -EINVAL;
			}
		}
		else
		{
#endif		
			if (io == -1 || dma == -1 || irq == -1)
			{
				printk(KERN_ERR "sb_card: I/O, IRQ, and DMA are mandatory\n");
				return -EINVAL;
			}

			config.io_base 	= io;
			config.irq 		= irq;
			config.dma 		= dma;
			config.dma2 	= dma16;
#ifdef CONFIG_ISAPNP
		}
#endif

		/* If this is not before the #ifdef line, there's a reason... */
		config.card_subtype = type;

		if (!probe_sb(&config))
			return -ENODEV;
		attach_sb_card(&config);
	
		if(config.slots[0]==-1)
			return -ENODEV;

		if (isapnp == 0) 
			config_mpu.io_base = mpu_io;
		if (probe_sbmpu(&config_mpu))
			sbmpu = 1;
		if (sbmpu)
			attach_sbmpu(&config_mpu);
	}
	SOUND_LOCK;
	return 0;
}

void cleanup_module(void)
{
	if (smw_free)
		vfree(smw_free);
	if (!mad16 && !trix && !pas2 && !support)
		unload_sb(&config);
	if (sbmpu)
		unload_sbmpu(&config_mpu);
	SOUND_LOCK_END;

	if(sb_dev)	sb_dev->deactivate(sb_dev);
	if(jp_dev)	jp_dev->deactivate(jp_dev);
	if(wt_dev)	wt_dev->deactivate(wt_dev);
	if(mpu_dev)	mpu_dev->deactivate(mpu_dev);
	if(wss_dev)	wss_dev->deactivate(wss_dev);
}

#else

#ifdef CONFIG_SM_GAMES
int             sm_games = 1;
#else
int             sm_games = 0;
#endif
#ifdef CONFIG_SB_ACER
int             acer = 1;
#else
int             acer = 0;
#endif
#endif

EXPORT_SYMBOL(sb_dsp_init);
EXPORT_SYMBOL(sb_dsp_detect);
EXPORT_SYMBOL(sb_dsp_unload);
EXPORT_SYMBOL(sb_dsp_disable_midi);
EXPORT_SYMBOL(attach_sb_card);
EXPORT_SYMBOL(probe_sb);
EXPORT_SYMBOL(unload_sb);
EXPORT_SYMBOL(sb_be_quiet);
EXPORT_SYMBOL(attach_sbmpu);
EXPORT_SYMBOL(probe_sbmpu);
EXPORT_SYMBOL(unload_sbmpu);

#endif
