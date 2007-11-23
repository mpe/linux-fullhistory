/*
 * sound/pss.c
 *
 * The low level driver for the Personal Sound System (ECHO ESC614).
 */
/*
 * Copyright by Hannu Savolainen 1993-1996
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: 1. Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer. 2.
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include <linux/config.h>


#include "sound_config.h"

#if defined(CONFIG_PSS) && defined(CONFIG_AUDIO)

/*
 * PSS registers.
 */
#define REG(x)	(devc->base+x)
#define	PSS_DATA	0
#define	PSS_STATUS	2
#define PSS_CONTROL	2
#define	PSS_ID		4
#define	PSS_IRQACK	4
#define	PSS_PIO		0x1a

/*
 * Config registers
 */
#define CONF_PSS	0x10
#define CONF_WSS	0x12
#define CONF_SB		0x13
#define CONF_CDROM	0x16
#define CONF_MIDI	0x18

/*
 * Status bits.
 */
#define PSS_FLAG3     0x0800
#define PSS_FLAG2     0x0400
#define PSS_FLAG1     0x1000
#define PSS_FLAG0     0x0800
#define PSS_WRITE_EMPTY  0x8000
#define PSS_READ_FULL    0x4000

#include "coproc.h"

#ifdef PSS_HAVE_LD
#include "synth-ld.h"
#else
static int      pss_synthLen = 0;
static unsigned char pss_synth[1] =
{0};

#endif

typedef struct pss_config
  {
    int             base;
    int             irq;
    int             dma;
    int            *osp;
  }

pss_config;

static pss_config pss_data;
static pss_config *devc = &pss_data;

static int      pss_initialized = 0;
static int      nonstandard_microcode = 0;

int
probe_pss (struct address_info *hw_config)
{
  unsigned short  id;
  int             irq, dma;

  devc->base = hw_config->io_base;
  irq = devc->irq = hw_config->irq;
  dma = devc->dma = hw_config->dma;
  devc->osp = hw_config->osp;

  if (devc->base != 0x220 && devc->base != 0x240)
    if (devc->base != 0x230 && devc->base != 0x250)	/* Some cards use these */
      return 0;

  if (check_region (devc->base, 16))
    {
      printk ("PSS: I/O port conflict\n");
      return 0;
    }

  id = inw (REG (PSS_ID));
  if ((id >> 8) != 'E')
    {
      /* printk ("No PSS signature detected at 0x%x (0x%x)\n", devc->base, id); */
      return 0;
    }

  return 1;
}

static int
set_irq (pss_config * devc, int dev, int irq)
{
  static unsigned short irq_bits[16] =
  {
    0x0000, 0x0000, 0x0000, 0x0008,
    0x0000, 0x0010, 0x0000, 0x0018,
    0x0000, 0x0020, 0x0028, 0x0030,
    0x0038, 0x0000, 0x0000, 0x0000
  };

  unsigned short  tmp, bits;

  if (irq < 0 || irq > 15)
    return 0;

  tmp = inw (REG (dev)) & ~0x38;	/* Load confreg, mask IRQ bits out */

  if ((bits = irq_bits[irq]) == 0 && irq != 0)
    {
      printk ("PSS: Invalid IRQ %d\n", irq);
      return 0;
    }

  outw (tmp | bits, REG (dev));
  return 1;
}

static int
set_io_base (pss_config * devc, int dev, int base)
{
  unsigned short  tmp = inw (REG (dev)) & 0x003f;
  unsigned short  bits = (base & 0x0ffc) << 4;

  outw (bits | tmp, REG (dev));

  return 1;
}

static int
set_dma (pss_config * devc, int dev, int dma)
{
  static unsigned short dma_bits[8] =
  {
    0x0001, 0x0002, 0x0000, 0x0003,
    0x0000, 0x0005, 0x0006, 0x0007
  };

  unsigned short  tmp, bits;

  if (dma < 0 || dma > 7)
    return 0;

  tmp = inw (REG (dev)) & ~0x07;	/* Load confreg, mask DMA bits out */

  if ((bits = dma_bits[dma]) == 0 && dma != 4)
    {
      printk ("PSS: Invalid DMA %d\n", dma);
      return 0;
    }

  outw (tmp | bits, REG (dev));
  return 1;
}

static int
pss_reset_dsp (pss_config * devc)
{
  unsigned long   i, limit = jiffies + 10;

  outw (0x2000, REG (PSS_CONTROL));

  for (i = 0; i < 32768 && jiffies < limit; i++)
    inw (REG (PSS_CONTROL));

  outw (0x0000, REG (PSS_CONTROL));

  return 1;
}

static int
pss_put_dspword (pss_config * devc, unsigned short word)
{
  int             i, val;

  for (i = 0; i < 327680; i++)
    {
      val = inw (REG (PSS_STATUS));
      if (val & PSS_WRITE_EMPTY)
	{
	  outw (word, REG (PSS_DATA));
	  return 1;
	}
    }
  return 0;
}

static int
pss_get_dspword (pss_config * devc, unsigned short *word)
{
  int             i, val;

  for (i = 0; i < 327680; i++)
    {
      val = inw (REG (PSS_STATUS));
      if (val & PSS_READ_FULL)
	{
	  *word = inw (REG (PSS_DATA));
	  return 1;
	}
    }

  return 0;
}

static int
pss_download_boot (pss_config * devc, unsigned char *block, int size, int flags)
{
  int             i, limit, val, count;

  if (flags & CPF_FIRST)
    {
/*_____ Warn DSP software that a boot is coming */
      outw (0x00fe, REG (PSS_DATA));

      limit = jiffies + 10;

      for (i = 0; i < 32768 && jiffies < limit; i++)
	if (inw (REG (PSS_DATA)) == 0x5500)
	  break;

      outw (*block++, REG (PSS_DATA));

      pss_reset_dsp (devc);
    }

  count = 1;
  while (1)
    {
      int             j;

      for (j = 0; j < 327670; j++)
	{
/*_____ Wait for BG to appear */
	  if (inw (REG (PSS_STATUS)) & PSS_FLAG3)
	    break;
	}

      if (j == 327670)
	{
	  /* It's ok we timed out when the file was empty */
	  if (count >= size && flags & CPF_LAST)
	    break;
	  else
	    {
	      printk ("\nPSS: DownLoad timeout problems, byte %d=%d\n",
		      count, size);
	      return 0;
	    }
	}
/*_____ Send the next byte */
      outw (*block++, REG (PSS_DATA));
      count++;
    }

  if (flags & CPF_LAST)
    {
/*_____ Why */
      outw (0, REG (PSS_DATA));

      limit = jiffies + 10;
      for (i = 0; i < 32768 && jiffies < limit; i++)
	val = inw (REG (PSS_STATUS));

      limit = jiffies + 10;
      for (i = 0; i < 32768 && jiffies < limit; i++)
	{
	  val = inw (REG (PSS_STATUS));
	  if (val & 0x4000)
	    break;
	}

      /* now read the version */
      for (i = 0; i < 32000; i++)
	{
	  val = inw (REG (PSS_STATUS));
	  if (val & PSS_READ_FULL)
	    break;
	}
      if (i == 32000)
	return 0;

      val = inw (REG (PSS_DATA));
      /* printk("<PSS: microcode version %d.%d loaded>", val/16, val % 16); */
    }

  return 1;
}

long
attach_pss (long mem_start, struct address_info *hw_config)
{
  unsigned short  id;
  char            tmp[100];

  devc->base = hw_config->io_base;
  devc->irq = hw_config->irq;
  devc->dma = hw_config->dma;
  devc->osp = hw_config->osp;

  if (!probe_pss (hw_config))
    return mem_start;

  id = inw (REG (PSS_ID)) & 0x00ff;

  /*
     * Disable all emulations. Will be enabled later (if required).
   */
  outw (0x0000, REG (CONF_PSS));
  outw (0x0000, REG (CONF_WSS));
  outw (0x0000, REG (CONF_SB));
  outw (0x0000, REG (CONF_MIDI));
  outw (0x0000, REG (CONF_CDROM));

#if YOU_REALLY_WANT_TO_ALLOCATE_THESE_RESOURCES
  if (sound_alloc_dma (hw_config->dma, "PSS"))
    {
      printk ("pss.c: Can't allocate DMA channel\n");
      return mem_start;
    }

  if (!set_irq (devc, CONF_PSS, devc->irq))
    {
      printk ("PSS: IRQ error\n");
      return mem_start;
    }

  if (!set_dma (devc, CONF_PSS, devc->dma))
    {
      printk ("PSS: DRQ error\n");
      return mem_start;
    }
#endif

  pss_initialized = 1;
  sprintf (tmp, "ECHO-PSS  Rev. %d", id);
  conf_printf (tmp, hw_config);

  return mem_start;
}

int
probe_pss_mpu (struct address_info *hw_config)
{
  int             timeout;

  if (!pss_initialized)
    return 0;

  if (check_region (hw_config->io_base, 2))
    {
      printk ("PSS: MPU I/O port conflict\n");
      return 0;
    }

  if (!set_io_base (devc, CONF_MIDI, hw_config->io_base))
    {
      printk ("PSS: MIDI base error.\n");
      return 0;
    }

  if (!set_irq (devc, CONF_MIDI, hw_config->irq))
    {
      printk ("PSS: MIDI IRQ error.\n");
      return 0;
    }

  if (!pss_synthLen)
    {
      printk ("PSS: Can't enable MPU. MIDI synth microcode not available.\n");
      return 0;
    }

  if (!pss_download_boot (devc, pss_synth, pss_synthLen, CPF_FIRST | CPF_LAST))
    {
      printk ("PSS: Unable to load MIDI synth microcode to DSP.\n");
      return 0;
    }

/*
 * Finally wait until the DSP algorithm has initialized itself and
 * deactivates receive interrupt.
 */

  for (timeout = 900000; timeout > 0; timeout--)
    {
      if ((inb (hw_config->io_base + 1) & 0x80) == 0)	/* Input data avail */
	inb (hw_config->io_base);	/* Discard it */
      else
	break;			/* No more input */
    }

#if (defined(CONFIG_MPU401) || defined(CONFIG_MPU_EMU)) && defined(CONFIG_MIDI)
  return probe_mpu401 (hw_config);
#else
  return 0;
#endif
}

static int
pss_coproc_open (void *dev_info, int sub_device)
{
  switch (sub_device)
    {
    case COPR_MIDI:

      if (pss_synthLen == 0)
	{
	  printk ("PSS: MIDI synth microcode not available.\n");
	  return -EIO;
	}

      if (nonstandard_microcode)
	if (!pss_download_boot (devc, pss_synth, pss_synthLen, CPF_FIRST | CPF_LAST))
	  {
	    printk ("PSS: Unable to load MIDI synth microcode to DSP.\n");
	    return -EIO;
	  }
      nonstandard_microcode = 0;
      break;

    default:;
    }
  return 0;
}

static void
pss_coproc_close (void *dev_info, int sub_device)
{
  return;
}

static void
pss_coproc_reset (void *dev_info)
{
  if (pss_synthLen)
    if (!pss_download_boot (devc, pss_synth, pss_synthLen, CPF_FIRST | CPF_LAST))
      {
	printk ("PSS: Unable to load MIDI synth microcode to DSP.\n");
      }
  nonstandard_microcode = 0;
}

static int
download_boot_block (void *dev_info, copr_buffer * buf)
{
  if (buf->len <= 0 || buf->len > sizeof (buf->data))
    return -EINVAL;

  if (!pss_download_boot (devc, buf->data, buf->len, buf->flags))
    {
      printk ("PSS: Unable to load microcode block to DSP.\n");
      return -EIO;
    }
  nonstandard_microcode = 1;	/* The MIDI microcode has been overwritten */

  return 0;
}

static int
pss_coproc_ioctl (void *dev_info, unsigned int cmd, caddr_t arg, int local)
{
  /* printk("PSS coproc ioctl %x %x %d\n", cmd, arg, local); */

  switch (cmd)
    {
    case SNDCTL_COPR_RESET:
      pss_coproc_reset (dev_info);
      return 0;
      break;

    case SNDCTL_COPR_LOAD:
      {
	copr_buffer    *buf;
	int             err;

	buf = (copr_buffer *) kmalloc (sizeof (copr_buffer), GFP_KERNEL);
	if (buf == NULL)
	  return -ENOSPC;

	memcpy_fromfs ((char *) buf, &(((char *) arg)[0]), sizeof (*buf));
	err = download_boot_block (dev_info, buf);
	kfree (buf);
	return err;
      }
      break;

    case SNDCTL_COPR_SENDMSG:
      {
	/* send buf->len words from buf->data to DSP */

	copr_msg       *buf;
	unsigned long   flags;
	unsigned short *data;
	int             i;

	buf = (copr_msg *) kmalloc (sizeof (copr_msg), GFP_KERNEL);
	if (buf == NULL)
	  return -ENOSPC;

	memcpy_fromfs ((char *) buf, &(((char *) arg)[0]), sizeof (*buf));

	data = (unsigned short *) (buf->data);

	save_flags (flags);
	cli ();

	for (i = 0; i < buf->len; i++)
	  {
	    if (!pss_put_dspword (devc, *data++))
	      {
		restore_flags (flags);
		/* feed back number of WORDs sent */
		memcpy_tofs( (char *)(&(((copr_msg *)arg)->len)),
				(char *)(&i), sizeof(buf->len));
		kfree (buf);
		return -EIO;
	      }
	  }

	restore_flags (flags);
	kfree (buf);

	return 0;
      }
      break;


    case SNDCTL_COPR_RCVMSG:
      {
	/* try to read as much words as possible from DSP into buf */
	copr_msg       *buf;
	unsigned long   flags;
	unsigned short *data;
	unsigned int    i;
	int             err = 0;

	buf = (copr_msg *) kmalloc (sizeof (copr_msg), GFP_KERNEL);
	if (buf == NULL)
	  return -ENOSPC;
#if 0
	memcpy_fromfs ((char *) buf, &(((char *) arg)[0]), sizeof (*buf));
#endif

	data = (unsigned short *) buf->data;

	save_flags (flags);
	cli ();

	for (i = 0; i < sizeof(buf->data); i++)
	  {
	    if (!pss_get_dspword (devc, data++))
	      {
		buf->len = i;	/* feed back number of WORDs read */
		err = (i==0)? -EIO : 0;	   /* EIO only if no word read */
		break;
	      }
	  }

	if( i==sizeof(buf->data) )
		buf->len = i;

	restore_flags (flags);

	memcpy_tofs ((&((char *) arg)[0]), buf, sizeof (*buf));
	kfree (buf);

	return err;
      }
      break;


    case SNDCTL_COPR_RDATA:
      {
	copr_debug_buf  buf;
	unsigned long   flags;
	unsigned short  tmp;

	memcpy_fromfs ((char *) &buf, &(((char *) arg)[0]), sizeof (buf));

	save_flags (flags);
	cli ();
	if (!pss_put_dspword (devc, 0x00d0))
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	if (!pss_put_dspword (devc, (unsigned short) (buf.parm1 & 0xffff)))
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	if (!pss_get_dspword (devc, &tmp))
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	buf.parm1 = tmp;
	restore_flags (flags);

	memcpy_tofs ((&((char *) arg)[0]), &buf, sizeof (buf));
	return 0;
      }
      break;

    case SNDCTL_COPR_WDATA:
      {
	copr_debug_buf  buf;
	unsigned long   flags;
	unsigned short  tmp;

	memcpy_fromfs ((char *) &buf, &(((char *) arg)[0]), sizeof (buf));

	save_flags (flags);
	cli ();
	if (!pss_put_dspword (devc, 0x00d1))
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	if (!pss_put_dspword (devc, (unsigned short) (buf.parm1 & 0xffff)))
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	tmp = (unsigned int) buf.parm2 & 0xffff;
	if (!pss_put_dspword (devc, tmp))
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	restore_flags (flags);
	return 0;
      }
      break;

    case SNDCTL_COPR_WCODE:
      {
	copr_debug_buf  buf;
	unsigned long   flags;
	unsigned short  tmp;

	memcpy_fromfs ((char *) &buf, &(((char *) arg)[0]), sizeof (buf));

	save_flags (flags);
	cli ();
	if (!pss_put_dspword (devc, 0x00d3))
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	if (!pss_put_dspword (devc, (unsigned short) (buf.parm1 & 0xffff)))
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	tmp = (unsigned int) buf.parm2 & 0x00ff;
	if (!pss_put_dspword (devc, tmp))
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	tmp = ((unsigned int) buf.parm2 >> 8) & 0xffff;
	if (!pss_put_dspword (devc, tmp))
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	restore_flags (flags);
	return 0;
      }
      break;

    case SNDCTL_COPR_RCODE:
      {
	copr_debug_buf  buf;
	unsigned long   flags;
	unsigned short  tmp;

	memcpy_fromfs ((char *) &buf, &(((char *) arg)[0]), sizeof (buf));

	save_flags (flags);
	cli ();
	if (!pss_put_dspword (devc, 0x00d2))
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	if (!pss_put_dspword (devc, (unsigned short) (buf.parm1 & 0xffff)))
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	if (!pss_get_dspword (devc, &tmp))	/* Read msb */
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	buf.parm1 = tmp << 8;

	if (!pss_get_dspword (devc, &tmp))	/* Read lsb */
	  {
	    restore_flags (flags);
	    return -EIO;
	  }

	buf.parm1 |= tmp & 0x00ff;

	restore_flags (flags);

	memcpy_tofs ((&((char *) arg)[0]), &buf, sizeof (buf));
	return 0;
      }
      break;

    default:
      return -EINVAL;
    }

  return -EINVAL;
}

static coproc_operations pss_coproc_operations =
{
  "ADSP-2115",
  pss_coproc_open,
  pss_coproc_close,
  pss_coproc_ioctl,
  pss_coproc_reset,
  &pss_data
};

long
attach_pss_mpu (long mem_start, struct address_info *hw_config)
{
  long            ret;

#if (defined(CONFIG_MPU401) || defined(CONFIG_MPU_EMU)) && defined(CONFIG_MIDI)
  {
    int             prev_devs;
    prev_devs = num_midis;
    ret = attach_mpu401 (mem_start, hw_config);

    if (num_midis == (prev_devs + 1))	/* The MPU driver installed itself */
      midi_devs[prev_devs]->coproc = &pss_coproc_operations;
  }
#endif
  return ret;
}

int
probe_pss_mss (struct address_info *hw_config)
{
  int             timeout;

  if (!pss_initialized)
    return 0;

  if (check_region (hw_config->io_base, 8))
    {
      printk ("PSS: WSS I/O port conflict\n");
      return 0;
    }

  if (!set_io_base (devc, CONF_WSS, hw_config->io_base))
    {
      printk ("PSS: WSS base error.\n");
      return 0;
    }

  if (!set_irq (devc, CONF_WSS, hw_config->irq))
    {
      printk ("PSS: WSS IRQ error.\n");
      return 0;
    }

  if (!set_dma (devc, CONF_WSS, hw_config->dma))
    {
      printk ("PSS: WSS DRQ error\n");
      return 0;
    }

  /*
     * For some reason the card returns 0xff in the WSS status register
     * immediately after boot. Probably MIDI+SB emulation algorithm
     * downloaded to the ADSP2115 spends some time initializing the card.
     * Let's try to wait until it finishes this task.
   */
  for (timeout = 0;
       timeout < 100000 && (inb (hw_config->io_base + 3) & 0x3f) != 0x04;
       timeout++);

  outb (0x0b, hw_config->io_base + 4);	/* Required by some cards */
  return probe_ms_sound (hw_config);
}

long
attach_pss_mss (long mem_start, struct address_info *hw_config)
{
  int             prev_devs;
  long            ret;

  prev_devs = num_audiodevs;
  ret = attach_ms_sound (mem_start, hw_config);

  if (num_audiodevs == (prev_devs + 1))		/* The MSS driver installed itself */
    audio_devs[prev_devs]->coproc = &pss_coproc_operations;

  return ret;
}

void
unload_pss (struct address_info *hw_config)
{
}

void
unload_pss_mpu (struct address_info *hw_config)
{
  unload_mpu401 (hw_config);
}

void
unload_pss_mss (struct address_info *hw_config)
{
  unload_ms_sound (hw_config);
}

#endif
