/*
 * drivers/sbus/audio/cs4231.c
 *
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@noc.rutgers.edu)
 * Copyright (C) 1996 Derrick J Brashear (shadow@andrew.cmu.edu)
 *
 * This is the lowlevel driver for the CS4231 audio chip found on some
 * sun4m machines.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/auxio.h>
#include <asm/delay.h>
#include "audio.h"
#include "cs4231.h"

/* Stolen for now from compat.h */
#ifndef MAX                             /* Usually found in <sys/param.h>. */
#define MAX(_a,_b)      ((_a)<(_b)?(_b):(_a))
#endif
#ifndef MIN                             /* Usually found in <sys/param.h>. */
#define MIN(_a,_b)      ((_a)<(_b)?(_a):(_b))
#endif

static int cs4231_node, cs4231_irq, cs4231_is_revision_a, cs4231_ints_on = 0;
static unsigned int cs4231_monitor_gain_value; 
cs4231_regs_size

static int cs4231_output_muted_value;

static struct cs4231_stream_info cs4231_input;
static struct cs4231_stream_info cs4231_output;

static int cs4231_busy = 0, cs4231_need_init = 0;

static volatile struct cs4231_chip *cs4231_chip = NULL;

static __u8 * ptr;
static size_t count;

#define CHIP_BUG udelay(100); cs4231_ready(); udelay(1000);

static void cs4231_ready(void) 
{
  register unsigned int x = 0;

  cs4231_chip->pioregs.iar = (u_char)IAR_AUTOCAL_END;
  while (cs4231_chip->pioregs.iar == IAR_NOT_READY && x <= CS_TIMEOUT) {
    x++;
  }

  x = 0;

  cs4231_chip->pioregs.iar = 0x0b;

  while (cs4231_chip->pioregs.idr == AUTOCAL_IN_PROGRESS && x <= CS_TIMEOUT) {
    x++;
  }
}

/* Enable cs4231 interrupts atomically. */
static __inline__ void cs4231_enable_ints(void)
{
	register unsigned long flags;

	if (cs4231_ints_on)
		return;

	save_and_cli(flags);
	/* do init here
	amd7930_regs->cr = AMR_INIT;
	amd7930_regs->dr = AM_INIT_ACTIVE;
	*/
	restore_flags(flags);

	cs4231_ints_on = 1;
}

/* Disable cs4231 interrupts atomically. */
static __inline__ void cs4231_disable_ints(void)
{
	register unsigned long flags;

	if (!cs4231_ints_on)
		return;

	save_and_cli(flags);
/*
	amd7930_regs->cr = AMR_INIT;
	amd7930_regs->dr = AM_INIT_ACTIVE | AM_INIT_DISABLE_INTS;
*/
	restore_flags(flags);

	cs4231_ints_on = 0;
}  


/* Audio interrupt handler. */
static void cs4231_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	__u8 dummy;

        /* Clear the interrupt. */
        dummy = cs4231_chip->dmaregs.dmacsr;

        cs4231_chip->dmaregs.dmacsr = dummy;

}

static unsigned int cs4231_output_muted(unsigned int value)
{
  if (!value) {
    cs4231_chip->pioregs.iar = 0x7;
    cs4231_chip->pioregs.idr &= OUTCR_UNMUTE;
    cs4231_chip->pioregs.iar = 0x6;
    cs4231_chip->pioregs.idr &= OUTCR_UNMUTE;
    cs4231_output_muted_value = 0;
  } else {
    cs4231_chip->pioregs.iar = 0x7;
    cs4231_chip->pioregs.idr |= OUTCR_MUTE;
    cs4231_chip->pioregs.iar = 0x6;
    cs4231_chip->pioregs.idr |= OUTCR_MUTE;
    cs4231_output_muted_value = 1;
  }
  return (cs4231_output_muted_value);
}

static unsigned int cs4231_out_port(unsigned int value)
{
  unsigned int r = 0;

  /* You can have any combo you want. Just don't tell anyone. */

  cs4231_chip->pioregs.iar = 0x1a;
  cs4231_chip->pioregs.idr |= MONO_IOCR_MUTE;
  cs4231_chip->pioregs.iar = 0x0a;
  cs4231_chip->pioregs.idr |= PINCR_LINE_MUTE;
  cs4231_chip->pioregs.idr |= PINCR_HDPH_MUTE;

  if (value & AUDIO_SPEAKER) {
    cs4231_chip->pioregs.iar = 0x1a;
    cs4231_chip->pioregs.idr &= ~MONO_IOCR_MUTE;
    r |= AUDIO_SPEAKER;
  }

  if (value & AUDIO_HEADPHONE) {
   cs4231_chip->pioregs.iar = 0x0a;
   cs4231_chip->pioregs.idr &= ~PINCR_HDPH_MUTE;
   r |= AUDIO_HEADPHONE;
  }

  if (value & AUDIO_LINE_OUT) {
    cs4231_chip->pioregs.iar = 0x0a;
    cs4231_chip->pioregs.idr &= ~PINCR_LINE_MUTE;
    r |= AUDIO_LINE_OUT;
  }
  
  return (r);
}

static unsigned int cs4231_in_port(unsigned int value)
{
  unsigned int r = 0;

  /* The order of these seems to matter. Can't tell yet why. */

  if (value & AUDIO_INTERNAL_CD_IN) {
    cs4231_chip->pioregs.iar = 0x1;
    cs4231_chip->pioregs.idr = CDROM_ENABLE(cs4231_chip->pioregs.idr);
    cs4231_chip->pioregs.iar = 0x0;
    cs4231_chip->pioregs.idr = CDROM_ENABLE(cs4231_chip->pioregs.idr);
    r = AUDIO_INTERNAL_CD_IN;
  }
  if ((value & AUDIO_LINE_IN)) {
    cs4231_chip->pioregs.iar = 0x1;
    cs4231_chip->pioregs.idr = LINE_ENABLE(cs4231_chip->pioregs.idr);
    cs4231_chip->pioregs.iar = 0x0;
    cs4231_chip->pioregs.idr = LINE_ENABLE(cs4231_chip->pioregs.idr);
    r = AUDIO_LINE_IN;
  } else if (value & AUDIO_MICROPHONE) {
    cs4231_chip->pioregs.iar = 0x1;
    cs4231_chip->pioregs.idr = MIC_ENABLE(cs4231_chip->pioregs.idr);
    cs4231_chip->pioregs.iar = 0x0;
    cs4231_chip->pioregs.idr = MIC_ENABLE(cs4231_chip->pioregs.idr);
    r = AUDIO_MICROPHONE;
  }

  return (r);
}

static unsigned int cs4231_monitor_gain(unsigned int value)
{
  int a = 0;

  a = CS4231_MON_MAX_ATEN - (value * (CS4231_MON_MAX_ATEN + 1) /
                            (AUDIO_MAX_GAIN + 1));

  cs4231_chip->pioregs.iar = 0x0d;
  if (a >= CS4231_MON_MAX_ATEN) 
    cs4231_chip->pioregs.idr = LOOPB_OFF;
  else 
    cs4231_chip->pioregs.idr = ((a << 2) | LOOPB_ON);


  if (value == AUDIO_MAX_GAIN)
    return AUDIO_MAX_GAIN;

  return ((CS4231_MAX_DEV_ATEN - a) * (AUDIO_MAX_GAIN + 1) /
          (CS4231_MAX_DEV_ATEN + 1));
}

/* Set record gain */
static unsigned int cs4231_record_gain(unsigned int value, unsigned char balance)
{
  unsigned int tmp = 0, r, l, ra, la;
  unsigned char old_gain;


  r = l = value;

  if (balance < AUDIO_MID_BALANCE) {
    r = MAX(0, (int)(value -
                     ((AUDIO_MID_BALANCE - balance) << AUDIO_BALANCE_SHIFT)));
  } else if (balance > AUDIO_MID_BALANCE) {
    l = MAX(0, (int)(value -
                     ((balance - AUDIO_MID_BALANCE) << AUDIO_BALANCE_SHIFT)));
  }

  la = l * (CS4231_MAX_GAIN + 1) / (AUDIO_MAX_GAIN + 1);
  ra = r * (CS4231_MAX_GAIN + 1) / (AUDIO_MAX_GAIN + 1);
  
  cs4231_chip->pioregs.iar = 0x0;
  old_gain = cs4231_chip->pioregs.idr;
  cs4231_chip->pioregs.idr = RECGAIN_SET(old_gain, la);
  cs4231_chip->pioregs.iar = 0x1;
  old_gain = cs4231_chip->pioregs.idr;
  cs4231_chip->pioregs.idr = RECGAIN_SET(old_gain, ra);
  
  if (l == value) {
    (l == 0) ? (tmp = 0) : (tmp = ((la + 1) * AUDIO_MAX_GAIN) / (CS4231_MAX_GAIN + 1));
  } else if (r == value) {
    (r == 0) ? (tmp = 0) : (tmp = ((ra + 1) * AUDIO_MAX_GAIN) / (CS4231_MAX_GAIN + 1));
  }
  return (tmp);
}

/* Set play gain */
static unsigned int cs4231_play_gain(unsigned int value, unsigned char balance)
{
  unsigned int tmp = 0, r, l, ra, la;
  unsigned char old_gain;

  r = l = value;
  if (balance < AUDIO_MID_BALANCE) {
    r = MAX(0, (int)(value -
                     ((AUDIO_MID_BALANCE - balance) << AUDIO_BALANCE_SHIFT)));
  } else if (balance > AUDIO_MID_BALANCE) {
    l = MAX(0, (int)(value -
                     ((balance - AUDIO_MID_BALANCE) << AUDIO_BALANCE_SHIFT)));
  }

  if (l == 0) {
    la = CS4231_MAX_DEV_ATEN;
  } else {
    la = CS4231_MAX_ATEN -
      (l * (CS4231_MAX_ATEN + 1) / (AUDIO_MAX_GAIN + 1));
  }
  if (r == 0) {
    ra = CS4231_MAX_DEV_ATEN;
  } else {
    ra = CS4231_MAX_ATEN -
      (r * (CS4231_MAX_ATEN + 1) / (AUDIO_MAX_GAIN + 1));
  }
  
  cs4231_chip->pioregs.iar = 0x6;
  old_gain = cs4231_chip->pioregs.idr;
  cs4231_chip->pioregs.idr = GAIN_SET(old_gain, la);
  cs4231_chip->pioregs.iar = 0x7;
  old_gain = cs4231_chip->pioregs.idr;
  cs4231_chip->pioregs.idr = GAIN_SET(old_gain, ra);
  
  if ((value == 0) || (value == AUDIO_MAX_GAIN)) {
    tmp = value;
  } else {
    if (l == value) {
      tmp = ((CS4231_MAX_ATEN - la) *
                 (AUDIO_MAX_GAIN + 1) /
                 (CS4231_MAX_ATEN + 1));
    } else if (r == value) {
      tmp = ((CS4231_MAX_ATEN - ra) *
                 (AUDIO_MAX_GAIN + 1) /
                 (CS4231_MAX_ATEN + 1));
    }
  }
  return (tmp);
}

/* Reset the audio chip to a sane state. */
static void cs4231_reset(void)
{
        cs4231_chip->dmaregs.dmacsr = APC_RESET;
        cs4231_chip->dmaregs.dmacsr = 0x00;
        cs4231_chip->dmaregs.dmacsr |= APC_CODEC_PDN;

        udelay(20);

        cs4231_chip->dmaregs.dmacsr &= ~(APC_CODEC_PDN);
        cs4231_chip->pioregs.iar |= IAR_AUTOCAL_BEGIN;

        CHIP_BUG

        cs4231_chip->pioregs.iar = IAR_AUTOCAL_BEGIN | 0x0c;
        cs4231_chip->pioregs.idr = MISC_IR_MODE2;
        cs4231_chip->pioregs.iar = IAR_AUTOCAL_BEGIN | 0x08;
        cs4231_chip->pioregs.idr = DEFAULT_DATA_FMAT;

        CHIP_BUG

        cs4231_chip->pioregs.iar = IAR_AUTOCAL_BEGIN | 0x1c;
        cs4231_chip->pioregs.idr = DEFAULT_DATA_FMAT;

        CHIP_BUG

        cs4231_chip->pioregs.iar = 0x19;

        if (cs4231_chip->pioregs.idr & CS4231A)
                cs4231_is_revision_a = 1;
        else
                cs4231_is_revision_a = 0;

        cs4231_chip->pioregs.iar = IAR_AUTOCAL_BEGIN | 0x10;
        cs4231_chip->pioregs.idr = (u_char)OLB_ENABLE;

        cs4231_chip->pioregs.iar = IAR_AUTOCAL_BEGIN | 0x11;
        if (cs4231_is_revision_a)
                cs4231_chip->pioregs.idr = (HPF_ON | XTALE_ON);
        else
                cs4231_chip->pioregs.idr = (HPF_ON);

        cs4231_chip->pioregs.iar = IAR_AUTOCAL_BEGIN | 0x1a;
        cs4231_chip->pioregs.idr = 0x00;

        cs4231_output.gain = cs4231_play_gain(CS4231_DEFAULT_PLAYGAIN,
                                              AUDIO_MID_BALANCE);
        cs4231_input.gain = cs4231_record_gain(CS4231_DEFAULT_RECGAIN,
                                               AUDIO_MID_BALANCE);

        cs4231_output.port = cs4231_out_port(AUDIO_SPEAKER);
        cs4231_input.port = cs4231_in_port(AUDIO_MICROPHONE);

        cs4231_monitor_gain_value = cs4231_monitor_gain(LOOPB_OFF);

        cs4231_chip->pioregs.iar = (u_char)IAR_AUTOCAL_END;

        cs4231_ready();

        cs4231_chip->pioregs.iar = IAR_AUTOCAL_BEGIN | 0x09;
        cs4231_chip->pioregs.idr &= ACAL_DISABLE;
        cs4231_chip->pioregs.iar = (u_char)IAR_AUTOCAL_END;

        cs4231_ready();

        cs4231_output_muted_value = cs4231_output_muted(0x0);
}

static int cs4231_open(struct inode * inode, struct file * file, struct sparcaudio_driver *drv)
{
	int level;

        /* Set the default audio parameters. */

        cs4231_output.sample_rate = CS4231_RATE;
        cs4231_output.channels = CS4231_CHANNELS;
        cs4231_output.precision = CS4231_PRECISION;
        cs4231_output.encoding = AUDIO_ENCODING_ULAW;

        cs4231_input.sample_rate = CS4231_RATE;
        cs4231_input.channels = CS4231_CHANNELS;
        cs4231_input.precision = CS4231_PRECISION;
        cs4231_input.encoding = AUDIO_ENCODING_ULAW;

        cs4231_ready();

        cs4231_need_init = 1;
#if 1
        /* Arguably this should only happen once. I need to play around 
         * on a Solaris box and see what happens
         */
        cs4231_chip->pioregs.iar = IAR_AUTOCAL_BEGIN | 0x08;
        cs4231_chip->pioregs.idr = DEFAULT_DATA_FMAT;
        cs4231_chip->pioregs.iar = IAR_AUTOCAL_BEGIN | 0x1c;
        cs4231_chip->pioregs.idr = DEFAULT_DATA_FMAT;

#endif

        CHIP_BUG

	MOD_INC_USE_COUNT;

	return 0;
}

static void cs4231_release(struct inode * inode, struct file * file, struct sparcaudio_driver *drv)
{
	cs4231_disable_ints();
	MOD_DEC_USE_COUNT;
}

static void cs4231_start_output(struct sparcaudio_driver *drv, __u8 * buffer, size_t the_count)
{
	count = the_count;
	ptr = buffer;
	cs4231_enable_ints();
}

static void cs4231_stop_output(struct sparcaudio_driver *drv)
{
	cs4231_disable_ints();
	ptr = NULL;
	count = 0;
}


static struct sparcaudio_operations cs4231_ops = {
	cs4231_open,
	cs4231_release,
	NULL,			/* cs4231_ioctl */
	cs4231_start_output,
	cs4231_stop_output,
};

static struct sparcaudio_driver cs4231_drv = {
	"cs4231",
	&cs4231_ops,
};

/* Probe for the cs4231 chip and then attach the driver. */
#ifdef MODULE
int init_module(void)
#else
__initfunc(int cs4231_init(void))
#endif
{
	struct linux_prom_registers regs[1];
	struct linux_prom_irqs irq;
	int err;

#ifdef MODULE
	register_symtab(0);
#endif

        /* Find the PROM CS4231 node. */
        cs4231_node = prom_getchild(prom_root_node);
        cs4231_node = prom_searchsiblings(cs4231_node,"iommu");
        cs4231_node = prom_getchild(cs4231_node);
        cs4231_node = prom_searchsiblings(cs4231_node,"sbus");
        cs4231_node = prom_getchild(cs4231_node);
        cs4231_node = prom_searchsiblings(cs4231_node,"SUNW,CS4231");

	if (!cs4231_node)
		return -EIO;

	/* XXX Add for_each_sbus() search as well for LX and friends. */
	/* XXX Copy out for prom_apply_sbus_ranges. */

	/* Map the registers into memory. */
        prom_getproperty(cs4231_node, "reg", (char *)regs, sizeof(regs));
	cs4231_regs_size = regs[0].reg_size;
	cs4231_regs = sparc_alloc_io(regs[0].phys_addr, 0, regs[0].reg_size,
				      "cs4231", regs[0].which_io, 0);
	if (!cs4231_regs) {
		printk(KERN_ERR "cs4231: could not allocate registers\n");
		return -EIO;
	}

	/* Disable cs4231 interrupt generation. */
	cs4231_disable_ints();

        /* Reset the audio chip. */
        cs4231_reset();

	/* Attach the interrupt handler to the audio interrupt. */
        prom_getproperty(cs4231_node, "intr", (char *)&irq, sizeof(irq));
        cs4231_irq = irq.pri;
        request_irq(cs4231_irq, cs4231_interrupt, SA_INTERRUPT, "cs4231", NULL);
        enable_irq(cs4231_irq);

	/* Register ourselves with the midlevel audio driver. */
	err = register_sparcaudio_driver(&cs4231_drv);
	if (err < 0) {
		/* XXX We should do something. Complain for now. */
		printk(KERN_ERR "cs4231: really screwed now\n");
		return -EIO;
	}

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	unregister_sparcaudio_driver(&cs4231_drv);
	cs4231_disable_ints();
	disable_irq(cs4231_irq);
	free_irq(cs4231_irq, NULL);
	sparc_free_io(cs4231_regs, cs4231_regs_size);
}
#endif
