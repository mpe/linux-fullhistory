/*
    bttv-cards.c  --  this file has card-specific stuff


    bttv - Bt848 frame grabber driver

    Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
                           & Marcus Metzler (mocm@thp.uni-koeln.de)
    (c) 1999,2000 Gerd Knorr <kraxel@goldbach.in-berlin.de>

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

#define __NO_VERSION__ 1

#include <linux/version.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kmod.h>
#include <linux/init.h>

#include <asm/io.h>

#include "bttv.h"
#include "tuner.h"

/* fwd decl */
static void hauppauge_eeprom(struct bttv *btv);
static void hauppauge_boot_msp34xx(struct bttv *btv);
static void init_PXC200(struct bttv *btv);
static void init_tea5757(struct bttv *btv);

MODULE_PARM(card,"1-4i");
MODULE_PARM(pll,"1-4i");
MODULE_PARM(autoload,"i");

static unsigned int card[4] = { -1, -1, -1, -1 };
static unsigned int pll[4]  = { -1, -1, -1, -1 };
#ifdef MODULE
static unsigned int autoload = 1;
#else
static unsigned int autoload = 0;
#endif

/* ----------------------------------------------------------------------- */
/* list of card IDs for bt878+ cards                                       */

static struct CARD {
	unsigned id;
	int cardnr;
	char *name;
} cards[] __devinitdata = {
	{ 0x00011002, BTTV_HAUPPAUGE878,  "ATI TV Wonder" },
	{ 0x00011461, BTTV_AVPHONE98,     "AVerMedia TVPhone98" },
	{ 0x00021461, BTTV_AVERMEDIA98,   "Avermedia TVCapture 98" },
	{ 0x00031461, BTTV_AVPHONE98,     "AVerMedia TVPhone98" },
	{ 0x00041461, BTTV_AVPHONE98,     "AVerMedia TVPhone98" },
	{ 0x10b42636, BTTV_HAUPPAUGE878,  "STB ???" },
	{ 0x1118153b, BTTV_TERRATVALUE,   "Terratec TV Value" },
	{ 0x1123153b, BTTV_TERRATVRADIO,  "Terratec TV/Radio+" },
	{ 0x1200bd11, BTTV_PINNACLERAVE,  "Pinnacle PCTV Rave" },
	{ 0x13eb0070, BTTV_HAUPPAUGE878,  "Hauppauge WinTV" },
#if 0 /* probably wrong */
	{ 0x14610002, BTTV_AVERMEDIA98,   "Avermedia TVCapture 98" },
#endif
	{ 0x18501851, BTTV_CHRONOS_VS2,   "Chronos Video Shuttle II" },
	{ 0x18521852, BTTV_TYPHOON_TVIEW, "Typhoon TView TV/FM Tuner" },
	{ 0x263610b4, BTTV_STB2,          "STB TV PCI FM, P/N 6000704" },
	{ 0x3000144f, BTTV_MAGICTVIEW063, "TView 99 (CPH063)" },
	{ 0x300014ff, BTTV_MAGICTVIEW061, "TView 99 (CPH061)" },
	{ 0x3002144f, BTTV_MAGICTVIEW061, "Askey Magic TView" },
	{ 0x300214ff, BTTV_PHOEBE_TVMAS,  "Phoebe TV Master" },
	{ 0x400a15b0, BTTV_ZOLTRIX_GENIE, "Zoltrix Genie TV" },
	{ 0x402010fc, 0 /* no tvcards entry yet */, "I-O Data Co. GV-BCV3/PCI" },
	{ 0x6606217d, BTTV_WINFAST2000,   "Leadtek WinFast TV 2000" },
	{ 0, -1, NULL }
};

/* ----------------------------------------------------------------------- */
/* array with description for bt848 / bt878 tv/grabber cards               */

struct tvcard bttv_tvcards[] = 
{
	/* 0x00 */
        { " *** UNKNOWN *** ",
          3, 1, 0, 2, 0, { 2, 3, 1, 1}, { 0, 0, 0, 0, 0},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "MIRO PCTV",
          4, 1, 0, 2,15, { 2, 3, 1, 1}, { 2, 0, 0, 0,10},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "Hauppauge old",
          4, 1, 0, 2, 7, { 2, 3, 1, 1}, { 0, 1, 2, 3, 4},0,
	  1,1,0,1,0,0,0,1,  PLL_NONE, -1 },
        { "STB",
          3, 1, 0, 2, 7, { 2, 3, 1, 1}, { 4, 0, 2, 3, 1},0,
	  0,1,1,1,1,0,0,1,  PLL_NONE, -1 },

        { "Intel",
          3, 1, 0, -1, 7, { 2, 3, 1, 1}, { 0, 1, 2, 3, 4},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
	{ "Diamond DTV2000",
          3, 1, 0, 2, 3, { 2, 3, 1, 1}, { 0, 1, 0, 1, 3},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
	{ "AVerMedia TVPhone",
          3, 1, 0, 3,15, { 2, 3, 1, 1}, {12, 4,11,11, 0},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "MATRIX-Vision MV-Delta",
          5, 1, -1, 3, 0, { 2, 3, 1, 0, 0},{0 }, 0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },

	/* 0x08 */
        { "Fly Video II",
          3, 1, 0, 2, 0xc00, { 2, 3, 1, 1},
	  { 0, 0xc00, 0x800, 0x400, 0xc00, 0},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "TurboTV",
          3, 1, 0, 2, 3, { 2, 3, 1, 1}, { 1, 1, 2, 3, 0},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "Hauppauge new (bt878)",
	  4, 1, 0, 2, 7, { 2, 0, 1, 1}, { 0, 1, 2, 3, 4},0,
	  1,1,0,1,0,0,0,1,  PLL_28,   -1 },
        { "MIRO PCTV pro",
	  3, 1, 0, 2, 65551, { 2, 3, 1, 1}, {1,65537, 0, 0,10},0,
       /* 3, 1, 0, 2, 0x3004F, { 2, 3, 1, 1}, {1, 0x10011, 5, 0,10}, 0x3004F, */
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },

	{ "ADS Technologies Channel Surfer TV",
	  3, 1, 2, 2, 15, { 2, 3, 1, 1}, { 13, 14, 11, 7, 0, 0},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "AVerMedia TVCapture 98",
	  3, 4, 0, 2, 15, { 2, 3, 1, 1}, { 13, 14, 11, 7, 0, 0},0,
	  1,1,1,1,0,0,0,1,  PLL_28,   -1 },
        { "Aimslab VHX",
          3, 1, 0, 2, 7, { 2, 3, 1, 1}, { 0, 1, 2, 3, 4},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "Zoltrix TV-Max",
          3, 1, 0, 2,15, { 2, 3, 1, 1}, {0 , 0, 1 , 0, 10},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },

	/* 0x10 */
        { "Pixelview PlayTV (bt878)",
          3, 1, 0, 2, 0x01fe00, { 2, 3, 1, 1},
	  { 0x01c000, 0, 0x018000, 0x014000, 0x002000, 0 },0,
	  1,1,1,1,0,0,0,1,  PLL_28,   -1 },
        { "Leadtek WinView 601",
          3, 1, 0, 2, 0x8300f8, { 2, 3, 1, 1,0},
	  { 0x4fa007,0xcfa007,0xcfa007,0xcfa007,0xcfa007,0xcfa007},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "AVEC Intercapture",
	  3, 2, 0, 2, 0, {2, 3, 1, 1}, {1, 0, 0, 0, 0},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
	{ "LifeView FlyKit w/o Tuner",
	  3, 1, -1, -1, 0x8dff00, { 2, 3, 1, 1}, { 0 },0,
	  0,0,0,0,0,0,0,1,  PLL_NONE, -1 },

        { "CEI Raffles Card",
	  3, 3, 0, 2, 0, {2, 3, 1, 1}, {0, 0, 0, 0 ,0},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
	{ "Lucky Star Image World ConferenceTV",
	  3, 1, 0, 2, 0x00fffe07, { 2, 3, 1, 1}, { 131072, 1, 1638400, 3, 4},0,
	  1,1,1,1,0,0,0,1,  PLL_28,   TUNER_PHILIPS_PAL_I },
	{ "Phoebe Tv Master + FM",
	  3, 1, 0, 2, 0xc00, { 2, 3, 1, 1},{0, 1, 0x800, 0x400, 0xc00, 0},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "Modular Technology MM205 PCTV, bt878",
	  2, 1, 0, -1, 7, { 2, 3 }, { 0, 0, 0, 0, 0 },0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },

	/* 0x18 */
        { "Askey/Typhoon/Anubis Magic TView CPH051/061 (bt878)",
	  3, 1, 0, 2, 0xe00, { 2, 3, 1, 1}, {0x400, 0x400, 0x400, 0x400, 0},0,
	  1,1,1,1,0,0,0,1,  PLL_28,   -1 },
        { "Terratec/Vobis TV-Boostar",
          3, 1, 0, 2, 16777215 , { 2, 3, 1, 1}, { 131072, 1, 1638400, 3,4},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "Newer Hauppauge WinCam (bt878)",
 	  4, 1, 0, 3, 7, { 2, 0, 1, 1}, { 0, 1, 2, 3, 4},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "MAXI TV Video PCI2",
          3, 1, 0, 2, 0xffff, { 2, 3, 1, 1}, { 0, 1, 2, 3, 0xc00},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, TUNER_PHILIPS_SECAM },

        { "Terratec TerraTV+",
          3, 1, 0, 2, 0x70000, { 2, 3, 1, 1}, 
          { 0x20000, 0x30000, 0x00000, 0x10000, 0x40000},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "Imagenation PXC200",
          5, 1, -1, 4, 0, { 2, 3, 1, 0, 0}, { 0 }, 0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "FlyVideo 98",
          3, 1, 0, 2, 0x8dff00, {2, 3, 1, 1}, 
          { 0, 0x8dff00, 0x8df700, 0x8de700, 0x8dff00, 0 },0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
        { "iProTV",
	  3, 1, 0, 2, 1, { 2, 3, 1, 1}, { 1, 0, 0, 0, 0 },0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },

	/* 0x20 */
	{ "Intel Create and Share PCI",
	  4, 1, 0, 2, 7, { 2, 3, 1, 1}, { 4, 4, 4, 4, 4},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
	{ "Terratec TerraTValue",
	  3, 1, 0, 2, 0xffff00, { 2, 3, 1, 1},
	  { 0x500, 0, 0x300, 0x900, 0x900},0,
	  1,1,1,1,0,0,0,1,  PLL_NONE, -1 },
	{ "Leadtek WinFast 2000",
	  3, 1, 0, 2, 0xfff000, { 2, 3, 1, 1,0},
	  { 0x621000,0x620100,0x621100,0x620000,0xE210000,0x620000},0,
	  1,1,1,1,1,0,0,1,  PLL_28,   -1 },
	{ "Chronos Video Shuttle II",
	  3, 3, 0, 2, 0x1800, { 2, 3, 1, 1}, { 0, 0, 0x1000, 0x1000, 0x0800},0,
	  1,1,1,1,0,0,0,1,  PLL_28,   -1 },

	{ "Typhoon TView TV/FM Tuner",
	  3, 3, 0, 2, 0x1800, { 2, 3, 1, 1}, { 0, 0x800, 0, 0, 0x1800, 0 },0,
	  1,1,1,1,0,0,0,1,  PLL_28,   -1 },
	{ "PixelView PlayTV pro",
          3, 1, 0, 2, 0xff, { 2, 3, 1, 1 },
          { 0x21, 0x20, 0x24, 0x2c, 0x29, 0x29 }, 0,
	  0,0,0,0,0,0,0,1,  PLL_28,   -1 },
	{ "TView99 CPH063",
	  3, 1, 0, 2, 0x551e00, { 2, 3, 1, 1},
	  { 0x551400, 0x551200, 0, 0, 0, 0x551200 }, 0,
	  1,1,1,1,0,0,0,1,  PLL_28, -1 },
	{ "Pinnacle PCTV Rave",
	  3, 1, 0, 2, 0x03000F, { 2, 3, 1, 1}, { 2, 0, 0, 0, 1},0,
	  1,1,1,1,0,0,0,1,  PLL_28, -1 },

        /* 0x28 */
        { "STB2",
          3, 1, 0, 2, 7, { 2, 3, 1, 1}, { 4, 0, 2, 3, 1},0,
	  0,1,1,1,0,1,1,1,  PLL_NONE, -1 },
        { "AVerMedia TVPhone 98",
	  3, 4, 0, 2, 4, { 2, 3, 1, 1}, { 13, 14, 11, 7, 0, 0},0,
	  1,1,1,1,0,0,0,1,  PLL_28,   5 },
        { "ProVideo PV951", /* pic16c54 */
          3, 1, 0, 2, 0, { 2, 3, 1, 1}, { 0, 0, 0, 0, 0},0,
	  0,0,0,0,0,0,0,0,  PLL_28,   1 },
	{ "Little OnAir TV",
	  3, 1, 0, 2, 0xe00b, {2, 3, 1, 1},
	  {0xff9ff6, 0xff9ff6, 0xff1ff7, 0, 0xff3ffc},0,
	  0,0,0,0,0,0,0,0, PLL_NONE, -1 },

	{ "Sigma TVII-FM",
	  2, 1, 0, -1, 3, {2, 3, 1, 1}, {1, 1, 0, 2, 3},0,
	  0,0,0,0,0,0,0,0, PLL_NONE, -1 },	
	{ "MATRIX-Vision MV-Delta 2",
	  5, 1, -1, 3, 0, { 2, 3, 1, 0, 0},{0 }, 0,
	  0,0,0,0,0,0,0,0,  PLL_28, -1 },
	{ "Zoltrix Genie TV",
	  3, 1, 0, 2, 0xbcf03f, { 2, 3, 1, 1},
	  { 0xbc803f, 0, 0xbcb03f, 0, 0xbcb03f}, 0,
	  0,0,0,0,0,0,0,0,  PLL_28, 5 },
	{ "Terratec TV/Radio+", /* Radio ?? */
	  3, 1, 0, 2, 0x1f0000, { 2, 3, 1, 1},
	  { 0xe2ffff, 0, 0, 0, 0xe0ffff, 0xe2ffff },0,
	  0,0,0,0,0,0,0,0,  PLL_35,  1 },

	/* 0x30 */
	{ "Dynalink Magic TView ",
	  3, 1, 0, 2, 15, { 2, 3, 1, 1}, {2,0,0,0,1},0,
	  1,1,1,1,0,0,0,1,  PLL_28,  -1 },
};
const int bttv_num_tvcards = (sizeof(bttv_tvcards)/sizeof(struct tvcard));

/* ----------------------------------------------------------------------- */

static unsigned char eeprom_data[256];

static void __devinit bttv_dump_eeprom(struct bttv *btv,int addr)
{
	int i;

	if (bttv_verbose < 2)
		return;
	/* for debugging: dump eeprom to syslog */
	printk(KERN_DEBUG "bttv%d: dump eeprom @ 0x%02x\n",btv->nr,addr);
	for (i = 0; i < 256;) {
		printk(KERN_DEBUG "  %02x:",i);
		do {
			printk(" %02x",eeprom_data[i++]);
		} while (i % 16);
		printk("\n");
	}
}

static int __devinit bttv_idcard_eeprom(struct bttv *btv)
{
	unsigned id;
	int i,n;

	id = (eeprom_data[252] << 24) |
		(eeprom_data[253] << 16) |
		(eeprom_data[254] << 8)  |
		(eeprom_data[255]);
	if (id == 0 || id == 0xffffffff)
	    return -1;

	/* look for the card */
	btv->cardid = id;
	for (n = -1, i = 0; cards[i].id != 0; i++)
		if (cards[i].id  == id)
			n = i;

	if (n != -1) {
		/* found it */
		printk(KERN_INFO "bttv%d: card id: %s (0x%08x) => card=%d\n",
		       btv->nr,cards[n].name,id,cards[n].cardnr);
		return cards[n].cardnr;
	} else {
		/* 404 */
		printk(KERN_INFO "bttv%d: id: unknown (0x%08x)\n",
		       btv->nr, id);
		printk(KERN_INFO "please mail id, board name and "
		       "the correct card= insmod option to "
		       "kraxel@goldbach.in-berlin.de\n");
		return -1;
	}
}

#ifndef HAVE_TVAUDIO
/* can tda9855.c handle this too maybe? */
static void __devinit init_tda9840(struct bttv *btv)
{
        /* Horrible Hack */
        bttv_I2CWrite(btv, I2C_TDA9840, TDA9840_SW, 0x2a, 1);  /* sound mode switching */
        /* 00 - mute
           10 - mono / averaged stereo
           2a - stereo
           12 - dual A
           1a - dual AB
           16 - dual BA
           1e - dual B
           7a - external */
}
#endif

void __devinit bttv_idcard(struct bttv *btv)
{
	int type,eeprom = 0;

	btwrite(0, BT848_GPIO_OUT_EN);

	/* try to autodetect the card */
	/* many bt878 cards have a eeprom @ 0xa0 => read ID
	   and try to identify it */
	if (bttv_I2CRead(btv, I2C_HAUPEE, "eeprom") >= 0) {
		eeprom = 0xa0;
		bttv_readee(btv,eeprom_data,0xa0);
		bttv_dump_eeprom(btv,0xa0); /* DEBUG */
		type = bttv_idcard_eeprom(btv);
		if (-1 != type) {
			btv->type = type;
		} else if (btv->id <= 849) {
			/* for unknown bt848, assume old Hauppauge */
			btv->type=BTTV_HAUPPAUGE;
		}
		
	/* STB cards have a eeprom @ 0xae (old bt848) */
	} else if (bttv_I2CRead(btv, I2C_STBEE, "eeprom")>=0) {
		btv->type=BTTV_STB;
	}

	/* let the user override the autodetected type */
	if (card[btv->nr] >= 0 && card[btv->nr] < bttv_num_tvcards)
		btv->type=card[btv->nr];
	
	/* print which card config we are using */
	sprintf(btv->video_dev.name,"BT%d%s(%.23s)",
		btv->id,
		(btv->id==848 && btv->revision==0x12) ? "A" : "",
		bttv_tvcards[btv->type].name);
	printk(KERN_INFO "bttv%d: model: %s [%s]\n",btv->nr,btv->video_dev.name,
	       (card[btv->nr] >= 0 && card[btv->nr] < bttv_num_tvcards) ?
	       "insmod option" : "autodetected");
	
        /* board specific initialisations */
        if (btv->type == BTTV_MIRO || btv->type == BTTV_MIROPRO) {
                /* auto detect tuner for MIRO cards */
                btv->tuner_type=((btread(BT848_GPIO_DATA)>>10)-1)&7;
#if 0
		if (btv->type == BTTV_MIROPRO) {
			if (bttv_verbose)
				printk(KERN_INFO "Initializing TEA5757...\n");
			init_tea5757(btv);
		}
#endif
        }
        if (btv->type == BTTV_HAUPPAUGE || btv->type == BTTV_HAUPPAUGE878) {
		/* pick up some config infos from the eeprom */
		if (0xa0 != eeprom) {
			eeprom = 0xa0;
			bttv_readee(btv,eeprom_data,0xa0);
		}
                hauppauge_eeprom(btv);
                hauppauge_boot_msp34xx(btv);
        }
 	if (btv->type == BTTV_PXC200)
		init_PXC200(btv);

	/* pll configuration */
        if (!(btv->id==848 && btv->revision==0x11)) {
		/* defaults from card list */
		if (PLL_28 == bttv_tvcards[btv->type].pll) {
			btv->pll.pll_ifreq=28636363;
			btv->pll.pll_crystal=BT848_IFORM_XT0;
		}
		/* insmod options can override */
                switch (pll[btv->nr]) {
                case 0: /* none */
			btv->pll.pll_crystal = 0;
			btv->pll.pll_ifreq   = 0;
			btv->pll.pll_ofreq   = 0;
                        break;
                case 1: /* 28 MHz */
                        btv->pll.pll_ifreq   = 28636363;
			btv->pll.pll_ofreq   = 0;
                        btv->pll.pll_crystal=BT848_IFORM_XT0;
                        break;
                case 2: /* 35 MHz */
                        btv->pll.pll_ifreq   = 35468950;
			btv->pll.pll_ofreq   = 0;
                        btv->pll.pll_crystal=BT848_IFORM_XT1;
                        break;
                }
        }
	

	/* tuner configuration */
 	if (-1 != bttv_tvcards[btv->type].tuner_type)
                btv->tuner_type = bttv_tvcards[btv->type].tuner_type;
	if (btv->tuner_type != -1)
		bttv_call_i2c_clients(btv,TUNER_SET_TYPE,&btv->tuner_type);

	/* try to detect audio/fader chips */
	if (bttv_tvcards[btv->type].msp34xx &&
	    bttv_I2CRead(btv, I2C_MSP3400, "MSP34xx") >=0) {
		if (autoload)
			request_module("msp3400");
	}

#ifndef HAVE_TVAUDIO
	if (bttv_tvcards[btv->type].tda8425 &&
	    bttv_I2CRead(btv, I2C_TDA8425, "TDA8425") >=0) {
		if (autoload)
			request_module("tda8425");
        }

	if (bttv_tvcards[btv->type].tda9840 &&
	    bttv_I2CRead(btv, I2C_TDA9840, "TDA9840") >=0) {
		init_tda9840(btv);
                btv->audio_chip = TDA9840;
		/* move this to a module too? */
		init_tda9840(btv);
	}

	if (bttv_tvcards[btv->type].tda985x &&
	    bttv_I2CRead(btv, I2C_TDA9850, "TDA985x") >=0) {
		if (autoload)
			request_module("tda985x");
	}
	if (bttv_tvcards[btv->type].tea63xx) {
		if (autoload)
			request_module("tea6300");
	}
#else
	if (bttv_tvcards[btv->type].tda8425 ||
	    bttv_tvcards[btv->type].tda9840 ||
	    bttv_tvcards[btv->type].tda985x ||
	    bttv_tvcards[btv->type].tea63xx) {
		if (autoload)
			request_module("tvaudio");
	}
#endif

	if (bttv_tvcards[btv->type].tda9875 &&
	    bttv_I2CRead(btv, I2C_TDA9875, "TDA9875") >=0) {
		if (autoload)
			request_module("tda9875");
	}

	if (bttv_tvcards[btv->type].tda7432 &&
	    bttv_I2CRead(btv, I2C_TDA7432, "TDA7432") >=0) {
		if (autoload)
			request_module("tda7432");
	}


	if (bttv_tvcards[btv->type].tea64xx) {
		if (autoload)
			request_module("tea6420");
	}

	if (bttv_tvcards[btv->type].tuner != -1) {
		if (autoload)
			request_module("tuner");
	}
}


/* ----------------------------------------------------------------------- */
/* some hauppauge specific stuff                                           */

static struct HAUPPAUGE_TUNER 
{
        int  id;
        char *name;
} 
hauppauge_tuner[] __devinitdata = 
{
        { TUNER_ABSENT,        "" },
        { TUNER_ABSENT,        "External" },
        { TUNER_ABSENT,        "Unspecified" },
        { TUNER_ABSENT,        "Philips FI1216" },
        { TUNER_PHILIPS_SECAM, "Philips FI1216MF" },
        { TUNER_PHILIPS_NTSC,  "Philips FI1236" },
        { TUNER_ABSENT,        "Philips FI1246" },
        { TUNER_ABSENT,        "Philips FI1256" },
        { TUNER_PHILIPS_PAL,   "Philips FI1216 MK2" },
        { TUNER_PHILIPS_SECAM, "Philips FI1216MF MK2" },
        { TUNER_PHILIPS_NTSC,  "Philips FI1236 MK2" },
        { TUNER_PHILIPS_PAL_I, "Philips FI1246 MK2" },
        { TUNER_ABSENT,        "Philips FI1256 MK2" },
        { TUNER_ABSENT,        "Temic 4032FY5" },
        { TUNER_TEMIC_PAL,     "Temic 4002FH5" },
        { TUNER_TEMIC_PAL_I,   "Temic 4062FY5" },
        { TUNER_ABSENT,        "Philips FR1216 MK2" },
        { TUNER_PHILIPS_SECAM, "Philips FR1216MF MK2" },
        { TUNER_PHILIPS_NTSC,  "Philips FR1236 MK2" },
        { TUNER_PHILIPS_PAL_I, "Philips FR1246 MK2" },
        { TUNER_ABSENT,        "Philips FR1256 MK2" },
        { TUNER_PHILIPS_PAL,   "Philips FM1216" },
        { TUNER_ABSENT,        "Philips FM1216MF" },
        { TUNER_PHILIPS_NTSC,  "Philips FM1236" },
        { TUNER_PHILIPS_PAL_I, "Philips FM1246" },
        { TUNER_ABSENT,        "Philips FM1256" },
        { TUNER_TEMIC_4036FY5_NTSC,  "Temic 4036FY5" },
        { TUNER_ABSENT,        "Samsung TCPN9082D" },
        { TUNER_ABSENT,        "Samsung TCPM9092P" },
        { TUNER_TEMIC_PAL,     "Temic 4006FH5" },
        { TUNER_ABSENT,        "Samsung TCPN9085D" },
        { TUNER_ABSENT,        "Samsung TCPB9085P" },
        { TUNER_ABSENT,        "Samsung TCPL9091P" },
        { TUNER_ABSENT,        "Temic 4039FR5" },
        { TUNER_ABSENT,        "Philips FQ1216 ME" },
        { TUNER_TEMIC_PAL_I,   "Temic 4066FY5" },
        { TUNER_ABSENT,        "Philips TD1536" },
        { TUNER_ABSENT,        "Philips TD1536D" },
        { TUNER_ABSENT,        "Philips FMR1236" },
        { TUNER_ABSENT,        "Philips FI1256MP" },
        { TUNER_ABSENT,        "Samsung TCPQ9091P" },
        { TUNER_ABSENT,        "Temic 4006FN5" },
        { TUNER_ABSENT,        "Temic 4009FR5" },
        { TUNER_ABSENT,        "Temic 4046FM5" },
};

static void __devinit hauppauge_eeprom(struct bttv *btv)
{
        if (eeprom_data[9] < sizeof(hauppauge_tuner)/sizeof(struct HAUPPAUGE_TUNER)) 
        {
                btv->tuner_type = hauppauge_tuner[eeprom_data[9]].id;
		if (bttv_verbose)
			printk("bttv%d: Hauppauge eeprom: tuner=%s (%d)\n",btv->nr,
			       hauppauge_tuner[eeprom_data[9]].name,btv->tuner_type);
        }
}

static void __devinit hauppauge_boot_msp34xx(struct bttv *btv)
{
	int i;

        /* reset/enable the MSP on some Hauppauge cards */
        /* Thanks to Kyösti Mälkki (kmalkki@cc.hut.fi)! */
        btaor(32, ~32, BT848_GPIO_OUT_EN);
        btaor(0, ~32, BT848_GPIO_DATA);
        udelay(2500);
        btaor(32, ~32, BT848_GPIO_DATA);

	if (bttv_verbose)
		printk("bttv%d: Hauppauge msp34xx: reset line init\n",btv->nr);

	/* look if the msp3400 driver is already registered */
	for (i = 0; i < I2C_CLIENTS_MAX; i++) {
		if (btv->i2c_clients[i] != NULL &&
		    btv->i2c_clients[i]->driver->id == I2C_DRIVERID_MSP3400) {
			return;
		}
	}

	/* if not: look for the chip ... */
	if (bttv_I2CRead(btv, I2C_MSP3400, "MSP34xx")) {
		/* ... if found re-register to trigger a i2c bus rescan, */
		/*     this time with the msp34xx chip activated */
		i2c_bit_del_bus(&btv->i2c_adap);
		i2c_bit_add_bus(&btv->i2c_adap);
	}
}


/* ----------------------------------------------------------------------- */
/*  Imagenation L-Model PXC200 Framegrabber */
/*  This is basically the same procedure as 
 *  used by Alessandro Rubini in his pxc200 
 *  driver, but using BTTV functions */

static void __devinit init_PXC200(struct bttv *btv)
{
	static const int vals[] = { 0x08, 0x09, 0x0a, 0x0b, 0x0d, 0x0d,
				    0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
				    0x00 };
	int i,tmp;

	/* Initialise GPIO-connevted stuff */
	btwrite(1<<13,BT848_GPIO_OUT_EN); /* Reset pin only */
	btwrite(0,BT848_GPIO_DATA);
	udelay(3);
	btwrite(1<<13,BT848_GPIO_DATA);
	/* GPIO inputs are pulled up, so no need to drive 
	 * reset pin any longer */
	btwrite(0,BT848_GPIO_OUT_EN);

	/*  we could/should try and reset/control the AD pots? but
	    right now  we simply  turned off the crushing.  Without
	    this the AGC drifts drifts
	    remember the EN is reverse logic -->
	    setting BT848_ADC_AGC_EN disable the AGC
	    tboult@eecs.lehigh.edu
	*/
	btwrite(BT848_ADC_RESERVED|BT848_ADC_AGC_EN, BT848_ADC);
	
	/*	Initialise MAX517 DAC */
	printk(KERN_INFO "Setting DAC reference voltage level ...\n");
	bttv_I2CWrite(btv,0x5E,0,0x80,1);
	
	/*	Initialise 12C508 PIC */
	/*	The I2CWrite and I2CRead commmands are actually to the 
	 *	same chips - but the R/W bit is included in the address
	 *	argument so the numbers are different */
	
	printk(KERN_INFO "Initialising 12C508 PIC chip ...\n");

	for (i = 0; i < sizeof(vals)/sizeof(int); i++) {
		tmp=bttv_I2CWrite(btv,0x1E,vals[i],0,1);
		printk(KERN_INFO "I2C Write(0x08) = %i\nI2C Read () = %x\n\n",
		       tmp,bttv_I2CRead(btv,0x1F,NULL));
	}
	printk(KERN_INFO "PXC200 Initialised.\n");
}

/* ----------------------------------------------------------------------- */
/* Miro Pro radio stuff -- the tea5757 is connected to some GPIO ports     */
/*
 * Copyright (c) 1999 Csaba Halasz <qgehali@uni-miskolc.hu>
 * This code is placed under the terms of the GNU General Public License
 *
 * Brutally hacked by Dan Sheridan <dan.sheridan@contact.org.uk> djs52 8/3/00
 */

/* bus bits on the GPIO port */
#define TEA_WE			6
#define TEA_DATA		9
#define TEA_CLK			8
#define TEA_MOST		7

#define BUS_LOW(bit) 	btand(~(1<<TEA_##bit), BT848_GPIO_DATA)
#define BUS_HIGH(bit)	btor((1<<TEA_##bit), BT848_GPIO_DATA)
#define BUS_IN(bit)	((btread(BT848_GPIO_DATA) >> TEA_##bit) & 1)

/* TEA5757 register bits */
#define TEA_FREQ		0:14
#define TEA_BUFFER		15:15

#define TEA_SIGNAL_STRENGTH	16:17

#define TEA_PORT1		18:18
#define TEA_PORT0		19:19

#define TEA_BAND		20:21
#define TEA_BAND_FM		0
#define TEA_BAND_MW		1
#define TEA_BAND_LW		2
#define TEA_BAND_SW		3

#define TEA_MONO		22:22
#define TEA_ALLOW_STEREO	0
#define TEA_FORCE_MONO		1

#define TEA_SEARCH_DIRECTION	23:23
#define TEA_SEARCH_DOWN		0
#define TEA_SEARCH_UP		1

#define TEA_STATUS		24:24
#define TEA_STATUS_TUNED	0
#define TEA_STATUS_SEARCHING	1

/* Low-level stuff */
static int tea_read(struct bttv *btv)
{
	int value = 0;
	long timeout;
	int i;
	
	/* better safe than sorry */
	btaor((1<<TEA_CLK) | (1<<TEA_WE), ~((1<<TEA_CLK) | (1<<TEA_DATA) | (1<<TEA_WE) | (1<<TEA_MOST)), BT848_GPIO_OUT_EN);
	
	BUS_LOW(WE);
	BUS_LOW(CLK);
	
	udelay(10);
	for(timeout = jiffies + 10 * HZ;
	    BUS_IN(DATA) == 1 && time_before(jiffies, timeout);
	    schedule());	/* 10 s */
	if (BUS_IN(DATA) == 1) {
		printk("tea5757: read timeout\n");
		return -1;
	}
	for(timeout = jiffies + HZ/5;
	    BUS_IN(MOST) == 1 && time_before(jiffies, timeout);
	    schedule());	/* 0.2 s */
	if (bttv_debug) printk("tea5757:");
	for(i = 0; i < 24; i++)
	{
		udelay(10);
		BUS_HIGH(CLK);
		udelay(10);
		if (bttv_debug) printk("%c", (BUS_IN(MOST) == 0)?'T':'-');
		BUS_LOW(CLK);
		value <<= 1;					
		value |= (BUS_IN(DATA) == 0)?0:1;	/* MSB first */
		if (bttv_debug) printk("%c", (BUS_IN(MOST) == 0)?'S':'M');
	}
	if (bttv_debug) printk("\ntea5757: read 0x%X\n", value);
	return value;
}

static int tea_write(struct bttv *btv, int value)
{
	int i;
	int reg = value;
	
	btaor((1<<TEA_CLK) | (1<<TEA_WE) | (1<<TEA_DATA), ~((1<<TEA_CLK) | (1<<TEA_DATA) | (1<<TEA_WE) | (1<<TEA_MOST)), BT848_GPIO_OUT_EN);
	if (bttv_debug) printk("tea5757: write 0x%X\n", value);
	BUS_LOW(CLK);
	BUS_HIGH(WE);
	for(i = 0; i < 25; i++)
	{
		if (reg & 0x1000000)
			BUS_HIGH(DATA);
		else
			BUS_LOW(DATA);
		reg <<= 1;
		BUS_HIGH(CLK);
		udelay(10);
		BUS_LOW(CLK);
		udelay(10);
	}
	BUS_LOW(WE);	/* unmute !!! */
	return 0;
}

void tea5757_set_freq(struct bttv *btv, unsigned short freq)
{
	tea_write(btv, 5 * freq + 0x358); /* add 10.7MHz (see docs) */
	if (bttv_debug) tea_read(btv);
}

void init_tea5757(struct bttv *btv)
{
	BUS_LOW(CLK);
	BUS_LOW(WE); /* just to be on the safe side... */

	/* software CLK (unused) */
	btaor(0, BT848_GPIO_DMA_CTL_GPCLKMODE, BT848_GPIO_DMA_CTL);
	/* normal mode for GPIO */
	btaor(0, BT848_GPIO_DMA_CTL_GPIOMODE, BT848_GPIO_DMA_CTL);
}

/* ----------------------------------------------------------------------- */
/* winview                                                                 */

void winview_setvol(struct bttv *btv, struct video_audio *v)
{
	/* PT2254A programming Jon Tombs, jon@gte.esi.us.es */
	int bits_out, loops, vol, data;
	
	/* 32 levels logarithmic */
	vol = 32 - ((v->volume>>11));
	/* units */
	bits_out = (PT2254_DBS_IN_2>>(vol%5));
	/* tens */
	bits_out |= (PT2254_DBS_IN_10>>(vol/5));
	bits_out |= PT2254_L_CHANEL | PT2254_R_CHANEL;
	data = btread(BT848_GPIO_DATA);
	data &= ~(WINVIEW_PT2254_CLK| WINVIEW_PT2254_DATA|
		  WINVIEW_PT2254_STROBE);
	for (loops = 17; loops >= 0 ; loops--) {
		if (bits_out & (1<<loops))
			data |=  WINVIEW_PT2254_DATA;
		else
			data &= ~WINVIEW_PT2254_DATA;
		btwrite(data, BT848_GPIO_DATA);
		udelay(5);
		data |= WINVIEW_PT2254_CLK;
		btwrite(data, BT848_GPIO_DATA);
		udelay(5);
		data &= ~WINVIEW_PT2254_CLK;
		btwrite(data, BT848_GPIO_DATA);
	}
	data |=  WINVIEW_PT2254_STROBE;
	data &= ~WINVIEW_PT2254_DATA;
	btwrite(data, BT848_GPIO_DATA);
	udelay(10);                     
	data &= ~WINVIEW_PT2254_STROBE;
	btwrite(data, BT848_GPIO_DATA);
}

/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */
