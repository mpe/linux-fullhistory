/*
 * sound/sscape.c
 *
 * Low level driver for Ensoniq Soundscape
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

#if defined(CONFIG_SSCAPE)

#include "coproc.h"

/*
 *    I/O ports
 */
#define MIDI_DATA        0
#define MIDI_CTRL        1
#define HOST_CTRL        2
#define TX_READY		0x02
#define RX_READY		0x01
#define HOST_DATA        3
#define ODIE_ADDR        4
#define ODIE_DATA        5

/*
 *    Indirect registers
 */
#define GA_INTSTAT_REG   0
#define GA_INTENA_REG    1
#define GA_DMAA_REG      2
#define GA_DMAB_REG      3
#define GA_INTCFG_REG    4
#define GA_DMACFG_REG    5
#define GA_CDCFG_REG     6
#define GA_SMCFGA_REG    7
#define GA_SMCFGB_REG    8
#define GA_HMCTL_REG     9

/*
 * DMA channel identifiers (A and B)
 */
#define SSCAPE_DMA_A 		0
#define SSCAPE_DMA_B		1

#define PORT(name)	(devc->base+name)

/*
 * Host commands recognized by the OBP microcode
 */
#define CMD_GEN_HOST_ACK        0x80
#define CMD_GEN_MPU_ACK         0x81
#define CMD_GET_BOARD_TYPE      0x82
#define CMD_SET_CONTROL         0x88
#define CMD_GET_CONTROL         0x89
#define 	CTL_MASTER_VOL          0
#define 	CTL_MIC_MODE            2
#define 	CTL_SYNTH_VOL           4
#define 	CTL_WAVE_VOL            7
#define CMD_SET_MT32            0x96
#define CMD_GET_MT32            0x97
#define CMD_SET_EXTMIDI         0x9b
#define CMD_GET_EXTMIDI         0x9c

#define CMD_ACK			0x80

typedef struct sscape_info
  {
    int             base, irq, dma;
    int             ok;		/* Properly detected */
    int             failed;
    int             dma_allocated;
    int             my_audiodev;
    int             opened;
    int            *osp;
  }

sscape_info;
static struct sscape_info dev_info =
{0};
static struct sscape_info *devc = &dev_info;

static wait_handle *sscape_sleeper = NULL;
static volatile struct snd_wait sscape_sleep_flag =
{0};

/* Some older cards have assigned interrupt bits differently than new ones */
static char     valid_interrupts_old[] =
{9, 7, 5, 15};

static char     valid_interrupts_new[] =
{9, 5, 7, 10};

static char    *valid_interrupts = valid_interrupts_new;

#ifdef REVEAL_SPEA
static char     old_hardware = 1;

#else
static char     old_hardware = 0;

#endif

static unsigned char
sscape_read (struct sscape_info *devc, int reg)
{
  unsigned long   flags;
  unsigned char   val;

  save_flags (flags);
  cli ();
  outb (reg, PORT (ODIE_ADDR));
  val = inb (PORT (ODIE_DATA));
  restore_flags (flags);
  return val;
}

static void
sscape_write (struct sscape_info *devc, int reg, int data)
{
  unsigned long   flags;

  save_flags (flags);
  cli ();
  outb (reg, PORT (ODIE_ADDR));
  outb (data, PORT (ODIE_DATA));
  restore_flags (flags);
}

static void
host_open (struct sscape_info *devc)
{
  outb (0x00, PORT (HOST_CTRL));	/* Put the board to the host mode */
}

static void
host_close (struct sscape_info *devc)
{
  outb (0x03, PORT (HOST_CTRL));	/* Put the board to the MIDI mode */
}

static int
host_write (struct sscape_info *devc, unsigned char *data, int count)
{
  unsigned long   flags;
  int             i, timeout_val;

  save_flags (flags);
  cli ();

  /*
     * Send the command and data bytes
   */

  for (i = 0; i < count; i++)
    {
      for (timeout_val = 10000; timeout_val > 0; timeout_val--)
	if (inb (PORT (HOST_CTRL)) & TX_READY)
	  break;

      if (timeout_val <= 0)
	{
	  restore_flags (flags);
	  return 0;
	}

      outb (data[i], PORT (HOST_DATA));
    }


  restore_flags (flags);

  return 1;
}

static int
host_read (struct sscape_info *devc)
{
  unsigned long   flags;
  int             timeout_val;
  unsigned char   data;

  save_flags (flags);
  cli ();

  /*
     * Read a byte
   */

  for (timeout_val = 10000; timeout_val > 0; timeout_val--)
    if (inb (PORT (HOST_CTRL)) & RX_READY)
      break;

  if (timeout_val <= 0)
    {
      restore_flags (flags);
      return -1;
    }

  data = inb (PORT (HOST_DATA));

  restore_flags (flags);

  return data;
}

static int
host_command1 (struct sscape_info *devc, int cmd)
{
  unsigned char   buf[10];

  buf[0] = (unsigned char) (cmd & 0xff);

  return host_write (devc, buf, 1);
}

static int
host_command2 (struct sscape_info *devc, int cmd, int parm1)
{
  unsigned char   buf[10];

  buf[0] = (unsigned char) (cmd & 0xff);
  buf[1] = (unsigned char) (parm1 & 0xff);

  return host_write (devc, buf, 2);
}

static int
host_command3 (struct sscape_info *devc, int cmd, int parm1, int parm2)
{
  unsigned char   buf[10];

  buf[0] = (unsigned char) (cmd & 0xff);
  buf[1] = (unsigned char) (parm1 & 0xff);
  buf[2] = (unsigned char) (parm2 & 0xff);

  return host_write (devc, buf, 3);
}

static void
set_mt32 (struct sscape_info *devc, int value)
{
  host_open (devc);
  host_command2 (devc, CMD_SET_MT32,
		 value ? 1 : 0);
  if (host_read (devc) != CMD_ACK)
    {
      /* printk ("SNDSCAPE: Setting MT32 mode failed\n"); */
    }
  host_close (devc);
}

static void
set_control (struct sscape_info *devc, int ctrl, int value)
{
  host_open (devc);
  host_command3 (devc, CMD_SET_CONTROL, ctrl, value);
  if (host_read (devc) != CMD_ACK)
    {
      /* printk ("SNDSCAPE: Setting control (%d) failed\n", ctrl); */
    }
  host_close (devc);
}

static int
get_board_type (struct sscape_info *devc)
{
  int             tmp;

  host_open (devc);
  if (!host_command1 (devc, CMD_GET_BOARD_TYPE))
    tmp = -1;
  else
    tmp = host_read (devc);
  host_close (devc);
  return tmp;
}

void
sscapeintr (int irq, void *dev_id, struct pt_regs *dummy)
{
  unsigned char   bits, tmp;
  static int      debug = 0;

  bits = sscape_read (devc, GA_INTSTAT_REG);
  if ((sscape_sleep_flag.flags & WK_SLEEP))
    {
      {
	sscape_sleep_flag.flags = WK_WAKEUP;
	module_wake_up (&sscape_sleeper);
      };
    }

  if (bits & 0x02)		/* Host interface interrupt */
    {
      printk ("SSCAPE: Host interrupt, data=%02x\n", host_read (devc));
    }

#if defined(CONFIG_UART401) && defined(CONFIG_MIDI)
  if (bits & 0x01)
    {
      uart401intr (irq, NULL, NULL);
      if (debug++ > 10)		/* Temporary debugging hack */
	{
	  sscape_write (devc, GA_INTENA_REG, 0x00);	/* Disable all interrupts */
	}
    }
#endif

  /*
     * Acknowledge interrupts (toggle the interrupt bits)
   */

  tmp = sscape_read (devc, GA_INTENA_REG);
  sscape_write (devc, GA_INTENA_REG, (~bits & 0x0e) | (tmp & 0xf1));

}


static void
do_dma (struct sscape_info *devc, int dma_chan, unsigned long buf, int blk_size, int mode)
{
  unsigned char   temp;

  if (dma_chan != SSCAPE_DMA_A)
    {
      printk ("SSCAPE: Tried to use DMA channel  != A. Why?\n");
      return;
    }

  audio_devs[devc->my_audiodev]->flags &= ~DMA_AUTOMODE;
  DMAbuf_start_dma (devc->my_audiodev,
		    buf,
		    blk_size, mode);
  audio_devs[devc->my_audiodev]->flags |= DMA_AUTOMODE;

  temp = devc->dma << 4;	/* Setup DMA channel select bits */
  if (devc->dma <= 3)
    temp |= 0x80;		/* 8 bit DMA channel */

  temp |= 1;			/* Trigger DMA */
  sscape_write (devc, GA_DMAA_REG, temp);
  temp &= 0xfe;			/* Clear DMA trigger */
  sscape_write (devc, GA_DMAA_REG, temp);
}

static int
verify_mpu (struct sscape_info *devc)
{
  /*
     * The SoundScape board could be in three modes (MPU, 8250 and host).
     * If the card is not in the MPU mode, enabling the MPU driver will
     * cause infinite loop (the driver believes that there is always some
     * received data in the buffer.
     *
     * Detect this by looking if there are more than 10 received MIDI bytes
     * (0x00) in the buffer.
   */

  int             i;

  for (i = 0; i < 10; i++)
    {
      if (inb (devc->base + HOST_CTRL) & 0x80)
	return 1;

      if (inb (devc->base) != 0x00)
	return 1;
    }

  printk ("SoundScape: The device is not in the MPU-401 mode\n");
  return 0;
}

static int
sscape_coproc_open (void *dev_info, int sub_device)
{
  if (sub_device == COPR_MIDI)
    {
      set_mt32 (devc, 0);
      if (!verify_mpu (devc))
	return -(EIO);
    }

  sscape_sleep_flag.flags = WK_NONE;
  return 0;
}

static void
sscape_coproc_close (void *dev_info, int sub_device)
{
  struct sscape_info *devc = dev_info;
  unsigned long   flags;

  save_flags (flags);
  cli ();
  if (devc->dma_allocated)
    {
      sscape_write (devc, GA_DMAA_REG, 0x20);	/* DMA channel disabled */
      devc->dma_allocated = 0;
    }
  sscape_sleep_flag.flags = WK_NONE;
  restore_flags (flags);

  return;
}

static void
sscape_coproc_reset (void *dev_info)
{
}

static int
sscape_download_boot (struct sscape_info *devc, unsigned char *block, int size, int flag)
{
  unsigned long   flags;
  unsigned char   temp;
  int             done, timeout_val;
  static unsigned char codec_dma_bits = 0;

  if (flag & CPF_FIRST)
    {
      /*
         * First block. Have to allocate DMA and to reset the board
         * before continuing.
       */

      save_flags (flags);
      cli ();
      codec_dma_bits = sscape_read (devc, GA_CDCFG_REG);
      sscape_write (devc, GA_CDCFG_REG,
		    codec_dma_bits & ~0x08);	/* Disable codec DMA */

      if (devc->dma_allocated == 0)
	{
	  devc->dma_allocated = 1;
	}
      restore_flags (flags);

      sscape_write (devc, GA_HMCTL_REG,
		    (temp = sscape_read (devc, GA_HMCTL_REG)) & 0x3f);	/*Reset */

      for (timeout_val = 10000; timeout_val > 0; timeout_val--)
	sscape_read (devc, GA_HMCTL_REG);	/* Delay */

      /* Take board out of reset */
      sscape_write (devc, GA_HMCTL_REG,
		    (temp = sscape_read (devc, GA_HMCTL_REG)) | 0x80);
    }

  /*
   * Transfer one code block using DMA
   */
  memcpy (audio_devs[devc->my_audiodev]->dmap_out->raw_buf, block, size);

  save_flags (flags);
  cli ();
/******** INTERRUPTS DISABLED NOW ********/
  do_dma (devc, SSCAPE_DMA_A,
	  audio_devs[devc->my_audiodev]->dmap_out->raw_buf_phys,
	  size, DMA_MODE_WRITE);

  /*
   * Wait until transfer completes.
   */
  sscape_sleep_flag.flags = WK_NONE;
  done = 0;
  timeout_val = 100;
  while (!done && timeout_val-- > 0)
    {
      int             resid;


      {
	unsigned long   tlimit;

	if (1)
	  current_set_timeout (tlimit = jiffies + (1));
	else
	  tlimit = (unsigned long) -1;
	sscape_sleep_flag.flags = WK_SLEEP;
	module_interruptible_sleep_on (&sscape_sleeper);
	if (!(sscape_sleep_flag.flags & WK_WAKEUP))
	  {
	    if (jiffies >= tlimit)
	      sscape_sleep_flag.flags |= WK_TIMEOUT;
	  }
	sscape_sleep_flag.flags &= ~WK_SLEEP;
      };
      clear_dma_ff (devc->dma);
      if ((resid = get_dma_residue (devc->dma)) == 0)
	{
	  done = 1;
	}
    }

  restore_flags (flags);
  if (!done)
    return 0;

  if (flag & CPF_LAST)
    {
      /*
         * Take the board out of reset
       */
      outb (0x00, PORT (HOST_CTRL));
      outb (0x00, PORT (MIDI_CTRL));

      temp = sscape_read (devc, GA_HMCTL_REG);
      temp |= 0x40;
      sscape_write (devc, GA_HMCTL_REG, temp);	/* Kickstart the board */

      /*
         * Wait until the ODB wakes up
       */

      save_flags (flags);
      cli ();
      done = 0;
      timeout_val = 5 * HZ;
      while (!done && timeout_val-- > 0)
	{

	  {
	    unsigned long   tlimit;

	    if (1)
	      current_set_timeout (tlimit = jiffies + (1));
	    else
	      tlimit = (unsigned long) -1;
	    sscape_sleep_flag.flags = WK_SLEEP;
	    module_interruptible_sleep_on (&sscape_sleeper);
	    if (!(sscape_sleep_flag.flags & WK_WAKEUP))
	      {
		if (jiffies >= tlimit)
		  sscape_sleep_flag.flags |= WK_TIMEOUT;
	      }
	    sscape_sleep_flag.flags &= ~WK_SLEEP;
	  };
	  if (inb (PORT (HOST_DATA)) == 0xff)	/* OBP startup acknowledge */
	    done = 1;
	}
      sscape_write (devc, GA_CDCFG_REG, codec_dma_bits);

      restore_flags (flags);
      if (!done)
	{
	  printk ("SoundScape: The OBP didn't respond after code download\n");
	  return 0;
	}

      save_flags (flags);
      cli ();
      done = 0;
      timeout_val = 5 * HZ;
      while (!done && timeout_val-- > 0)
	{

	  {
	    unsigned long   tlimit;

	    if (1)
	      current_set_timeout (tlimit = jiffies + (1));
	    else
	      tlimit = (unsigned long) -1;
	    sscape_sleep_flag.flags = WK_SLEEP;
	    module_interruptible_sleep_on (&sscape_sleeper);
	    if (!(sscape_sleep_flag.flags & WK_WAKEUP))
	      {
		if (jiffies >= tlimit)
		  sscape_sleep_flag.flags |= WK_TIMEOUT;
	      }
	    sscape_sleep_flag.flags &= ~WK_SLEEP;
	  };
	  if (inb (PORT (HOST_DATA)) == 0xfe)	/* Host startup acknowledge */
	    done = 1;
	}
      restore_flags (flags);
      if (!done)
	{
	  printk ("SoundScape: OBP Initialization failed.\n");
	  return 0;
	}

      printk ("SoundScape board of type %d initialized OK\n",
	      get_board_type (devc));

      set_control (devc, CTL_MASTER_VOL, 100);
      set_control (devc, CTL_SYNTH_VOL, 100);

#ifdef SSCAPE_DEBUG3
      /*
         * Temporary debugging aid. Print contents of the registers after
         * downloading the code.
       */
      {
	int             i;

	for (i = 0; i < 13; i++)
	  printk ("I%d = %02x (new value)\n", i, sscape_read (devc, i));
      }
#endif

    }

  return 1;
}

static int
download_boot_block (void *dev_info, copr_buffer * buf)
{
  if (buf->len <= 0 || buf->len > sizeof (buf->data))
    return -(EINVAL);

  if (!sscape_download_boot (devc, buf->data, buf->len, buf->flags))
    {
      printk ("SSCAPE: Unable to load microcode block to the OBP.\n");
      return -(EIO);
    }

  return 0;
}

static int
sscape_coproc_ioctl (void *dev_info, unsigned int cmd, caddr_t arg, int local)
{

  switch (cmd)
    {
    case SNDCTL_COPR_RESET:
      sscape_coproc_reset (dev_info);
      return 0;
      break;

    case SNDCTL_COPR_LOAD:
      {
	copr_buffer    *buf;
	int             err;

	buf = (copr_buffer *) vmalloc (sizeof (copr_buffer));
	if (buf == NULL)
	  return -(ENOSPC);
	memcpy_fromfs ((char *) buf, &((char *) arg)[0], sizeof (*buf));
	err = download_boot_block (dev_info, buf);
	vfree (buf);
	return err;
      }
      break;

    default:
      return -(EINVAL);
    }

}

static coproc_operations sscape_coproc_operations =
{
  "SoundScape M68K",
  sscape_coproc_open,
  sscape_coproc_close,
  sscape_coproc_ioctl,
  sscape_coproc_reset,
  &dev_info
};

static int      sscape_detected = 0;

void
attach_sscape (struct address_info *hw_config)
{

#ifndef SSCAPE_REGS
  /*
     * Config register values for Spea/V7 Media FX and Ensoniq S-2000.
     * These values are card
     * dependent. If you have another SoundScape based card, you have to
     * find the correct values. Do the following:
     *  - Compile this driver with SSCAPE_DEBUG1 defined.
     *  - Shut down and power off your machine.
     *  - Boot with DOS so that the SSINIT.EXE program is run.
     *  - Warm boot to {Linux|SYSV|BSD} and write down the lines displayed
     *    when detecting the SoundScape.
     *  - Modify the following list to use the values printed during boot.
     *    Undefine the SSCAPE_DEBUG1
   */
#define SSCAPE_REGS { \
/* I0 */	0x00, \
		0xf0, /* Note! Ignored. Set always to 0xf0 */ \
		0x20, /* Note! Ignored. Set always to 0x20 */ \
		0x20, /* Note! Ignored. Set always to 0x20 */ \
		0xf5, /* Ignored */ \
		0x10, \
		0x00, \
		0x2e, /* I7 MEM config A. Likely to vary between models */ \
		0x00, /* I8 MEM config B. Likely to vary between models */ \
/* I9 */	0x40 /* Ignored */ \
	}
#endif

  unsigned long   flags;
  static unsigned char regs[10] = SSCAPE_REGS;

  int             i, irq_bits = 0xff;

  if (sscape_detected != hw_config->io_base)
    return;

  if (old_hardware)
    {
      valid_interrupts = valid_interrupts_old;
      conf_printf ("Ensoniq Soundscape (old)", hw_config);
    }
  else
    conf_printf ("Ensoniq Soundscape", hw_config);

  for (i = 0; i < sizeof (valid_interrupts); i++)
    if (hw_config->irq == valid_interrupts[i])
      {
	irq_bits = i;
	break;
      }

  if (hw_config->irq > 15 || (regs[4] = irq_bits == 0xff))
    {
      printk ("Invalid IRQ%d\n", hw_config->irq);
      return;
    }

  save_flags (flags);
  cli ();

  for (i = 1; i < 10; i++)
    switch (i)
      {
      case 1:			/* Host interrupt enable */
	sscape_write (devc, i, 0xf0);	/* All interrupts enabled */
	break;

      case 2:			/* DMA A status/trigger register */
      case 3:			/* DMA B status/trigger register */
	sscape_write (devc, i, 0x20);	/* DMA channel disabled */
	break;

      case 4:			/* Host interrupt config reg */
	sscape_write (devc, i, 0xf0 | (irq_bits << 2) | irq_bits);
	break;

      case 5:			/* Don't destroy CD-ROM DMA config bits (0xc0) */
	sscape_write (devc, i, (regs[i] & 0x3f) |
		      (sscape_read (devc, i) & 0xc0));
	break;

      case 6:			/* CD-ROM config. Don't touch. */
	break;

      case 9:			/* Master control reg. Don't modify CR-ROM bits. Disable SB emul */
	sscape_write (devc, i,
		      (sscape_read (devc, i) & 0xf0) | 0x08);
	break;

      default:
	sscape_write (devc, i, regs[i]);
      }

  restore_flags (flags);

#ifdef SSCAPE_DEBUG2
  /*
     * Temporary debugging aid. Print contents of the registers after
     * changing them.
   */
  {
    int             i;

    for (i = 0; i < 13; i++)
      printk ("I%d = %02x (new value)\n", i, sscape_read (devc, i));
  }
#endif

#if defined(CONFIG_MIDI) && defined(CONFIG_UART401)
  if (probe_uart401 (hw_config))
    hw_config->always_detect = 1;
  {
    int             prev_devs;

    prev_devs = num_midis;
    hw_config->name = "Soundscape";

    hw_config->irq *= -1;	/* Negative value signals IRQ sharing */
    attach_uart401 (hw_config);
    hw_config->irq *= -1;	/* Restore it */

    if (num_midis == (prev_devs + 1))	/* The MPU driver installed itself */
      midi_devs[prev_devs]->coproc = &sscape_coproc_operations;
  }
#endif

  sscape_write (devc, GA_INTENA_REG, 0x80);	/* Master IRQ enable */
  devc->ok = 1;
  devc->failed = 0;
}

int
probe_sscape (struct address_info *hw_config)
{
  unsigned char   save;

  devc->failed = 1;
  devc->base = hw_config->io_base;
  devc->irq = hw_config->irq;
  devc->dma = hw_config->dma;
  devc->osp = hw_config->osp;

  if (sscape_detected != 0 && sscape_detected != hw_config->io_base)
    return 0;

  /*
     * First check that the address register of "ODIE" is
     * there and that it has exactly 4 writeable bits.
     * First 4 bits
   */
  if ((save = inb (PORT (ODIE_ADDR))) & 0xf0)
    return 0;

  outb (0x00, PORT (ODIE_ADDR));
  if (inb (PORT (ODIE_ADDR)) != 0x00)
    return 0;

  outb (0xff, PORT (ODIE_ADDR));
  if (inb (PORT (ODIE_ADDR)) != 0x0f)
    return 0;

  outb (save, PORT (ODIE_ADDR));

  /*
     * Now verify that some indirect registers return zero on some bits.
     * This may break the driver with some future revisions of "ODIE" but...
   */

  if (sscape_read (devc, 0) & 0x0c)
    return 0;

  if (sscape_read (devc, 1) & 0x0f)
    return 0;

  if (sscape_read (devc, 5) & 0x0f)
    return 0;

#ifdef SSCAPE_DEBUG1
  /*
     * Temporary debugging aid. Print contents of the registers before
     * changing them.
   */
  {
    int             i;

    for (i = 0; i < 13; i++)
      printk ("I%d = %02x (old value)\n", i, sscape_read (devc, i));
  }
#endif

  if (old_hardware)		/* Check that it's really an old Spea/Reveal card. */
    {
      unsigned char   tmp;
      int             cc;

      if (!((tmp = sscape_read (devc, GA_HMCTL_REG)) & 0xc0))
	{
	  sscape_write (devc, GA_HMCTL_REG, tmp | 0x80);
	  for (cc = 0; cc < 200000; ++cc)
	    inb (devc->base + ODIE_ADDR);
	}
      else
	old_hardware = 0;
    }


  sscape_detected = hw_config->io_base;

  return 1;
}

int
probe_ss_ms_sound (struct address_info *hw_config)
{
  int             i, irq_bits = 0xff;

  if (devc->failed)
    return 0;

  if (devc->ok == 0)
    {
      printk ("SoundScape: Invalid initialization order.\n");
      return 0;
    }

  for (i = 0; i < sizeof (valid_interrupts); i++)
    if (hw_config->irq == valid_interrupts[i])
      {
	irq_bits = i;
	break;
      }
  if (hw_config->irq > 15 || irq_bits == 0xff)
    {
      printk ("SoundScape: Invalid MSS IRQ%d\n", hw_config->irq);
      return 0;
    }

  return ad1848_detect (hw_config->io_base, NULL, hw_config->osp);
}

void
attach_ss_ms_sound (struct address_info *hw_config)
{
  /*
     * This routine configures the SoundScape card for use with the
     * Win Sound System driver. The AD1848 codec interface uses the CD-ROM
     * config registers of the "ODIE".
   */

  int             i, irq_bits = 0xff;

  int             prev_devs = num_audiodevs;

  hw_config->dma = devc->dma;	/* Share the DMA with the ODIE/OPUS chip */

  /*
   * Setup the DMA polarity.
   */
  sscape_write (devc, GA_DMACFG_REG, 0x50);

  /*
     * Take the gate-arry off of the DMA channel.
   */
  sscape_write (devc, GA_DMAB_REG, 0x20);

  /*
     * Init the AD1848 (CD-ROM) config reg.
   */

  for (i = 0; i < sizeof (valid_interrupts); i++)
    if (hw_config->irq == valid_interrupts[i])
      {
	irq_bits = i;
	break;
      }

  sscape_write (devc, GA_CDCFG_REG, 0x89 | (hw_config->dma << 4) |
		(irq_bits << 1));

  if (hw_config->irq == devc->irq)
    printk ("SoundScape: Warning! The WSS mode can't share IRQ with MIDI\n");

  ad1848_init ("SoundScape", hw_config->io_base,
	       hw_config->irq,
	       hw_config->dma,
	       hw_config->dma,
	       0,
	       devc->osp);

  if (num_audiodevs == (prev_devs + 1))		/* The AD1848 driver installed itself */
    audio_devs[prev_devs]->coproc = &sscape_coproc_operations;
  devc->my_audiodev = prev_devs;

#ifdef SSCAPE_DEBUG5
  /*
     * Temporary debugging aid. Print contents of the registers
     * after the AD1848 device has been initialized.
   */
  {
    int             i;

    for (i = 0; i < 13; i++)
      printk ("I%d = %02x\n", i, sscape_read (devc, i));
  }
#endif

}

void
unload_sscape (struct address_info *hw_config)
{
#if defined(CONFIG_UART401) && defined(CONFIG_MIDI)
  unload_uart401 (hw_config);
#endif
  snd_release_irq (hw_config->irq);
}

void
unload_ss_ms_sound (struct address_info *hw_config)
{
  ad1848_unload (hw_config->io_base,
		 hw_config->irq,
		 devc->dma,
		 devc->dma,
		 0);
}


#endif
