/*
 * $Id: cx88-cards.c,v 1.66 2005/03/04 09:12:23 kraxel Exp $
 *
 * device driver for Conexant 2388x based TV cards
 * card-specific stuff.
 *
 * (c) 2003 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/delay.h>

#include "cx88.h"

/* ------------------------------------------------------------------ */
/* board config info                                                  */

struct cx88_board cx88_boards[] = {
	[CX88_BOARD_UNKNOWN] = {
		.name		= "UNKNOWN/GENERIC",
		.tuner_type     = UNSET,
		.input          = {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 0,
		},{
			.type   = CX88_VMUX_COMPOSITE2,
			.vmux   = 1,
		},{
			.type   = CX88_VMUX_COMPOSITE3,
			.vmux   = 2,
		},{
			.type   = CX88_VMUX_COMPOSITE4,
			.vmux   = 3,
		}},
	},
	[CX88_BOARD_HAUPPAUGE] = {
		.name		= "Hauppauge WinTV 34xxx models",
		.tuner_type     = UNSET,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0xff00,  // internal decoder
		},{
			.type   = CX88_VMUX_DEBUG,
			.vmux   = 0,
			.gpio0  = 0xff01,  // mono from tuner chip
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0xff02,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0xff02,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.gpio0  = 0xff01,
		},
	},
	[CX88_BOARD_GDI] = {
		.name		= "GDI Black Gold",
		.tuner_type     = UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
		}},
	},
	[CX88_BOARD_PIXELVIEW] = {
		.name           = "PixelView",
		.tuner_type     = 5,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0xff00,  // internal decoder
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
		}},
		.radio = {
			 .type  = CX88_RADIO,
			 .gpio0 = 0xff10,
		 },
	},
	[CX88_BOARD_ATI_WONDER_PRO] = {
		.name           = "ATI TV Wonder Pro",
		.tuner_type     = 44,
		.tda9887_conf   = TDA9887_PRESENT | TDA9887_INTERCARRIER,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
                        .gpio0  = 0x03ff,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
                        .gpio0  = 0x03fe,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
                        .gpio0  = 0x03fe,
		}},
	},
        [CX88_BOARD_WINFAST2000XP_EXPERT] = {
                .name           = "Leadtek Winfast 2000XP Expert",
                .tuner_type     = 44,
		.tda9887_conf   = TDA9887_PRESENT,
                .input          = {{
                        .type   = CX88_VMUX_TELEVISION,
                        .vmux   = 0,
			.gpio0	= 0x00F5e700,
			.gpio1  = 0x00003004,
			.gpio2  = 0x00F5e700,
			.gpio3  = 0x02000000,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0	= 0x00F5c700,
			.gpio1  = 0x00003004,
			.gpio2  = 0x00F5c700,
			.gpio3  = 0x02000000,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0	= 0x00F5c700,
			.gpio1  = 0x00003004,
			.gpio2  = 0x00F5c700,
			.gpio3  = 0x02000000,
                }},
                .radio = {
                        .type   = CX88_RADIO,
			.gpio0	= 0x00F5d700,
			.gpio1  = 0x00003004,
			.gpio2  = 0x00F5d700,
			.gpio3  = 0x02000000,
                },
        },
	[CX88_BOARD_AVERTV_303] = {
		.name           = "AverTV Studio 303 (M126)",
		.tuner_type     = 38,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio1  = 0x309f,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio1  = 0x305f,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio1  = 0x305f,
		}},
		.radio = {
			.type   = CX88_RADIO,
		},
	},
	[CX88_BOARD_MSI_TVANYWHERE_MASTER] = {
		// added gpio values thanks to Michal
		// values for PAL from DScaler
		.name           = "MSI TV-@nywhere Master",
		.tuner_type     = 33,
		.tda9887_conf	= TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x000040bf,
			.gpio1  = 0x000080c0,
			.gpio2  = 0x0000ff40,
		},{
                        .type   = CX88_VMUX_COMPOSITE1,
                        .vmux   = 1,
			.gpio0  = 0x000040bf,
			.gpio1  = 0x000080c0,
			.gpio2  = 0x0000ff40,
		},{
                        .type   = CX88_VMUX_SVIDEO,
                        .vmux   = 2,
			.gpio0  = 0x000040bf,
			.gpio1  = 0x000080c0,
			.gpio2  = 0x0000ff40,
                }},
                .radio = {
			 .type   = CX88_RADIO,
                },
	},
	[CX88_BOARD_WINFAST_DV2000] = {
                .name           = "Leadtek Winfast DV2000",
                .tuner_type     = 38,
		.tda9887_conf   = TDA9887_PRESENT,
                .input          = {{
                        .type   = CX88_VMUX_TELEVISION,
                        .vmux   = 0,
			.gpio0  = 0x0035e700,
			.gpio1  = 0x00003004,
			.gpio2  = 0x0035e700,
			.gpio3  = 0x02000000,
		},{

			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x0035c700,
			.gpio1  = 0x00003004,
			.gpio2  = 0x0035c700,
			.gpio3  = 0x02000000,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x0035c700,
			.gpio1  = 0x0035c700,
			.gpio2  = 0x02000000,
			.gpio3  = 0x02000000,
		}},
                .radio = {
			.type   = CX88_RADIO,
			.gpio0  = 0x0035d700,
			.gpio1  = 0x00007004,
			.gpio2  = 0x0035d700,
			.gpio3  = 0x02000000,
		 },
        },
        [CX88_BOARD_LEADTEK_PVR2000] = {
		// gpio values for PAL version from regspy by DScaler
                .name           = "Leadtek PVR 2000",
                .tuner_type     = 38,
		.tda9887_conf   = TDA9887_PRESENT,
                .input          = {{
                        .type   = CX88_VMUX_TELEVISION,
                        .vmux   = 0,
                        .gpio0  = 0x0000bde6,
                },{
                        .type   = CX88_VMUX_COMPOSITE1,
                        .vmux   = 1,
                        .gpio0  = 0x0000bde6,
                },{
                        .type   = CX88_VMUX_SVIDEO,
                        .vmux   = 2,
                        .gpio0  = 0x0000bde6,
                }},
                .radio = {
                        .type   = CX88_RADIO,
                        .gpio0  = 0x0000bd62,
                },
		.blackbird = 1,
        },
	[CX88_BOARD_IODATA_GVVCP3PCI] = {
 		.name		= "IODATA GV-VCP3/PCI",
		.tuner_type     = TUNER_ABSENT,
 		.input          = {{
 			.type   = CX88_VMUX_COMPOSITE1,
 			.vmux   = 0,
 		},{
 			.type   = CX88_VMUX_COMPOSITE2,
 			.vmux   = 1,
 		},{
 			.type   = CX88_VMUX_SVIDEO,
 			.vmux   = 2,
 		}},
 	},
	[CX88_BOARD_PROLINK_PLAYTVPVR] = {
                .name           = "Prolink PlayTV PVR",
                .tuner_type     = 43,
		.tda9887_conf	= TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0xff00,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0xff03,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0xff03,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.gpio0  = 0xff00,
		},
	},
	[CX88_BOARD_ASUS_PVR_416] = {
		.name		= "ASUS PVR-416",
		.tuner_type     = 43,
                .tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x0000fde6,
 		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x0000fde6, // 0x0000fda6 L,R RCA audio in?
		}},
                .radio = {
                        .type   = CX88_RADIO,
			.gpio0  = 0x0000fde2,
                },
		.blackbird = 1,
	},
	[CX88_BOARD_MSI_TVANYWHERE] = {
		.name           = "MSI TV-@nywhere",
		.tuner_type     = 33,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x00000fbf,
			.gpio2  = 0x0000fc08,
		},{
  			.type   = CX88_VMUX_COMPOSITE1,
  			.vmux   = 1,
			.gpio0  = 0x00000fbf,
			.gpio2  = 0x0000fc68,
		},{
  			.type   = CX88_VMUX_SVIDEO,
  			.vmux   = 2,
			.gpio0  = 0x00000fbf,
			.gpio2  = 0x0000fc68,
  		}},
	},
        [CX88_BOARD_KWORLD_DVB_T] = {
                .name           = "KWorld/VStream XPert DVB-T",
		.tuner_type     = TUNER_ABSENT,
                .input          = {{
                        .type   = CX88_VMUX_COMPOSITE1,
                        .vmux   = 1,
			.gpio0  = 0x0700,
			.gpio2  = 0x0101,
                },{
                        .type   = CX88_VMUX_SVIDEO,
                        .vmux   = 2,
			.gpio0  = 0x0700,
			.gpio2  = 0x0101,
                }},
		.dvb            = 1,
	},
	[CX88_BOARD_DVICO_FUSIONHDTV_DVB_T1] = {
		.name           = "DVICO FusionHDTV DVB-T1",
		.tuner_type     = TUNER_ABSENT, /* No analog tuner */
		.input          = {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x000027df,
		 },{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x000027df,
		}},
		.dvb            = 1,
	},
	[CX88_BOARD_KWORLD_LTV883] = {
		.name           = "KWorld LTV883RF",
                .tuner_type     = 48,
                .input          = {{
                        .type   = CX88_VMUX_TELEVISION,
                        .vmux   = 0,
                        .gpio0  = 0x07f8,
		},{
			.type   = CX88_VMUX_DEBUG,
			.vmux   = 0,
			.gpio0  = 0x07f9,  // mono from tuner chip
                },{
                        .type   = CX88_VMUX_COMPOSITE1,
                        .vmux   = 1,
                        .gpio0  = 0x000007fa,
                },{
                        .type   = CX88_VMUX_SVIDEO,
                        .vmux   = 2,
                        .gpio0  = 0x000007fa,
                }},
                .radio = {
                        .type   = CX88_RADIO,
                        .gpio0  = 0x000007f8,
                },
	},
	[CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD] = {
		.name		= "DViCO - FusionHDTV 3 Gold",
		.tuner_type     = TUNER_MICROTUNE_4042FI5,
		/*
		   GPIO[0] resets DT3302 DTV receiver
		    0 - reset asserted
		    1 - normal operation
		   GPIO[1] mutes analog audio output connector
		    0 - enable selected source
		    1 - mute
		   GPIO[2] selects source for analog audio output connector
		    0 - analog audio input connector on tab
		    1 - analog DAC output from CX23881 chip
		   GPIO[3] selects RF input connector on tuner module
		    0 - RF connector labeled CABLE
		    1 - RF connector labeled ANT
		*/
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0	= 0x0f0d,
		},{
			.type   = CX88_VMUX_CABLE,
			.vmux   = 0,
			.gpio0	= 0x0f05,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0	= 0x0f00,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0	= 0x0f00,
		}},
#if 0
		.ts             = {
			 .type   = CX88_TS,
			 .gpio0  = 0x00000f01,   /* Hooked to tuner reset bit */
		 }
#endif
	},
        [CX88_BOARD_HAUPPAUGE_DVB_T1] = {
                .name           = "Hauppauge Nova-T DVB-T",
		.tuner_type     = TUNER_ABSENT,
                .input          = {{
                        .type   = CX88_VMUX_DVB,
                        .vmux   = 0,
                }},
		.dvb            = 1,
	},
        [CX88_BOARD_CONEXANT_DVB_T1] = {
		.name           = "Conexant DVB-T reference design",
		.tuner_type     = TUNER_ABSENT,
                .input          = {{
                        .type   = CX88_VMUX_DVB,
                        .vmux   = 0,
                }},
		.dvb            = 1,
	},
	[CX88_BOARD_PROVIDEO_PV259] = {
		.name		= "Provideo PV259",
		.tuner_type     = TUNER_PHILIPS_FQ1216ME,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
		}},
		.blackbird = 1,
	},
	[CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_PLUS] = {
		.name           = "DVICO FusionHDTV DVB-T Plus",
		.tuner_type     = TUNER_ABSENT, /* No analog tuner */
		.input          = {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x000027df,
		 },{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x000027df,
		}},
		.dvb            = 1,
	},
	[CX88_BOARD_DNTV_LIVE_DVB_T] = {
		.name	        = "digitalnow DNTV Live! DVB-T",
		.tuner_type     = TUNER_ABSENT,
		.input	        = {{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x00000700,
			.gpio2  = 0x00000101,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x00000700,
			.gpio2  = 0x00000101,
		}},
		.dvb            = 1,
	},
	[CX88_BOARD_PCHDTV_HD3000] = {
		.name           = "pcHDTV HD3000 HDTV",
		.tuner_type     = TUNER_THOMSON_DTT7610,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x00008484,
			.gpio1  = 0x00000000,
			.gpio2  = 0x00000000,
			.gpio3  = 0x00000000,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x00008400,
			.gpio1  = 0x00000000,
			.gpio2  = 0x00000000,
			.gpio3  = 0x00000000,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x00008400,
			.gpio1  = 0x00000000,
			.gpio2  = 0x00000000,
			.gpio3  = 0x00000000,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.vmux   = 2,
			.gpio0  = 0x00008400,
			.gpio1  = 0x00000000,
			.gpio2  = 0x00000000,
			.gpio3  = 0x00000000,
		},
		.dvb            = 1,
	},
	[CX88_BOARD_HAUPPAUGE_ROSLYN] = {
		// entry added by Kaustubh D. Bhalerao <bhalerao.1@osu.edu>
		// GPIO values obtained from regspy, courtesy Sean Covel
		.name        = "Hauppauge WinTV 28xxx (Roslyn) models",
		.tuner_type  = UNSET,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0xed12,  // internal decoder
			.gpio2  = 0x00ff,
		},{
			.type   = CX88_VMUX_DEBUG,
			.vmux   = 0,
			.gpio0  = 0xff01,  // mono from tuner chip
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0xff02,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0xed92,
			.gpio2  = 0x00ff,
		}},
		.radio = {
			 .type   = CX88_RADIO,
			 .gpio0  = 0xed96,
			 .gpio2  = 0x00ff,
		 },
		.blackbird = 1,
	},
	[CX88_BOARD_DIGITALLOGIC_MEC] = {
		/* params copied over from Leadtek PVR 2000 */
		.name           = "Digital-Logic MICROSPACE Entertainment Center (MEC)",
		/* not sure yet about the tuner type */
		.tuner_type     = 38,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 0,
			.gpio0  = 0x0000bde6,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 1,
			.gpio0  = 0x0000bde6,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 2,
			.gpio0  = 0x0000bde6,
		}},
		.radio = {
			.type   = CX88_RADIO,
			.gpio0  = 0x0000bd62,
		},
		.blackbird = 1,
	},
	[CX88_BOARD_IODATA_GVBCTV7E] = {
		.name           = "IODATA GV/BCTV7E",
		.tuner_type     = TUNER_PHILIPS_FQ1286,
		.tda9887_conf   = TDA9887_PRESENT,
		.input          = {{
			.type   = CX88_VMUX_TELEVISION,
			.vmux   = 1,
			.gpio1  = 0x0000e03f,
		},{
			.type   = CX88_VMUX_COMPOSITE1,
			.vmux   = 2,
			.gpio1  = 0x0000e07f,
		},{
			.type   = CX88_VMUX_SVIDEO,
			.vmux   = 3,
			.gpio1  = 0x0000e07f,
		}}
	},
};
const unsigned int cx88_bcount = ARRAY_SIZE(cx88_boards);

/* ------------------------------------------------------------------ */
/* PCI subsystem IDs                                                  */

struct cx88_subid cx88_subids[] = {
	{
		.subvendor = 0x0070,
		.subdevice = 0x3400,
		.card      = CX88_BOARD_HAUPPAUGE,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x3401,
		.card      = CX88_BOARD_HAUPPAUGE,
	},{
		.subvendor = 0x14c7,
		.subdevice = 0x0106,
		.card      = CX88_BOARD_GDI,
	},{
		.subvendor = 0x14c7,
		.subdevice = 0x0107, /* with mpeg encoder */
		.card      = CX88_BOARD_GDI,
	},{
		.subvendor = PCI_VENDOR_ID_ATI,
		.subdevice = 0x00f8,
		.card      = CX88_BOARD_ATI_WONDER_PRO,
	},{
                .subvendor = 0x107d,
                .subdevice = 0x6611,
                .card      = CX88_BOARD_WINFAST2000XP_EXPERT,
	},{
                .subvendor = 0x107d,
                .subdevice = 0x6613,	/* NTSC */
                .card      = CX88_BOARD_WINFAST2000XP_EXPERT,
	},{
		.subvendor = 0x107d,
                .subdevice = 0x6620,
                .card      = CX88_BOARD_WINFAST_DV2000,
        },{
                .subvendor = 0x107d,
                .subdevice = 0x663b,
                .card      = CX88_BOARD_LEADTEK_PVR2000,
        },{
                .subvendor = 0x107d,
                .subdevice = 0x663C,
                .card      = CX88_BOARD_LEADTEK_PVR2000,
        },{
		.subvendor = 0x1461,
		.subdevice = 0x000b,
		.card      = CX88_BOARD_AVERTV_303,
	},{
		.subvendor = 0x1462,
		.subdevice = 0x8606,
		.card      = CX88_BOARD_MSI_TVANYWHERE_MASTER,
	},{
 		.subvendor = 0x10fc,
 		.subdevice = 0xd003,
 		.card      = CX88_BOARD_IODATA_GVVCP3PCI,
	},{
 		.subvendor = 0x1043,
 		.subdevice = 0x4823,  /* with mpeg encoder */
 		.card      = CX88_BOARD_ASUS_PVR_416,
	},{
		.subvendor = 0x17de,
		.subdevice = 0x08a6,
		.card      = CX88_BOARD_KWORLD_DVB_T,
	},{
		.subvendor = 0x18ac,
		.subdevice = 0xd810,
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_3_GOLD,
	},{
		.subvendor = 0x18AC,
		.subdevice = 0xDB00,
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_DVB_T1,
 	},{
		.subvendor = 0x0070,
		.subdevice = 0x9002,
		.card      = CX88_BOARD_HAUPPAUGE_DVB_T1,
 	},{
		.subvendor = 0x14f1,
		.subdevice = 0x0187,
		.card      = CX88_BOARD_CONEXANT_DVB_T1,
 	},{
		.subvendor = 0x1540,
		.subdevice = 0x2580,
		.card      = CX88_BOARD_PROVIDEO_PV259,
	},{
		.subvendor = 0x18AC,
		.subdevice = 0xDB10,
		.card      = CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_PLUS,
	},{
                .subvendor = 0x1554,
                .subdevice = 0x4811,
                .card      = CX88_BOARD_PIXELVIEW,
	},{
		.subvendor = 0x7063,
		.subdevice = 0x3000, /* HD-3000 card */
		.card      = CX88_BOARD_PCHDTV_HD3000,
	},{
		.subvendor = 0x17DE,
		.subdevice = 0xA8A6,
		.card      = CX88_BOARD_DNTV_LIVE_DVB_T,
	},{
		.subvendor = 0x0070,
		.subdevice = 0x2801,
		.card      = CX88_BOARD_HAUPPAUGE_ROSLYN,
	},{
		.subvendor = 0x14F1,
		.subdevice = 0x0342,
		.card      = CX88_BOARD_DIGITALLOGIC_MEC,
	},{
		.subvendor = 0x10fc,
		.subdevice = 0xd035,
		.card      = CX88_BOARD_IODATA_GVBCTV7E,
	}
};
const unsigned int cx88_idcount = ARRAY_SIZE(cx88_subids);

/* ----------------------------------------------------------------------- */
/* some leadtek specific stuff                                             */

static void __devinit leadtek_eeprom(struct cx88_core *core, u8 *eeprom_data)
{
	/* This is just for the "Winfast 2000XP Expert" board ATM; I don't have data on
	 * any others.
	 *
	 * Byte 0 is 1 on the NTSC board.
	 */

	if (eeprom_data[4] != 0x7d ||
	    eeprom_data[5] != 0x10 ||
	    eeprom_data[7] != 0x66) {
		printk(KERN_WARNING "%s: Leadtek eeprom invalid.\n",
		       core->name);
		return;
	}

	core->has_radio  = 1;
	core->tuner_type = (eeprom_data[6] == 0x13) ? 43 : 38;

	printk(KERN_INFO "%s: Leadtek Winfast 2000XP Expert config: "
	       "tuner=%d, eeprom[0]=0x%02x\n",
	       core->name, core->tuner_type, eeprom_data[0]);
}


/* ----------------------------------------------------------------------- */

static void hauppauge_eeprom(struct cx88_core *core, u8 *eeprom_data)
{
	struct tveeprom tv;

	tveeprom_hauppauge_analog(&tv, eeprom_data);
	core->tuner_type = tv.tuner_type;
	core->has_radio  = tv.has_radio;
}

static int hauppauge_eeprom_dvb(struct cx88_core *core, u8 *ee)
{
	int model;
	int tuner;

	/* Make sure we support the board model */
	model = ee[0x1f] << 24 | ee[0x1e] << 16 | ee[0x1d] << 8 | ee[0x1c];
	switch(model) {
	case 90002:
	case 90500:
	case 90501:
		/* known */
		break;
	default:
		printk("%s: warning: unknown hauppauge model #%d\n",
		       core->name, model);
		break;
	}

	/* Make sure we support the tuner */
	tuner = ee[0x2d];
	switch(tuner) {
	case 0x4B: /* dtt 7595 */
	case 0x4C: /* dtt 7592 */
		break;
	default:
		printk("%s: error: unknown hauppauge tuner 0x%02x\n",
		       core->name, tuner);
		return -ENODEV;
	}
	printk(KERN_INFO "%s: hauppauge eeprom: model=%d, tuner=%d\n",
	       core->name, model, tuner);
	return 0;
}

/* ----------------------------------------------------------------------- */
/* some GDI (was: Modular Technology) specific stuff                       */

static struct {
	int  id;
	int  fm;
	char *name;
} gdi_tuner[] = {
	[ 0x01 ] = { .id   = TUNER_ABSENT,
		     .name = "NTSC_M" },
	[ 0x02 ] = { .id   = TUNER_ABSENT,
		     .name = "PAL_B" },
	[ 0x03 ] = { .id   = TUNER_ABSENT,
		     .name = "PAL_I" },
	[ 0x04 ] = { .id   = TUNER_ABSENT,
		     .name = "PAL_D" },
	[ 0x05 ] = { .id   = TUNER_ABSENT,
		     .name = "SECAM" },

	[ 0x10 ] = { .id   = TUNER_ABSENT,
		     .fm   = 1,
		     .name = "TEMIC_4049" },
	[ 0x11 ] = { .id   = TUNER_TEMIC_4136FY5,
		     .name = "TEMIC_4136" },
	[ 0x12 ] = { .id   = TUNER_ABSENT,
		     .name = "TEMIC_4146" },

	[ 0x20 ] = { .id   = TUNER_PHILIPS_FQ1216ME,
		     .fm   = 1,
		     .name = "PHILIPS_FQ1216_MK3" },
	[ 0x21 ] = { .id   = TUNER_ABSENT, .fm = 1,
		     .name = "PHILIPS_FQ1236_MK3" },
	[ 0x22 ] = { .id   = TUNER_ABSENT,
		     .name = "PHILIPS_FI1236_MK3" },
	[ 0x23 ] = { .id   = TUNER_ABSENT,
		     .name = "PHILIPS_FI1216_MK3" },
};

static void gdi_eeprom(struct cx88_core *core, u8 *eeprom_data)
{
	char *name = (eeprom_data[0x0d] < ARRAY_SIZE(gdi_tuner))
		? gdi_tuner[eeprom_data[0x0d]].name : NULL;

	printk(KERN_INFO "%s: GDI: tuner=%s\n", core->name,
	       name ? name : "unknown");
	if (NULL == name)
		return;
	core->tuner_type = gdi_tuner[eeprom_data[0x0d]].id;
	core->has_radio  = gdi_tuner[eeprom_data[0x0d]].fm;
}

/* ----------------------------------------------------------------------- */

void cx88_card_list(struct cx88_core *core, struct pci_dev *pci)
{
	int i;

	if (0 == pci->subsystem_vendor &&
	    0 == pci->subsystem_device) {
		printk("%s: Your board has no valid PCI Subsystem ID and thus can't\n"
		       "%s: be autodetected.  Please pass card=<n> insmod option to\n"
		       "%s: workaround that.  Redirect complaints to the vendor of\n"
		       "%s: the TV card.  Best regards,\n"
		       "%s:         -- tux\n",
		       core->name,core->name,core->name,core->name,core->name);
	} else {
		printk("%s: Your board isn't known (yet) to the driver.  You can\n"
		       "%s: try to pick one of the existing card configs via\n"
		       "%s: card=<n> insmod option.  Updating to the latest\n"
		       "%s: version might help as well.\n",
		       core->name,core->name,core->name,core->name);
	}
	printk("%s: Here is a list of valid choices for the card=<n> insmod option:\n",
	       core->name);
	for (i = 0; i < cx88_bcount; i++)
		printk("%s:    card=%d -> %s\n",
		       core->name, i, cx88_boards[i].name);
}

void cx88_card_setup(struct cx88_core *core)
{
	static u8 eeprom[128];

	if (0 == core->i2c_rc) {
		core->i2c_client.addr = 0xa0 >> 1;
		tveeprom_read(&core->i2c_client,eeprom,sizeof(eeprom));
	}

	switch (core->board) {
	case CX88_BOARD_HAUPPAUGE:
	case CX88_BOARD_HAUPPAUGE_ROSLYN:
		if (0 == core->i2c_rc)
			hauppauge_eeprom(core,eeprom+8);
		break;
	case CX88_BOARD_GDI:
		if (0 == core->i2c_rc)
			gdi_eeprom(core,eeprom);
		break;
	case CX88_BOARD_WINFAST2000XP_EXPERT:
		if (0 == core->i2c_rc)
			leadtek_eeprom(core,eeprom);
		break;
	case CX88_BOARD_HAUPPAUGE_DVB_T1:
		if (0 == core->i2c_rc)
			hauppauge_eeprom_dvb(core,eeprom);
		break;
	case CX88_BOARD_DVICO_FUSIONHDTV_DVB_T1:
	case CX88_BOARD_DVICO_FUSIONHDTV_DVB_T_PLUS:
		/* GPIO0:0 is hooked to mt352 reset pin */
		cx_set(MO_GP0_IO, 0x00000101);
		cx_clear(MO_GP0_IO, 0x00000001);
		msleep(1);
		cx_set(MO_GP0_IO, 0x00000101);
		break;
	case CX88_BOARD_KWORLD_DVB_T:
	case CX88_BOARD_DNTV_LIVE_DVB_T:
		cx_set(MO_GP0_IO, 0x00000707);
		cx_set(MO_GP2_IO, 0x00000101);
		cx_clear(MO_GP2_IO, 0x00000001);
		msleep(1);
		cx_clear(MO_GP0_IO, 0x00000007);
		cx_set(MO_GP2_IO, 0x00000101);
		break;
	}
	if (cx88_boards[core->board].radio.type == CX88_RADIO)
		core->has_radio = 1;
}

/* ------------------------------------------------------------------ */

EXPORT_SYMBOL(cx88_boards);
EXPORT_SYMBOL(cx88_bcount);
EXPORT_SYMBOL(cx88_subids);
EXPORT_SYMBOL(cx88_idcount);
EXPORT_SYMBOL(cx88_card_list);
EXPORT_SYMBOL(cx88_card_setup);

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
