#undef FKS_LOGGING
#undef FKS_TEST

/*
 * tabs should be 4 spaces, in vi(m): set tabstop=4
 *
 * TODO: 	consistency speed calculations!!
 *			cleanup!
 * ????:	Did I break MIDI support?
 *
 * History:
 *
 * Rolf Fokkens	(Dec 20 1998):	ES188x recording level support on a per
 * fokkensr@vertis.nl			input basis.
 *				(Dec 24 1998):	Recognition of ES1788, ES1887, ES1888,
 *								ES1868, ES1869 and ES1878. Could be used for
 *								specific handling in the future. All except
 *								ES1887 and ES1888 and ES688 are handled like
 *								ES1688.
 *				(Dec 27 1998):	RECLEV for all (?) ES1688+ chips. ES188x now
 *								have the "Dec 20" support + RECLEV
 *				(Jan  2 1999):	Preparation for Full Duplex. This means
 *								Audio 2 is now used for playback when dma16
 *								is specified. The next step would be to use
 *								Audio 1 and Audio 2 at the same time.
 *				(Jan  9 1999):	Put all ESS stuff into sb_ess.[ch], this
 *								includes both the ESS stuff that has been in
 *								sb_*[ch] before I touched it and the ESS support
 *								I added later
 *				(Jan 23 1999):	Full Duplex seems to work. I wrote a small
 *								test proggy which works OK. Haven't found
 *								any applications to test it though. So why did
 *								I bother to create it anyway?? :) Just for
 *								fun.
 *				(May  2 1999):	I tried to be too smart by "introducing"
 *								ess_calc_best_speed (). The idea was that two
 *								dividers could be used to setup a samplerate,
 *								ess_calc_best_speed () would choose the best.
 *								This works for playback, but results in
 *								recording problems for high samplerates. I
 *								fixed this by removing ess_calc_best_speed ()
 *								and just doing what the documentation says. 
 *Javier Achirica(May 15 1999): Major cleanup, MPU IRQ sharing, hardware
 *								volume support, PNP chip configuration,
 *								full duplex in most cards, sample rate fine
 *								tuning.
 *
 * This files contains ESS chip specifics. It's based on the existing ESS
 * handling as it resided in sb_common.c, sb_mixer.c and sb_audio.c. This
 * file adds features like:
 * - Chip Identification (as shown in /proc/sound)
 * - RECLEV support for ES1688 and later
 * - 6 bits playback level support chips later than ES1688
 * - Recording level support on a per-device basis for ES1887
 * - Full-Duplex for ES1887 (under development)
 *
 * Full duplex is enabled by specifying dma16. While the normal dma must
 * be one of 0, 1 or 3, dma16 can be one of 0, 1, 3 or 5. DMA 5 is a 16 bit
 * DMA channel, while the others are 8 bit..
 *
 * ESS detection isn't full proof (yet). If it fails an additional module
 * parameter esstype can be specified to be one of the following:
 * -1, 0, 688, 1688, 1868, 1869, 1788, 1887, 1888
 * -1 means: mimic 2.0 behaviour, 
 *  0 means: auto detect.
 *   others: explicitly specify chip
 * -1 is default, cause auto detect still doesn't work.
 */

/*
 * About the documentation
 *
 * I don't know if the chips all are OK, but the documentation is buggy. 'cause
 * I don't have all the cips myself, there's a lot I cannot verify. I'll try to
 * keep track of my latest insights about his here. If you have additional info,
 * please enlighten me (fokkensr@vertis.nl)!
 *
 * I had the impression that ES1688 also has 6 bit master volume control. The
 * documentation about ES1888 (rev C, october '95) claims that ES1888 has
 * the following features ES1688 doesn't have:
 * - 6 bit master volume
 * - Full Duplex
 * So ES1688 apparently doesn't have 6 bit master volume control, but the
 * ES1688 does have RECLEV control. Makes me wonder: does ES688 have it too?
 * Without RECLEV ES688 won't be much fun I guess.
 *
 * From the ES1888 (rev C, october '95) documentation I got the impression
 * that registers 0x68 to 0x6e don't exist which means: no recording volume
 * controls. To my surprise the ES888 documentation (1/14/96) claims that
 * ES888 does have these record mixer registers, but that ES1888 doesn't have
 * 0x69 and 0x6b. So the rest should be there.
 *
 * I'm trying to get ES1887 Full Duplex. Audio 2 is playback only, while Audio 2
 * is both record and playback. I think I should use Audio 2 for all playback.
 *
 * The documentation is an adventure: it's close but not fully accurate. I
 * found out that after a reset some registers are *NOT* reset, though the
 * docs say the would be. Interresting ones are 0x7f, 0x7d and 0x7a. They are
 * related to the Audio 2 channel. I also was suprised about the consequenses
 * of writing 0x00 to 0x7f (which should be done by reset): The ES1887 moves
 * into ES1888 mode. This means that it claims IRQ 11, which happens to be my
 * ISDN adapter. Needless to say it no longer worked. I now understand why
 * after rebooting 0x7f already was 0x05, the value of my choise: the BIOS
 * did it.
 *
 * Oh, and this is another trap: in ES1887 docs mixer register 0x70 is decribed
 * as if it's exactly the same as register 0xa1. This is *NOT* true. The
 * description of 0x70 in ES1869 docs is accurate however.
 * Well, the assumption about ES1869 was wrong: register 0x70 is very much
 * like register 0xa1, except that bit 7 is allways 1, whatever you want
 * it to be.
 *
 * When using audio 2 mixer register 0x72 seems te be meaningless. Only 0xa2
 * has effect.
 *
 * Software reset not being able to reset all registers is great! Especially
 * the fact that register 0x78 isn't reset is great when you wanna change back
 * to single dma operation (simplex): audio 2 is still operation, and uses the
 * same dma as audio 1: your ess changes into a funny echo machine.
 *
 * Received the new that ES1688 is detected as a ES1788. Did some thinking:
 * the ES1887 detection scheme suggests in step 2 to try if bit 3 of register
 * 0x64 can be changed. This is inaccurate, first I inverted the * check: "If
 * can be modified, it's a 1688", which lead to a correct detection
 * of my ES1887. It resulted however in bad detection of 1688 (reported by mail)
 * and 1868 (if no PnP detection first): they result in a 1788 being detected.
 * I don't have docs on 1688, but I do have docs on 1868: The documentation is
 * probably inaccurate in the fact that I should check bit 2, not bit 3. This
 * is what I do now.
 */

/*
 * About recognition of ESS chips
 *
 * The distinction of ES688, ES1688, ES1788, ES1887 and ES1888 is described in
 * a (preliminary ??) datasheet on ES1887. It's aim is to identify ES1887, but
 * during detection the text claims that "this chip may be ..." when a step
 * fails. This scheme is used to distinct between the above chips.
 * It appears however that some PnP chips like ES1868 are recognized as ES1788
 * by the ES1887 detection scheme. These PnP chips can be detected in another
 * way however: ES1868, ES1869 and ES1878 can be recognized (full proof I think)
 * by repeatedly reading mixer register 0x40. This is done by ess_identify in
 * sb_common.c.
 * This results in the following detection steps:
 * - distinct between ES688 and ES1688+ (as always done in this driver)
 *   if ES688 we're ready
 * - try to detect ES1868, ES1869 or ES1878
 *   if successful we're ready
 * - try to detect ES1888, ES1887 or ES1788
 *   if successful we're ready
 * - Dunno. Must be 1688. Will do in general
 *
 * About RECLEV support:
 *
 * The existing ES1688 support didn't take care of the ES1688+ recording
 * levels very well. Whenever a device was selected (recmask) for recording
 * it's recording level was loud, and it couldn't be changed. The fact that
 * internal register 0xb4 could take care of RECLEV, didn't work meaning until
 * it's value was restored every time the chip was reset; this reset the
 * value of 0xb4 too. I guess that's what 4front also had (have?) trouble with.
 *
 * About ES1887 support:
 *
 * The ES1887 has separate registers to control the recording levels, for all
 * inputs. The ES1887 specific software makes these levels the same as their
 * corresponding playback levels, unless recmask says they aren't recorded. In
 * the latter case the recording volumes are 0.
 * Now recording levels of inputs can be controlled, by changing the playback
 * levels. Futhermore several devices can be recorded together (which is not
 * possible with the ES1688.
 * Besides the separate recording level control for each input, the common
 * recordig level can also be controlled by RECLEV as described above.
 *
 * Not only ES1887 have this recording mixer. I know the following from the
 * documentation:
 * ES688	no
 * ES1688	no
 * ES1868	no
 * ES1869	yes
 * ES1878	no
 * ES1879	yes
 * ES1888	no/yes	Contradicting documentation; most recent: yes
 * ES1946	yes		This is a PCI chip; not handled by this driver
 */

#include <linux/delay.h>

#include "sound_config.h"
#include "sb_mixer.h"
#include "sb.h"

#include "sb_ess.h"

#define ESSTYPE_LIKE20	-1		/* Mimic 2.0 behaviour					*/
#define ESSTYPE_DETECT	0		/* Mimic 2.0 behaviour					*/

int esstype = ESSTYPE_LIKE20; /* module parameter in sb_card.c */

#define SUBMDL_ES688	0x00	/* Subtype ES688 for specific handling */
#define SUBMDL_ES1688	0x08	/* Subtype ES1688 for specific handling */
#define SUBMDL_ES1788	0x10	/* Subtype ES1788 for specific handling */
#define SUBMDL_ES1868	0x11	/* Subtype ES1868 for specific handling */
#define SUBMDL_ES1869	0x12	/* Subtype ES1869 for specific handling */
#define SUBMDL_ES1878	0x13	/* Subtype ES1878 for specific handling */
#define SUBMDL_ES1879	0x14	/* Subtype ES1879 for specific handling */
#define SUBMDL_ES1887	0x15	/* Subtype ES1887 for specific handling */
#define SUBMDL_ES1888	0x16	/* Subtype ES1888 for specific handling */

	/* Recording mixer, stereo full duplex */
#define ESSCAP_NEW		0x00000100
	/* ISA PnP configuration */
#define ESSCAP_PNP		0x00000200
	/* Full duplex, 6-bit volume, hardware volume controls */
#define ESSCAP_ES18		0x00000400
	/* New interrupt handling system (ESS 1887) */
#define ESSCAP_IRQ		0x00000800

#define ESSFMT_16		0x00000001
#define ESSFMT_SIGNED	0x00000004

#ifdef FKS_LOGGING
static void ess_show_mixerregs (sb_devc *devc);
#endif
static int ess_read (sb_devc * devc, unsigned char reg);
static int ess_write (sb_devc * devc, unsigned char reg, unsigned char data);
static void ess_chgmixer
	(sb_devc * devc, unsigned int reg, unsigned int mask, unsigned int val);

/****************************************************************************
 *																			*
 *									ESS audio								*
 *																			*
 ****************************************************************************/

static void ess_change
	(sb_devc *devc, unsigned int reg, unsigned int mask, unsigned int val)
{
	int value;

	value = ess_read (devc, reg);
	value = (value & ~mask) | (val & mask);
	ess_write (devc, reg, value);
}

static void ess_set_output_parms
	(int dev, unsigned long buf, int nr_bytes, int intrflag)
{
	sb_devc *devc = audio_devs[dev]->devc;

	if (devc->duplex) {
		devc->trg_buf_16 = buf;
		devc->trg_bytes_16 = nr_bytes;
		devc->trg_intrflag_16 = intrflag;
		devc->irq_mode_16 = IMODE_OUTPUT;
	} else {
		devc->trg_buf = buf;
		devc->trg_bytes = nr_bytes;
		devc->trg_intrflag = intrflag;
		devc->irq_mode = IMODE_OUTPUT;
	}
}

static void ess_set_input_parms
	(int dev, unsigned long buf, int count, int intrflag)
{
	sb_devc *devc = audio_devs[dev]->devc;

	devc->trg_buf = buf;
	devc->trg_bytes = count;
	devc->trg_intrflag = intrflag;
	devc->irq_mode = IMODE_INPUT;
}

static int ess_calc_div (int clock, int *speedp, int *diffp)
{
	int divider;
	int speed, diff;

	speed   = *speedp;
	divider = (clock + speed / 2) / speed;
	if (divider > 127) {
		divider = 127;
	}
	*speedp	= clock / divider;
	diff	= speed - *speedp;
	*diffp = diff < 0 ? -diff : diff;

	return 128 - divider;
}

/*
 * Depending on the audiochannel ESS devices can
 * have different clock settings. These are made consistent for duplex
 * however.
 * callers of ess_speed only do an audionum suggestion, which means
 * input suggests 1, output suggests 2. This suggestion is only true
 * however when doing duplex.
 */
static void ess_common_speed (sb_devc *devc, int *speedp, int *divp)
{
	int speed1 = *speedp, speed2 = *speedp;
	int div1, div2;
	int diff1, diff2;

	if (devc->caps & ESSCAP_NEW) {
		div1 = 0x000 | ess_calc_div (793800, &speed1, &diff1);
		div2 = 0x080 | ess_calc_div (768000, &speed2, &diff2);
	} else {
		if (*speedp > 22000) {
			div1 = 0x080 | ess_calc_div (795444, &speed1, &diff1);
			div2 = 0x180 | ess_calc_div (793800, &speed2, &diff2);
		} else {
			div1 = 0x000 | ess_calc_div (397722, &speed1, &diff1);
			div2 = 0x100 | ess_calc_div (396900, &speed2, &diff2);
		}
	}

	if (diff1 < diff2) {
		*divp   = div1;
		*speedp = speed1;
	} else {
		*divp   = div2;
		*speedp = speed2;
	}
}

static void ess_speed (sb_devc *devc, int audionum)
{
	int speed;
	int div, div2;

	ess_common_speed (devc, &(devc->speed), &div);

#ifdef FKS_REG_LOGGING
printk (KERN_INFO "FKS: ess_speed (%d) b speed = %d, div=%x\n", audionum, devc->speed, div);
#endif

	/* Set filter roll-off to 90% of speed/2 */
	speed = (devc->speed * 9) / 20;

	div2 = 256 - 7160000 / (speed * 82);

	if ((devc->caps & ESSCAP_NEW) && audionum != 1) {
		ess_setmixer (devc, 0x70, div);
		ess_setmixer (devc, 0x72, div2);
	} else {
		ess_change (devc, 0xba, 0x40, (div & 0x100) ? 0x40 : 0x00);
		ess_write (devc, 0xa1, div & 0xff);
		ess_write (devc, 0xa2, div2);
	}
}

static int ess_audio_prepare_for_input(int dev, int bsize, int bcount)
{
	sb_devc *devc = audio_devs[dev]->devc;

	ess_write (devc, 0xb8, 0x0e);	/* Auto init DMA mode */
	ess_change (devc, 0xa8, 0x0b, 3 - devc->channels);	/* Mono/stereo */

	ess_speed(devc, 1);

    ess_write (devc, 0xb7, (devc->bits & ESSFMT_SIGNED) ? 0x71 : 0x51);
    ess_write (devc, 0xb7, 0x90 | ((devc->bits & ESSFMT_SIGNED) ? 0x20 : 0) |
		((devc->bits & ESSFMT_16) ? 4 : 0) | ((devc->channels > 1) ? 8 : 0x40));

	devc->trigger_bits = 0;
	return 0;
}

static int ess_audio_prepare_for_output(int dev, int bsize, int bcount)
{
	sb_devc *devc = audio_devs[dev]->devc;

#ifdef FKS_REG_LOGGING
printk(KERN_INFO "ess_audio_prepare_for_output: dma_out=%d,dma_in=%d\n"
, audio_devs[dev]->dmap_out->dma, audio_devs[dev]->dmap_in->dma);
#endif

	if (devc->duplex) {
		ess_speed(devc, 2);

	    ess_chgmixer (devc, 0x7a, 0x07, ((devc->bits & ESSFMT_SIGNED) ? 4 : 0) |
			((devc->bits & ESSFMT_16) ? 1 : 0) | ((devc->channels > 1) ? 2 : 0));

		if (devc->caps & ESSCAP_NEW)
			ess_mixer_reload (devc, SOUND_MIXER_PCM);	/* There be sound! */
		else
			sb_dsp_command(devc, DSP_CMD_SPKON);	/* There be sound! */
	} else {
		ess_write (devc, 0xb8, 4);	/* Auto init DMA mode */
		ess_change (devc, 0xa8, 0x03, 3 - devc->channels);	/* Mono/stereo */

		ess_speed(devc, 1);

	    ess_write (devc, 0xb6, (devc->bits & ESSFMT_SIGNED) ? 0 : 0x80);
	    ess_write (devc, 0xb7, (devc->bits & ESSFMT_SIGNED) ? 0x71 : 0x51);
	    ess_write (devc, 0xb7, 0x90 | ((devc->bits & ESSFMT_SIGNED) ? 0x20 : 0) |
			((devc->bits & ESSFMT_16) ? 4 : 0) | ((devc->channels > 1) ? 8 : 0x40));

		sb_dsp_command(devc, DSP_CMD_SPKON);	/* There be sound! */
	}
	devc->trigger_bits = 0;
	return 0;
}

static void ess_audio_halt_xfer(int dev)
{
	sb_devc *devc = audio_devs[dev]->devc;

	sb_dsp_command (devc, DSP_CMD_SPKOFF);

	if (devc->caps & ESSCAP_NEW) {
		ess_setmixer (devc, 0x7c, 0);
	}

	ess_change (devc, 0xb8, 0x0f, 0x00);	/* Stop */

	if (devc->duplex) {			/* Audio 2 may still be operational! */
		ess_chgmixer (devc, 0x78, 0x03, 0x00);
	}
}

static void ess_audio_trigger(int dev, int bits)
{
	sb_devc *devc = audio_devs[dev]->devc;

	int bits_16 = bits & devc->irq_mode_16 & IMODE_OUTPUT;
	bits &= devc->irq_mode;

	if (!bits && !bits_16) {
		sb_dsp_command (devc, 0xd0);			/* Halt DMA */
		ess_chgmixer (devc, 0x78, 0x04, 0x00);	/* Halt DMA 2 */
	}

	if (bits) {
		short c = -devc->trg_bytes;

		ess_write (devc, 0xa4, (unsigned char)((unsigned short) c & 0xff));
		ess_write (devc, 0xa5, (unsigned char)((unsigned short) c >> 8));
		ess_change (devc, 0xb8, 0x0f, (devc->irq_mode==IMODE_INPUT)?0x0f:0x05);

		devc->intr_active = 1;
	}

	if (bits_16) {
		short c = -devc->trg_bytes_16;

		ess_setmixer (devc, 0x74, (unsigned char)((unsigned short) c & 0xff));
		ess_setmixer (devc, 0x76, (unsigned char)((unsigned short) c >> 8));
		ess_chgmixer (devc, 0x78, 0x03, 0x03);   /* Go */

		devc->intr_active_16 = 1;
	}

	devc->trigger_bits = bits | bits_16;
}

static int ess_audio_set_speed(int dev, int speed)
{
	sb_devc *devc = audio_devs[dev]->devc;
	int minspeed, maxspeed, dummydiv;

	if (speed > 0) {
		minspeed = (devc->caps & ESSCAP_NEW) ? 6047  : 3125;
		maxspeed = 48000;
		if (speed < minspeed) speed = minspeed;
		if (speed > maxspeed) speed = maxspeed;

		ess_common_speed (devc, &speed, &dummydiv);

		devc->speed = speed;
	}
	return devc->speed;
}

static unsigned int ess_audio_set_bits(int dev, unsigned int bits)
{
	sb_devc *devc = audio_devs[dev]->devc;

	switch (bits) {
		case 0:
			break;
		case AFMT_S16_LE:
			devc->bits = ESSFMT_16 | ESSFMT_SIGNED;
			break;
		case AFMT_U16_LE:
			devc->bits = ESSFMT_16;
			break;
		case AFMT_S8:
			devc->bits = ESSFMT_SIGNED;
			break;
		default:
			devc->bits = 0;
			break;
	}

	return devc->bits;
}

static short ess_audio_set_channels(int dev, short channels)
{
	sb_devc *devc = audio_devs[dev]->devc;

	if (devc->fullduplex && !(devc->caps & ESSCAP_NEW)) {
		devc->channels = 1;
	} else {
		if (channels == 1 || channels == 2) {
			devc->channels = channels;
		}
	}

	return devc->channels;
}

static struct audio_driver ess_audio_driver =   /* ESS ES688/1688/18xx */
{
	sb_audio_open,
	sb_audio_close,
	ess_set_output_parms,
	ess_set_input_parms,
	NULL,
	ess_audio_prepare_for_input,
	ess_audio_prepare_for_output,
	ess_audio_halt_xfer,
	NULL,		/* local_qlen */
	NULL,		/* copy_from_user */
	NULL,
	NULL,
	ess_audio_trigger,
	ess_audio_set_speed,
	ess_audio_set_bits,
	ess_audio_set_channels
};

/*
 * ess_audio_init must be called from sb_audio_init
 */
struct audio_driver *ess_audio_init
		(sb_devc *devc, int *audio_flags, int *format_mask)
{
	*audio_flags = DMA_AUTOMODE;
	*format_mask |= AFMT_S16_LE | AFMT_U16_LE | AFMT_S8;

	if (devc->duplex) {
		int tmp_dma;
		/*
		 * sb_audio_init thinks dma8 is for playback and
		 * dma16 is for record. Not now! So swap them.
		 */
		tmp_dma		= devc->dma16;
		devc->dma16	= devc->dma8;
		devc->dma8	= tmp_dma;

		*audio_flags |= DMA_DUPLEX;
	}

	return &ess_audio_driver;
}

/****************************************************************************
 *																			*
 *								ESS common									*
 *																			*
 ****************************************************************************/
static void ess_handle_channel (int dev, int irq_mode)
{
#ifdef FKS_REG_LOGGING
printk(KERN_INFO "FKS: ess_handle_channel %s irq_mode=%d\n", channel, irq_mode);
#endif
	switch (irq_mode) {
		case IMODE_OUTPUT:
			DMAbuf_outputintr (dev, 1);
			break;

		case IMODE_INPUT:
			DMAbuf_inputintr (dev);
			break;

		case IMODE_INIT:
			break;

		default:
			/* printk(KERN_WARN "ESS: Unexpected interrupt\n"); */
	}
}

/*
 * In the ESS 1888 model, how do we found out if the MPU interrupted ???
 */
void ess_intr (sb_devc *devc)
{
	int				status;
	unsigned char	src;

	if (devc->caps & ESSCAP_PNP) {
		outb (devc->pcibase + 7, 0);		/* Mask IRQs */
		src = inb (devc->pcibase + 6) & 0x0f;
	} else if (devc->caps & ESSCAP_IRQ) {
		src = ess_getmixer (devc, 0x7f) >> 4;
	} else {
		src = inb (DSP_STATUS) & 0x01;
		if (devc->duplex && (ess_getmixer (devc, 0x7a) & 0x80)) {
			src |= 0x02;
		}
		if ((devc->caps & ESSCAP_ES18) && (ess_getmixer (devc, 0x64) & 0x10)) {
			src |= 0x04;
		}
#if defined(CONFIG_MIDI) && defined(CONFIG_SOUND_MPU401)
		/*
		 * This should work if dev_conf wasn't local to mpu401.c
		 */
#if 0
		if ((int)devc->midi_irq_cookie >= 0 &&
			!(inb(dev_conf[(int)devc->midi_irq_cookie].base + 1) & 0x80)) {
			src |= 0x08;
		}
#endif
#endif
	}

#ifdef FKS_REG_LOGGING
printk(KERN_INFO "FKS: sbintr src=%x\n",(int)src);
#endif
	if (src & 0x01) {
		status = inb(DSP_DATA_AVAIL);	/* Acknowledge interrupt */
		if (devc->intr_active)
			ess_handle_channel (devc->dev, devc->irq_mode   );
	}

	if (src & 0x02) {
		ess_chgmixer (devc, 0x7a, 0x80, 0x00);	/* Acknowledge interrupt */
		if (devc->intr_active_16)
			ess_handle_channel (devc->dev, devc->irq_mode_16);
	}

	if (src & 0x04) {
		int left, right;

		ess_setmixer (devc, 0x66, 0x00);	/* Hardware volume IRQ ack */

		left = ess_getmixer (devc, 0x60);
		right = ess_getmixer (devc, 0x62);

		left = (left & 0x40) ? 0 : ((left * 100 + 31)/ 63);	/* Mute or scale */
		right = (right & 0x40) ? 0 : ((right * 100 + 31)/ 63);

		devc->levels[SOUND_MIXER_VOLUME] = left | (right << 8);
	}

#if defined(CONFIG_MIDI) && defined(CONFIG_SOUND_MPU401)
	if ((int)devc->midi_irq_cookie >= 0 && (src & 0x08)) {
		mpuintr (devc->irq, devc->midi_irq_cookie, NULL);
	}
#endif

	if (devc->caps & ESSCAP_PNP) {
		outb (devc->pcibase + 7, 0xff);		/* Unmask IRQs */
	}
}

static int ess_write (sb_devc * devc, unsigned char reg, unsigned char data)
{
#ifdef FKS_REG_LOGGING
printk(KERN_INFO "FKS: write reg %x: %x\n", reg, data);
#endif
	/* Write a byte to an extended mode register of ES1688 */

	if (!sb_dsp_command(devc, reg))
		return 0;

	return sb_dsp_command(devc, data);
}

static int ess_read (sb_devc * devc, unsigned char reg)
{
	/* Read a byte from an extended mode register of ES1688 */

	/* Read register command */
	if (!sb_dsp_command(devc, 0xc0)) return -1;

	if (!sb_dsp_command(devc, reg )) return -1;

	return sb_dsp_get_byte(devc);
}

int ess_dsp_reset(sb_devc * devc)
{
	int loopc, val;

#ifdef FKS_REG_LOGGING
printk(KERN_INFO "FKS: ess_dsp_reset 1\n");
ess_show_mixerregs (devc);
#endif

	DEB(printk("Entered ess_dsp_reset()\n"));

	outb(3, DSP_RESET); /* Reset FIFO too */

	udelay(10);
	outb(0, DSP_RESET);
	udelay(30);

	for (loopc = 0; loopc < 1000 && !(inb(DSP_DATA_AVAIL) & 0x80); loopc++);

	if (inb(DSP_READ) != 0xAA) {
		DDB(printk("sb: No response to RESET\n"));
		return 0;   /* Sorry */
	}

	sb_dsp_command(devc, 0xc6);			/* Enable extended mode */
	if (!(devc->caps & ESSCAP_PNP)) {
		ess_setmixer (devc, 0x40, 0x03);	/* Enable joystick and OPL3 */

		switch (devc->irq) {
			case 2:
			case 9:
				val = 1;
				break;
			case 5:
				val = 2;
				break;
			case 7:
				val = 3;
				break;
			case 10:
				val = 4;
				break;
			case 11:
				val = 5;
				break;
			default:
				val = 0;
		}						/* IRQ config */
		ess_write (devc, 0xb1, 0xf0 | ((val && val != 5) ? val - 1 : 0));

		if (devc->caps & ESSCAP_IRQ) {
			ess_setmixer (devc, 0x7f, 0x01 | (val << 1)); /* IRQ config */
		}

		switch ((devc->duplex) ? devc->dma16 : devc->dma8) {
			case 0:
				val = 0x54;
				break;
			case 1:
				val = 0x58;
				break;
			case 3:
				val = 0x5c;
				break;
			default:
				val = 0;
		}
		ess_write (devc, 0xb2, val); /* DMA1 config */

		if (devc->duplex) {
			switch (devc->dma8) {
				case 0:
					val = 0x04;
					break;
				case 1:
					val = 0x05;
					break;
				case 3:
					val = 0x06;
					break;
				case 5:
					val = 0x07;
					break;
				default:
					val = 0;
			}
			ess_write (devc, 0x7d, val); /* DMA2 config */
		}
	}
	ess_change (devc, 0xb1, 0xf0, 0x50);	/* Enable IRQ 1 */
	ess_change (devc, 0xb2, 0xf0, 0x50);	/* Enable DMA 1 */
	ess_write (devc, 0xb9, 2);			/* Demand mode (4 bytes/DMA request) */
	ess_setmixer (devc, 0x7a, 0x40);	/* Enable IRQ 2 */
			/* Auto-Initialize DMA mode + demand mode (8 bytes/request) */
	if (devc->caps & ESSCAP_PNP) {
		ess_setmixer (devc, 0x78, 0xd0);
		ess_setmixer (devc, 0x64, 0x82);		/* Enable HW volume interrupt */
	} else {
		ess_setmixer (devc, 0x78, (devc->dma8 > 4) ? 0xf0 : 0xd0);
		ess_setmixer (devc, 0x64, 0x42);		/* Enable HW volume interrupt */
	}

    if (devc->caps & ESSCAP_NEW) {
		ess_setmixer (devc, 0x71, 0x32); /* Change behaviour of register A1 */
		ess_setmixer (devc, 0x1c, 0x05); /* Recording source is mixer */
	} else {
		ess_change (devc, 0xb7, 0x80, 0x80); /* Enable DMA FIFO */
	}

	DEB(printk("sb_dsp_reset() OK\n"));

#ifdef FKS_LOGGING
printk(KERN_INFO "FKS: dsp_reset 2\n");
ess_show_mixerregs (devc);
#endif

	return 1;
}

#ifdef FKS_TEST

/*
 * FKS_test:
 *	for ES1887: 00, 18, non wr bits: 0001 1000
 *	for ES1868: 00, b8, non wr bits: 1011 1000
 *	for ES1888: 00, f8, non wr bits: 1111 1000
 *	for ES1688: 00, f8, non wr bits: 1111 1000
 *	+   ES968
 */

static void FKS_test (sb_devc * devc)
{
	int val1, val2;
	val1 = ess_getmixer (devc, 0x64);
	ess_setmixer (devc, 0x64, ~val1);
	val2 = ess_getmixer (devc, 0x64) ^ ~val1;
	ess_setmixer (devc, 0x64, val1);
	val1 ^= ess_getmixer (devc, 0x64);
printk (KERN_INFO "FKS: FKS_test %02x, %02x\n", (val1 & 0x0ff), (val2 & 0x0ff));
};
#endif

static unsigned int ess_identify (sb_devc * devc, int *control)
{
	unsigned int val;
	unsigned long flags;

	save_flags(flags);
	cli();
	outb(((unsigned char) (0x40 & 0xff)), MIXER_ADDR);

	udelay(20);
	val  = inb(MIXER_DATA) << 8;
	udelay(20);
	val |= inb(MIXER_DATA);
	udelay(20);
	*control  = inb(MIXER_DATA) << 8;
	udelay(20);
	*control |= inb(MIXER_DATA);
	udelay(20);
	restore_flags(flags);

	if (*control < 0 || *control > 0x3ff || check_region (*control, 8))
		*control = 0;

	return val;
}

/*
 * ESS technology describes a detection scheme in their docs. It involves
 * fiddling with the bits in certain mixer registers. ess_probe is supposed
 * to help.
 *
 * FKS: tracing shows ess_probe writes wrong value to 0x64. Bit 3 reads 1, but
 * should be written 0 only. Check this.
 */
static int ess_probe (sb_devc * devc, int reg, int xorval)
{
	int  val1, val2, val3;

	val1 = ess_getmixer (devc, reg);
	val2 = val1 ^ xorval;
	ess_setmixer (devc, reg, val2);
	val3 = ess_getmixer (devc, reg);
	ess_setmixer (devc, reg, val1);

	return (val2 == val3);
}

int ess_init(sb_devc * devc, struct address_info *hw_config)
{
	int ess_major = 0, ess_minor = 0;
	int i;
	static char name[100], modelname[10];

	/*
	 * Try to detect ESS chips.
	 */
	devc->pcibase = 0;

	sb_dsp_command(devc, 0xe7); /* Return identification */

	for (i = 1000; i; i--) {
		if (inb(DSP_DATA_AVAIL) & 0x80) {
			if (ess_major == 0) {
				ess_major = inb(DSP_READ);
			} else {
				ess_minor = inb(DSP_READ);
				break;
			}
		}
	}

	if (ess_major == 0) return 0;

	if (ess_major == 0x48 && (ess_minor & 0xf0) == 0x80) {
		sprintf(name, "ESS ES488 AudioDrive (rev %d)",
			ess_minor & 0x0f);
		hw_config->name = name;
		devc->model = MDL_SBPRO;
		return 1;
	}

	/*
	 * This the detection heuristic of ESS technology, though somewhat
	 * changed to actually make it work.
	 * This results in the following detection steps:
	 * - distinct between ES688 and ES1688+ (as always done in this driver)
	 *   if ES688 we're ready
	 * - try to detect ES1868, ES1869 or ES1878 (ess_identify)
	 *   if successful we're ready
	 * - try to detect ES1888, ES1887 or ES1788 (aim: detect ES1887)
	 *   if successful we're ready
	 * - Dunno. Must be 1688. Will do in general
	 *
	 * This is the most BETA part of the software: Will the detection
	 * always work?
	 */
	devc->model = MDL_ESS;
	devc->submodel = ess_minor & 0x0f;

	if (ess_major == 0x68 && (ess_minor & 0xf0) == 0x80) {
		char *chip = NULL;
		int submodel = -1;

		switch (esstype) {
		case ESSTYPE_DETECT:
		case ESSTYPE_LIKE20:
			break;
		case 688:
			submodel = SUBMDL_ES688;
			break;
		case 1688:
			submodel = SUBMDL_ES1688;
			break;
		case 1868:
			submodel = SUBMDL_ES1868;
			break;
		case 1869:
			submodel = SUBMDL_ES1869;
			break;
		case 1788:
			submodel = SUBMDL_ES1788;
			break;
		case 1878:
			submodel = SUBMDL_ES1878;
			break;
		case 1879:
			submodel = SUBMDL_ES1879;
			break;
		case 1887:
			submodel = SUBMDL_ES1887;
			break;
		case 1888:
			submodel = SUBMDL_ES1888;
			break;
		default:
			printk (KERN_ERR "Invalid esstype=%d specified\n", esstype);
			return 0;
		};
		if (submodel != -1) {
			devc->submodel = submodel;
			sprintf (modelname, "ES%d", esstype);
			chip = modelname;
		};
		if (chip == NULL && (ess_minor & 0x0f) < 8) {
			chip = "ES688";
		};
#ifdef FKS_TEST
FKS_test (devc);
#endif
		/*
		 * If Nothing detected yet, and we want 2.0 behaviour...
		 * Then let's assume it's ES1688.
		 */
		if (chip == NULL && esstype == ESSTYPE_LIKE20) {
			chip = "ES1688";
		};

		if (chip == NULL) {
			int type;

			type = ess_identify (devc, &devc->pcibase);

			switch (type) {
			case 0x1868:
				chip = "ES1868";
				devc->submodel = SUBMDL_ES1868;
				break;
			case 0x1869:
				chip = "ES1869";
				devc->submodel = SUBMDL_ES1869;
				break;
			case 0x1878:
				chip = "ES1878";
				devc->submodel = SUBMDL_ES1878;
				break;
			case 0x1879:
				chip = "ES1879";
				devc->submodel = SUBMDL_ES1879;
				break;
			default:
				if ((type & 0x00ff) != ((type >> 8) & 0x00ff)) {
					printk ("ess_init: Unrecognized %04x\n", type);
				}
			};
		};
#if 0
		/*
		 * this one failed:
		 * the probing of bit 4 is another thought: from ES1788 and up, all
		 * chips seem to have hardware volume control. Bit 4 is readonly to
		 * check if a hardware volume interrupt has fired.
		 * Cause ES688/ES1688 don't have this feature, bit 4 might be writeable
		 * for these chips.
		 */
		if (chip == NULL && !ess_probe(devc, 0x64, (1 << 4))) {
#endif
		/*
		 * the probing of bit 2 is my idea. The ES1887 docs want me to probe
		 * bit 3. This results in ES1688 being detected as ES1788.
		 * Bit 2 is for "Enable HWV IRQE", but as ES(1)688 chips don't have
		 * HardWare Volume, I think they don't have this IRQE.
		 */
		if (chip == NULL && ess_probe(devc, 0x64, (1 << 2))) {
			if (ess_probe (devc, 0x70, 0x7f)) {
				if (ess_probe (devc, 0x64, (1 << 5))) {
					chip = "ES1887";
					devc->submodel = SUBMDL_ES1887;
				} else {
					chip = "ES1888";
					devc->submodel = SUBMDL_ES1888;
				}
			} else {
				chip = "ES1788";
				devc->submodel = SUBMDL_ES1788;
			}
		};
		if (chip == NULL) {
			chip = "ES1688";
		};

	    printk ( KERN_INFO "ESS chip %s %s%s\n"
               , chip
               , ( esstype == ESSTYPE_DETECT || esstype == ESSTYPE_LIKE20
                 ? "detected"
                 : "specified"
                 )
               , ( esstype == ESSTYPE_LIKE20
                 ? " (kernel 2.0 compatible)"
                 : ""
                 )
               );

		sprintf(name,"ESS %s AudioDrive (rev %d)", chip, ess_minor & 0x0f);
	} else {
		strcpy(name, "Jazz16");
	}

	switch (devc->submodel) {
	case SUBMDL_ES1869:
	case SUBMDL_ES1879:
		devc->caps |= ESSCAP_NEW;
	case SUBMDL_ES1868:
	case SUBMDL_ES1878:
		devc->caps |= ESSCAP_PNP | ESSCAP_ES18;
		break;
	case SUBMDL_ES1887:
		devc->caps |= ESSCAP_IRQ;
	case SUBMDL_ES1888:
		devc->caps |= ESSCAP_NEW | ESSCAP_ES18;
	}
    if (devc->caps & ESSCAP_PNP) {
		if (!devc->pcibase) {
			printk (KERN_ERR "ESS PnP chip without PnP registers. Ignored\n");
			return 0;
		}
		request_region (devc->pcibase, 8, "ESS18xx ctrl");

		outb (0x07, devc->pcibase);		/* Selects logical device #1 */
		outb (0x01, devc->pcibase + 1);
		outb (0x28, devc->pcibase);
		i = inb (devc->pcibase + 1) & 0x0f;
		outb (0x28, devc->pcibase);		/* Sets HW volume IRQ */
		outb (devc->irq << 4 | i, devc->pcibase + 1);
		outb (0x70, devc->pcibase);		/* Sets IRQ 1 */
		outb (devc->irq, devc->pcibase + 1);
		outb (0x72, devc->pcibase);		/* Sets IRQ 2 */
		outb (devc->irq, devc->pcibase + 1);
		outb (0x74, devc->pcibase);		/* Sets DMA 1 */
		outb (hw_config->dma, devc->pcibase + 1);
		outb (0x75, devc->pcibase);		/* Sets DMA 2 */
		outb (hw_config->dma2 >= 0 ? hw_config->dma2 : 4, devc->pcibase + 1);
	} else if (devc->pcibase) {
		printk (KERN_INFO "Non-PnP ESS card with PnP registers at %04Xh, ignoring them.\n", devc->pcibase);
		devc->pcibase = 0;
	}

	devc->caps |= SB_NO_MIDI;   /* ES1688 uses MPU401 MIDI mode */

	hw_config->name = name;

	sb_dsp_reset(devc); /* Turn on extended mode */

	ess_setmixer (devc, 0x00, 0x00);	/* Reset mixer registers */

	return 1;
}

/*
 * This one is called from sb_dsp_init.
 */
int ess_dsp_init (sb_devc *devc, struct address_info *hw_config)
{
	/*
	 * For SB16 having both dma8 and dma16 means enable
	 * Full Duplex. Let's try this too
	 */
	if ((devc->caps & ESSCAP_ES18) && hw_config->dma2 >= 0) {
		devc->dma16 = hw_config->dma2;
		if (devc->dma8 != devc->dma16) {
			devc->duplex = 1;
		}
	}
	return 1;
}

/****************************************************************************
 *																			*
 *									ESS mixer								*
 *																			*
 ****************************************************************************/

#define ES688_RECORDING_DEVICES	\
			( SOUND_MASK_LINE	| SOUND_MASK_MIC	| SOUND_MASK_CD		)
#define ES688_MIXER_DEVICES		\
			( SOUND_MASK_SYNTH	| SOUND_MASK_PCM	| SOUND_MASK_LINE	\
			| SOUND_MASK_MIC	| SOUND_MASK_CD		| SOUND_MASK_VOLUME	\
			| SOUND_MASK_LINE2	| SOUND_MASK_SPEAKER					)

#define ES1688_RECORDING_DEVICES	\
			( ES688_RECORDING_DEVICES					)
#define ES1688_MIXER_DEVICES		\
			( ES688_MIXER_DEVICES | SOUND_MASK_RECLEV	)

#define ES_NEW_RECORDING_DEVICES	\
			( ES1688_RECORDING_DEVICES | SOUND_MASK_LINE2 | SOUND_MASK_SYNTH)
#define ES_NEW_MIXER_DEVICES		\
			( ES1688_MIXER_DEVICES											)

/*
 * Mixer registers of ES18xx with new capabilities
 *
 * These registers specifically take care of recording levels. To make the
 * mapping from playback devices to recording devices every recording
 * devices = playback device + ES_REC_MIXER_RECDIFF
 */
#define ES_REC_MIXER_RECBASE	(SOUND_MIXER_LINE3 + 1)
#define ES_REC_MIXER_RECDIFF	(ES_REC_MIXER_RECBASE - SOUND_MIXER_SYNTH)

#define ES_REC_MIXER_RECSYNTH	(SOUND_MIXER_SYNTH	 + ES_REC_MIXER_RECDIFF)
#define ES_REC_MIXER_RECPCM		(SOUND_MIXER_PCM	 + ES_REC_MIXER_RECDIFF)
#define ES_REC_MIXER_RECSPEAKER	(SOUND_MIXER_SPEAKER + ES_REC_MIXER_RECDIFF)
#define ES_REC_MIXER_RECLINE	(SOUND_MIXER_LINE	 + ES_REC_MIXER_RECDIFF)
#define ES_REC_MIXER_RECMIC		(SOUND_MIXER_MIC	 + ES_REC_MIXER_RECDIFF)
#define ES_REC_MIXER_RECCD		(SOUND_MIXER_CD		 + ES_REC_MIXER_RECDIFF)
#define ES_REC_MIXER_RECIMIX	(SOUND_MIXER_IMIX	 + ES_REC_MIXER_RECDIFF)
#define ES_REC_MIXER_RECALTPCM	(SOUND_MIXER_ALTPCM	 + ES_REC_MIXER_RECDIFF)
#define ES_REC_MIXER_RECRECLEV	(SOUND_MIXER_RECLEV	 + ES_REC_MIXER_RECDIFF)
#define ES_REC_MIXER_RECIGAIN	(SOUND_MIXER_IGAIN	 + ES_REC_MIXER_RECDIFF)
#define ES_REC_MIXER_RECOGAIN	(SOUND_MIXER_OGAIN	 + ES_REC_MIXER_RECDIFF)
#define ES_REC_MIXER_RECLINE1	(SOUND_MIXER_LINE1	 + ES_REC_MIXER_RECDIFF)
#define ES_REC_MIXER_RECLINE2	(SOUND_MIXER_LINE2	 + ES_REC_MIXER_RECDIFF)
#define ES_REC_MIXER_RECLINE3	(SOUND_MIXER_LINE3	 + ES_REC_MIXER_RECDIFF)

static mixer_tab es688_mix = {
MIX_ENT(SOUND_MIXER_VOLUME,			0x32, 7, 4, 0x32, 3, 4),
MIX_ENT(SOUND_MIXER_BASS,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,			0x36, 7, 4, 0x36, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,			0x14, 7, 4, 0x14, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,		0x3c, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,			0x3e, 7, 4, 0x3e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,			0x1a, 7, 4, 0x1a, 3, 4),
MIX_ENT(SOUND_MIXER_CD,				0x38, 7, 4, 0x38, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_IGAIN,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE1,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE2,			0x3a, 7, 4, 0x3a, 3, 4),
MIX_ENT(SOUND_MIXER_LINE3,			0x00, 0, 0, 0x00, 0, 0)
};

/*
 * The ES1688 specifics... hopefully correct...
 * - 6 bit master volume
 *   I was wrong, ES1888 docs say ES1688 didn't have it.
 * - RECLEV control
 * These may apply to ES688 too. I have no idea.
 */
static mixer_tab es1688_mix = {
MIX_ENT(SOUND_MIXER_VOLUME,			0x32, 7, 4, 0x32, 3, 4),
MIX_ENT(SOUND_MIXER_BASS,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,			0x36, 7, 4, 0x36, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,			0x14, 7, 4, 0x14, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,		0x3c, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,			0x3e, 7, 4, 0x3e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,			0x1a, 7, 4, 0x1a, 3, 4),
MIX_ENT(SOUND_MIXER_CD,				0x38, 7, 4, 0x38, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,			0xb4, 7, 4, 0xb4, 3, 4),
MIX_ENT(SOUND_MIXER_IGAIN,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE1,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE2,			0x3a, 7, 4, 0x3a, 3, 4),
MIX_ENT(SOUND_MIXER_LINE3,			0x00, 0, 0, 0x00, 0, 0)
};

static mixer_tab es1688later_mix = {
MIX_ENT(SOUND_MIXER_VOLUME,			0x60, 5, 6, 0x62, 5, 6),
MIX_ENT(SOUND_MIXER_BASS,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,			0x36, 7, 4, 0x36, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,			0x14, 7, 4, 0x14, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,		0x3c, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,			0x3e, 7, 4, 0x3e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,			0x1a, 7, 4, 0x1a, 3, 4),
MIX_ENT(SOUND_MIXER_CD,				0x38, 7, 4, 0x38, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,			0xb4, 7, 4, 0xb4, 3, 4),
MIX_ENT(SOUND_MIXER_IGAIN,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE1,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE2,			0x3a, 7, 4, 0x3a, 3, 4),
MIX_ENT(SOUND_MIXER_LINE3,			0x00, 0, 0, 0x00, 0, 0)
};

/*
 * This one is for all ESS chips with a record mixer.
 * It's not used (yet) however
 */
static mixer_tab es_rec_mix = {
MIX_ENT(SOUND_MIXER_VOLUME,			0x60, 5, 6, 0x62, 5, 6),
MIX_ENT(SOUND_MIXER_BASS,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,			0x36, 7, 4, 0x36, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,			0x14, 7, 4, 0x14, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,		0x3c, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,			0x3e, 7, 4, 0x3e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,			0x1a, 7, 4, 0x1a, 3, 4),
MIX_ENT(SOUND_MIXER_CD,				0x38, 7, 4, 0x38, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,			0xb4, 7, 4, 0xb4, 3, 4),
MIX_ENT(SOUND_MIXER_IGAIN,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE1,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE2,			0x3a, 7, 4, 0x3a, 3, 4),
MIX_ENT(SOUND_MIXER_LINE3,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECSYNTH,		0x6b, 7, 4, 0x6b, 3, 4),
MIX_ENT(ES_REC_MIXER_RECPCM,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECSPEAKER,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECLINE,		0x6e, 7, 4, 0x6e, 3, 4),
MIX_ENT(ES_REC_MIXER_RECMIC,		0x68, 7, 4, 0x68, 3, 4),
MIX_ENT(ES_REC_MIXER_RECCD,			0x6a, 7, 4, 0x6a, 3, 4),
MIX_ENT(ES_REC_MIXER_RECIMIX,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECALTPCM,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECRECLEV,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECIGAIN,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECOGAIN,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECLINE1,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECLINE2,		0x6c, 7, 4, 0x6c, 3, 4),
MIX_ENT(ES_REC_MIXER_RECLINE3,		0x00, 0, 0, 0x00, 0, 0)
};

/*
 * This one is for new ES's. It's little different from es_rec_mix: it
 * has 0x7c for PCM playback level. This is because uses
 * Audio 2 for playback.
 */
static mixer_tab es_new_mix = {
MIX_ENT(SOUND_MIXER_VOLUME,			0x60, 5, 6, 0x62, 5, 6),
MIX_ENT(SOUND_MIXER_BASS,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_TREBLE,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_SYNTH,			0x36, 7, 4, 0x36, 3, 4),
MIX_ENT(SOUND_MIXER_PCM,			0x7c, 7, 4, 0x7c, 3, 4),
MIX_ENT(SOUND_MIXER_SPEAKER,		0x3c, 2, 3, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE,			0x3e, 7, 4, 0x3e, 3, 4),
MIX_ENT(SOUND_MIXER_MIC,			0x1a, 7, 4, 0x1a, 3, 4),
MIX_ENT(SOUND_MIXER_CD,				0x38, 7, 4, 0x38, 3, 4),
MIX_ENT(SOUND_MIXER_IMIX,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_ALTPCM,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_RECLEV,			0xb4, 7, 4, 0xb4, 3, 4),
MIX_ENT(SOUND_MIXER_IGAIN,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_OGAIN,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE1,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(SOUND_MIXER_LINE2,			0x3a, 7, 4, 0x3a, 3, 4),
MIX_ENT(SOUND_MIXER_LINE3,			0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECSYNTH,		0x6b, 7, 4, 0x6b, 3, 4),
MIX_ENT(ES_REC_MIXER_RECPCM,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECSPEAKER,	0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECLINE,		0x6e, 7, 4, 0x6e, 3, 4),
MIX_ENT(ES_REC_MIXER_RECMIC,		0x68, 7, 4, 0x68, 3, 4),
MIX_ENT(ES_REC_MIXER_RECCD,			0x6a, 7, 4, 0x6a, 3, 4),
MIX_ENT(ES_REC_MIXER_RECIMIX,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECALTPCM,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECRECLEV,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECIGAIN,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECOGAIN,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECLINE1,		0x00, 0, 0, 0x00, 0, 0),
MIX_ENT(ES_REC_MIXER_RECLINE2,		0x6c, 7, 4, 0x6c, 3, 4),
MIX_ENT(ES_REC_MIXER_RECLINE3,		0x00, 0, 0, 0x00, 0, 0)
};

#ifdef FKS_LOGGING
static int ess_mixer_mon_regs[]
	= { 0x70, 0x71, 0x72, 0x74, 0x76, 0x78, 0x7a, 0x7c, 0x7d, 0x7f
	  , 0xa1, 0xa2, 0xa4, 0xa5, 0xa8, 0xa9
	  , 0xb1, 0xb2, 0xb4, 0xb5, 0xb6, 0xb7, 0xb9
	  , 0x00};

static void ess_show_mixerregs (sb_devc *devc)
{
	int *mp = ess_mixer_mon_regs;

return;

	while (*mp != 0) {
		printk (KERN_INFO "res (%x)=%x\n", *mp, (int)(ess_getmixer (devc, *mp)));
		mp++;
	}
}
#endif

void ess_setmixer (sb_devc * devc, unsigned int port, unsigned int value)
{
	unsigned long flags;

#ifdef FKS_LOGGING
printk(KERN_INFO "FKS: write mixer %x: %x\n", port, value);
#endif

	save_flags(flags);
	cli();

	outb(((unsigned char) (port & 0xff)), MIXER_ADDR);

	udelay(20);
	outb(((unsigned char) (value & 0xff)), MIXER_DATA);
	udelay(20);

	restore_flags(flags);
}

unsigned int ess_getmixer (sb_devc * devc, unsigned int port)
{
	unsigned int val;
	unsigned long flags;

	save_flags(flags);
	cli();

	outb(((unsigned char) (port & 0xff)), MIXER_ADDR);

	udelay(20);
	val = inb(MIXER_DATA);
	udelay(20);

	restore_flags(flags);

	return val;
}

static void ess_chgmixer
	(sb_devc * devc, unsigned int reg, unsigned int mask, unsigned int val)
{
	int value;

	value = ess_getmixer (devc, reg);
	value = (value & ~mask) | (val & mask);
	ess_setmixer (devc, reg, value);
}

/*
 * ess_mixer_init must be called from sb_mixer_init
 */
void ess_mixer_init (sb_devc * devc)
{
	devc->mixer_caps = SOUND_CAP_EXCL_INPUT;

	/*
	* Take care of new ES's specifics...
	*/
	if (devc->caps & ESSCAP_NEW) {
		devc->supported_devices		= ES_NEW_MIXER_DEVICES;
		devc->supported_rec_devices	= ES_NEW_RECORDING_DEVICES;
#ifdef FKS_LOGGING
printk (KERN_INFO "FKS: ess_mixer_init dup = %d\n", devc->duplex);
#endif
		if (devc->duplex) {
			devc->iomap				= &es_new_mix;
		} else {
			devc->iomap				= &es_rec_mix;
		}
	} else {
		if (devc->submodel == SUBMDL_ES688) {
			devc->supported_devices		= ES688_MIXER_DEVICES;
			devc->supported_rec_devices	= ES688_RECORDING_DEVICES;
			devc->iomap					= &es688_mix;
		} else {
			/*
			 * es1688 has 4 bits master vol.
			 * later chips have 6 bits (?)
			 */
			devc->supported_devices		= ES1688_MIXER_DEVICES;
			devc->supported_rec_devices	= ES1688_RECORDING_DEVICES;
			if (devc->caps & ESSCAP_ES18) {
				devc->iomap				= &es1688later_mix;
			} else {
				devc->iomap				= &es1688_mix;
			}
		}
	}
}

/*
 * Changing playback levels at an ESS chip with record mixer means having to
 * take care of recording levels of recorded inputs (devc->recmask) too!
 */
int ess_mixer_set(sb_devc *devc, int dev, int left, int right)
{
	if ((devc->caps & ESSCAP_NEW) && (devc->recmask & (1 << dev))) {
		sb_common_mixer_set (devc, dev + ES_REC_MIXER_RECDIFF, left, right);
	}
	/* Set & unmute master volume */
	if ((devc->caps & ESSCAP_ES18) && (dev == SOUND_MIXER_VOLUME)) {
		ess_chgmixer (devc, 0x60, 0x7f, 0x3f & ((left * 0x3f + 50) / 100));
		ess_chgmixer (devc, 0x62, 0x7f, 0x3f & ((right * 0x3f + 50) / 100));
		return left | (right << 8);
	}
	return sb_common_mixer_set (devc, dev, left, right);
}

/*
 * After a sb_dsp_reset extended register 0xb4 (RECLEV) is reset too. After
 * sb_dsp_reset RECLEV has to be restored. This is where ess_mixer_reload
 * helps.
 */
void ess_mixer_reload (sb_devc *devc, int dev)
{
	int left, right, value;

	value = devc->levels[dev];
	left  = value & 0x000000ff;
	right = (value & 0x0000ff00) >> 8;

	sb_common_mixer_set(devc, dev, left, right);
}

int es_rec_set_recmask(sb_devc * devc, int mask)
{
	int i, i_mask, cur_mask, diff_mask;
	int value, left, right;

#ifdef FKS_LOGGING
printk (KERN_INFO "FKS: es_rec_set_recmask mask = %x\n", mask);
#endif
	/*
	 * Changing the recmask on an ESS chip with recording mixer means:
	 * (1) Find the differences
	 * (2) For "turned-on"  inputs: make the recording level the playback level
	 * (3) For "turned-off" inputs: make the recording level zero
	 */
	cur_mask  = devc->recmask;
	diff_mask = (cur_mask ^ mask);

	for (i = 0; i < 32; i++) {
		i_mask = (1 << i);
		if (diff_mask & i_mask) {	/* Difference? (1)  */
			if (mask & i_mask) {	/* Turn it on  (2)  */
				value = devc->levels[i];
				left  = value & 0x000000ff;
				right = (value & 0x0000ff00) >> 8;
			} else {				/* Turn it off (3)  */
				left  = 0;
				right = 0;
			}
			sb_common_mixer_set(devc, i + ES_REC_MIXER_RECDIFF, left, right);
		}
	}
	return mask;
}

int ess_set_recmask(sb_devc * devc, int *mask)
{
	/* This applies to ESS chips with record mixers only! */

	if (devc->caps & ESSCAP_NEW) {
		*mask	= es_rec_set_recmask (devc, *mask);
		return 1;									/* Applied		*/
	} else {
		return 0;									/* Not applied	*/
	}
}

/*
 * ess_mixer_reset must be called from sb_mixer_reset
 */
int ess_mixer_reset (sb_devc * devc)
{
	/*
	 * Separate actions for ESS chips with a record mixer:
	 */
	if (devc->caps & ESSCAP_NEW) {
		/*
		 * Call set_recmask for proper initialization
		 */
		devc->recmask = devc->supported_rec_devices;
		es_rec_set_recmask(devc, 0);
		devc->recmask = 0;

		return 1;	/* We took care of recmask.				*/
	} else {
		return 0;	/* We didn't take care; caller do it	*/
	}
}

/****************************************************************************
 *																			*
 *								ESS midi									*
 *																			*
 ****************************************************************************/

int ess_midi_init(sb_devc * devc, struct address_info *hw_config)
{
	int val;

	if (devc->submodel == SUBMDL_ES688) {
		return 0;				/* ES688 doesn't support MPU401 mode */
	}

	if (hw_config->irq < 2) {
		hw_config->irq = devc->irq;
	}

	if (devc->caps & ESSCAP_PNP) {
		outb (0x07, devc->pcibase);		/* Selects logical device #1 */
		outb (0x01, devc->pcibase + 1);
		outb (0x28, devc->pcibase);
		val = inb (devc->pcibase + 1) & 0xf0;
		outb (0x28, devc->pcibase);		/* Sets MPU IRQ */
		outb (hw_config->irq | val, devc->pcibase + 1);
		if (hw_config->io_base) {
			outb (0x64, devc->pcibase);		/* Sets MPU I/O address */
			outb ((hw_config->io_base & 0xf00) >> 8, devc->pcibase + 1);
			outb (0x65, devc->pcibase);		/* Sets MPU I/O address */
			outb (hw_config->io_base & 0xfc, devc->pcibase + 1);
		} else {
			outb (0x64, devc->pcibase);		/* Read MPU I/O address */
			hw_config->io_base = (inb (devc->pcibase + 1) & 0x0f) << 8;
			outb (0x65, devc->pcibase);		/* Read MPU I/O address */
			hw_config->io_base |= inb (devc->pcibase + 1) & 0xfc;
		}

		ess_setmixer (devc, 0x64, 0xc2);	/* Enable MPU interrupt */
	} else {
		if (devc->irq == hw_config->irq && (devc->caps & ESSCAP_IRQ)) {
			val = 0x43;
		}
		else switch (hw_config->irq) {
			case 11:
				if (!(devc->caps & ESSCAP_IRQ)) {
					return 0;
				}
				val = 0x63;
				break;
			case 2:
			case 9:
				val = 0x83;
				break;
			case 5:
				val = 0xa3;
				break;
			case 7:
				val = 0xc3;
				break;
			case 10:
				val = 0xe3;
				break;
			default:
				return 0;
		}
		switch (hw_config->io_base) {
			case 0x300:
			case 0x310:
			case 0x320:
			case 0x330:
				ess_setmixer (devc, 0x40, val
								| ((hw_config->io_base & 0x0f0) >> 1));
				break;
			default:
				return 0;
		}
	}

	if (devc->irq == hw_config->irq)	/* Shared IRQ */
		hw_config->irq = -devc->irq;

	return 1;
}

