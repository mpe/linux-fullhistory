/*
 * sound/sound_pnp.c
 *
 * PnP soundcard support (Linux spesific)
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

#if defined(CONFIG_SPNP)


static struct wait_queue *maui_sleeper = NULL;
static volatile struct snd_wait maui_sleep_flag =
{0};

extern unsigned long init_pnp (unsigned long, int *);

#include "pnp.h"
extern int     *sound_osp;

extern int      (*pnp_ioctl) (unsigned int cmd, caddr_t arg);

extern int      sound_pnp_port;
static int      disabled_devices[] =
{
  PNP_DEVID ('G', 'R', 'V', 0x0003),	/* GUS SB emulation */
  PNP_DEVID ('G', 'R', 'V', 0x0004),	/* GUS MPU emulation */
  0
};

static int      special_devices[] =
{
  PNP_DEVID ('C', 'S', 'C', 0x0010),	/* CS4232/6 control port */
  PNP_DEVID ('C', 'S', 'C', 0x0002),	/* CS4232/6 control port */
  0
};

static int      pnp_sig = 0;

static void
pnp_delay (long t)
{
  t = (t * HZ) / 1000000;	/* Convert to jiffies */


  {
    unsigned long   tlimit;

    if (t)
      current->timeout = tlimit = jiffies + (t);
    else
      tlimit = (unsigned long) -1;
    maui_sleep_flag.opts = WK_SLEEP;
    interruptible_sleep_on (&maui_sleeper);
    if (!(maui_sleep_flag.opts & WK_WAKEUP))
      {
	if (jiffies >= tlimit)
	  maui_sleep_flag.opts |= WK_TIMEOUT;
      }
    maui_sleep_flag.opts &= ~WK_SLEEP;
  };
}

void
cs4232_pnp (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;
  int             old_num_mixers = num_mixers;
  int             is_4232 = 0;	/* CS4232 (not CS4236 or something better) */

  int             portmask = 0xff;
  int             irqmask = 0x01, dmamask = 0x03;
  int             opl3_driver, wss_driver;


  if (pnp_trace)
    printk ("CS4232/6 driver waking up\n");

  if (dev->card->key == (PNP_DEVID ('C', 'S', 'C', 0x4232)))
    is_4232 = 1;

#ifndef USE_HOT_PNP_INIT
  if (is_4232)			/* CS4232 may cause lockups */
    if (pnp_get_port (dev, 0) == NO_PORT ||
	pnp_get_port (dev, 1) == NO_PORT ||
	pnp_get_irq (dev, 0) == NO_IRQ ||
	pnp_get_dma (dev, 0) == NO_DMA
      )
      {
	printk ("Sound: CS4232 in PnP mode requires prior initialization by PnP BIOS\n");
	return;
      }
#endif

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "PnP WSS";

  if ((wss_driver = sndtable_identify_card ("AD1848")))
    portmask |= 0x01;		/* MSS */
  else
    printk ("Sound: PnP MSS/WSS device detected but no driver enabled\n");

  if ((opl3_driver = sndtable_identify_card ("OPL3")))
    portmask |= 0x02;		/* OPL3 */
  else
    printk ("Sound: PnP OPL3/4 device detected but no driver enabled\n");

  /* printk ("WSS driver %d, OPL3 driver %d\n", wss_driver, opl3_driver); */

  if (!portmask)		/* No drivers available */
    return;

  if (!is_4232)
    if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
      {
	printk ("sound_pnp: Failed to find free resources\n");
	return;
      }

  {
    struct address_info hw_config;
    int             wss_base, opl3_base;
    int             irq;
    int             dma1, dma2;

    if (pnp_trace)
      printk ("Device activation OK\n");
    wss_base = pnp_get_port (dev, 0);
    opl3_base = pnp_get_port (dev, 1);
    irq = pnp_get_irq (dev, 0);
    dma1 = pnp_get_dma (dev, 0);
    dma2 = pnp_get_dma (dev, 1);

    pnp_delay (1000000);

    if (pnp_trace)
      {
	printk ("I/O0 %03x\n", wss_base);
	printk ("I/O1 %03x\n", opl3_base);
	printk ("IRQ %d\n", irq);
	printk ("DMA0 %d\n", dma1);
	printk ("DMA1 %d\n", dma2);
      }

    if (opl3_base && opl3_driver)
      {
	hw_config.io_base = opl3_base;
	hw_config.irq = 0;
	hw_config.dma = -1;
	hw_config.dma2 = -1;
	hw_config.always_detect = 0;
	hw_config.name = "";
	hw_config.driver_use_1 = 0;
	hw_config.driver_use_2 = 0;
	hw_config.osp = sound_osp;
	hw_config.card_subtype = 0;

	sndtable_start_card (opl3_driver, &hw_config);

      }

    if (wss_base && wss_driver)
      {
	hw_config.io_base = wss_base;
	hw_config.irq = irq;
	hw_config.dma = dma1;
	hw_config.dma2 = (dma2 == NO_DMA) ? dma1 : dma2;
	hw_config.always_detect = 0;
	hw_config.name = name;
	hw_config.driver_use_1 = 0;
	hw_config.driver_use_2 = 0;
	hw_config.osp = sound_osp;
	hw_config.card_subtype = 0;

	sndtable_start_card (wss_driver, &hw_config);


	if (num_mixers > old_num_mixers)
	  {			/* Assume the mixer map is as suggested in the CS4232 spec */
	    AD1848_REROUTE (SOUND_MIXER_LINE1, SOUND_MIXER_LINE);
	    AD1848_REROUTE (SOUND_MIXER_LINE2, SOUND_MIXER_CD);
	    AD1848_REROUTE (SOUND_MIXER_LINE3, SOUND_MIXER_SYNTH);	/* FM */
	  }
      }
  }
}

void
opti82C924_pnp (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;

  int             portmask = 0xff, irqmask = 0x01, dmamask = 0x03;
  int             opl3_driver, wss_driver;

  if (pnp_trace)
    printk ("OPTi 82C924 driver waking up\n");

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "PnP WSS";

  if ((wss_driver = sndtable_identify_card ("AD1848")))
    portmask |= 0x01;		/* MSS */
  else
    printk ("Sound: PnP MSS/WSS device detected but no driver enabled\n");

  if ((opl3_driver = sndtable_identify_card ("OPL3")))
    portmask |= 0x02;		/* OPL3 */
  else
    printk ("Sound: PnP OPL3/4 device detected but no driver enabled\n");

  /* printk ("WSS driver %d, OPL3 driver %d\n", wss_driver, opl3_driver); */

  if (!portmask)		/* No drivers available */
    return;

  if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
    printk ("sound_pnp: Failed to find free resources\n");
  else
    {
      struct address_info hw_config;
      int             wss_base, opl3_base;
      int             irq;
      int             dma1, dma2;

      if (pnp_trace)
	printk ("Device activation OK\n");
      wss_base = pnp_get_port (dev, 1);
      opl3_base = pnp_get_port (dev, 2);
      irq = pnp_get_irq (dev, 0);
      dma1 = pnp_get_dma (dev, 0);
      dma2 = pnp_get_dma (dev, 1);

      pnp_delay (2000000);

      if (pnp_trace)
	{
	  printk ("I/O0 %03x\n", wss_base);
	  printk ("I/O1 %03x\n", opl3_base);
	  printk ("IRQ %d\n", irq);
	  printk ("DMA0 %d\n", dma1);
	  printk ("DMA1 %d\n", dma2);
	}

      if (opl3_base && opl3_driver)
	{
	  hw_config.io_base = opl3_base + 8;
	  hw_config.irq = 0;
	  hw_config.dma = -1;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = "";
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (opl3_driver, &hw_config);

	}

      if (wss_base && wss_driver)
	{
	  hw_config.io_base = wss_base + 4;
	  hw_config.irq = irq;
	  hw_config.dma = dma1;
	  hw_config.dma2 = (dma2 == NO_DMA) ? dma1 : dma2;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (wss_driver, &hw_config);

	}
    }
}

void
opl3sa2_pnp (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;

  int             portmask = 0x00, irqmask = 0x01, dmamask = 0x03;
  int             opl3_driver, wss_driver, mpu_driver;

  if (pnp_trace)
    printk ("OPL3-SA2 driver waking up\n");

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "PnP WSS";

  if ((wss_driver = sndtable_identify_card ("AD1848")))
    portmask |= 0x02;		/* MSS */
  else
    printk ("Sound: PnP MSS/WSS device detected but no driver enabled\n");

  if ((opl3_driver = sndtable_identify_card ("OPL3")))
    portmask |= 0x04;		/* OPL3 */
  else
    printk ("Sound: PnP OPL3/4 device detected but no driver enabled\n");

  if ((mpu_driver = sndtable_identify_card ("UART401")))
    portmask |= 0x08;		/* OPL3 */
  else
    printk ("Sound: PnP UART401 device detected but no driver enabled\n");

  /* printk ("WSS driver %d, OPL3 driver %d\n", wss_driver, opl3_driver); */

  if (!portmask)		/* No drivers available */
    return;

  if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
    printk ("sound_pnp: Failed to find free resources\n");
  else
    {
      struct address_info hw_config;
      int             wss_base, opl3_base, mpu_base;
      int             irq;
      int             dma1, dma2;

      if (pnp_trace)
	printk ("Device activation OK\n");
      wss_base = pnp_get_port (dev, 1);
      opl3_base = pnp_get_port (dev, 2);
      mpu_base = pnp_get_port (dev, 3);
      irq = pnp_get_irq (dev, 0);
      dma1 = pnp_get_dma (dev, 0);
      dma2 = pnp_get_dma (dev, 1);

      pnp_delay (1000000);

      if (pnp_trace)
	{
	  printk ("I/O0 %03x\n", wss_base);
	  printk ("I/O1 %03x\n", opl3_base);
	  printk ("I/O3 %03x\n", mpu_base);
	  printk ("IRQ %d\n", irq);
	  printk ("DMA0 %d\n", dma1);
	  printk ("DMA1 %d\n", dma2);
	}

      if (opl3_base && opl3_driver)
	{
	  hw_config.io_base = opl3_base;
	  hw_config.irq = 0;
	  hw_config.dma = -1;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = "";
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (opl3_driver, &hw_config);

	}

      if (wss_base && wss_driver)
	{
	  hw_config.io_base = wss_base + 4;
	  hw_config.irq = irq;
	  hw_config.dma = dma1;
	  hw_config.dma2 = (dma2 == NO_DMA) ? dma1 : dma2;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (wss_driver, &hw_config);

	}

      if (mpu_base && mpu_driver)
	{
	  hw_config.io_base = mpu_base;
	  hw_config.irq = 0;
	  hw_config.dma = -1;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = "";
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (mpu_driver, &hw_config);

	}
    }
}

static unsigned char
C931_read (int base, int reg)
{
  unsigned char   data;
  unsigned long   flags;

  save_flags (flags);
  cli ();
  outb ((0xE4), base);
  outb ((reg), base + 2);
  data = inb (base + 3);
  restore_flags (flags);
  return data;
}

static void
C931_write (int base, int reg, unsigned char data)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();
  outb ((0xE4), base);
  outb ((reg), base + 2);
  outb ((data), base + 3);
  restore_flags (flags);
}

void
opti82C931_pnp (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;

  int             portmask = 0xff, irqmask = 0x01, dmamask = 0x03;
  int             opl3_driver, wss_driver;

  if (pnp_trace)
    printk ("OPTi 82C931 driver waking up\n");

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "PnP WSS";

  if ((wss_driver = sndtable_identify_card ("AD1848")))
    portmask |= 0x01;		/* MSS */
  else
    printk ("Sound: PnP MSS/WSS device detected but no driver enabled\n");

  if ((opl3_driver = sndtable_identify_card ("OPL3")))
    portmask |= 0x02;		/* OPL3 */
  else
    printk ("Sound: PnP OPL3/4 device detected but no driver enabled\n");

  /* printk ("WSS driver %d, OPL3 driver %d\n", wss_driver, opl3_driver); */

  if (!portmask)		/* No drivers available */
    return;

  if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
    printk ("sound_pnp: Failed to find free resources\n");
  else
    {
      struct address_info hw_config;
      int             wss_base, opl3_base, master_ctl;
      int             irq;
      int             dma1, dma2;

      if (pnp_trace)
	printk ("Device activation OK\n");
      wss_base = pnp_get_port (dev, 0);
      opl3_base = pnp_get_port (dev, 1);
      master_ctl = pnp_get_port (dev, 3);
      irq = pnp_get_irq (dev, 0);
      dma1 = pnp_get_dma (dev, 0);
      dma2 = pnp_get_dma (dev, 1);

      if (pnp_trace)
	{
	  int             i;

	  printk ("I/O0 %03x\n", wss_base);
	  printk ("I/O1 %03x\n", opl3_base);
	  printk ("Master control port %x\n", master_ctl);
	  for (i = 0; i < 4; i++)
	    printk ("Port %x = %x\n", master_ctl + i, inb (master_ctl + i));
	  printk ("IRQ %d\n", irq);
	  printk ("DMA0 %d\n", dma1);
	  printk ("DMA1 %d\n", dma2);
	}
      {
	unsigned char   tmp;

	tmp = C931_read (master_ctl, 5) | 0x20;		/* Enable codec registers I16 to I31 */
	C931_write (master_ctl, 5, tmp);

	tmp = 0x82;		/* MPU and WSS enabled, SB disabled */
	C931_write (master_ctl, 6, tmp);
      }

      pnp_delay (2000000);

      if (opl3_base && opl3_driver)
	{
	  hw_config.io_base = opl3_base + 8;
	  hw_config.irq = 0;
	  hw_config.dma = -1;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = "";
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (opl3_driver, &hw_config);

	}

      if (wss_base && wss_driver)
	{
	  hw_config.io_base = wss_base;
	  hw_config.irq = irq;
	  hw_config.dma = dma1;
	  hw_config.dma2 = (dma2 == NO_DMA) ? dma1 : dma2;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (wss_driver, &hw_config);

	  ad1848_control (AD1848_SET_C930_PWD, master_ctl);
	}
    }
}

void
opti82C924mpu_pnp (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;

  int             portmask = 0xff, irqmask = 0x01, dmamask = 0x03;
  int             mpu_driver;

  if (pnp_trace)
    printk ("OPTi 82C924/C925/C931 MPU driver waking up\n");

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "PnP MPU";

  if ((mpu_driver = sndtable_identify_card ("UART401")))
    portmask |= 0x01;		/* MPU401 */
  else
    printk ("Sound: PnP MPU device detected but no driver enabled\n");

  /* printk ("MPU driver %d\n", mpu_driver); */

  if (!portmask)		/* No drivers available */
    return;

  if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
    printk ("sound_pnp: Failed to find free resources\n");
  else
    {
      struct address_info hw_config;
      int             mpu_base;
      int             irq;

      if (pnp_trace)
	printk ("Device activation OK\n");
      mpu_base = pnp_get_port (dev, 0);
      irq = pnp_get_irq (dev, 0);

      pnp_delay (1000000);

      if (pnp_trace)
	{
	  printk ("I/O %03x\n", mpu_base);
	  printk ("IRQ %d\n", irq);
	}

      if (mpu_base && mpu_driver)
	{
	  hw_config.io_base = mpu_base;
	  hw_config.irq = irq;
	  hw_config.dma = -1;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (mpu_driver, &hw_config);

	}
    }
}

void
cs4236mpu_pnp (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;

  int             portmask = 0xff, irqmask = 0x01, dmamask = 0x03;
  int             mpu_driver;

  if (dev->card->key == (PNP_DEVID ('C', 'S', 'C', 0x4232)))	/* CS4232 */
    return;

  if (pnp_trace)
    printk ("CS4236 MPU driver waking up\n");

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "PnP MPU";

  if ((mpu_driver = sndtable_identify_card ("UART401")))
    portmask |= 0x01;		/* MPU401 */
  else
    printk ("Sound: CS4236 PnP MPU device detected but no driver enabled\n");

  /* printk ("MPU driver %d\n", mpu_driver); */

  if (!portmask)		/* No drivers available */
    return;

  if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
    printk ("sound_pnp: Failed to find free resources\n");
  else
    {
      struct address_info hw_config;
      int             mpu_base;
      int             irq;

      if (pnp_trace)
	printk ("Device activation OK\n");
      mpu_base = pnp_get_port (dev, 0);
      irq = pnp_get_irq (dev, 0);

      pnp_delay (1500000);

      if (pnp_trace)
	{
	  printk ("I/O %03x\n", mpu_base);
	  printk ("IRQ %d\n", irq);
	}

      if (mpu_base && mpu_driver)
	{
	  hw_config.io_base = mpu_base;
	  hw_config.irq = irq;
	  hw_config.dma = -1;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (mpu_driver, &hw_config);

	}
    }
}

void
soundscape_pnp (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;

  int             portmask = 0xff, irqmask = 0x03, dmamask = 0x01;
  int             sscape_driver, wss_driver;

  if (pnp_trace)
    printk ("Soundscape PnP driver waking up\n");

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "SoundScape PnP";

  if ((sscape_driver = sndtable_identify_card ("SSCAPE")))
    portmask |= 0x01;		/* MPU401 */
  else
    printk ("Sound: Soundscape PnP device detected but no driver enabled\n");

  /* printk ("Soundscape driver %d\n", sscape_driver); */

  if ((wss_driver = sndtable_identify_card ("SSCAPEMSS")))
    portmask |= 0x01;
  else
    printk ("Sound: Soundscape codec device detected but no driver enabled\n");

  if (!portmask)		/* No drivers available */
    return;

  if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
    printk ("sound_pnp: Failed to find free resources\n");
  else
    {
      struct address_info hw_config;
      int             sscape_base;
      int             irq, irq2, dma, dma2;

      if (pnp_trace)
	printk ("Device activation OK\n");
      sscape_base = pnp_get_port (dev, 0);
      irq = pnp_get_irq (dev, 0);
      irq2 = pnp_get_irq (dev, 1);
      dma = pnp_get_dma (dev, 0);
      dma2 = pnp_get_dma (dev, 1);

      pnp_delay (1000000);

      if (pnp_trace)
	{
	  printk ("I/O %03x\n", sscape_base);
	  printk ("IRQ %d\n", irq);
	  printk ("IRQ2 %d\n", irq2);
	  printk ("DMA %d\n", dma);
	  printk ("DMA2 %d\n", dma2);
	}

      if (sscape_base && sscape_driver)
	{
	  hw_config.io_base = sscape_base;
	  hw_config.irq = irq;
	  hw_config.dma = dma;
	  hw_config.dma2 = dma2;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0x12345678;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (sscape_driver, &hw_config);
	}

      if (sscape_base && wss_driver)
	{
	  hw_config.io_base = sscape_base + 8;	/* The codec base */
	  hw_config.irq = irq2;
	  hw_config.dma = dma;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (wss_driver, &hw_config);
	  ad1848_control (AD1848_SET_XTAL, 1);	/* 14.3 MHz is used */
	}
    }
}

void
soundscape_vivo (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;

  int             portmask = 0x07, irqmask = 0x03, dmamask = 0x03;
  int             mpu_driver, wss_driver, vivo_driver;
  int             is_vivo_classic = 0;

  if (pnp_trace)
    printk ("Soundscape VIVO driver waking up\n");

  if (dev->card->key == (PNP_DEVID ('E', 'N', 'S', 0x4080)))
    is_vivo_classic = 1;

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "SoundScape VIVO";

  if ((mpu_driver = sndtable_identify_card ("UART401")))
    portmask |= 0x01;		/* MPU401 */

  /* printk ("MPU driver %d\n", mpu_driver); */

  if ((wss_driver = sndtable_identify_card ("AD1848")))
    portmask |= 0x02;
  else
    printk ("Sound: Soundscape codec device detected but no driver enabled\n");

  if ((vivo_driver = sndtable_identify_card ("VIVO")))
    portmask |= 0x07;
  else
    printk ("Sound: Soundscape VIVO/OTTO device detected but no driver installed\n");

  if (!portmask)		/* No drivers available */
    return;

  if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
    printk ("sound_pnp: Failed to find free resources\n");
  else
    {
      struct address_info hw_config;
      int             mpu_base, mss_base, otto_base;
      int             irq, irq2, dma, dma2;

      if (pnp_trace)
	printk ("Device activation OK\n");
      mpu_base = pnp_get_port (dev, 0);
      mss_base = pnp_get_port (dev, 1);
      otto_base = pnp_get_port (dev, 2);
      irq = pnp_get_irq (dev, 0);
      irq2 = pnp_get_irq (dev, 1);
      dma = pnp_get_dma (dev, 0);
      dma2 = pnp_get_dma (dev, 1);
      if (dma2 == NO_DMA)
	dma2 = dma;

      if (pnp_trace)
	{
	  printk ("I/O %03x\n", mpu_base);
	  printk ("MSS I/O %03x\n", mss_base);
	  printk ("OTTO I/O %03x\n", otto_base);
	  printk ("IRQ %d\n", irq);
	  printk ("IRQ2 %d\n", irq2);
	  printk ("DMA %d\n", dma);
	  printk ("DMA2 %d\n", dma2);
	}


      if (mss_base && wss_driver)
	{
	  hw_config.io_base = mss_base + 4;	/* The codec base */
	  hw_config.irq = irq;
	  hw_config.dma = dma;
	  hw_config.dma2 = dma2;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (wss_driver, &hw_config);
	}

      if (otto_base && vivo_driver)
	{
	  hw_config.io_base = otto_base;
	  hw_config.irq = irq2;
	  hw_config.dma = -1;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = mpu_base;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (vivo_driver, &hw_config);

	  if (is_vivo_classic)
	    {
	      /*
	       * The original VIVO uses XCTL0 pin of AD1845 as a synth (un)mute bit. Turn it
	       * on _after_ the synth is initialized. Btw, XCTL1 controls 30 dB mic boost
	       * circuit.
	       */

	      ad1848_control (AD1848_SET_XCTL0, 1);	/* Unmute */
	    }
	  AD1848_REROUTE (SOUND_MIXER_LINE1, SOUND_MIXER_SYNTH);	/* AUX1 is OTTO input */
	  AD1848_REROUTE (SOUND_MIXER_LINE3, SOUND_MIXER_LINE);		/* Line in */

	}
    }
}

void
gus_pnp (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;

  int             portmask = 0x00, irqmask = 0x01, dmamask = 0x03;
  int             gus_driver, wss_driver;

  if (pnp_trace)
    printk ("GUS PnP driver waking up\n");

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "Ultrasound PnP";

  if ((gus_driver = sndtable_identify_card ("GUSPNP")))
    portmask |= 0x07;
  else
    printk ("Sound: GUS PnP device detected but no driver enabled\n");

  if ((wss_driver = sndtable_identify_card ("AD1848")))
    portmask |= 0x01;		/* MAX */
  else
    printk ("Sound: GUS PnP codec device detected but no driver enabled\n");

  if (!portmask)		/* No drivers available */
    return;

  if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
    printk ("sound_pnp: Failed to find free resources\n");
  else
    {
      struct address_info hw_config;
      int             gus_base;
      int             irq;
      int             dma1, dma2;

      gus_base = pnp_get_port (dev, 0);

      irq = pnp_get_irq (dev, 0);
      dma1 = pnp_get_dma (dev, 0);
      dma2 = pnp_get_dma (dev, 1);

      if (pnp_trace)
	printk ("Device activation OK (P%x I%d D%d d%d)\n",
		gus_base, irq, dma1, dma2);

      if (gus_base && gus_driver)
	{

	  hw_config.io_base = gus_base;
	  hw_config.irq = irq;
	  hw_config.dma = dma1;
	  hw_config.dma2 = (dma2 == NO_DMA) ? dma1 : dma2;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (gus_driver, &hw_config);
	}
    }
}

void
sb_pnp (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;

  int             portmask = 0x00, irqmask = 0x01, dmamask = 0x03;
  int             sb_driver, mpu_driver, opl3_driver;

  if (pnp_trace)
    printk ("SB PnP driver waking up\n");
  pnp_delay (1000000);

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "SoundBlaster PnP";

  if ((sb_driver = sndtable_identify_card ("SBPNP")))
    portmask |= 0x01;
  else
    printk ("Sound: SB PnP device detected but no driver enabled\n");

  if ((mpu_driver = sndtable_identify_card ("SBMPU")))
    portmask |= 0x02;
  else
    printk ("Sound: SB PnP device detected but SB MPU driver not enabled\n");

  if ((opl3_driver = sndtable_identify_card ("OPL3")))
    portmask |= 0x04;
  else
    printk ("Sound: SB PnP device detected but OPL3 driver not enabled\n");

  if (!portmask)		/* No drivers available */
    return;

  if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
    printk ("sound_pnp: Failed to find free resources\n");
  else
    {
      struct address_info hw_config;
      int             sb_base, mpu_base, opl3_base;
      int             irq;
      int             dma1, dma2;

      if (pnp_trace)
	printk ("Device activation OK\n");
      sb_base = pnp_get_port (dev, 0);
      mpu_base = pnp_get_port (dev, 1);
      opl3_base = pnp_get_port (dev, 2);

      irq = pnp_get_irq (dev, 0);
      dma1 = pnp_get_dma (dev, 0);
      dma2 = pnp_get_dma (dev, 1);

      if (sb_base && sb_driver)
	{
	  hw_config.io_base = sb_base;
	  hw_config.irq = irq;
	  hw_config.dma = dma1;
	  hw_config.dma2 = (dma2 == NO_DMA) ? dma1 : dma2;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (sb_driver, &hw_config);
	}

      if (opl3_base && opl3_driver)
	{
	  hw_config.io_base = opl3_base;
	  hw_config.irq = 0;
	  hw_config.dma = -1;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = "";
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (opl3_driver, &hw_config);

	}

      if (mpu_base && mpu_driver)
	{
	  hw_config.io_base = mpu_base;
	  hw_config.irq = irq;
	  hw_config.dma = -1;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = "";
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (mpu_driver, &hw_config);

	}
    }
}

void
als_pnp (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;

  int             portmask = 0x00, irqmask = 0x01, dmamask = 0x03;
  int             sb_driver;

  if (pnp_trace)
    printk ("ALS### PnP driver waking up\n");

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "SB16 clone";

  if ((sb_driver = sndtable_identify_card ("SBPNP")))
    portmask |= 0x01;
  else
    printk ("Sound: ALS PnP device detected but no driver enabled\n");

  if (!portmask)		/* No drivers available */
    return;

  if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
    printk ("sound_pnp: Failed to find free resources\n");
  else
    {
      struct address_info hw_config;
      int             sb_base;
      int             irq;
      int             dma1, dma2;

      if (pnp_trace)
	printk ("Device activation OK\n");
      sb_base = pnp_get_port (dev, 0);

      irq = pnp_get_irq (dev, 0);
      dma1 = pnp_get_dma (dev, 1);
      dma2 = pnp_get_dma (dev, 0);

      if (sb_base && sb_driver)
	{
	  hw_config.io_base = sb_base;
	  hw_config.irq = irq;
	  hw_config.dma = dma1;
	  hw_config.dma2 = dma2;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (sb_driver, &hw_config);
	}
    }
}

void
als_pnp_mpu (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;

  int             portmask = 0x00, irqmask = 0x01, dmamask = 0x03;
  int             mpu_driver;

  if (pnp_trace)
    printk ("ALS### PnP MPU driver waking up\n");

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "SB16 clone";

  if ((mpu_driver = sndtable_identify_card ("UART401")))
    portmask |= 0x01;
  else
    printk ("Sound: ALS PnP device detected but no MPU driver enabled\n");

  if (!portmask)		/* No drivers available */
    return;

  if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
    printk ("sound_pnp: Failed to find free resources\n");
  else
    {
      struct address_info hw_config;
      int             mpu_base;
      int             irq;

      if (pnp_trace)
	printk ("Device activation OK\n");
      mpu_base = pnp_get_port (dev, 0);

      irq = pnp_get_irq (dev, 0);

      if (mpu_base && mpu_driver)
	{
	  hw_config.io_base = mpu_base;
	  hw_config.irq = irq;
	  hw_config.dma = -1;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (mpu_driver, &hw_config);
	}
    }
}

void
als_pnp_opl (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;

  int             portmask = 0x00, irqmask = 0x01, dmamask = 0x03;
  int             opl3_driver;

  if (pnp_trace)
    printk ("ALS### PnP OPL3 driver waking up\n");

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "SB16 clone";

  if ((opl3_driver = sndtable_identify_card ("OPL3")))
    portmask |= 0x01;
  else
    printk ("Sound: ALS PnP device detected but no OPL3 driver enabled\n");

  if (!portmask)		/* No drivers available */
    return;

  if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
    printk ("sound_pnp: Failed to find free resources\n");
  else
    {
      struct address_info hw_config;
      int             opl3_base;
      int             irq;

      if (pnp_trace)
	printk ("Device activation OK\n");
      opl3_base = pnp_get_port (dev, 0);

      irq = pnp_get_irq (dev, 0);

      if (opl3_base && opl3_driver)
	{
	  hw_config.io_base = opl3_base;
	  hw_config.irq = 0;
	  hw_config.dma = -1;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = sound_osp;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (opl3_driver, &hw_config);
	}
    }
}

void
ess_pnp (void *parm)
{
  struct pnp_dev *dev = (struct pnp_dev *) parm;
  char           *name;

  int             portmask = 0x03, irqmask = 0x01, dmamask = 0x03;
  int             sb_driver, mpu_driver, opl3_driver;

  if (pnp_trace)
    printk ("ESS PnP driver waking up\n");

  if (pnp_trace)
    {
      printk ("ESS1868: IRQB,IRQA = %x\n", pnp_readreg (dev, 0x20));
      printk ("ESS1868: IRQD,IRQC = %x\n", pnp_readreg (dev, 0x21));
      printk ("ESS1868: IRQF,IRQE = %x\n", pnp_readreg (dev, 0x22));
      printk ("ESS1868: DRQB,DRQA = %x\n", pnp_readreg (dev, 0x23));
      printk ("ESS1868: DRQD,DRQC = %x\n", pnp_readreg (dev, 0x24));
      printk ("ESS1868: Configuration ROM Header 0 = %x\n", pnp_readreg (dev, 0x25));
      printk ("ESS1868: Configuration ROM Header 1 = %x\n", pnp_readreg (dev, 0x26));
      printk ("ESS1868: HW Volume IRQ = %x\n", pnp_readreg (dev, 0x27));
      printk ("ESS1868: MPU401 IRQ = %x\n", pnp_readreg (dev, 0x28));
    }

  if (pnp_readreg (dev, 0x27) & 0x01)	/* MPU401 is at logical device #3 */
    printk ("Nonstandard ESS1868 implementation - contact support@4front-tech.com\n");

  if (dev->card && dev->card->name)
    name = dev->card->name;
  else
    name = "ESS AudioDrive PnP";

  if ((sb_driver = sndtable_identify_card ("SBLAST")))
    portmask |= 0x01;
  else
    printk ("Sound: SB PnP device detected but no driver enabled\n");

  if ((mpu_driver = sndtable_identify_card ("SBMPU")))
    portmask |= 0x02;
  else
    printk ("Sound: SB PnP device detected but SB MPU driver not enabled\n");

  if ((opl3_driver = sndtable_identify_card ("OPL3")))
    portmask |= 0x04;
  else
    printk ("Sound: SB PnP device detected but OPL3 driver not enabled\n");

  if (!portmask)		/* No drivers available */
    return;

  if (!pnp_allocate_device (pnp_sig, dev, portmask, irqmask, dmamask, 0x00))
    printk ("sound_pnp: Failed to find free resources\n");
  else
    {
      struct address_info hw_config;
      int             sb_base, mpu_base, opl3_base;
      int             irq;
      int             dma1, dma2;

      if (pnp_trace)
	printk ("Device activation OK\n");
      sb_base = pnp_get_port (dev, 0);
      opl3_base = pnp_get_port (dev, 1);
      mpu_base = pnp_get_port (dev, 2);

      irq = pnp_get_irq (dev, 0);
      dma1 = pnp_get_dma (dev, 0);
      /* dma2 = pnp_get_dma (dev, 1); */ dma2 = -1;

      if (pnp_trace)
	{
	  printk ("ESS PnP at %x/%x/%x, %d, %d/%d\n",
		  sb_base, mpu_base, opl3_base,
		  irq, dma1, dma2);
	}

      if (sb_base && sb_driver)
	{
	  hw_config.io_base = sb_base;
	  hw_config.irq = irq;
	  hw_config.dma = dma1;
	  hw_config.dma2 = (dma2 == NO_DMA) ? dma1 : dma2;
	  hw_config.always_detect = 0;
	  hw_config.name = name;
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = NULL;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (sb_driver, &hw_config);
	}

      if (opl3_base && opl3_driver)
	{
	  hw_config.io_base = opl3_base;
	  hw_config.irq = 0;
	  hw_config.dma = -1;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = "";
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = NULL;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (opl3_driver, &hw_config);

	}

      if (mpu_base && mpu_driver)
	{
	  hw_config.io_base = mpu_base;
	  hw_config.irq = -irq;
	  hw_config.dma = -1;
	  hw_config.dma2 = -1;
	  hw_config.always_detect = 0;
	  hw_config.name = "";
	  hw_config.driver_use_1 = 0;
	  hw_config.driver_use_2 = 0;
	  hw_config.osp = NULL;
	  hw_config.card_subtype = 0;

	  sndtable_start_card (mpu_driver, &hw_config);

	}
    }
}

static struct pnp_sounddev pnp_devs[] =
{
  {PNP_DEVID ('C', 'S', 'C', 0x0000), cs4232_pnp, "CS4232"},
  {PNP_DEVID ('C', 'S', 'C', 0x0003), cs4236mpu_pnp, "CS4236MPU"},
  {PNP_DEVID ('G', 'R', 'V', 0x0000), gus_pnp, "GUS"},
  {PNP_DEVID ('R', 'V', 'L', 0x0010), gus_pnp, "WAVXTREME"},
  {PNP_DEVID ('A', 'D', 'V', 0x0010), gus_pnp, "IWAVE"},
  {PNP_DEVID ('D', 'X', 'P', 0x0010), gus_pnp, "IWAVE"},
  {PNP_DEVID ('Y', 'M', 'H', 0x0021), opl3sa2_pnp, "OPL3SA2"},
  {PNP_DEVID ('O', 'P', 'T', 0x0000), opti82C924_pnp, "82C924"},
  {PNP_DEVID ('O', 'P', 'T', 0x9250), opti82C924_pnp, "82C925"},
  {PNP_DEVID ('O', 'P', 'T', 0x9310), opti82C931_pnp, "82C931"},
  {PNP_DEVID ('O', 'P', 'T', 0x0002), opti82C924mpu_pnp, "82C924MPU"},
  {PNP_DEVID ('E', 'N', 'S', 0x0000), soundscape_pnp, "SSCAPE"},
  {PNP_DEVID ('N', 'E', 'C', 0x0000), soundscape_pnp, "NEC"},
  {PNP_DEVID ('E', 'N', 'S', 0x1010), soundscape_vivo, "SSCAPE"},
  {PNP_DEVID ('E', 'N', 'S', 0x1011), soundscape_vivo, "SSCAPE"},
  {PNP_DEVID ('C', 'T', 'L', 0x0031), sb_pnp, "SB"},
  {PNP_DEVID ('C', 'T', 'L', 0x0001), sb_pnp, "SB"},
  {PNP_DEVID ('C', 'T', 'L', 0x0041), sb_pnp, "SB"},	/* SB32 (new revision) */
  {PNP_DEVID ('C', 'T', 'L', 0x0042), sb_pnp, "SB"},	/* SB64 */
  {PNP_DEVID ('C', 'T', 'L', 0x0044), sb_pnp, "SB"},	/* SB64 Gold */
  {PNP_DEVID ('@', '@', '@', 0x0001), als_pnp, "SB"},
  {PNP_DEVID ('@', 'X', '@', 0x0001), als_pnp_mpu, "SB"},
  {PNP_DEVID ('@', 'H', '@', 0x0001), als_pnp_opl, "SB"},
  {PNP_DEVID ('E', 'S', 'S', 0x1868), ess_pnp, "ESS"}
};

static int      nr_pnpdevs = sizeof (pnp_devs) / sizeof (struct pnp_sounddev);

static int
pnp_activate (int id, struct pnp_dev *dev)
{
  int             i;

  for (i = 0; i < nr_pnpdevs; i++)
    if (pnp_devs[i].id == id)
      {

	if (pnp_trace)
	  printk ("PnP dev: %08x, %s\n", id,
		  pnp_devid2asc (id));

	pnp_devs[i].setup ((void *) dev);
	return 1;
      }

  return 0;
}

void
cs423x_special (struct pnp_dev *dev)
{
}

void
sound_pnp_init (int *osp)
{

  struct pnp_dev *dev;

  if (pnp_sig == 0)
    init_pnp (0, osp);

  if (pnp_sig == 0)
    if ((pnp_sig = pnp_connect ("sound")) == -1)
      {
	printk ("Sound: Can't connect to kernel PnP services.\n");
	return;
      }

/*
 * First handle some special configuration ports.
 */
  dev = NULL;
  while ((dev = pnp_get_next_device (pnp_sig, dev)) != NULL)
    {
      int             i;

      for (i = 0; special_devices[i] != 0; i++)
	if (special_devices[i] == dev->key)
	  switch (i)
	    {
	    case 0:
	    case 1:
	      cs423x_special (dev);
	      break;
	    }
    }

/*
 * Next disable some unused sound devices so that they don't consume
 * valuable IRQ and DMA resources.
 */
  dev = NULL;
  while ((dev = pnp_get_next_device (pnp_sig, dev)) != NULL)
    {
      int             i;

      for (i = 0; disabled_devices[i] != 0; i++)
	if (disabled_devices[i] == dev->key)
	  pnp_enable_device (dev, 0);	/* Disable it */
    }

/*
 * Then initialize drivers for known PnP devices.
 */
  dev = NULL;
  while ((dev = pnp_get_next_device (pnp_sig, dev)) != NULL)
    {
      if (!pnp_activate (dev->key, dev))
	{
	  /* Scan all compatible devices */

	  int             i;

	  for (i = 0; i < dev->ncompat; i++)
	    if (pnp_activate (dev->compat_keys[i], dev))
	      break;
	}
    }
}

void
sound_pnp_disconnect (void)
{
  pnp_disconnect (pnp_sig);
}


#endif
