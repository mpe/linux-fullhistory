/*
    cx88x-audio.c - Conexant CX23880/23881 audio downstream driver driver

     (c) 2001 Michael Eskin, Tom Zakrajsek [Windows version]
     (c) 2002 Yurij Sysoev <yurij@naturesoft.net>
     (c) 2003 Gerd Knorr <kraxel@bytesex.org>

    -----------------------------------------------------------------------

    Lot of voodoo here.  Even the data sheet doesn't help to
    understand what is going on here, the documentation for the audio
    part of the cx2388x chip is *very* bad.

    Some of this comes from party done linux driver sources I got from
    [undocumented].

    Some comes from the dscaler sources, the dscaler driver guy works
    for Conexant ...
    
    -----------------------------------------------------------------------
    
    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/poll.h>
#include <linux/pci.h>
#include <linux/signal.h>
#include <linux/ioport.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/vmalloc.h>
#include <linux/init.h>

#include "cx88.h"

static unsigned int audio_debug = UNSET;
MODULE_PARM(audio_debug,"i");
MODULE_PARM_DESC(audio_debug,"enable debug messages [audio]");

#define dprintk(fmt, arg...)	if (audio_debug) \
	printk(KERN_DEBUG "%s: " fmt, dev->name , ## arg)

/* ----------------------------------------------------------- */

struct rlist {
	u32 reg;
	u32 val;
};

static void set_audio_registers(struct cx8800_dev *dev,
				const struct rlist *l)
{
	int i;

	for (i = 0; l[i].reg; i++)
		cx_write(l[i].reg, l[i].val);
}

static void set_audio_standard_BTSC(struct cx8800_dev *dev, unsigned int sap)
{
	dprintk("set_audio_standard_BTSC() [TODO]\n");
}

static void set_audio_standard_NICAM(struct cx8800_dev *dev)
{
	static const struct rlist nicam[] = {
		//  increase level of input by 12dB
		{ AUD_AFE_12DB_EN,         0x00000001 },

    		// initialize NICAM                 
    		{ AUD_INIT,                0x00000010 },
    		{ AUD_INIT_LD,             0x00000001 },
    		{ AUD_SOFT_RESET,          0x00000001 },
    
   		// WARNING!!!! Stereo mode is FORCED!!!!
    		{ AUD_CTL,                 EN_DAC_ENABLE | EN_DMTRX_LR | EN_NICAM_FORCE_STEREO },
    
    		{ AUD_SOFT_RESET,          0x00000001 },
    		{ AUD_RATE_ADJ1,           0x00000010 },
    		{ AUD_RATE_ADJ2,           0x00000040 },
    		{ AUD_RATE_ADJ3,           0x00000100 },
    		{ AUD_RATE_ADJ4,           0x00000400 },
    		{ AUD_RATE_ADJ5,           0x00001000 },
    //		{ AUD_DMD_RA_DDS,          0x00c0d5ce },

                { /* end of list */ },
        };

        printk("set_audio_standard_NICAM()\n");
        set_audio_registers(dev, nicam);

    	// setup QAM registers
    	cx_write(0x320d01,                0x06);
    	cx_write(0x320d02,                0x82);
    	cx_write(0x320d03,                0x16);
    	cx_write(0x320d04,                0x05);
    	cx_write(0x320d2a,                0x34);
    	cx_write(0x320d2b,                0x4c);

    	// setup Audio PLL
    	//cx_write(AUD_PLL_PRESCALE,        0x0002);
    	//cx_write(AUD_PLL_INT,             0x001f);

    	// de-assert Audio soft reset
    	cx_write(AUD_SOFT_RESET,          0x00000000);  // Causes a pop every time
}

static void set_audio_standard_A2(struct cx8800_dev *dev)
{
	static const struct rlist a2[] = {
		//  increase level of input by 12dB
		{ AUD_AFE_12DB_EN,         0x00000001 },

		//  initialize A2
		{ AUD_INIT,                0x00000004 },
		{ AUD_INIT_LD,             0x00000001 },
		{ AUD_SOFT_RESET,          0x00000001 },
    
		// ; WARNING!!! A2 STEREO DEMATRIX HAS TO BE
		// ; SET MANUALLY!!!  Value sould be 0x100c
		{ AUD_CTL, EN_DAC_ENABLE | EN_DMTRX_SUMR | EN_A2_AUTO_STEREO },

		{ AUD_DN0_FREQ,            0x0000312b },
		{ AUD_POLY0_DDS_CONSTANT,  0x000a62b4 },
		{ AUD_IIR1_0_SEL,          0x00000000 },
		{ AUD_IIR1_1_SEL,          0x00000001 },
		{ AUD_IIR1_2_SEL,          0x0000001f },
		{ AUD_IIR1_3_SEL,          0x00000020 },
		{ AUD_IIR1_4_SEL,          0x00000023 },
		{ AUD_IIR1_5_SEL,          0x00000007 },
		{ AUD_IIR1_0_SHIFT,        0x00000000 },
		{ AUD_IIR1_1_SHIFT,        0x00000000 },
		{ AUD_IIR1_2_SHIFT,        0x00000007 },
		{ AUD_IIR1_3_SHIFT,        0x00000007 },
		{ AUD_IIR1_4_SHIFT,        0x00000007 },
		{ AUD_IIR1_5_SHIFT,        0x00000000 },
		{ AUD_IIR2_0_SEL,          0x00000002 },
		{ AUD_IIR2_1_SEL,          0x00000003 },
		{ AUD_IIR2_2_SEL,          0x00000004 },
		{ AUD_IIR2_3_SEL,          0x00000005 },
		{ AUD_IIR3_0_SEL,          0x00000021 },
		{ AUD_IIR3_1_SEL,          0x00000023 },
		{ AUD_IIR3_2_SEL,          0x00000016 },
		{ AUD_IIR3_0_SHIFT,        0x00000000 },
		{ AUD_IIR3_1_SHIFT,        0x00000000 },
		{ AUD_IIR3_2_SHIFT,        0x00000000 },
		{ AUD_IIR4_0_SEL,          0x0000001d },
		{ AUD_IIR4_1_SEL,          0x00000019 },
		{ AUD_IIR4_2_SEL,          0x00000008 },
		{ AUD_IIR4_0_SHIFT,        0x00000000 },
		{ AUD_IIR4_1_SHIFT,        0x00000000 },
		{ AUD_IIR4_2_SHIFT,        0x00000001 },
		{ AUD_IIR4_0_CA0,          0x0003e57e },
		{ AUD_IIR4_0_CA1,          0x00005e11 },
		{ AUD_IIR4_0_CA2,          0x0003a7cf },
		{ AUD_IIR4_0_CB0,          0x00002368 },
		{ AUD_IIR4_0_CB1,          0x0003bf1b },
		{ AUD_IIR4_1_CA0,          0x00006349 },
		{ AUD_IIR4_1_CA1,          0x00006f27 },
		{ AUD_IIR4_1_CA2,          0x0000e7a3 },
		{ AUD_IIR4_1_CB0,          0x00005653 },
		{ AUD_IIR4_1_CB1,          0x0000cf97 },
		{ AUD_IIR4_2_CA0,          0x00006349 },
		{ AUD_IIR4_2_CA1,          0x00006f27 },
		{ AUD_IIR4_2_CA2,          0x0000e7a3 },
		{ AUD_IIR4_2_CB0,          0x00005653 },
		{ AUD_IIR4_2_CB1,          0x0000cf97 },
		{ AUD_HP_MD_IIR4_1,        0x00000001 },
		{ AUD_HP_PROG_IIR4_1,      0x00000017 },
		{ AUD_DN1_FREQ,            0x00003618 },
		{ AUD_DN1_SRC_SEL,         0x00000017 },
		{ AUD_DN1_SHFT,            0x00000007 },
		{ AUD_DN1_AFC,             0x00000000 },
		{ AUD_DN1_FREQ_SHIFT,      0x00000000 },
		{ AUD_DN2_SRC_SEL,         0x00000040 },
		{ AUD_DN2_SHFT,            0x00000000 },
		{ AUD_DN2_AFC,             0x00000002 },
		{ AUD_DN2_FREQ,            0x0000caaf },
		{ AUD_DN2_FREQ_SHIFT,      0x00000000 },
		{ AUD_PDET_SRC,            0x00000014 },
		{ AUD_PDET_SHIFT,          0x00000000 },
		{ AUD_DEEMPH0_SRC_SEL,     0x00000011 },
		{ AUD_DEEMPH1_SRC_SEL,     0x00000013 },
		{ AUD_DEEMPH0_SHIFT,       0x00000000 },
		{ AUD_DEEMPH1_SHIFT,       0x00000000 },
		{ AUD_DEEMPH0_G0,          0x000004da },
		{ AUD_DEEMPH0_A0,          0x0000777a },
		{ AUD_DEEMPH0_B0,          0x00000000 },
		{ AUD_DEEMPH0_A1,          0x0003f062 },
		{ AUD_DEEMPH0_B1,          0x00000000 },
		{ AUD_DEEMPH1_G0,          0x000004da },
		{ AUD_DEEMPH1_A0,          0x0000777a },
		{ AUD_DEEMPH1_B0,          0x00000000 },
		{ AUD_DEEMPH1_A1,          0x0003f062 },
		{ AUD_DEEMPH1_B1,          0x00000000 },
		{ AUD_PLL_EN,              0x00000000 },
		{ AUD_DMD_RA_DDS,          0x002a4efb },
		{ AUD_RATE_ADJ1,           0x00001000 },
		{ AUD_RATE_ADJ2,           0x00002000 },
		{ AUD_RATE_ADJ3,           0x00003000 },
		{ AUD_RATE_ADJ4,           0x00004000 },
		{ AUD_RATE_ADJ5,           0x00005000 },
		{ AUD_C2_UP_THR,           0x0000ffff },
		{ AUD_C2_LO_THR,           0x0000e800 },
		{ AUD_C1_UP_THR,           0x00008c00 },
		{ AUD_C1_LO_THR,           0x00006c00 },

		//   ; Completely ditch AFC feedback
		{ AUD_DCOC_0_SRC,          0x00000021 },
		{ AUD_DCOC_1_SRC,          0x0000001a },
		{ AUD_DCOC1_SHIFT,         0x00000000 },
		{ AUD_DCOC_1_SHIFT_IN0,    0x0000000a },
		{ AUD_DCOC_1_SHIFT_IN1,    0x00000008 },
		{ AUD_DCOC_PASS_IN,        0x00000000 },
		{ AUD_IIR4_0_SEL,          0x00000023 },

		//  ; Completely ditc FM-2 AFC feedback
		{ AUD_DN1_AFC,             0x00000000 },
		{ AUD_DCOC_2_SRC,          0x0000001b },
		{ AUD_IIR4_1_SEL,          0x00000025 },

		// ; WARNING!!! THIS CHANGE WAS NOT EXPECTED!!!
		// ; Swap I & Q inputs into second rotator
		// ; to reverse frequency and therefor invert
		// ; phase from the cordic FM demodulator
		// ; (frequency rotation must also be reversed
		{ AUD_DN2_SRC_SEL,         0x00000001 },
		{ AUD_DN2_FREQ,            0x00003551 },


		//  setup Audio PLL
		{ AUD_PLL_PRESCALE,        0x00000002 },
		{ AUD_PLL_INT,             0x0000001f },

		//  de-assert Audio soft reset
		{ AUD_SOFT_RESET,          0x00000000 },

		{ /* end of list */ },
	};

	dprintk("set_audio_standard_A2()\n");
	set_audio_registers(dev, a2);
}

static void set_audio_standard_EIAJ(struct cx8800_dev *dev)
{
	dprintk("set_audio_standard_EIAJ() [TODO]\n");
}

static void set_audio_standard_FM(struct cx8800_dev *dev)
{
	dprintk("set_audio_standard_FM\n");

	// initialize FM Radio
	cx_write(AUD_INIT,0x0020);
	cx_write(AUD_INIT_LD,0x0001);
	cx_write(AUD_SOFT_RESET,0x0001);

#if 0 /* FIXME */
	switch (dev->audio_properties.FM_deemphasis)
	{
		case WW_FM_DEEMPH_50:
			//Set De-emphasis filter coefficients for 50 usec
			cx_write(AUD_DEEMPH0_G0, 0x0C45);
			cx_write(AUD_DEEMPH0_A0, 0x6262);
			cx_write(AUD_DEEMPH0_B0, 0x1C29);
			cx_write(AUD_DEEMPH0_A1, 0x3FC66);
			cx_write(AUD_DEEMPH0_B1, 0x399A);

			cx_write(AUD_DEEMPH1_G0, 0x0D80);
			cx_write(AUD_DEEMPH1_A0, 0x6262);
			cx_write(AUD_DEEMPH1_B0, 0x1C29);
			cx_write(AUD_DEEMPH1_A1, 0x3FC66);
			cx_write(AUD_DEEMPH1_B1, 0x399A);
			
			break;

		case WW_FM_DEEMPH_75:
			//Set De-emphasis filter coefficients for 75 usec
			cx_write(AUD_DEEMPH0_G0, 0x91B );
			cx_write(AUD_DEEMPH0_A0, 0x6B68);
			cx_write(AUD_DEEMPH0_B0, 0x11EC);
			cx_write(AUD_DEEMPH0_A1, 0x3FC66);
			cx_write(AUD_DEEMPH0_B1, 0x399A);

			cx_write(AUD_DEEMPH1_G0, 0xAA0 );
			cx_write(AUD_DEEMPH1_A0, 0x6B68);
			cx_write(AUD_DEEMPH1_B0, 0x11EC);
			cx_write(AUD_DEEMPH1_A1, 0x3FC66);
			cx_write(AUD_DEEMPH1_B1, 0x399A);

			break;
	}
#endif

	// de-assert Audio soft reset
	cx_write(AUD_SOFT_RESET,0x0000);

	// AB: 10/2/01: this register is not being reset appropriately on occasion.
	cx_write(AUD_POLYPH80SCALEFAC,3);
}

/* ----------------------------------------------------------- */

void cx88_set_tvaudio(struct cx8800_dev *dev)
{
	cx_write(AUD_CTL, 0x00);

	switch (dev->tvaudio) {
	case WW_BTSC:
		set_audio_standard_BTSC(dev,0);
		break;
	case WW_NICAM_I:
	case WW_NICAM_BGDKL:
		set_audio_standard_NICAM(dev);
		break;
	case WW_A2_BG:
	case WW_A2_DK:
	case WW_A2_M:
		set_audio_standard_A2(dev);
		break;
	case WW_EIAJ:
		set_audio_standard_EIAJ(dev);
		break;
	case WW_FM:
		set_audio_standard_FM(dev);
		break;
	case WW_NONE:
	default:
		printk("%s: unknown tv audio mode [%d]\n",
		       dev->name, dev->tvaudio);
		break;
	}

	// unmute
	cx_set(AUD_CTL, EN_DAC_ENABLE);
	cx_write(AUD_VOL_CTL, 0x00);
	return;
}

void cx88_get_stereo(struct cx8800_dev *dev, struct v4l2_tuner *t)
{
	static char *m[] = {"mono", "dual mono", "stereo", "sap"};
	static char *p[] = {"no pilot", "pilot c1", "pilot c2", "?"};
	u32 reg,mode,pilot;

	reg   = cx_read(AUD_STATUS);
	mode  = reg & 0x03;
	pilot = (reg >> 2) & 0x03;
	dprintk("AUD_STATUS: %s / %s [status=0x%x,ctl=0x%x,vol=0x%x]\n",
		m[mode], p[pilot], reg,
		cx_read(AUD_CTL), cx_read(AUD_VOL_CTL));

	t->capability = V4L2_TUNER_CAP_STEREO | V4L2_TUNER_CAP_SAP |
		V4L2_TUNER_CAP_LANG1 | V4L2_TUNER_CAP_LANG2;
	t->rxsubchans = V4L2_TUNER_SUB_MONO;
	t->audmode    = V4L2_TUNER_MODE_MONO;

	switch (dev->tvaudio) {
	case WW_A2_BG:
 		if (2 == pilot) {
			/* stereo */
			t->rxsubchans = V4L2_TUNER_SUB_MONO | V4L2_TUNER_SUB_STEREO;
			if (2 == mode)
				t->audmode = V4L2_TUNER_MODE_STEREO;
		}
 		if (1 == pilot) {
			/* dual language -- FIXME */
			t->rxsubchans = V4L2_TUNER_SUB_LANG1 | V4L2_TUNER_SUB_LANG2;
			t->audmode = V4L2_TUNER_MODE_LANG1;
		}
		break;
	case WW_NICAM_BGDKL:
		if (2 == mode)
			t->audmode = V4L2_TUNER_MODE_STEREO;
		break;
	default:
		t->rxsubchans = V4L2_TUNER_SUB_MONO;
		t->audmode    = V4L2_TUNER_MODE_MONO;
		break;
	}
	return;
}

void cx88_set_stereo(struct cx8800_dev *dev, u32 mode)
{
	u32 ctl  = UNSET;
	u32 mask = UNSET;

	switch (dev->tvaudio) {
	case WW_A2_BG:
		switch (mode) {
		case V4L2_TUNER_MODE_MONO:   
		case V4L2_TUNER_MODE_LANG1:
			ctl  = EN_A2_FORCE_MONO1;
			mask = 0x3f;
			break;
		case V4L2_TUNER_MODE_LANG2:
			ctl  = EN_A2_AUTO_MONO2;
			mask = 0x3f;
			break;
		case V4L2_TUNER_MODE_STEREO:
			ctl  = EN_A2_AUTO_STEREO | EN_DMTRX_SUMR;
			mask = 0x8bf;
			break;
		}
		break;
	case WW_NICAM_BGDKL:
		switch (mode) {
		case V4L2_TUNER_MODE_MONO:   
			ctl  = EN_NICAM_FORCE_MONO1;
			mask = 0x3f;
			break;
		case V4L2_TUNER_MODE_LANG1:
			ctl  = EN_NICAM_AUTO_MONO2;
			mask = 0x3f;
			break;
		case V4L2_TUNER_MODE_STEREO:
			ctl  = EN_NICAM_FORCE_STEREO | EN_DMTRX_LR;
			mask = 0x93f;
			break;
		}
		break;	
	case WW_FM:
		switch (mode) {
		case V4L2_TUNER_MODE_MONO:   
			ctl  = EN_FMRADIO_FORCE_MONO;
			mask = 0x3f;
			break;
		case V4L2_TUNER_MODE_STEREO:
			ctl  = EN_FMRADIO_AUTO_STEREO;
			mask = 0x3f;
			break;
		}
		break;	
	}

	if (UNSET != ctl) {
		cx_write(AUD_SOFT_RESET, 0x0001);
		cx_andor(AUD_CTL, mask, ctl);
		cx_write(AUD_SOFT_RESET, 0x0000);
		dprintk("cx88_set_stereo: mask 0x%x, ctl 0x%x "
			"[status=0x%x,ctl=0x%x,vol=0x%x]\n",
			mask, ctl, cx_read(AUD_STATUS),
			cx_read(AUD_CTL), cx_read(AUD_VOL_CTL));
	}
	return;
}

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
