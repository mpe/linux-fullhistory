/*
 * drivers/sbus/audio/cs4231.c
 *
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@noc.rutgers.edu)
 * Copyright (C) 1996 Derrick J Brashear (shadow@andrew.cmu.edu)
 *
 * This is the lowlevel driver for the CS4231 audio chip found on some
 * sun4m machines.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/malloc.h>
#include <linux/init.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/delay.h>
#include <asm/sbus.h>

#include "audio.h"
#include "cs4231.h"

/* Stolen for now from compat.h */
#ifndef MAX                             /* Usually found in <sys/param.h>. */
#define MAX(_a,_b)      ((_a)<(_b)?(_b):(_a))
#endif
#ifndef MIN                             /* Usually found in <sys/param.h>. */
#define MIN(_a,_b)      ((_a)<(_b)?(_a):(_b))
#endif

#define MAX_DRIVERS 1
static struct sparcaudio_driver drivers[MAX_DRIVERS];
static int num_drivers;

static int cs4231_playintr(struct sparcaudio_driver *drv);
static int cs4231_recintr(struct sparcaudio_driver *drv);
static void cs4231_output_muted(struct sparcaudio_driver *drv, unsigned int value);
static void cs4231_mute(struct sparcaudio_driver *drv);
static void cs4231_pollinput(struct sparcaudio_driver *drv);

#define CHIP_BUG udelay(100); cs4231_ready(drv); udelay(1000);

/* Disable mode change, let chip auto-calibrate */
static void cs4231_ready(struct sparcaudio_driver *drv) 
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;
  unsigned int x = 0;

  cs4231_chip->pioregs->iar = (u_char)IAR_AUTOCAL_END;
  while (cs4231_chip->pioregs->iar == IAR_NOT_READY && x <= CS_TIMEOUT) {
    x++;
  }

  x = 0;
  cs4231_chip->pioregs->iar = 0x0b;
  while (cs4231_chip->pioregs->idr == AUTOCAL_IN_PROGRESS && x <= CS_TIMEOUT) {
    x++;
  }
}

/* Audio interrupt handler. */
static void cs4231_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
  struct sparcaudio_driver *drv = (struct sparcaudio_driver *)dev_id;
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;
  __u8 dummy;
  int ic = 1;
  
  /* Clear the interrupt. */
  dummy = cs4231_chip->dmaregs.dmacsr;
  cs4231_chip->dmaregs.dmacsr = dummy;

  /* now go through and figure out what gets to claim the interrupt */
  if (dummy & CS_PLAY_INT) {
    if (dummy & CS_XINT_PNVA) {
      /* recalculate number of samples */
      cs4231_playintr(drv);
    }
    ic = 0;
  }
  if (dummy & CS_CAPT_INT) {
    if (dummy & CS_XINT_CNVA) {
      /* recalculate number of samples */
      cs4231_recintr(drv);
    }
    ic = 0;
  }
  if ((dummy & CS_XINT_CEMP) 
      && (cs4231_chip->perchip_info.record.active == 0)) 
    {
      ic = 0;
    }
  if ((dummy & CS_XINT_EMPT) && (cs4231_chip->perchip_info.play.active == 0)) {
    cs4231_chip->dmaregs.dmacsr |= (CS_PPAUSE);
    cs4231_chip->pioregs->iar = 0x9;
    cs4231_chip->pioregs->idr &= PEN_DISABLE;
    
    cs4231_mute(drv);
    
    /* recalculate number of samples */
    /* cleanup DMA */
    ic = 0;
  }
  if (dummy & CS_GENL_INT) {
    ic = 0;
  }
}

/* Set output mute */
static void cs4231_output_muted(struct sparcaudio_driver *drv, unsigned int value)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;
  if (!value) {
    cs4231_chip->pioregs->iar = 0x7;
    cs4231_chip->pioregs->idr &= OUTCR_UNMUTE;
    cs4231_chip->pioregs->iar = 0x6;
    cs4231_chip->pioregs->idr &= OUTCR_UNMUTE;
    cs4231_chip->perchip_info.output_muted = 0;
  } else {
    cs4231_chip->pioregs->iar = 0x7;
    cs4231_chip->pioregs->idr |= OUTCR_MUTE;
    cs4231_chip->pioregs->iar = 0x6;
    cs4231_chip->pioregs->idr |= OUTCR_MUTE;
    cs4231_chip->perchip_info.output_muted = 1;
  }
  return /*(cs4231_chip->perchip_info.output_muted)*/;
}

/* Set chip "output" port */
static unsigned int cs4231_out_port(struct sparcaudio_driver *drv, unsigned int value)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;
  unsigned int r = 0;

  /* You can have any combo you want. Just don't tell anyone. */

  cs4231_chip->pioregs->iar = 0x1a;
  cs4231_chip->pioregs->idr |= MONO_IOCR_MUTE;
  cs4231_chip->pioregs->iar = 0x0a;
  cs4231_chip->pioregs->idr |= PINCR_LINE_MUTE;
  cs4231_chip->pioregs->idr |= PINCR_HDPH_MUTE;

  if (value & AUDIO_SPEAKER) {
    cs4231_chip->pioregs->iar = 0x1a;
    cs4231_chip->pioregs->idr &= ~MONO_IOCR_MUTE;
    r |= AUDIO_SPEAKER;
  }

  if (value & AUDIO_HEADPHONE) {
   cs4231_chip->pioregs->iar = 0x0a;
   cs4231_chip->pioregs->idr &= ~PINCR_HDPH_MUTE;
   r |= AUDIO_HEADPHONE;
  }

  if (value & AUDIO_LINE_OUT) {
    cs4231_chip->pioregs->iar = 0x0a;
    cs4231_chip->pioregs->idr &= ~PINCR_LINE_MUTE;
    r |= AUDIO_LINE_OUT;
  }
  
  return (r);
}

/* Set chip "input" port */
static unsigned int cs4231_in_port(struct sparcaudio_driver *drv, unsigned int value)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;
  unsigned int r = 0;

  /* The order of these seems to matter. Can't tell yet why. */
  if (value & AUDIO_INTERNAL_CD_IN) {
    cs4231_chip->pioregs->iar = 0x1;
    cs4231_chip->pioregs->idr = CDROM_ENABLE(cs4231_chip->pioregs->idr);
    cs4231_chip->pioregs->iar = 0x0;
    cs4231_chip->pioregs->idr = CDROM_ENABLE(cs4231_chip->pioregs->idr);
    r = AUDIO_INTERNAL_CD_IN;
  }
  if ((value & AUDIO_LINE_IN)) {
    cs4231_chip->pioregs->iar = 0x1;
    cs4231_chip->pioregs->idr = LINE_ENABLE(cs4231_chip->pioregs->idr);
    cs4231_chip->pioregs->iar = 0x0;
    cs4231_chip->pioregs->idr = LINE_ENABLE(cs4231_chip->pioregs->idr);
    r = AUDIO_LINE_IN;
  } else if (value & AUDIO_MICROPHONE) {
    cs4231_chip->pioregs->iar = 0x1;
    cs4231_chip->pioregs->idr = MIC_ENABLE(cs4231_chip->pioregs->idr);
    cs4231_chip->pioregs->iar = 0x0;
    cs4231_chip->pioregs->idr = MIC_ENABLE(cs4231_chip->pioregs->idr);
    r = AUDIO_MICROPHONE;
  }

  return (r);
}

/* Set chip "monitor" gain */
static unsigned int cs4231_monitor_gain(struct sparcaudio_driver *drv, unsigned int value)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;
  int a = 0;

  a = CS4231_MON_MAX_ATEN - (value * (CS4231_MON_MAX_ATEN + 1) / (AUDIO_MAX_GAIN + 1));

  cs4231_chip->pioregs->iar = 0x0d;
  if (a >= CS4231_MON_MAX_ATEN) 
    cs4231_chip->pioregs->idr = LOOPB_OFF;
  else 
    cs4231_chip->pioregs->idr = ((a << 2) | LOOPB_ON);

  if (value == AUDIO_MAX_GAIN) return AUDIO_MAX_GAIN;

  return ((CS4231_MAX_DEV_ATEN - a) * (AUDIO_MAX_GAIN + 1) / (CS4231_MAX_DEV_ATEN + 1));
}

/* Set chip record gain */
static unsigned int cs4231_record_gain(struct sparcaudio_driver *drv, unsigned int value, unsigned char balance)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;
  unsigned int tmp = 0, r, l, ra, la;
  unsigned char old_gain;

  r = l = value;

  if (balance < AUDIO_MID_BALANCE) {
    r = MAX(0, (int)(value - ((AUDIO_MID_BALANCE - balance) << AUDIO_BALANCE_SHIFT)));
  } else if (balance > AUDIO_MID_BALANCE) {
    l = MAX(0, (int)(value - ((balance - AUDIO_MID_BALANCE) << AUDIO_BALANCE_SHIFT)));
  }

  la = l * (CS4231_MAX_GAIN + 1) / (AUDIO_MAX_GAIN + 1);
  ra = r * (CS4231_MAX_GAIN + 1) / (AUDIO_MAX_GAIN + 1);
  
  cs4231_chip->pioregs->iar = 0x0;
  old_gain = cs4231_chip->pioregs->idr;
  cs4231_chip->pioregs->idr = RECGAIN_SET(old_gain, la);
  cs4231_chip->pioregs->iar = 0x1;
  old_gain = cs4231_chip->pioregs->idr;
  cs4231_chip->pioregs->idr = RECGAIN_SET(old_gain, ra);
  
  if (l == value) {
    (l == 0) ? (tmp = 0) : (tmp = ((la + 1) * AUDIO_MAX_GAIN) / (CS4231_MAX_GAIN + 1));
  } else if (r == value) {
    (r == 0) ? (tmp = 0) : (tmp = ((ra + 1) * AUDIO_MAX_GAIN) / (CS4231_MAX_GAIN + 1));
  }
  return (tmp);
}

/* Set chip play gain */
static unsigned int cs4231_play_gain(struct sparcaudio_driver *drv, unsigned int value, unsigned char balance)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;
  unsigned int tmp = 0, r, l, ra, la;
  unsigned char old_gain;

  r = l = value;
  if (balance < AUDIO_MID_BALANCE) {
    r = MAX(0, (int)(value - ((AUDIO_MID_BALANCE - balance) << AUDIO_BALANCE_SHIFT)));
  } else if (balance > AUDIO_MID_BALANCE) {
    l = MAX(0, (int)(value - ((balance - AUDIO_MID_BALANCE) << AUDIO_BALANCE_SHIFT)));
  }

  if (l == 0) {
    la = CS4231_MAX_DEV_ATEN;
  } else {
    la = CS4231_MAX_ATEN - (l * (CS4231_MAX_ATEN + 1) / (AUDIO_MAX_GAIN + 1));
  }
  if (r == 0) {
    ra = CS4231_MAX_DEV_ATEN;
  } else {
    ra = CS4231_MAX_ATEN - (r * (CS4231_MAX_ATEN + 1) / (AUDIO_MAX_GAIN + 1));
  }
  
  cs4231_chip->pioregs->iar = 0x6;
  old_gain = cs4231_chip->pioregs->idr;
  cs4231_chip->pioregs->idr = GAIN_SET(old_gain, la);
  cs4231_chip->pioregs->iar = 0x7;
  old_gain = cs4231_chip->pioregs->idr;
  cs4231_chip->pioregs->idr = GAIN_SET(old_gain, ra);
  
  if ((value == 0) || (value == AUDIO_MAX_GAIN)) {
    tmp = value;
  } else {
    if (l == value) {
      tmp = ((CS4231_MAX_ATEN - la) * (AUDIO_MAX_GAIN + 1) / (CS4231_MAX_ATEN + 1));
    } else if (r == value) {
      tmp = ((CS4231_MAX_ATEN - ra) * (AUDIO_MAX_GAIN + 1) / (CS4231_MAX_ATEN + 1));
    }
  }
  return (tmp);
}

/* Reset the audio chip to a sane state. */
static void cs4231_reset(struct sparcaudio_driver *drv)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;

  cs4231_chip->dmaregs.dmacsr = CS_CHIP_RESET;
  cs4231_chip->dmaregs.dmacsr = 0x00;
  cs4231_chip->dmaregs.dmacsr |= CS_CDC_RESET;
  
  udelay(100);
  
  cs4231_chip->dmaregs.dmacsr &= ~(CS_CDC_RESET);
  cs4231_chip->pioregs->iar |= IAR_AUTOCAL_BEGIN;
  
  CHIP_BUG
    
  cs4231_chip->pioregs->iar = IAR_AUTOCAL_BEGIN | 0x0c;
  cs4231_chip->pioregs->idr = MISC_IR_MODE2;
  cs4231_chip->pioregs->iar = IAR_AUTOCAL_BEGIN | 0x08;
  cs4231_chip->pioregs->idr = DEFAULT_DATA_FMAT; /* Ulaw */
  
  CHIP_BUG
    
  cs4231_chip->pioregs->iar = IAR_AUTOCAL_BEGIN | 0x1c;
  cs4231_chip->pioregs->idr = DEFAULT_DATA_FMAT; /* Ulaw */
  
  CHIP_BUG
    
  cs4231_chip->pioregs->iar = 0x19;
  
  /* see what we can turn on */
  if (cs4231_chip->pioregs->idr & CS4231A)
    cs4231_chip->status |= CS_STATUS_REV_A;
  else
    cs4231_chip->status &= ~CS_STATUS_REV_A;
  
  cs4231_chip->pioregs->iar = IAR_AUTOCAL_BEGIN | 0x10;
  cs4231_chip->pioregs->idr = OLB_ENABLE;
  
  cs4231_chip->pioregs->iar = IAR_AUTOCAL_BEGIN | 0x11;
  if (cs4231_chip->status & CS_STATUS_REV_A)
    cs4231_chip->pioregs->idr = (HPF_ON | XTALE_ON);
  else
    cs4231_chip->pioregs->idr = (HPF_ON);
  
  cs4231_chip->pioregs->iar = IAR_AUTOCAL_BEGIN | 0x1a;
  cs4231_chip->pioregs->idr = 0x00;
  
  /* Now set things up for defaults */
  cs4231_chip->perchip_info.play.port = cs4231_out_port(drv, AUDIO_SPEAKER);
  cs4231_chip->perchip_info.record.port = cs4231_in_port(drv, AUDIO_MICROPHONE);
  cs4231_chip->perchip_info.play.gain = cs4231_play_gain(drv, CS4231_DEFAULT_PLAYGAIN, AUDIO_MID_BALANCE);
  cs4231_chip->perchip_info.record.gain = cs4231_record_gain(drv, CS4231_DEFAULT_RECGAIN, AUDIO_MID_BALANCE);
  cs4231_chip->perchip_info.monitor_gain = cs4231_monitor_gain(drv, LOOPB_OFF);
  
  cs4231_chip->pioregs->iar = (u_char)IAR_AUTOCAL_END;
  
  cs4231_ready(drv);
  
  cs4231_chip->pioregs->iar = IAR_AUTOCAL_BEGIN | 0x09;
  cs4231_chip->pioregs->idr &= ACAL_DISABLE;
  cs4231_chip->pioregs->iar = (u_char)IAR_AUTOCAL_END;
  
  cs4231_ready(drv);

  cs4231_output_muted(drv, 0);
}

static void cs4231_mute(struct sparcaudio_driver *drv)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;

  if (!(cs4231_chip->status & CS_STATUS_REV_A)) {
    cs4231_chip->pioregs->iar = IAR_AUTOCAL_BEGIN;
    udelay(100);
    cs4231_chip->pioregs->iar = IAR_AUTOCAL_END;
    CHIP_BUG
  }
}

/* Not yet useful */
#if 0
static int cs4231_len_to_sample(struct sparcaudio_driver *drv, int length, int direction)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;
  int sample;

  if (/* number of channels == 2*/0) {
    sample = (length/2);
  } else {
    sample = length;
  }
  if (/*encoding == AUDIO_ENCODING_LINEAR*/0) {
    sample = sample/2;
  }
  return (sample);
}
#endif

static int cs4231_open(struct inode * inode, struct file * file, struct sparcaudio_driver *drv)
{	
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;

  /* Set the default audio parameters. */
  
  cs4231_chip->perchip_info.play.sample_rate = CS4231_RATE;
  cs4231_chip->perchip_info.play.channels = CS4231_CHANNELS;
  cs4231_chip->perchip_info.play.precision = CS4231_PRECISION;
  cs4231_chip->perchip_info.play.encoding = AUDIO_ENCODING_ULAW;
  
  cs4231_chip->perchip_info.record.sample_rate = CS4231_RATE;
  cs4231_chip->perchip_info.record.channels = CS4231_CHANNELS;
  cs4231_chip->perchip_info.record.precision = CS4231_PRECISION;
  cs4231_chip->perchip_info.record.encoding = AUDIO_ENCODING_ULAW;
  
  cs4231_ready(drv);
  
  cs4231_chip->status |= CS_STATUS_NEED_INIT;
  
  CHIP_BUG
    
  MOD_INC_USE_COUNT;
  
  return 0;
}

static void cs4231_release(struct inode * inode, struct file * file, struct sparcaudio_driver *drv)
{
  /* zero out any info about what data we have as well */
  /* should insert init on close variable optionally calling cs4231_reset() */
  MOD_DEC_USE_COUNT;
}

static int cs4231_playintr(struct sparcaudio_driver *drv)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;

  /* Send the next byte of outgoing data. */
#if 0
  if (cs4231_chip->output_ptr && cs4231_chip->output_count > 0) {
    cs4231_chip->dmaregs.dmapnva = dma_handle;
    cs4231_chip->dmaregs.dmapnc = length; 
    cs4231_chip->output_ptr++;
    cs4231_chip->output_count--;
    
    /* Done with the buffer? Notify the midlevel driver. */
    if (cs4231_chip->output_count == 0) {
      cs4231_chip->output_ptr = NULL;
      cs4231_chip->output_count = 0;
      sparcaudio_output_done(drv);
    }
  }
#endif
}

static void cs4231_recmute(int fmt)
{
  switch (fmt) {
  case AUDIO_ENCODING_LINEAR:
    /* Insert 0x00 from "here" to end of data stream */
    break;
  case AUDIO_ENCODING_ALAW:
    /* Insert 0xd5 from "here" to end of data stream */
    break;
  case AUDIO_ENCODING_ULAW:
    /* Insert 0xff from "here" to end of data stream */
    break;
  }
}

static int cs4231_recintr(struct sparcaudio_driver *drv)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;

  cs4231_recmute(cs4231_chip->perchip_info.record.encoding);

  if (cs4231_chip->perchip_info.record.active == 0) {
    cs4231_pollinput(drv);
    cs4231_chip->pioregs->iar = 0x9;
    cs4231_chip->pioregs->idr &= CEN_DISABLE;
  }
  /* Read the next byte of incoming data. */
#if 0
  if (cs4231_chip->input_ptr && cs4231_chip->input_count > 0) {
    cs4231_chip->dmaregs.dmacnva = dma_handle;
    cs4231_chip->dmaregs.dmacnc = length;
    cs4231_chip->input_ptr++;
    cs4231_chip->input_count--;
    
    /* Done with the buffer? Notify the midlevel driver. */
    if (cs4231_chip->input_count == 0) {
      cs4231_chip->input_ptr = NULL;
      cs4231_chip->input_count = 0;
      sparcaudio_input_done(drv);
    }
  }
#endif
}

static void cs4231_start_output(struct sparcaudio_driver *drv, __u8 * buffer, unsigned long count)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;

  if (cs4231_chip->perchip_info.play.active || (cs4231_chip->perchip_info.play.pause))
    return;

  cs4231_ready(drv);

  if (cs4231_chip->status & CS_STATUS_NEED_INIT)
    {
      cs4231_chip->pioregs->iar = IAR_AUTOCAL_BEGIN | 0x08;
      cs4231_chip->pioregs->idr = DEFAULT_DATA_FMAT;
      cs4231_chip->pioregs->iar = IAR_AUTOCAL_BEGIN | 0x1c;
      cs4231_chip->pioregs->idr = DEFAULT_DATA_FMAT;

      CHIP_BUG

      cs4231_chip->status &= ~CS_STATUS_NEED_INIT;
    }

  if (!cs4231_chip->perchip_info.play.pause) 
    {
      /* init dma foo here */
      cs4231_chip->dmaregs.dmacsr &= ~CS_XINT_PLAY;
      cs4231_chip->dmaregs.dmacsr &= ~CS_PPAUSE;
      if (cs4231_playintr(drv)) {
	cs4231_chip->dmaregs.dmacsr |= CS_PLAY_SETUP;
	cs4231_chip->pioregs->iar = 0x9;
	cs4231_chip->pioregs->idr |= PEN_ENABLE;
      }
    }
  cs4231_chip->perchip_info.play.active = 1;
}

static void cs4231_stop_output(struct sparcaudio_driver *drv)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;

  cs4231_chip->perchip_info.play.active = 0;
  cs4231_chip->dmaregs.dmacsr |= (CS_PPAUSE);
}

static void cs4231_pollinput(struct sparcaudio_driver *drv)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;
  int x = 0;

  while (!(cs4231_chip->dmaregs.dmacsr & CS_XINT_COVF) && x <= CS_TIMEOUT) {
    x++;
  }
  cs4231_chip->dmaregs.dmacsr |= CS_XINT_CEMP;
}

static void cs4231_start_input(struct sparcaudio_driver *drv, __u8 * buffer, unsigned long count)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;

  if (cs4231_chip->perchip_info.record.active || (cs4231_chip->perchip_info.record.pause))
    return;

  cs4231_ready(drv);

  if (cs4231_chip->status & CS_STATUS_NEED_INIT)
    {
      cs4231_chip->pioregs->iar = IAR_AUTOCAL_BEGIN | 0x08;
      cs4231_chip->pioregs->idr = DEFAULT_DATA_FMAT;
      cs4231_chip->pioregs->iar = IAR_AUTOCAL_BEGIN | 0x1c;
      cs4231_chip->pioregs->idr = DEFAULT_DATA_FMAT;

      CHIP_BUG

      cs4231_chip->status &= ~CS_STATUS_NEED_INIT;
    }

  if (!cs4231_chip->perchip_info.record.pause)
    {
      /* init dma foo here */
      cs4231_chip->dmaregs.dmacsr &= ~CS_XINT_CAPT;
      cs4231_chip->dmaregs.dmacsr &= ~CS_CPAUSE;
      cs4231_recintr(drv);
      cs4231_chip->dmaregs.dmacsr |= CS_CAPT_SETUP;
      cs4231_chip->pioregs->iar = 0x9;
      cs4231_chip->pioregs->idr |= CEN_ENABLE;
    }
  cs4231_chip->perchip_info.record.active = 1;
}

static void cs4231_stop_input(struct sparcaudio_driver *drv)
{
  struct cs4231_chip *cs4231_chip = (struct cs4231_chip *)drv->private;

  cs4231_chip->perchip_info.record.active = 0;
  cs4231_chip->dmaregs.dmacsr |= (CS_CPAUSE);

  cs4231_pollinput(drv);

  /* need adjust the end pointer, process the input, and clean up the dma */

  cs4231_chip->pioregs->iar = 0x09;
  cs4231_chip->pioregs->idr &= CEN_DISABLE;
}

static void cs4231_audio_getdev(struct sparcaudio_driver *drv,
                                 audio_device_t * audinfo)
{
        strncpy(audinfo->name, "cs4231", sizeof(audinfo->name) - 1);
        strncpy(audinfo->version, "x", sizeof(audinfo->version) - 1);
        strncpy(audinfo->config, "audio", sizeof(audinfo->config) - 1);
}


/* The ioctl handler should be expected to identify itself and handle loopback
   mode */
/* There will also be a handler for getinfo and setinfo */

static struct sparcaudio_operations cs4231_ops = {
	cs4231_open,
	cs4231_release,
	NULL,			/* cs4231_ioctl */
	cs4231_start_output,
	cs4231_stop_output,
	cs4231_start_input,
        cs4231_stop_input,
	cs4231_audio_getdev,
};

/* Attach to an cs4231 chip given its PROM node. */
static inline int
cs4231_attach(struct sparcaudio_driver *drv, struct linux_sbus_device *sdev)
{
  struct cs4231_chip *cs4231_chip;
  int err;
  struct linux_sbus *sbus = sdev->my_bus;
#ifdef __sparc_v9__
  struct devid_cookie dcookie;
#endif

  /* Allocate our private information structure. */
  drv->private = kmalloc(sizeof(struct cs4231_chip), GFP_KERNEL);
  if (!drv->private)
    return -ENOMEM;

  /* Point at the information structure and initialize it. */
  drv->ops = &cs4231_ops;
  cs4231_chip = (struct cs4231_chip *)drv->private;
#if 0
  cs4231_chip->input_ptr = NULL;
  cs4231_chip->input_count = 0;
  cs4231_chip->output_ptr = NULL;
  cs4231_chip->output_count = 0;
#endif

  /* Map the registers into memory. */
  prom_apply_sbus_ranges(sbus, sdev->reg_addrs, 1, sdev);
  cs4231_chip->regs_size = sdev->reg_addrs[0].reg_size;
  cs4231_chip->pioregs = sparc_alloc_io(sdev->reg_addrs[0].phys_addr, 0, 
  					sdev->reg_addrs[0].reg_size,
				      "cs4231", sdev->reg_addrs[0].which_io, 0);
  if (!cs4231_chip->pioregs) {
    printk(KERN_ERR "cs4231: could not allocate registers\n");
    kfree(drv->private);
    return -EIO;
  }

  /* Reset the audio chip. */
  cs4231_reset(drv);

  /* Attach the interrupt handler to the audio interrupt. */
  cs4231_chip->irq = sdev->irqs[0].pri;

#ifndef __sparc_v9__
  request_irq(cs4231_chip->irq, cs4231_interrupt, SA_SHIRQ, "cs4231", drv);
#else
  dcookie.real_dev_id = s;
  dcookie.imap = dcookie.iclr = 0;
  dcookie.pil = -1;
  dcookie.bus_cookie = sdev->my_bus;
  request_irq (cs4231_chip->irq, cs4231_interrupt, (SA_SHIRQ | SA_SBUS | SA_DCOOKIE), "cs4231", drv);
  cs4231_chip->irq = dcookie.ret_ino;
#endif
  enable_irq(cs4231_chip->irq);

  /* Register ourselves with the midlevel audio driver. */
  err = register_sparcaudio_driver(drv);
  if (err < 0) {
    printk(KERN_ERR "cs4231: unable to register\n");
    disable_irq(cs4231_chip->irq);
    free_irq(cs4231_chip->irq, drv);
    sparc_free_io(cs4231_chip->pioregs, cs4231_chip->regs_size);
    kfree(drv->private);
    return -EIO;
  }

  /* Announce the hardware to the user. */
  printk(KERN_INFO "cs4231 at 0x%lx irq %d\n",
	 (unsigned long)cs4231_chip->pioregs, cs4231_chip->irq);
  
  /* Success! */
  return 0;
}

/* Probe for the cs4231 chip and then attach the driver. */
#ifdef MODULE
int init_module(void)
#else
__initfunc(int cs4231_init(void))
#endif
{
  struct linux_sbus *bus;
  struct linux_sbus_device *sdev;
  
  /* Probe each SBUS for cs4231 chips. */
  for_all_sbusdev(sdev,bus) {
    if (!strcmp(sdev->prom_name, "SUNW,CS4231")) {
      /* Don't go over the max number of drivers. */
      if (num_drivers >= MAX_DRIVERS)
	continue;
      
      if (cs4231_attach(&drivers[num_drivers], sdev) == 0)
	num_drivers++;
    }
  }
  
  /* Only return success if we found some cs4231 chips. */
  return (num_drivers > 0) ? 0 : -EIO;
}

#ifdef MODULE
/* Detach from an cs4231 chip given the device structure. */
static void cs4231_detach(struct sparcaudio_driver *drv)
{
        struct cs4231_chip *info = (struct cs4231_chip *)drv->private;

        unregister_sparcaudio_driver(drv);
        disable_irq(info->irq);
        free_irq(info->irq, drv);
        sparc_free_io(info->pioregs, info->regs_size);
        kfree(drv->private);
}

void cleanup_module(void)
{
        register int i;

        for (i = 0; i < num_drivers; i++) {
                cs4231_detach(&drivers[i]);
                num_drivers--;
        }
}
#endif

