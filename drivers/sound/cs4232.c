/*
 * sound/cs4232.c
 *
 * The low level driver for Crystal CS4232 based cards. The CS4232 is
 * a PnP compatible chip which contains a CS4231A codec, SB emulation,
 * a MPU401 compatible MIDI port, joystick and synthesizer and IDE CD-ROM 
 * interfaces. This is just a temporary driver until full PnP support
 * gets inplemented. Just the WSS codec, FM synth and the MIDI ports are
 * supported. Other interfaces are left uninitialized.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1996
 *
 * USS/Lite for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
#include <linux/config.h>


#include "sound_config.h"

#if defined(CONFIG_CS4232)

#define KEY_PORT	0x279	/* Same as LPT1 status port */
#define CSN_NUM		0x99	/* Just a random number */

static int     *osp;

static void 
CS_OUT (unsigned char a)
{
  outb (a, KEY_PORT);
}
#define CS_OUT2(a, b)		{CS_OUT(a);CS_OUT(b);}
#define CS_OUT3(a, b, c)	{CS_OUT(a);CS_OUT(b);CS_OUT(c);}

static int      mpu_base = 0, mpu_irq = 0;
static int      mpu_detected = 0;

int
probe_cs4232_mpu (struct address_info *hw_config)
{
/*
 * Just write down the config values.
 */

  mpu_base = hw_config->io_base;
  mpu_irq = hw_config->irq;

  return 0;
}

void
attach_cs4232_mpu (struct address_info *hw_config)
{
}

static unsigned char crystal_key[] =	/* A 32 byte magic key sequence */
{
  0x96, 0x35, 0x9a, 0xcd, 0xe6, 0xf3, 0x79, 0xbc,
  0x5e, 0xaf, 0x57, 0x2b, 0x15, 0x8a, 0xc5, 0xe2,
  0xf1, 0xf8, 0x7c, 0x3e, 0x9f, 0x4f, 0x27, 0x13,
  0x09, 0x84, 0x42, 0xa1, 0xd0, 0x68, 0x34, 0x1a
};

int
probe_cs4232 (struct address_info *hw_config)
{
  int             i, n;
  int             base = hw_config->io_base, irq = hw_config->irq;
  int             dma1 = hw_config->dma, dma2 = hw_config->dma2;

  static wait_handle *cs_sleeper = NULL;
  static volatile struct snd_wait cs_sleep_flag =
  {0};

  osp = hw_config->osp;

/*
 * Verify that the I/O port range is free.
 */

  if (check_region (base, 4))
    {
      printk ("cs4232.c: I/O port 0x%03x not free\n", base);
      return 0;
    }

  if (ad1848_detect (hw_config->io_base, NULL, hw_config->osp))
    return 1;			/* The card is already active */

/*
 * This version of the driver doesn't use the PnP method when configuring
 * the card but a simplified method defined by Crystal. This means that
 * just one CS4232 compatible device can exist on the system. Also this
 * method conflicts with possible PnP support in the OS. For this reason 
 * driver is just a temporary kludge.
 */

/*
 * Repeat initialization few times since it doesn't always succeed in
 * first time.
 */

  for (n = 0; n < 4; n++)
    {
      cs_sleep_flag.flags = WK_NONE;
/*
 * Wake up the card by sending a 32 byte Crystal key to the key port.
 */
      for (i = 0; i < 32; i++)
	CS_OUT (crystal_key[i]);


      {
	unsigned long   tlimit;

	if (HZ / 10)
	  current_set_timeout (tlimit = jiffies + (HZ / 10));
	else
	  tlimit = (unsigned long) -1;
	cs_sleep_flag.flags = WK_SLEEP;
	module_interruptible_sleep_on (&cs_sleeper);
	if (!(cs_sleep_flag.flags & WK_WAKEUP))
	  {
	    if (jiffies >= tlimit)
	      cs_sleep_flag.flags |= WK_TIMEOUT;
	  }
	cs_sleep_flag.flags &= ~WK_SLEEP;
      };			/* Delay */

/*
 * Now set the CSN (Card Select Number).
 */

      CS_OUT2 (0x06, CSN_NUM);


/*
 * Then set some config bytes. First logical device 0 
 */

      CS_OUT2 (0x15, 0x00);	/* Select logical device 0 (WSS/SB/FM) */
      CS_OUT3 (0x47, (base >> 8) & 0xff, base & 0xff);	/* WSSbase */

      if (check_region (0x388, 4))	/* Not free */
	CS_OUT3 (0x48, 0x00, 0x00)	/* FMbase off */
	  else
	CS_OUT3 (0x48, 0x03, 0x88);	/* FMbase 0x388 */

      CS_OUT3 (0x42, 0x00, 0x00);	/* SBbase off */
      CS_OUT2 (0x22, irq);	/* SB+WSS IRQ */
      CS_OUT2 (0x2a, dma1);	/* SB+WSS DMA */

      if (dma2 != -1)
	CS_OUT2 (0x25, dma2)	/* WSS DMA2 */
	  else
	CS_OUT2 (0x25, 4);	/* No WSS DMA2 */

      CS_OUT2 (0x33, 0x01);	/* Activate logical dev 0 */


      {
	unsigned long   tlimit;

	if (HZ / 10)
	  current_set_timeout (tlimit = jiffies + (HZ / 10));
	else
	  tlimit = (unsigned long) -1;
	cs_sleep_flag.flags = WK_SLEEP;
	module_interruptible_sleep_on (&cs_sleeper);
	if (!(cs_sleep_flag.flags & WK_WAKEUP))
	  {
	    if (jiffies >= tlimit)
	      cs_sleep_flag.flags |= WK_TIMEOUT;
	  }
	cs_sleep_flag.flags &= ~WK_SLEEP;
      };			/* Delay */

/*
 * Initialize logical device 3 (MPU)
 */

#if (defined(CONFIG_MPU401) || defined(CONFIG_MPU_EMU)) && defined(CONFIG_MIDI)
      if (mpu_base != 0 && mpu_irq != 0)
	{
	  CS_OUT2 (0x15, 0x03);	/* Select logical device 3 (MPU) */
	  CS_OUT3 (0x47, (mpu_base >> 8) & 0xff, mpu_base & 0xff);	/* MPUbase */
	  CS_OUT2 (0x22, mpu_irq);	/* MPU IRQ */
	  CS_OUT2 (0x33, 0x01);	/* Activate logical dev 3 */
	}
#endif

/*
 * Finally activate the chip
 */
      CS_OUT (0x79);


      {
	unsigned long   tlimit;

	if (HZ / 5)
	  current_set_timeout (tlimit = jiffies + (HZ / 5));
	else
	  tlimit = (unsigned long) -1;
	cs_sleep_flag.flags = WK_SLEEP;
	module_interruptible_sleep_on (&cs_sleeper);
	if (!(cs_sleep_flag.flags & WK_WAKEUP))
	  {
	    if (jiffies >= tlimit)
	      cs_sleep_flag.flags |= WK_TIMEOUT;
	  }
	cs_sleep_flag.flags &= ~WK_SLEEP;
      };			/* Delay */

/*
 * Then try to detect the codec part of the chip
 */

      if (ad1848_detect (hw_config->io_base, NULL, hw_config->osp))
	return 1;


      {
	unsigned long   tlimit;

	if (HZ)
	  current_set_timeout (tlimit = jiffies + (HZ));
	else
	  tlimit = (unsigned long) -1;
	cs_sleep_flag.flags = WK_SLEEP;
	module_interruptible_sleep_on (&cs_sleeper);
	if (!(cs_sleep_flag.flags & WK_WAKEUP))
	  {
	    if (jiffies >= tlimit)
	      cs_sleep_flag.flags |= WK_TIMEOUT;
	  }
	cs_sleep_flag.flags &= ~WK_SLEEP;
      };			/* Longer delay */
    }

  return 0;
}

void
attach_cs4232 (struct address_info *hw_config)
{
  int             base = hw_config->io_base, irq = hw_config->irq;
  int             dma1 = hw_config->dma, dma2 = hw_config->dma2;

  if (dma2 == -1)
    dma2 = dma1;

  ad1848_init ("CS4232", base,
	       irq,
	       dma1,		/* Playback DMA */
	       dma2,		/* Capture DMA */
	       0,
	       hw_config->osp);

#if (defined(CONFIG_MPU401) || defined(CONFIG_MPU_EMU)) && defined(CONFIG_MIDI)
  if (mpu_base != 0 && mpu_irq != 0)
    {
      static struct address_info hw_config2 =
      {0};			/* Ensure it's initialized */

      hw_config2.io_base = mpu_base;
      hw_config2.irq = mpu_irq;
      hw_config2.dma = -1;
      hw_config2.dma2 = -1;
      hw_config2.always_detect = 0;
      hw_config2.name = NULL;
      hw_config2.driver_use_1 = 0;
      hw_config2.driver_use_2 = 0;
      hw_config2.card_subtype = 0;
      hw_config2.osp = hw_config->osp;

      if (probe_mpu401 (&hw_config2))
	{
	  mpu_detected = 1;
	  attach_mpu401 (&hw_config2);
	}
      else
	{
	  mpu_base = mpu_irq = 0;
	}
    }
#endif
}

void
unload_cs4232 (struct address_info *hw_config)
{
  int             base = hw_config->io_base, irq = hw_config->irq;
  int             dma1 = hw_config->dma, dma2 = hw_config->dma2;

  if (dma2 == -1)
    dma2 = dma1;

  ad1848_unload (base,
		 irq,
		 dma1,		/* Playback DMA */
		 dma2,		/* Capture DMA */
		 0);

#if (defined(CONFIG_MPU401) || defined(CONFIG_MPU_EMU)) && defined(CONFIG_MIDI)
  if (mpu_base != 0 && mpu_irq != 0 && mpu_detected)
    {
      static struct address_info hw_config2 =
      {0};			/* Ensure it's initialized */

      hw_config2.io_base = mpu_base;
      hw_config2.irq = mpu_irq;
      hw_config2.dma = -1;
      hw_config2.dma2 = -1;
      hw_config2.always_detect = 0;
      hw_config2.name = NULL;
      hw_config2.driver_use_1 = 0;
      hw_config2.driver_use_2 = 0;
      hw_config2.card_subtype = 0;
      hw_config2.osp = hw_config->osp;

      unload_mpu401 (&hw_config2);
    }
#endif
}

void
unload_cs4232_mpu (struct address_info *hw_config)
{
  /* Not required. Handled by cs4232_unload */
}


#endif
