/*
 * Support for VIA 82Cxxx Audio Codecs
 * Copyright 1999,2000 Jeff Garzik <jgarzik@mandrakesoft.com>
 *
 * Distributed under the GNU GENERAL PUBLIC LICENSE (GPL) Version 2.
 * See the "COPYING" file distributed with this software for more info.
 *
 * Documentation for this driver available as
 * linux/Documentation/sound/via82cxxx.txt.
 *
 * Since the mixer is called from the OSS glue the kernel lock is always held
 * on our AC97 mixing
 */
 

#define VIA_VERSION	"1.1.2"



#include <linux/module.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

#include <asm/io.h>

#include "sound_config.h"
#include "soundmodule.h"
#include "sb.h"
#include "ac97.h"

#ifndef SOUND_LOCK
#define SOUND_LOCK do {} while (0)
#define SOUND_LOCK_END do {} while (0)
#endif

#define VIA_DEBUG 0	/* define to 1 to enable debugging output and checks */
#if VIA_DEBUG
/* note: prints function name for you */
#define DPRINTK(fmt, args...) printk(KERN_DEBUG "%s: " fmt, __FUNCTION__ , ## args)
#else
#define DPRINTK(fmt, args...)
#endif

#define VIA_NDEBUG 0	/* define to 1 to disable lightweight runtime checks */
#if VIA_NDEBUG
#define assert(expr)
#else
#define assert(expr) \
        if(!(expr)) {					\
        printk( "Assertion failed! %s,%s,%s,line=%d\n",	\
        #expr,__FILE__,__FUNCTION__,__LINE__);		\
        }
#endif

#define arraysize(x)            (sizeof(x)/sizeof(*(x)))

#define MAX_CARDS	2

#define	LINE_SIZE	10

#define VIA_CARD_NAME	"VIA 82Cxxx Audio driver " VIA_VERSION
#define VIA_MODULE_NAME "via82cxxx"
#define PFX		VIA_MODULE_NAME ": "

#define VIA_COUNTER_LIMIT	100000

/* 82C686 function 5 (audio codec) PCI configuration registers */
#define VIA_FUNC_ENABLE		0x42
#define VIA_PNP_CONTROL		0x43
#define VIA_AC97_CTRL		0x80

/* PCI configuration register bits and masks */
#define VIA_CR40_AC97_READY	0x01
#define VIA_CR40_AC97_LOW_POWER	0x02
#define VIA_CR40_SECONDARY_READY 0x04

#define VIA_CR41_ACLINK_ENABLE	0x80

#define VIA_CR42_SB_ENABLE	0x01
#define VIA_CR42_MIDI_ENABLE	0x02
#define VIA_CR42_FM_ENABLE	0x04
#define VIA_CR42_GAME_ENABLE	0x08

#define VIA_CR44_SECOND_CODEC_SUPPORT	(1 << 6)
#define VIA_CR44_AC_LINK_ACCESS		(1 << 7)

#define VIA_CR80_FIRST_CODEC		0
#define VIA_CR80_SECOND_CODEC		(1 << 30)
#define VIA_CR80_FIRST_CODEC_VALID	(1 << 25)
#define VIA_CR80_SECOND_CODEC_VALID	(1 << 27)
#define VIA_CR80_BUSY			(1 << 24)
#define VIA_CR80_READ_MODE		(1 << 23)
#define VIA_CR80_WRITE_MODE		0
#define VIA_CR80_REG_IDX(idx)		(((idx) & 0x7E) << 16)

struct via_info {
	struct address_info sb_data;
	struct address_info opl3_data;
	struct pci_dev *pdev;
	struct ac97_hwint ac97;
	int mixer_oss_dev;
	int have_ac97;
};
static struct via_info		cards [MAX_CARDS];
static unsigned			num_cards = 0;


static const struct {
	int revision;
	const char *rev_name;
} via_chip_revs[] __initdata = {
	{ 0x10, "A" },
	{ 0x11, "B" },
	{ 0x12, "C" },
	{ 0x13, "D" },
	{ 0x14, "E" },
	{ 0x20, "H" },
};

static inline void via_ac97_write32 (struct pci_dev *pdev, int port, u32 data)
{
	struct resource *rsrc = &pdev->resource[0];
	outw ((u16)data,rsrc->start+port);	
	outw ((u16)(data>>16),rsrc->start+port+2);	
}

static inline u32 via_ac97_read32 (struct pci_dev *pdev, int port)
{
	struct resource *rsrc = &pdev->resource[0];
	return
		((u32)inw (rsrc->start+port)) |
		(((u32)inw (rsrc->start+port+2)) << 16);
}

/****************************************************************
 *
 * Intel Audio Codec '97 interface
 *
 *
 */
 
static inline void via_ac97_wait_idle (struct pci_dev *pdev)
{
	u32 tmp;
	int counter = VIA_COUNTER_LIMIT;
	
	DPRINTK ("ENTER\n");

	assert (pdev != NULL);
	
	do {
		tmp = via_ac97_read32 (pdev,VIA_AC97_CTRL);
	} while ((tmp & VIA_CR80_BUSY) && (counter-- > 0));

	DPRINTK ("EXIT%s\n", counter > 0 ? "" : ", counter limit reached");
}


static int via_ac97_read_reg (struct ac97_hwint *dev, u8 reg)
{
	u32 data;
	struct via_info *card;
	struct pci_dev *pdev;
	
	DPRINTK ("ENTER\n");

	assert (dev != NULL);
	assert (dev->driver_private != NULL);

	card = (struct via_info *) dev->driver_private;
	pdev = card->pdev;
	assert (pdev != NULL);

	via_ac97_wait_idle (pdev);
	data =	VIA_CR80_FIRST_CODEC | VIA_CR80_FIRST_CODEC_VALID |
		VIA_CR80_READ_MODE | VIA_CR80_REG_IDX(reg);
	via_ac97_write32 (pdev,VIA_AC97_CTRL,data);
	via_ac97_wait_idle (pdev);
	data = via_ac97_read32 (pdev,VIA_AC97_CTRL);

#if 0
	if (! (data & VIA_CR80_FIRST_CODEC_VALID)) {
		DPRINTK ("EXIT, first codec not valid, returning -1\n");
		return -1;
	}
#endif

	DPRINTK ("EXIT, returning %d\n", data & 0xFFFF);
	return data & 0xFFFF;
}


static int via_ac97_write_reg (struct ac97_hwint *dev, u8 reg, u16 value)
{
	u32 data;
	struct via_info *card;
	struct pci_dev *pdev;
	
	DPRINTK ("ENTER\n");

	assert (dev != NULL);
	assert (dev->driver_private != NULL);

	card = (struct via_info *) dev->driver_private;
	pdev = card->pdev;
	assert (pdev != NULL);

	via_ac97_wait_idle (pdev);
	data =	VIA_CR80_FIRST_CODEC | VIA_CR80_FIRST_CODEC_VALID |
		VIA_CR80_WRITE_MODE | VIA_CR80_REG_IDX(reg) | value;
	via_ac97_write32 (pdev,VIA_AC97_CTRL,data);

#if 0
	if (! (data & VIA_CR80_FIRST_CODEC_VALID)) {
		DPRINTK ("EXIT, first codec invalid, returning -1\n");
		return -1;
	}
#endif

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


static int via_ac97_reset (struct ac97_hwint *dev)
{
	struct via_info *card;
	struct pci_dev *pdev;
	
	DPRINTK ("ENTER\n");

	assert (dev != NULL);
	assert (dev->driver_private != NULL);

	card = (struct via_info *) dev->driver_private;
	pdev = card->pdev;
	assert (pdev != NULL);
	
	pci_write_config_word (pdev, PCI_COMMAND, PCI_COMMAND_IO);

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


static struct via_info *via_ac97_find_card_for_mixer (int dev)
{
	int x;

	DPRINTK ("ENTER\n");

	for (x = 0; x < num_cards; x++)
		if (cards[x].mixer_oss_dev == dev) {
			DPRINTK ("EXIT, returning %p\n", cards + x);
			return cards + x;
		}

	DPRINTK ("EXIT, returning 0\n");
	return NULL;
}


static int
via_ac97_default_mixer_ioctl (int dev, unsigned int cmd, caddr_t arg)
{
	int rc;
	struct via_info *card = via_ac97_find_card_for_mixer (dev);

	DPRINTK ("ENTER\n");

	if (card != NULL) {
		rc = ac97_mixer_ioctl (&card->ac97, cmd, arg);
		DPRINTK ("EXIT, returning %d\n", rc);
		return rc;
	}
	
	DPRINTK ("EXIT, returning -ENODEV\n");
	return -ENODEV;

}

static struct mixer_operations via_ac97_mixer_operations =
{
	"VIA82Cxxx",
	"via82cxxxAC97Mixer",
	via_ac97_default_mixer_ioctl
};

static int __init via_attach_ac97 (struct via_info *card)
{
	int mixer;
	struct ac97_hwint *mdev;

	DPRINTK ("ENTER\n");

	assert (card != NULL);

	mdev = &card->ac97;

	memset (mdev, 0, sizeof (*mdev));
	mdev->reset_device = via_ac97_reset;
	mdev->read_reg = via_ac97_read_reg;
	mdev->write_reg = via_ac97_write_reg;
	mdev->driver_private = (void *) card;

	if (ac97_init (mdev)) {
		printk (KERN_ERR PFX "Unable to init AC97\n");
		DPRINTK ("EXIT, returning -1\n");
		return -1;
	}
	mixer = sound_alloc_mixerdev ();
	if (mixer < 0 || num_mixers >= MAX_MIXER_DEV) {
		printk (KERN_ERR PFX "Unable to alloc mixerdev\n");
		DPRINTK ("EXIT, returning -1\n");
		return -1;
	}
	mixer_devs[mixer] = &via_ac97_mixer_operations;
	card->mixer_oss_dev = mixer;

	/* Some reasonable default values.  */
	ac97_set_mixer (mdev, SOUND_MIXER_VOLUME, (85 << 8) | 85);
	ac97_set_mixer (mdev, SOUND_MIXER_SPEAKER, 100);
	ac97_set_mixer (mdev, SOUND_MIXER_PCM, (65 << 8) | 65);
	ac97_set_mixer (mdev, SOUND_MIXER_CD, (65 << 8) | 65);

	printk (KERN_INFO PFX "Initialized AC97 mixer\n");
	
	card->have_ac97 = mixer;
	
	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


static void via_unload_ac97 (struct via_info *card)
{
	DPRINTK ("ENTER\n");

	assert (card != NULL);

	if (card->have_ac97 >= 0)
		sound_unload_mixerdev (card->have_ac97);

	DPRINTK ("EXIT\n");
}


#ifdef CONFIG_PROC_FS

/****************************************************************
 *
 * /proc/driver/via82cxxx/info
 *
 *
 */

static int via_info_read_proc (char *page, char **start, off_t off,
			       int count, int *eof, void *data)
{
#define YN(val,bit) (((val) & (bit)) ? "yes" : "no")

	int len = 0, i;
	u8 r40, r41, r42, r44;
	
	DPRINTK ("ENTER\n");

	len += sprintf (page+len, VIA_CARD_NAME "\n\n");
	
	for (i = 0; i < num_cards; i++) {
		pci_read_config_byte (cards[i].pdev, 0x40, &r40);
		pci_read_config_byte (cards[i].pdev, 0x42, &r41);
		pci_read_config_byte (cards[i].pdev, 0x42, &r42);
		pci_read_config_byte (cards[i].pdev, 0x44, &r44);
	
		len += sprintf (page+len,
			"40  AC97 Codec Ready: %s\n"
			"    AC97 Codec Low-power: %s\n"
			"    Secondary Codec Ready: %s\n"

			"41  AC-Link Interface Enable: %s\n"

			"42  Game port enabled: %s\n"
			"    SoundBlaster enabled: %s\n"
			"    FM enabled: %s\n"
			"    MIDI enabled: %s\n"
			
			"44  AC-Link Interface Access: %s\n"
			"    Secondary Codec Support: %s\n"
			
			"\n",
			
			YN (r40, VIA_CR40_AC97_READY),
			YN (r40, VIA_CR40_AC97_LOW_POWER),
			YN (r40, VIA_CR40_SECONDARY_READY),

			YN (r41, VIA_CR41_ACLINK_ENABLE),

			YN (r42, VIA_CR42_GAME_ENABLE),
			YN (r42, VIA_CR42_SB_ENABLE),
			YN (r42, VIA_CR42_FM_ENABLE),
			YN (r42, VIA_CR42_MIDI_ENABLE),

			YN (r44, VIA_CR44_AC_LINK_ACCESS),
			YN (r44, VIA_CR44_SECOND_CODEC_SUPPORT)
			
			);
	}
	
	DPRINTK("EXIT, returning %d\n", len);
	return len;

#undef YN
}


/****************************************************************
 *
 * /proc/driver/via82cxxx
 *
 *
 */

static int __init via_init_proc (void)
{
	DPRINTK ("ENTER\n");

	proc_mkdir ("driver/via_audio", 0);
	create_proc_read_entry ("driver/via_audio/info", 0, 0, via_info_read_proc, NULL);
	
	DPRINTK("EXIT\n");
	return 0;
}



static void __exit via_cleanup_proc (void)
{
	DPRINTK ("ENTER\n");
	remove_proc_entry ("driver/via_audio/info", NULL);
	remove_proc_entry ("driver/via_audio", NULL);
	DPRINTK("EXIT\n");
}


#else

static inline int via_init_proc (void) { return 0; }
static inline void via_cleanup_proc (void) {}

#endif /* CONFIG_PROC_FS */


/****************************************************************
 *
 * Legacy SoundBlaster Pro, FM support via OSS
 *
 *
 */

static void __init via_attach_sb(struct address_info *hw_config)
{
	DPRINTK ("ENTER\n");

	if(!sb_dsp_init(hw_config))
		hw_config->slots[0] = -1;

	DPRINTK("EXIT\n");
}


static int __init via_probe_sb(struct address_info *hw_config)
{
	DPRINTK ("ENTER\n");

	if (check_region(hw_config->io_base, 16))
	{
		printk(KERN_DEBUG PFX "SBPro port 0x%x is already in use\n",
		       hw_config->io_base);
		return 0;
	}
	DPRINTK("EXIT after sb_dsp_detect\n");
	return sb_dsp_detect(hw_config, 0, 0);
}


static void __exit via_unload_sb(struct address_info *hw_config)
{
	DPRINTK ("ENTER\n");

	if(hw_config->slots[0] != -1)
		sb_dsp_unload(hw_config, 1);

	DPRINTK("EXIT\n");
}


static const struct {
	int sb_irq,
	    sb_dma,
	    midi_base,
	    sb_io_base;
} via_pnp_data[] __initdata = {
	{ 5, 0, 0x300, 0x220 },
	{ 7, 1, 0x310, 0x240 },
	{ 9, 2, 0x320, 0x260 },
	{ 10,3, 0x330, 0x280 },
};


/****************************************************************
 *
 * Chip setup and kernel registration
 *
 *
 */

static int __init via82cxxx_install (struct pci_dev *pcidev)
{
	int sb_io_base;
	int sb_irq;
	int sb_dma;
	int midi_base, rc;
	u8 tmp8;
	struct via_info *card = &cards[num_cards];
	
	DPRINTK ("ENTER\n");

	card->pdev = pcidev;
	card->have_ac97 = -1;
	
	/* turn off legacy features, if not already */
	pci_read_config_byte (pcidev, VIA_FUNC_ENABLE, &tmp8);
	tmp8 &= ~(VIA_CR42_SB_ENABLE | VIA_CR42_MIDI_ENABLE |
		  VIA_CR42_FM_ENABLE);
	pci_write_config_byte (pcidev, VIA_FUNC_ENABLE, tmp8);

	/* 
	 * try to init AC97 mixer device
	 */
	rc = via_attach_ac97 (card);
	if (rc) {
		printk (KERN_WARNING PFX
			"AC97 init failed, SB legacy mode only\n");
	}
	
	/* turn on legacy features */
	pci_read_config_byte (pcidev, VIA_FUNC_ENABLE, &tmp8);
	tmp8 |= VIA_CR42_SB_ENABLE | VIA_CR42_MIDI_ENABLE |
		VIA_CR42_FM_ENABLE;
	pci_write_config_byte (pcidev, VIA_FUNC_ENABLE, tmp8);

	/* read legacy PNP info byte */
	pci_read_config_byte (pcidev, VIA_PNP_CONTROL, &tmp8);
	pci_write_config_byte (pcidev, VIA_PNP_CONTROL, tmp8);
	
	sb_irq = via_pnp_data[((tmp8 >> 6) & 0x03)].sb_irq;
	sb_dma = via_pnp_data[((tmp8 >> 4) & 0x03)].sb_dma;
	midi_base = via_pnp_data[((tmp8 >> 2) & 0x03)].midi_base;
	sb_io_base = via_pnp_data[(tmp8 & 0x03)].sb_io_base;

	udelay(100);
	
	printk(KERN_INFO PFX "legacy "
	       "MIDI: 0x%X, SB: 0x%X / %d IRQ / %d DMA\n",
		midi_base, sb_io_base, sb_irq, sb_dma);
		
	card->sb_data.name = VIA_CARD_NAME;
	card->sb_data.card_subtype = MDL_SBPRO;
	card->sb_data.io_base = sb_io_base;
	card->sb_data.irq = sb_irq;
	card->sb_data.dma = sb_dma;
	
	/* register legacy SoundBlaster Pro */
	if (!via_probe_sb (&card->sb_data)) {
		printk (KERN_ERR PFX
			"SB probe @ 0x%X failed, aborting\n",
			sb_io_base);
		DPRINTK ("EXIT, returning -1\n");
		return -1;
	}
	via_attach_sb (&card->sb_data);

	card->opl3_data.name = card->sb_data.name;
	card->opl3_data.io_base = midi_base;
	card->opl3_data.irq = -1;
	
	/* register legacy MIDI */
	if (!probe_uart401 (&card->opl3_data)) {
		printk (KERN_WARNING PFX
			"MIDI probe @ 0x%X failed, continuing\n",
			midi_base);
		card->opl3_data.io_base = 0;
	} else {
		attach_uart401 (&card->opl3_data);
	}

	num_cards++;	
	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


/*
 * 	This loop walks the PCI configuration database and finds where
 *	the sound cards are.
 *
 *	Note - only a single PCI scan occurs, eliminating possibility
 *	of multiple audio chips
 *
 */
 
static int __init probe_via82cxxx (void)
{
	struct pci_dev *pcidev = NULL;

	DPRINTK ("ENTER\n");

	pcidev = pci_find_device (PCI_VENDOR_ID_VIA,
				  PCI_DEVICE_ID_VIA_82C686_5, NULL);

	if (!pcidev || via82cxxx_install (pcidev) != 0) {
		printk (KERN_ERR PFX "audio init failed\n");
		DPRINTK ("EXIT, returning -1\n");
		return -1;
	}

	DPRINTK ("EXIT, returning 0\n");
	return 0;
}


/*
 *	This function is called when the user or kernel loads the 
 *	module into memory.
 */


static int __init init_via82cxxx_module(void)
{
	u8 tmp;
	int i;
	const char *rev = "unknown!";
	
	memset (cards, 0, sizeof (cards));
	
	DPRINTK ("ENTER\n");

	if (!pci_present ()) {
		printk (KERN_DEBUG PFX "PCI not present, exiting\n");
		DPRINTK ("EXIT, returning -ENODEV\n");
		return -ENODEV;
	}

	if (probe_via82cxxx() != 0) {
		printk(KERN_ERR PFX "probe failed, aborting\n");
		/* XXX unload cards registered so far, if any */
		DPRINTK ("EXIT, returning -ENODEV\n");
		return -ENODEV;
	}

	pci_read_config_byte (cards[0].pdev, PCI_REVISION_ID, &tmp);
	for (i = 0; i < arraysize(via_chip_revs); i++)
		if (via_chip_revs[i].revision == tmp) {
			rev = via_chip_revs[i].rev_name;
			break;
		}
	printk (KERN_INFO PFX VIA_CARD_NAME " loaded\n");
	printk (KERN_INFO PFX "Chip rev %s.  Features: SBPro compat%s%s\n",
		rev,
		cards[0].opl3_data.io_base == 0 ? "" : ", MPU-401 MIDI",
		cards[0].have_ac97 == -1 ? "" : ", AC97 mixer");
	
	if (via_init_proc () != 0) {
		printk (KERN_WARNING PFX
			"Unable to init experimental /proc, ignoring\n");
	}

	/*
	 *	Binds us to the sound subsystem	
	 */
	SOUND_LOCK;
	DPRINTK ("EXIT, returning 0\n");
	return 0;
}

/*
 *	This is called when it is removed. It will only be removed 
 *	when its use count is 0. For sound the SOUND_LOCK/SOUND_UNLOCK
 *	macros hide the entire work for this.
 */
 
static void __exit cleanup_via82cxxx_module(void)
{
	DPRINTK("ENTER\n");
	
	if (cards[0].opl3_data.io_base)
		unload_uart401 (&cards[0].opl3_data);

	via_unload_sb (&cards[0].sb_data);
	
	via_unload_ac97 (&cards[0]);

	via_cleanup_proc ();
	
	/*
	 *	Final clean up with the sound layer
	 */
	SOUND_LOCK_END;

	DPRINTK("EXIT\n");
}

module_init(init_via82cxxx_module);
module_exit(cleanup_via82cxxx_module);

