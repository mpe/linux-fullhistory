/*
 * sound/maui.c
 *
 * The low level driver for Turtle Beach Maui and Tropez.
 */
/*
 * Copyright (C) by Hannu Savolainen 1993-1996
 *
 * USS/Lite for Linux is distributed under the GNU GENERAL PUBLIC LICENSE (GPL)
 * Version 2 (June 1991). See the "COPYING" file distributed with this software
 * for more info.
 */
#include <linux/config.h>


#define USE_SEQ_MACROS
#define USE_SIMPLE_MACROS

#include "sound_config.h"

#if defined(CONFIG_MAUI)

static int      maui_base = 0x330;

static volatile int irq_ok = 0;
static int     *maui_osp;

#define HOST_DATA_PORT	(maui_base + 2)
#define HOST_STAT_PORT	(maui_base + 3)
#define HOST_CTRL_PORT	(maui_base + 3)

#define STAT_TX_INTR	0x40
#define STAT_TX_AVAIL	0x20
#define STAT_TX_IENA	0x10
#define STAT_RX_INTR	0x04
#define STAT_RX_AVAIL	0x02
#define STAT_RX_IENA	0x01

static int      (*orig_load_patch) (int dev, int format, const char *addr,
				 int offs, int count, int pmgr_flag) = NULL;

#ifdef HAVE_MAUI_BOOT
#include "maui_boot.h"
#else
static unsigned char *maui_os = NULL;
static int      maui_osLen = 0;

#endif

static wait_handle *maui_sleeper = NULL;
static volatile struct snd_wait maui_sleep_flag =
{0};

static int
maui_wait (int mask)
{
  int             i;

/*
 * Perform a short initial wait without sleeping
 */

  for (i = 0; i < 100; i++)
    {
      if (inb (HOST_STAT_PORT) & mask)
	{
	  return 1;
	}
    }

/*
 * Wait up to 15 seconds with sleeping
 */

  for (i = 0; i < 150; i++)
    {
      if (inb (HOST_STAT_PORT) & mask)
	{
	  return 1;
	}


      {
	unsigned long   tlimit;

	if (HZ / 10)
	  current_set_timeout (tlimit = jiffies + (HZ / 10));
	else
	  tlimit = (unsigned long) -1;
	maui_sleep_flag.flags = WK_SLEEP;
	module_interruptible_sleep_on (&maui_sleeper);
	if (!(maui_sleep_flag.flags & WK_WAKEUP))
	  {
	    if (jiffies >= tlimit)
	      maui_sleep_flag.flags |= WK_TIMEOUT;
	  }
	maui_sleep_flag.flags &= ~WK_SLEEP;
      };
      if (current_got_fatal_signal ())
	return 0;
    }

  return 0;
}

static int
maui_read (void)
{
  if (maui_wait (STAT_RX_AVAIL))
    return inb (HOST_DATA_PORT);

  return -1;
}

static int
maui_write (unsigned char data)
{
  if (maui_wait (STAT_TX_AVAIL))
    {
      outb (data, HOST_DATA_PORT);
      return 1;
    }
  printk ("Maui: Write timeout\n");

  return 0;
}

void
mauiintr (int irq, void *dev_id, struct pt_regs *dummy)
{
  irq_ok = 1;
}

static int
download_code (void)
{
  int             i, lines = 0;
  int             eol_seen = 0, done = 0;
  int             skip = 1;

  printk ("Code download (%d bytes): ", maui_osLen);

  for (i = 0; i < maui_osLen; i++)
    {
      if (maui_os[i] != '\r')
	if (!skip || (maui_os[i] == 'S' && (i == 0 || maui_os[i - 1] == '\n')))
	  {
	    skip = 0;

	    if (maui_os[i] == '\n')
	      eol_seen = skip = 1;
	    else if (maui_os[i] == 'S')
	      {
		if (maui_os[i + 1] == '8')
		  done = 1;
		if (!maui_write (0xF1))
		  goto failure;
		if (!maui_write ('S'))
		  goto failure;
	      }
	    else
	      {
		if (!maui_write (maui_os[i]))
		  goto failure;
	      }

	    if (eol_seen)
	      {
		int             c = 0;

		int             n;

		eol_seen = 0;

		for (n = 0; n < 2; n++)
		  if (maui_wait (STAT_RX_AVAIL))
		    {
		      c = inb (HOST_DATA_PORT);
		      break;
		    }

		if (c != 0x80)
		  {
		    printk ("Doanload not acknowledged\n");
		    return 0;
		  }
		else if (!(lines++ % 10))
		  printk (".");

		if (done)
		  {
		    printk ("\nDownload complete\n");
		    return 1;
		  }
	      }
	  }
    }

failure:

  printk ("\nDownload failed!!!\n");
  return 0;
}

static int
maui_init (int irq)
{
  int             i;
  unsigned char   bits;

  switch (irq)
    {
    case 9:
      bits = 0x00;
      break;
    case 5:
      bits = 0x08;
      break;
    case 12:
      bits = 0x10;
      break;
    case 15:
      bits = 0x18;
      break;

    default:
      printk ("Maui: Invalid IRQ %d\n", irq);
      return 0;
    }

  outb (0x00, HOST_CTRL_PORT);	/* Reset */

  outb (bits, HOST_DATA_PORT);	/* Set the IRQ bits */
  outb (bits | 0x80, HOST_DATA_PORT);	/* Set the IRQ bits again? */

  outb (0x80, HOST_CTRL_PORT);	/* Leave reset */
  outb (0x80, HOST_CTRL_PORT);	/* Leave reset */

  outb (0xD0, HOST_CTRL_PORT);	/* Cause interrupt */

  for (i = 0; i < 1000000 && !irq_ok; i++);

  if (!irq_ok)
    return 0;

  outb (0x80, HOST_CTRL_PORT);	/* Leave reset */

  printk ("Turtle Beach Maui initialization\n");

  if (!download_code ())
    return 0;

  outb (0xE0, HOST_CTRL_PORT);	/* Normal operation */

  /* Select mpu401 mode */

  maui_write (0xf0);
  maui_write (1);
  if (maui_read () != 0x80)
    {
      maui_write (0xf0);
      maui_write (1);
      if (maui_read () != 0x80)
	printk ("Maui didn't acknowledge set HW mode command\n");
    }

  printk ("Maui initialized OK\n");
  return 1;
}

static int
maui_short_wait (int mask)
{
  int             i;

  for (i = 0; i < 1000; i++)
    {
      if (inb (HOST_STAT_PORT) & mask)
	{
	  return 1;
	}
    }

  return 0;
}

int
maui_load_patch (int dev, int format, const char *addr,
		 int offs, int count, int pmgr_flag)
{

  struct sysex_info header;
  unsigned long   left, src_offs;
  int             hdr_size = (unsigned long) &header.data[0] - (unsigned long) &header;
  int             i;

  if (format == SYSEX_PATCH)	/* Handled by midi_synth.c */
    return orig_load_patch (dev, format, addr, offs, count, pmgr_flag);

  if (format != MAUI_PATCH)
    {
      printk ("Maui: Unknown patch format\n");
    }

  if (count < hdr_size)
    {
      printk ("Maui error: Patch header too short\n");
      return -(EINVAL);
    }

  count -= hdr_size;

  /*
   * Copy the header from user space but ignore the first bytes which have
   * been transferred already.
   */

  memcpy_fromfs (&((char *) &header)[offs], &(addr)[offs], hdr_size - offs);

  if (count < header.len)
    {
      printk ("Maui warning: Host command record too short (%d<%d)\n",
	      count, (int) header.len);
      header.len = count;
    }

  left = header.len;
  src_offs = 0;

  for (i = 0; i < left; i++)
    {
      unsigned char   data;

      data = get_fs_byte (&((addr)[hdr_size + i]));
      if (i == 0 && !(data & 0x80))
	return -(EINVAL);

      if (maui_write (data) == -1)
	return -(EIO);
    }

  if ((i = maui_read ()) != 0x80)
    {
      if (i != -1)
	printk ("Maui: Error status %02x\n", i);

      return -(EIO);
    }

  return 0;
}

int
probe_maui (struct address_info *hw_config)
{
  int             i;
  int             tmp1, tmp2, ret;

  if (check_region (hw_config->io_base, 8))
    return 0;

  maui_base = hw_config->io_base;
  maui_osp = hw_config->osp;

  if (snd_set_irq_handler (hw_config->irq, mauiintr, "Maui", maui_osp) < 0)
    return 0;

  maui_sleep_flag.flags = WK_NONE;
/*
 * Initialize the processor if necessary
 */

  if (maui_osLen > 0)
    {
      if (!(inb (HOST_STAT_PORT) & STAT_TX_AVAIL) ||
	  !maui_write (0x9F) ||	/* Report firmware version */
	  !maui_short_wait (STAT_RX_AVAIL) ||
	  maui_read () == -1 || maui_read () == -1)
	if (!maui_init (hw_config->irq))
	  {
	    snd_release_irq (hw_config->irq);
	    return 0;
	  }
    }

  if (!maui_write (0xCF))	/* Report hardware version */
    {
      printk ("No WaveFront firmware detected (card uninitialized?)\n");
      snd_release_irq (hw_config->irq);
      return 0;
    }

  if ((tmp1 = maui_read ()) == -1 || (tmp2 = maui_read ()) == -1)
    {
      printk ("No WaveFront firmware detected (card uninitialized?)\n");
      snd_release_irq (hw_config->irq);
      return 0;
    }

  if (tmp1 == 0xff || tmp2 == 0xff)
    {
      snd_release_irq (hw_config->irq);
      return 0;
    }

  if (trace_init)
    printk ("WaveFront hardware version %d.%d\n", tmp1, tmp2);

  if (!maui_write (0x9F))	/* Report firmware version */
    return 0;
  if ((tmp1 = maui_read ()) == -1 || (tmp2 = maui_read ()) == -1)
    return 0;

  if (trace_init)
    printk ("WaveFront firmware version %d.%d\n", tmp1, tmp2);

  if (!maui_write (0x85))	/* Report free DRAM */
    return 0;
  tmp1 = 0;
  for (i = 0; i < 4; i++)
    {
      tmp1 |= maui_read () << (7 * i);
    }
  if (trace_init)
    printk ("Available DRAM %dk\n", tmp1 / 1024);

  for (i = 0; i < 1000; i++)
    if (probe_mpu401 (hw_config))
      break;

  ret = probe_mpu401 (hw_config);

  if (ret)
    request_region (hw_config->io_base + 2, 6, "Maui");

  return ret;
}

void
attach_maui (struct address_info *hw_config)
{
  int             this_dev = num_midis;

  conf_printf ("Maui", hw_config);

  hw_config->irq *= -1;
  hw_config->name = "Maui";
  attach_mpu401 (hw_config);

  if (num_midis > this_dev)	/* The MPU401 driver installed itself */
    {
      struct synth_operations *synth;

      /*
       * Intercept patch loading calls so that they canbe handled
       * by the Maui driver.
       */

      synth = midi_devs[this_dev]->converter;

      if (synth != NULL)
	{
	  orig_load_patch = synth->load_patch;
	  synth->load_patch = &maui_load_patch;
	}
      else
	printk ("Maui: Can't install patch loader\n");
    }
}

void
unload_maui (struct address_info *hw_config)
{
  int             irq = hw_config->irq;

  release_region (hw_config->io_base + 2, 6);

  unload_mpu401 (hw_config);

  if (irq < 0)
    irq = -irq;

  if (irq > 0)
    snd_release_irq (irq);
}


#endif
