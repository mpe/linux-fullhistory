/*
 * Copyright (C) by Hannu Savolainen 1993-1997
 *
 * OSS/Free for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
#include <linux/config.h>
#include <linux/module.h>
/*
 * sound/mad16.c
 *
 * Initialization code for OPTi MAD16 compatible audio chips. Including
 *
 *      OPTi 82C928     MAD16           (replaced by C929)
 *      OAK OTI-601D    Mozart
 *      OPTi 82C929     MAD16 Pro
 *      OPTi 82C930
 *      OPTi 82C924
 *
 * These audio interface chips don't produce sound themselves. They just
 * connect some other components (OPL-[234] and a WSS compatible codec)
 * to the PC bus and perform I/O, DMA and IRQ address decoding. There is
 * also a UART for the MPU-401 mode (not 82C928/Mozart).
 * The Mozart chip appears to be compatible with the 82C928 (can anybody
 * confirm this?).
 *
 * NOTE! If you want to set CD-ROM address and/or joystick enable, define
 *       MAD16_CONF in local.h as combination of the following bits:
 *
 *      0x01    - joystick disabled
 *
 *      CD-ROM type selection (select just one):
 *      0x00    - none
 *      0x02    - Sony 31A
 *      0x04    - Mitsumi
 *      0x06    - Panasonic (type "LaserMate", not "Sound Blaster")
 *      0x08    - Secondary IDE (address 0x170)
 *      0x0a    - Primary IDE (address 0x1F0)
 *      
 *      For example Mitsumi with joystick disabled = 0x04|0x01 = 0x05
 *      For example LaserMate (for use with sbpcd) plus joystick = 0x06
 *      
 *    MAD16_CDSEL:
 *      This defaults to CD I/O 0x340, no IRQ and DMA3 
 *      (DMA5 with Mitsumi or IDE). If you like to change these, define
 *      MAD16_CDSEL with the following bits:
 *
 *      CD-ROM port: 0x00=340, 0x40=330, 0x80=360 or 0xc0=320
 *      OPL4 select: 0x20=OPL4, 0x00=OPL3
 *      CD-ROM irq: 0x00=disabled, 0x04=IRQ5, 0x08=IRQ7, 0x0c=IRQ3, 0x10=IRQ9,
 *                  0x14=IRQ10 and 0x18=IRQ11.
 *
 *      CD-ROM DMA (Sony or Panasonic): 0x00=DMA3, 0x01=DMA2, 0x02=DMA1 or 0x03=disabled
 *   or
 *      CD-ROM DMA (Mitsumi or IDE):    0x00=DMA5, 0x01=DMA6, 0x02=DMA7 or 0x03=disabled
 *
 *      For use with sbpcd, address 0x340, set MAD16_CDSEL to 0x03 or 0x23.
 *
 *	Changes
 *	
 *	Alan Cox		Clean up, added module selections.
 *
 *	A. Wik			Added support for Opti924 PnP.
 *				Improved debugging support.	16-May-1998
 *				Fixed bug.			16-Jun-1998
 *
 *     Torsten Duwe            Made Opti924 PnP support non-destructive
 *                                                             1998-12-23
 */

#include "sound_config.h"
#include "soundmodule.h"

#ifdef MODULE
#define MAD16_CDSEL   mad16_cdsel
#define MAD16_CONF    mad16_conf

static int      mad16_conf;
static int      mad16_cdsel;

#endif

#ifdef CONFIG_MAD16

#include "sb.h"

static int      already_initialized = 0;

#define C928	1
#define MOZART	2
#define C929	3
#define C930	4
#define C924    5

/*
 *    Registers
 *
 *      The MAD16 occupies I/O ports 0xf8d to 0xf93 (fixed locations).
 *      All ports are inactive by default. They can be activated by
 *      writing 0xE2 or 0xE3 to the password register. The password is valid
 *      only until the next I/O read or write.
 *
 *      82C930 uses 0xE4 as the password and indirect addressing to access
 *      the config registers.
 */

#define MC0_PORT	0xf8c	/* Dummy port */
#define MC1_PORT	0xf8d	/* SB address, CD-ROM interface type, joystick */
#define MC2_PORT	0xf8e	/* CD-ROM address, IRQ, DMA, plus OPL4 bit */
#define MC3_PORT	0xf8f
#define PASSWD_REG	0xf8f
#define MC4_PORT	0xf90
#define MC5_PORT	0xf91
#define MC6_PORT	0xf92
#define MC7_PORT	0xf93
#define MC8_PORT	0xf94
#define MC9_PORT	0xf95
#define MC10_PORT	0xf96
#define MC11_PORT	0xf97
#define MC12_PORT	0xf98

static int      board_type = C928;

static int     *mad16_osp;
static int	c931_detected;	/* minor differences from C930 */
static char	c924pnp = 0;	/* "     "           "    C924 */
static int	debug = 0;	/* debugging output */

#ifdef DDB
#undef DDB
#endif
#define DDB(x) {if (debug) x;}

static unsigned char mad_read(int port)
{
	unsigned long flags;
	unsigned char tmp;

	save_flags(flags);
	cli();

	switch (board_type)	/* Output password */
	{
		case C928:
		case MOZART:
			outb((0xE2), PASSWD_REG);
			break;

		case C929:
			outb((0xE3), PASSWD_REG);
			break;

		case C930:
			/* outb(( 0xE4),  PASSWD_REG); */
			break;

		case C924:
			/* the c924 has its ports relocated by -128 if
			   PnP is enabled  -aw */
			if (!c924pnp)
				outb((0xE5), PASSWD_REG); else
				outb((0xE5), PASSWD_REG - 0x80);
			break;
	}

	if (board_type == C930)
	{
		outb((port - MC0_PORT), 0xe0e);	/* Write to index reg */
		tmp = inb(0xe0f);	/* Read from data reg */
	}
	else
		if (!c924pnp)
			tmp = inb(port); else
			tmp = inb(port-0x80);
	restore_flags(flags);

	return tmp;
}

static void mad_write(int port, int value)
{
	unsigned long   flags;

	save_flags(flags);
	cli();

	switch (board_type)	/* Output password */
	{
		case C928:
		case MOZART:
			outb((0xE2), PASSWD_REG);
			break;

		case C929:
			outb((0xE3), PASSWD_REG);
			break;

		case C930:
			/* outb(( 0xE4),  PASSWD_REG); */
			break;

		case C924:
			if (!c924pnp)
				outb((0xE5), PASSWD_REG); else
				outb((0xE5), PASSWD_REG - 0x80);
			break;
	}

	if (board_type == C930)
	{
		outb((port - MC0_PORT), 0xe0e);	/* Write to index reg */
		outb(((unsigned char) (value & 0xff)), 0xe0f);
	}
	else
		if (!c924pnp)
			outb(((unsigned char) (value & 0xff)), port); else
			outb(((unsigned char) (value & 0xff)), port-0x80);
	restore_flags(flags);
}

static int detect_c930(void)
{
	unsigned char   tmp = mad_read(MC1_PORT);

	if ((tmp & 0x06) != 0x06)
	{
		DDB(printk("Wrong C930 signature (%x)\n", tmp));
		/* return 0; */
	}
	mad_write(MC1_PORT, 0);

	if (mad_read(MC1_PORT) != 0x06)
	{
		DDB(printk("Wrong C930 signature2 (%x)\n", tmp));
		/* return 0; */
	}
	mad_write(MC1_PORT, tmp);	/* Restore bits */

	mad_write(MC7_PORT, 0);
	if ((tmp = mad_read(MC7_PORT)) != 0)
	{
		DDB(printk("MC7 not writable (%x)\n", tmp));
		return 0;
	}
	mad_write(MC7_PORT, 0xcb);
	if ((tmp = mad_read(MC7_PORT)) != 0xcb)
	{
		DDB(printk("MC7 not writable2 (%x)\n", tmp));
		return 0;
	}

	tmp = mad_read(MC0_PORT+18);
	if (tmp == 0xff || tmp == 0x00)
		return 1;
	/* We probably have a C931 */
	DDB(printk("Detected C931 config=0x%02x\n", tmp));
	c931_detected = 1;

	/*
         * We cannot configure the chip if it is in PnP mode.
         * If we have a CSN assigned (bit 8 in MC13) we first try
         * a software reset, then a software power off, finally
         * Clearing PnP mode. The last option is not
	 * Bit 8 in MC13 
         */
	if ((mad_read(MC0_PORT+13) & 0x80) == 0)
		return 1;

	/* Software reset */
	mad_write(MC9_PORT, 0x02);
	mad_write(MC9_PORT, 0x00);

	if ((mad_read(MC0_PORT+13) & 0x80) == 0)
		return 1;
	
	/* Power off, and on again */
	mad_write(MC9_PORT, 0xc2);
	mad_write(MC9_PORT, 0xc0);

	if ((mad_read(MC0_PORT+13) & 0x80) == 0)
		return 1;
	
#if 0	
	/* Force off PnP mode. This is not recommended because
	 * the PnP bios will not recognize the chip on the next
	 * warm boot and may assignd different resources to other
	 * PnP/PCI cards.
	 */
	mad_write(MC0_PORT+17, 0x04);
#endif
	return 1;
}

static int detect_mad16(void)
{
	unsigned char tmp, tmp2, bit;
	int i, port;

	/*
	 * Check that reading a register doesn't return bus float (0xff)
	 * when the card is accessed using password. This may fail in case
	 * the card is in low power mode. Normally at least the power saving
	 * mode bit should be 0.
	 */

	if ((tmp = mad_read(MC1_PORT)) == 0xff)
	{
		DDB(printk("MC1_PORT returned 0xff\n"));
		return 0;
	}
	for (i = 0xf8d; i <= 0xf98; i++)
		if (!c924pnp)
			DDB(printk("Port %0x (init value) = %0x\n", i, mad_read(i))) else
			DDB(printk("Port %0x (init value) = %0x\n", i-0x80, mad_read(i)));

	if (board_type == C930)
		return detect_c930();

	/*
	 * Now check that the gate is closed on first I/O after writing
	 * the password. (This is how a MAD16 compatible card works).
	 */

	if ((tmp2 = inb(MC1_PORT)) == tmp)	/* It didn't close */
	{
		DDB(printk("MC1_PORT didn't close after read (0x%02x)\n", tmp2));
		return 0;
	}

	bit  = (c924pnp) ?     0x20 : 0x80;
	port = (c924pnp) ? MC2_PORT : MC1_PORT;

	tmp = mad_read(port);
	mad_write(port, tmp ^ bit);	/* Toggle a bit */
	if ((tmp2 = mad_read(port)) != (tmp ^ bit))	/* Compare the bit */
	{
		mad_write(port, tmp);	/* Restore */
		DDB(printk("Bit revert test failed (0x%02x, 0x%02x)\n", tmp, tmp2));
		return 0;
	}
	mad_write(port, tmp);	/* Restore */
	return 1;		/* Bingo */
}

static int wss_init(struct address_info *hw_config)
{
	int ad_flags = 0;

	/*
	 *    Verify the WSS parameters
	 */

	if (check_region(hw_config->io_base, 8))
	{
		printk(KERN_ERR "MSS: I/O port conflict\n");
		return 0;
	}
	if (!ad1848_detect(hw_config->io_base + 4, &ad_flags, mad16_osp))
		return 0;
	/*
	 * Check if the IO port returns valid signature. The original MS Sound
	 * system returns 0x04 while some cards (AudioTrix Pro for example)
	 * return 0x00.
	 */

	if ((inb(hw_config->io_base + 3) & 0x3f) != 0x04 &&
	    (inb(hw_config->io_base + 3) & 0x3f) != 0x00)
	{
		DDB(printk("No MSS signature detected on port 0x%x (0x%x)\n", hw_config->io_base, inb(hw_config->io_base + 3)));
		return 0;
	}
	if (hw_config->irq > 11)
	{
		printk(KERN_ERR "MSS: Bad IRQ %d\n", hw_config->irq);
		return 0;
	}
	if (hw_config->dma != 0 && hw_config->dma != 1 && hw_config->dma != 3)
	{
		printk(KERN_ERR "MSS: Bad DMA %d\n", hw_config->dma);
		return 0;
	}
	/*
	 * Check that DMA0 is not in use with a 8 bit board.
	 */

	if (hw_config->dma == 0 && inb(hw_config->io_base + 3) & 0x80)
	{
		printk("MSS: Can't use DMA0 with a 8 bit card/slot\n");
		return 0;
	}
	if (hw_config->irq > 7 && hw_config->irq != 9 && inb(hw_config->io_base + 3) & 0x80)
		printk(KERN_ERR "MSS: Can't use IRQ%d with a 8 bit card/slot\n", hw_config->irq);
	return 1;
}

static int init_c930(struct address_info *hw_config)
{
	unsigned char cfg = 0;

#ifdef MAD16_CONF
	cfg |= (0x0f & MAD16_CONF);
#endif

	if(c931_detected)
	{
		/* Bit 0 has reversd meaning. Bits 1 and 2 sese
		   reversed on write.
		   Support only IDE cdrom. IDE port programmed
		   somewhere else. */
		cfg =  (cfg & 0x09) ^ 0x07;
	}

	switch (hw_config->io_base)
	{
		case 0x530:
			cfg |= 0x00;
			break;
		case 0xe80:
			cfg |= 0x10;
			break;
		case 0xf40:
			cfg |= 0x20;
			break;
		case 0x604:
			cfg |= 0x30;
			break;
		default:
			printk(KERN_ERR "MAD16: Invalid codec port %x\n", hw_config->io_base);
			return 0;
	}
	mad_write(MC1_PORT, cfg);

	/* MC2 is CD configuration. Don't touch it. */

	mad_write(MC3_PORT, 0);	/* Disable SB mode IRQ and DMA */

	/* bit 2 of MC4 reverses it's meaning between the C930
	   and the C931. */
	cfg = c931_detected ? 0x04 : 0x00;
#ifdef MAD16_CDSEL
	if(MAD16_CDSEL & 0x20)
		mad_write(MC4_PORT, 0x62|cfg);	/* opl4 */
	else
		mad_write(MC4_PORT, 0x52|cfg);	/* opl3 */
#else
	mad_write(MC4_PORT, 0x52|cfg);
#endif
	mad_write(MC5_PORT, 0x3C);	/* Init it into mode2 */
	mad_write(MC6_PORT, 0x02);	/* Enable WSS, Disable MPU and SB */
	mad_write(MC7_PORT, 0xCB);
	mad_write(MC10_PORT, 0x11);

	return wss_init(hw_config);
}

static int chip_detect(void)
{
	int i;

	/*
	 *    Then try to detect with the old password
	 */
	board_type = C924;

	DDB(printk("Detect using password = 0xE5\n"));
	
	if (!detect_mad16())	/* No luck. Try different model */
	{
		board_type = C928;

		DDB(printk("Detect using password = 0xE2\n"));

		if (!detect_mad16())
		{
			board_type = C929;

			DDB(printk("Detect using password = 0xE3\n"));

			if (!detect_mad16())
			{
				if (inb(PASSWD_REG) != 0xff)
					return 0;

				/*
				 * First relocate MC# registers to 0xe0e/0xe0f, disable password 
				 */

				outb((0xE4), PASSWD_REG);
				outb((0x80), PASSWD_REG);

				board_type = C930;

				DDB(printk("Detect using password = 0xE4\n"));

				for (i = 0xf8d; i <= 0xf93; i++)
					DDB(printk("port %03x = %02x\n", i, mad_read(i)));
                                if(!detect_mad16()) {

				  /* The C931 has the password reg at F8D */
				  outb((0xE4), 0xF8D);
				  outb((0x80), 0xF8D);
				  DDB(printk("Detect using password = 0xE4 for C931\n"));

				  if (!detect_mad16()) {
				    board_type = C924;
				    c924pnp++;
				    DDB(printk("Detect using password = 0xE5 (again), port offset -0x80\n"));
				    if (!detect_mad16()) {
				      c924pnp=0;
				      return 0;
				    }
				  
				    DDB(printk("mad16.c: 82C924 PnP detected\n"));
				  }
				}
				else
				  DDB(printk("mad16.c: 82C930 detected\n"));
			} else
				DDB(printk("mad16.c: 82C929 detected\n"));
		} else {
			unsigned char model;

			if (((model = mad_read(MC3_PORT)) & 0x03) == 0x03) {
				DDB(printk("mad16.c: Mozart detected\n"));
				board_type = MOZART;
			} else {
				DDB(printk("mad16.c: 82C928 detected???\n"));
				board_type = C928;
			}
		}
	}
	return 1;
}

int probe_mad16(struct address_info *hw_config)
{
	int i;
	static int valid_ports[] = 
	{
		0x530, 0xe80, 0xf40, 0x604
	};
	unsigned char tmp;
	unsigned char cs4231_mode = 0;

	int ad_flags = 0;

	if (already_initialized)
		return 0;

	mad16_osp = hw_config->osp;

	/*
	 *    Check that all ports return 0xff (bus float) when no password
	 *      is written to the password register.
	 */

	DDB(printk("--- Detecting MAD16 / Mozart ---\n"));
	if (!chip_detect())
		return 0;

	if (board_type == C930)
		return init_c930(hw_config);


	for (i = 0xf8d; i <= 0xf93; i++)
		if (!c924pnp)
			DDB(printk("port %03x = %02x\n", i, mad_read(i))) else
			DDB(printk("port %03x = %02x\n", i-0x80, mad_read(i)));

/*
 * Set the WSS address
 */

	tmp = (mad_read(MC1_PORT) & 0x0f) | 0x80;	/* Enable WSS, Disable SB */

	for (i = 0; i < 5; i++)
	{
		if (i > 3)	/* Not a valid port */
		{
			printk(KERN_ERR "MAD16/Mozart: Bad WSS base address 0x%x\n", hw_config->io_base);
			return 0;
		}
		if (valid_ports[i] == hw_config->io_base)
		{
			tmp |= i << 4;	/* WSS port select bits */
			break;
		}
	}

	/*
	 * Set optional CD-ROM and joystick settings.
	 */

	tmp &= ~0x0f;
#if defined(MAD16_CONF)
	tmp |= ((MAD16_CONF) & 0x0f);	/* CD-ROM and joystick bits */
#endif
	mad_write(MC1_PORT, tmp);

#if defined(MAD16_CONF) && defined(MAD16_CDSEL)
	tmp = MAD16_CDSEL;
#else
	tmp = mad_read(MC2_PORT);
#endif

#ifdef MAD16_OPL4
	tmp |= 0x20;		/* Enable OPL4 access */
#endif

	mad_write(MC2_PORT, tmp);
	mad_write(MC3_PORT, 0xf0);	/* Disable SB */

	if (board_type == C924)	/* Specific C924 init values */
	{
		mad_write(MC4_PORT, 0xA0);
		mad_write(MC5_PORT, 0x05);
		mad_write(MC6_PORT, 0x03);
	}
	if (!ad1848_detect(hw_config->io_base + 4, &ad_flags, mad16_osp))
		return 0;

	if (ad_flags & (AD_F_CS4231 | AD_F_CS4248))
		cs4231_mode = 0x02;	/* CS4248/CS4231 sync delay switch */

	if (board_type == C929)
	{
		mad_write(MC4_PORT, 0xa2);
		mad_write(MC5_PORT, 0xA5 | cs4231_mode);
		mad_write(MC6_PORT, 0x03);	/* Disable MPU401 */
	}
	else
	{
		mad_write(MC4_PORT, 0x02);
		mad_write(MC5_PORT, 0x30 | cs4231_mode);
	}

	for (i = 0xf8d; i <= 0xf93; i++) if (!c924pnp)
		DDB(printk("port %03x after init = %02x\n", i, mad_read(i))) else
		DDB(printk("port %03x after init = %02x\n", i-0x80, mad_read(i)));
	wss_init(hw_config);

	return 1;
}

void attach_mad16(struct address_info *hw_config)
{

	static char     interrupt_bits[12] = {
		-1, -1, -1, -1, -1, -1, -1, 0x08, -1, 0x10, 0x18, 0x20
	};
	char bits;

	static char     dma_bits[4] = {
		1, 2, 0, 3
	};

	int config_port = hw_config->io_base + 0, version_port = hw_config->io_base + 3;
	int ad_flags = 0, dma = hw_config->dma, dma2 = hw_config->dma2;
	unsigned char dma2_bit = 0;

	already_initialized = 1;

	if (!ad1848_detect(hw_config->io_base + 4, &ad_flags, mad16_osp))
		return;

	/*
	 * Set the IRQ and DMA addresses.
	 */
	
	if (board_type == C930)
		interrupt_bits[5] = 0x28;	/* Also IRQ5 is possible on C930 */

	bits = interrupt_bits[hw_config->irq];
	if (bits == -1)
		return;

	outb((bits | 0x40), config_port);
	if ((inb(version_port) & 0x40) == 0)
		printk(KERN_ERR "[IRQ Conflict?]\n");

	/*
	 * Handle the capture DMA channel
	 */

	if (ad_flags & AD_F_CS4231 && dma2 != -1 && dma2 != dma)
	{
		if (!((dma == 0 && dma2 == 1) ||
			(dma == 1 && dma2 == 0) ||
			(dma == 3 && dma2 == 0)))
		{		/* Unsupported combination. Try to swap channels */
			int tmp = dma;

			dma = dma2;
			dma2 = tmp;
		}
		if ((dma == 0 && dma2 == 1) || (dma == 1 && dma2 == 0) ||
			(dma == 3 && dma2 == 0))
		{
			dma2_bit = 0x04;	/* Enable capture DMA */
		}
		else
		{
			printk("MAD16: Invalid capture DMA\n");
			dma2 = dma;
		}
	}
	else dma2 = dma;

	outb((bits | dma_bits[dma] | dma2_bit), config_port);	/* Write IRQ+DMA setup */

	hw_config->slots[0] = ad1848_init("MAD16 WSS", hw_config->io_base + 4,
					  hw_config->irq,
					  dma,
					  dma2, 0,
					  hw_config->osp);
	request_region(hw_config->io_base, 4, "MAD16 WSS config");
}

void attach_mad16_mpu(struct address_info *hw_config)
{
	if (board_type < C929)	/* Early chip. No MPU support. Just SB MIDI */
	{
#if defined(CONFIG_MIDI) && defined(CONFIG_MAD16_OLDCARD)

		if (mad_read(MC1_PORT) & 0x20)
			hw_config->io_base = 0x240;
		else
			hw_config->io_base = 0x220;

		hw_config->name = "Mad16/Mozart";
		sb_dsp_init(hw_config);
#endif

		return;
	}
#if defined(CONFIG_UART401) && defined(CONFIG_MIDI)
	if (!already_initialized)
		return;

	hw_config->driver_use_1 = SB_MIDI_ONLY;
	hw_config->name = "Mad16/Mozart";
	attach_uart401(hw_config);
#endif
}

int probe_mad16_mpu(struct address_info *hw_config)
{
#if defined(CONFIG_UART401) && defined(CONFIG_MIDI)
	static int mpu_attached = 0;
	static int valid_ports[] = {
		0x330, 0x320, 0x310, 0x300
	};
	
	static short valid_irqs[] = {9, 10, 5, 7};
	unsigned char tmp;
	int i;				/* A variable with secret power */

	if (!already_initialized)	/* The MSS port must be initialized first */
		return 0;

	if (mpu_attached)		/* Don't let them call this twice */
		return 0;
	mpu_attached = 1;

	if (board_type < C929)	/* Early chip. No MPU support. Just SB MIDI */
	{

#if defined(CONFIG_MIDI) && defined(CONFIG_MAD16_OLDCARD)
		unsigned char   tmp;

		tmp = mad_read(MC3_PORT);

		/* 
		 * MAD16 SB base is defined by the WSS base. It cannot be changed 
		 * alone.
		 * Ignore configured I/O base. Use the active setting. 
		 */

		if (mad_read(MC1_PORT) & 0x20)
			hw_config->io_base = 0x240;
		else
			hw_config->io_base = 0x220;

		switch (hw_config->irq)
		{
			case 5:
				tmp = (tmp & 0x3f) | 0x80;
				break;
			case 7:
				tmp = (tmp & 0x3f);
				break;
			case 11:
				tmp = (tmp & 0x3f) | 0x40;
				break;
			default:
				printk(KERN_ERR "mad16/Mozart: Invalid MIDI IRQ\n");
				return 0;
		}

		mad_write(MC3_PORT, tmp | 0x04);
		hw_config->driver_use_1 = SB_MIDI_ONLY;
		return sb_dsp_detect(hw_config);
#else
		return 0;
#endif
	}
	tmp = mad_read(MC6_PORT) & 0x83;
	tmp |= 0x80;		/* MPU-401 enable */

/*
 * Set the MPU base bits
 */

	for (i = 0; i < 5; i++)
	{
		if (i > 3)	/* Out of array bounds */
		{
			printk(KERN_ERR "MAD16 / Mozart: Invalid MIDI port 0x%x\n", hw_config->io_base);
			return 0;
		}
		if (valid_ports[i] == hw_config->io_base)
		{
			tmp |= i << 5;
			break;
		}
	}

/*
 * Set the MPU IRQ bits
 */

	for (i = 0; i < 5; i++)
	{
		if (i > 3)	/* Out of array bounds */
		{
			printk(KERN_ERR "MAD16 / Mozart: Invalid MIDI IRQ %d\n", hw_config->irq);
			return 0;
		}
		if (valid_irqs[i] == hw_config->irq)
		{
			tmp |= i << 3;
			break;
		}
	}
	mad_write(MC6_PORT, tmp);	/* Write MPU401 config */

	return probe_uart401(hw_config);
#else
	return 0;
#endif
}

void unload_mad16(struct address_info *hw_config)
{
	ad1848_unload(hw_config->io_base + 4,
			hw_config->irq,
			hw_config->dma,
			hw_config->dma2, 0);
	release_region(hw_config->io_base, 4);
	sound_unload_audiodev(hw_config->slots[0]);
}

void
unload_mad16_mpu(struct address_info *hw_config)
{
#if defined(CONFIG_MIDI) && defined(CONFIG_MAD16_OLDCARD)
	if (board_type < C929)	/* Early chip. No MPU support. Just SB MIDI */
	{
		sb_dsp_unload(hw_config, 0);
		return;
	}
#endif

#if defined(CONFIG_UART401) && defined(CONFIG_MIDI)
	unload_uart401(hw_config);
#endif
}

#ifdef MODULE

int		mpu_io = 0;
int		mpu_irq = 0;
int             io = -1;
int             dma = -1;
int             dma16 = -1;	/* Set this for modules that need it */
int             irq = -1;

int             cdtype = 0;
int             cdirq = 0;
int             cdport = 0x340;
int             cddma = 3;
int             opl4 = 0;
int             joystick = 0;

MODULE_PARM(mpu_io, "i");
MODULE_PARM(mpu_irq, "i");
MODULE_PARM(io,"i");
MODULE_PARM(dma,"i");
MODULE_PARM(dma16,"i");
MODULE_PARM(irq,"i");
MODULE_PARM(cdtype,"i");
MODULE_PARM(cdirq,"i");
MODULE_PARM(cdport,"i");
MODULE_PARM(cddma,"i");
MODULE_PARM(opl4,"i");
MODULE_PARM(joystick,"i");
MODULE_PARM(debug,"i");

EXPORT_NO_SYMBOLS;

static int found_mpu;


static int dma_map[2][8] =
{
	{0x03, -1, -1, -1, -1, 0x00, 0x01, 0x02},
	{0x03, -1, 0x01, 0x00, -1, -1, -1, -1}
};

static int irq_map[16] =
{
	0x00, -1, -1, 0x0A,
	-1, 0x04, -1, 0x08,
	-1, 0x10, 0x14, 0x18,
	-1, -1, -1, -1
};

struct address_info config;
struct address_info config_mpu;

int init_module(void)
{
	int dmatype = 0;

	printk(KERN_INFO "MAD16 audio driver Copyright (C) by Hannu Savolainen 1993-1996\n");

	if (io == -1 || dma == -1 || irq == -1)
	{
		printk(KERN_ERR "I/O, DMA and irq are mandatory\n");
		return -EINVAL;
	}
	printk(KERN_INFO "CDROM ");
	switch (cdtype)
	{
		case 0x00:
			printk("Disabled");
			cdirq = 0;
			break;
		case 0x02:
			printk("Sony CDU31A");
			dmatype = 2;
			break;
		case 0x04:
			printk("Mitsumi");
			dmatype = 1;
			break;
		case 0x06:
			printk("Panasonic Lasermate");
			dmatype = 2;
			break;
		case 0x08:
			printk("Secondary IDE");
			dmatype = 1;
			break;
		case 0x0A:
			printk("Primary IDE");
			dmatype = 1;
			break;
		default:
			printk("\n");
			printk(KERN_ERR "Invalid CDROM type\n");
			return -EINVAL;
	}

	if (dmatype)
	{
		if (cddma > 7 || cddma < 0 || dma_map[dmatype][cddma] == -1)
		{
			printk("\n");
			printk(KERN_ERR "Invalid CDROM DMA\n");
			return -EINVAL;
		}
		if (cddma)
			printk(", DMA %d", cddma);
		else
			printk(", no DMA");
	}
	if (cdtype && !cdirq)
		printk(", no IRQ");
	else if (cdirq < 0 || cdirq > 15 || irq_map[cdirq] == -1)
	{
		  printk(", invalid IRQ (disabling)");
		  cdirq = 0;
	}
	else printk(", IRQ %d", cdirq);

	printk(".\n");
	printk(KERN_INFO "Joystick port ");
	if (joystick == 1)
		printk("enabled.\n");
	else
	{
		joystick = 0;
		printk("disabled.\n");
	}

	/*
	 *    Build the config words
	 */

	mad16_conf = (joystick ^ 1) | cdtype;
	mad16_cdsel = 0;
	if (opl4)
		mad16_cdsel |= 0x20;
	mad16_cdsel |= dma_map[dmatype][cddma];

	if (cdtype < 0x08)
	{
		switch (cdport)
		{
			case 0x340:
				mad16_cdsel |= 0x00;
				break;
			case 0x330:
				mad16_cdsel |= 0x40;
				break;
			case 0x360:
				mad16_cdsel |= 0x80;
				break;
			case 0x320:
				mad16_cdsel |= 0xC0;
				break;
			default:
				printk(KERN_ERR "Unknown CDROM I/O base %d\n", cdport);
				return -EINVAL;
		}
	}
	mad16_cdsel |= irq_map[cdirq];

	config.io_base = io;
	config.irq = irq;
	config.dma = dma;
	config.dma2 = dma16;

	if (!probe_mad16(&config))
		return -ENODEV;

	config_mpu.io_base = mpu_io;
	config_mpu.irq = mpu_irq;
	attach_mad16(&config);

	found_mpu = probe_mad16_mpu(&config_mpu);

	if (found_mpu)
		attach_mad16_mpu(&config_mpu);

	SOUND_LOCK;
	return 0;
}

void cleanup_module(void)
{
	if (found_mpu)
		unload_mad16_mpu(&config_mpu);
	unload_mad16(&config);
	SOUND_LOCK_END;
}

#endif



/* That's all folks */
#endif
