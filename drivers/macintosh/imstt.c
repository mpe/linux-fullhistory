/*
 * imstt.c: Console support for PowerMac "imstt" display adaptor.
 *
 * Copyright (C) 1997 Sigurdur Asgeirsson
 * Modified by Danilo Beuche 1997
 *	
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/string.h>
#include <linux/vc_ioctl.h>
#include <linux/pci.h>
#include <linux/nvram.h>
#include <asm/prom.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/pci-bridge.h>
#include <asm/init.h>
#include <linux/selection.h>
#include <linux/vt_kern.h>
#include "pmac-cons.h"
#include "imstt.h"


enum { 
  IBMRAMDAC = 0x00,
  TVPRAMDAC  = 0x01
};


// IMS TWIN TURBO 
enum
{
  S1SA		=  0, /* 0x00 */
  S2SA		=  1, /* 0x04 */
  SP		=  2, /* 0x08 */
  DSA		=  3, /* 0x0C */
  CNT		=  4, /* 0x10 */
  DP_OCTRL	=  5, /* 0x14 */
  BLTCTL	= 10, /* 0x28 */
		
  //	Scan Timing Generator Registers
  HES		= 12, /* 0x30 */
  HEB		= 13, /* 0x34 */
  HSB		= 14, /* 0x38 */
  HT		= 15, /* 0x3C */
  VES		= 16, /* 0x40 */
  VEB		= 17, /* 0x44 */
  VSB		= 18, /* 0x48 */
  VT		= 19, /* 0x4C */
  HCIV		= 20, /* 0x50 */
  VCIV		= 21, /* 0x54 */
  TCDR		= 22, /* 0x58 */
  VIL		= 23, /* 0x5C */
  STGCTL	= 24, /* 0x60 */
	
  //	Screen Refresh Generator Registers
  SSR		= 25, /* 0x64 */
  HRIR		= 26, /* 0x68 */
  SPR		= 27, /* 0x6C */
  CMR		= 28, /* 0x70 */
  SRGCTL	= 29, /* 0x74 */
		
  //	RAM Refresh Generator Registers
  RRCIV		= 30, /* 0x78 */
  RRSC		= 31, /* 0x7C */
  RRCR		= 34, /* 0x88 */
		
  //	System Registers
  GIOE		= 32, /* 0x80 */
  GIO		= 33, /* 0x84 */
  SCR		= 35, /* 0x8C */
  SSTATUS	= 36, /* 0x90 */
  PRC		= 37, /* 0x94 */

#if 0	
  //	PCI Registers
  DVID		= 0x00000000L,
  SC		= 0x00000004L,
  CCR		= 0x00000008L,
  OG		= 0x0000000CL,
  BARM		= 0x00000010L,
  BARER		= 0x00000030L,
#endif
};


// IBM RAMDAC
enum
{
 PADDRW		= 0x00,
 PDATA		= 0x04,
 PPMASK		= 0x08,
 PADDRR		= 0x0C,
 PIDXLO		= 0x10,	
 PIDXHI		= 0x14,	
 PIDXDATA	= 0x18,
 PIDXCTL	= 0x1C,

 PPIXREP	= 0x0A,
 PM0		= 0x20,
 PN0		= 0x21,
 PP0		= 0x22,
 PC0		= 0x23
};

// TI TVP 3030 RAMDAC Direct Registers
enum 
{
	TVPADDRW = 0x00, // 0  Palette/Cursor RAM Write Adress/Index
	TVPPDATA = 0x04, // 1  Palette Data RAM Data
	TVPPMASK = 0x08, // 2  Pixel Read-Mask
	TVPPADRR = 0x0c, // 3  Palette/Cursor RAM Read Adress
	TVPCADRW = 0x10, // 4  Cursor/Overscan Color Write Address
	TVPCDATA = 0x14, // 5  Cursor/Overscan Color Data
			 // 6  reserved
	TVPCADRR = 0x1c, // 7  Cursor/Overscan Color Read Address
			 // 8  reserved
	TVPDCCTL = 0x24, // 9  Direct Cursor Control
	TVPIDATA = 0x28, // 10 Index Data
	TVPCRDAT = 0x2c, // 11 Cursor RAM Data
	TVPCXPOL = 0x30, // 12 Cursor-Position X LSB
	TVPCXPOH = 0x34, // 13 Cursor-Position X MSB
	TVPCYPOL = 0x38, // 14 Cursor-Position Y LSB
	TVPCYPOH = 0x3c, // 15 Cursor-Position Y MSB
};

// TI TVP 3030 RAMDAC Indirect Registers
enum
{
	TVPIRREV = 0x01, // Silicon Revision [RO] 
	TVPIRICC = 0x06, // Indirect Cursor Control 	(0x00) 
	TVPIRBRC = 0x07, // Byte Router Control 	(0xe4)
	TVPIRLAC = 0x0f, // Latch Control 		(0x06)
	TVPIRTCC = 0x18, // True Color Control  	(0x80)
	TVPIRMXC = 0x19, // Multiplex Control		(0x98)
	TVPIRCLS = 0x1a, // Clock Selection		(0x07)
	TVPIRPPG = 0x1c, // Palette Page		(0x00)
	TVPIRGEC = 0x1d, // General Control 		(0x00)
	TVPIRMIC = 0x1e, // Miscellaneous Control	(0x00)
	TVPIRPLA = 0x2c, // PLL Address			
	TVPIRPPD = 0x2d, // Pixel Clock PLL Data
	TVPIRMPD = 0x2e, // Memory Clock PLL Data
	TVPIRLPD = 0x2f, // Loop Clock PLL Data
	TVPIRCKL = 0x30, // Color-Key Overlay Low
	TVPIRCKH = 0x31, // Color-Key Overlay High 
	TVPIRCRL = 0x32, // Color-Key Red Low
	TVPIRCRH = 0x33, // Color-Key Red High 
	TVPIRCGL = 0x34, // Color-Key Green Low
	TVPIRCGH = 0x35, // Color-Key Green High 
	TVPIRCBL = 0x36, // Color-Key Blue Low
	TVPIRCBH = 0x37, // Color-Key Blue High 
	TVPIRCKC = 0x38, // Color-Key Control 		(0x00)
	TVPIRMLC = 0x39, // MCLK/Loop Clock Control	(0x18)
	TVPIRSEN = 0x3a, // Sense Test			(0x00)
	TVPIRTMD = 0x3b, // Test Mode Data
	TVPIRRML = 0x3c, // CRC Remainder LSB [RO]
	TVPIRRMM = 0x3d, // CRC Remainder MSB [RO]
	TVPIRRMS = 0x3e, // CRC  Bit Select [WO] 
	TVPIRDID = 0x3f, // Device ID [RO] 		(0x30)
	TVPIRRES = 0xff, // Software Reset [WO]

};

struct initvalues
{
  unsigned char	addr, value;
};



// Values which only depend on resolution not on color mode
struct tt_single_rmodevals
{
	unsigned short hes;	
	unsigned short heb;	
	unsigned short hsb;	
	unsigned short ht;	
	unsigned short ves;	
	unsigned short veb;	
	unsigned short vsb;	
	unsigned short vt;	
};

struct tvp_single_rmodevals
{
	unsigned char pclk_n;
	unsigned char pclk_m;
	unsigned char pclk_p;
};

struct ibm_single_rmodevals
{
	unsigned char pclk_m;
	unsigned char pclk_n;
	unsigned char pclk_p;
	unsigned char pclk_c;
};

// Values which only depend on color mode not on resolution
struct tvp_single_cmodevals
{
	unsigned char tcc; // True Color control
	unsigned char mxc; // Multiplexer control
	unsigned char lckl_n;	// N value of LCKL PLL
};

struct ibm_single_cmodevals
{
	unsigned char pformat; // pixel format
};
	
// Values of the tvp which change depending on colormode x resolution
struct tvp_single_crmodevals
{
	unsigned char mlc;	// Memory Loop Config 0x39
	unsigned char lckl_p;	// P value of LCKL PLL
};

struct ibm_single_crmodevals
{
    // oh nothing changes
};

// complete configuration for a resolution in all depths 
// 0 = 8 Bit, 15/16 bit = 1 , 32 Bit = 2 
struct ims_crmodevals
{
	int pitch;
	struct tt_single_rmodevals tt[2];  // for each ramdac  seperate tt config

	struct tvp_single_rmodevals tvp_clock;  // for each ramdac  seperate clock config
	struct tvp_single_crmodevals tvp[3]; 	   // for each colormode 

	struct ibm_single_rmodevals ibm_clock;  // for each ramdac  seperate clock config
//	struct ibm_single_crmodevals ibm[3];	   // for each color mode 
};

struct ims_modevals
{
	int dac;			  // which dac do we have
	int total_vram;			  // how much vram is on board
	int sense;			  // what monitor
	unsigned char*	fb;		  // frame buffer address
	unsigned char*	fb_phys;	  // frame buffer address
	unsigned char*  cmap;		  // dac address
	unsigned char*  cmap_phys;	  // dac address
	unsigned int*	dc;		  // tt address
	unsigned int*	dc_phys;	  // tt address

	struct initvalues*     init[2];	  // initial register settings for each ramdac

	struct ims_crmodevals* mode[20];  // for each possible mode
	
	struct  tvp_single_cmodevals tvp[3]; // for each color mode
	
	struct  ibm_single_cmodevals ibm[3]; // for each color mode
};


struct ims_crmodevals imsmode_6 =
{
	640,
	{
  		{ 0x08, 0x12, 0x62, 0x6C, 0x0003, 0x002A, 0x020A, 0x020C },
		{ 0x04, 0x0009, 0x0031, 0x0036, 0x0003, 0x002a, 0x020a, 0x020d },
	},
	{	0xef, 0x2e, 0xb2 },
	{
		{ 0x39, 0xf3 },
		{ 0x39, 0xf3 },
		{ 0x38, 0xf3 }
	},
	// IBM CLOCK
	{       0x78, 0x13, 0x02, 0x02 },
};

struct ims_crmodevals imsmode_13 =
{
	832,
	{
  		{ 0x05, 0x20, 0x88, 0x90, 0x0003, 0x0028, 0x0298, 0x029B },
		{ 0x04, 0x0011, 0x0045, 0x0048, 0x0003, 0x002a, 0x029a, 0x029b},
	},
	{	0xfe, 0x3e, 0xf1 },
	{
		{ 0x39, 0xf3 },
		{ 0x38, 0xf3 },
		{ 0x38, 0xf2 }
	},
	{ 0x3E, 0x0A, 0x01, 0x02 }
};
struct ims_crmodevals imsmode_17 =
{
	1024,
	{
  		{ 0x0A, 0x1C, 0x9C, 0xA6, 0x0003, 0x0020, 0x0320, 0x0323 } ,
		{ 0x06, 0x0210, 0x0250, 0x0053, 0x1003, 0x0021, 0x0321, 0x0324 },
	},
	{	0xfc, 0x3a, 0xf1 },
	{
		{ 0x39, 0xf3 },
		{ 0x38, 0xf3 },
		{ 0x38, 0xf2 }
	},
	{ 0x07, 0x00, 0x01, 0x02 }
};
struct ims_crmodevals imsmode_18 =
{
	1152,
	{
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
  		{ 0x09, 0x0011, 0x059, 0x5b, 0x0003, 0x0031, 0x0397, 0x039a }, 
	},
	{	0xfd, 0x3a, 0xf1 },
	{
		{ 0x39, 0xf3 },
		{ 0x38, 0xf3 },
		{ 0x38, 0xf2 }
	},
	{ 0, 0, 0, 0 }
};
struct ims_crmodevals imsmode_19 =
{
	1280,
	{
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0x09, 0x0016, 0x0066, 0x0069, 0x0003, 0x0027, 0x03e7, 0x03e8 },
	},
	{	0xf7, 0x36, 0xf0 },
	{
		{ 0x38, 0xf3 },
		{ 0x38, 0xf2 },
		{ 0x38, 0xf1 }
	},
	{ 0, 0, 0, 0 }
};
struct ims_crmodevals imsmode_20 =
{
	1280,
	{
		{ 0, 0, 0, 0, 0, 0, 0, 0 },
		{ 0x09, 0x0018, 0x0068, 0x006a, 0x0003, 0x0029, 0x0429, 0x042a },
	},
	{	0xf0, 0x2d, 0xf0 },
	{
		{ 0x38, 0xf3 },
		{ 0x38, 0xf2 },
		{ 0x38, 0xf1 }
	},
      	{ 0, 0, 0, 0 }
};

// IBM RAMDAC initial register values

static struct initvalues ibm_initregs[] = 
{
 { 0x02, 0x21 },	/* (0x01) Miscellaneous Clock Control */
 { 0x03, 0x00 },	/* (0x00) Sync Control */
 { 0x04, 0x00 },	/* (0x00) Horizontal Sync Position */
 { 0x05, 0x00 },	/* (0x00) Power Management */
 { 0x06, 0x0B },	/* (0x02) DAC Operation */
 { 0x07, 0x00 },	/* (0x00) Palette Control */
 { 0x08, 0x01 },	/* (0x01) System Clock Control */
 { 0x0B, 0x00 },	/* (U) 8 BPP Control */
 { 0x0C, 0xC4 },	/* (U) 16 BPP Control */
 { 0x0D, 0x00 },	/* (U) 24 BPP Packed Control */
 { 0x0E, 0x03 },	/* (U) 32 BPP Control */
 { 0x10, 0x05 },	/* (0x00) Pixel PLL Control 1 */
 { 0x11, 0x00 },	/* (0x00) Pixel PLL Control 2 */
 { 0x15, 0x08 },	/* (0x08) SYSCLK N (System PLL Reference Divider) */
 { 0x16, 0x57 },	/* (0x41) SYSCLK M (System PLL VCO Divider) */
 { 0x17, 0x00 },	/* (U) SYSCLK P */
 { 0x18, 0x00 },	/* (U) SYSCLK C */
 { 0x30, 0x00 },	/* (0x00) Cursor Control */
 { 0x60, 0xFF },	/* (U) Border Color Red */
 { 0x61, 0xFF },	/* (U) Border Color Green */
 { 0x62, 0xFF },	/* (U) Border Color Blue */
 { 0x70, 0x01 },	/* (0x00) Miscellaneous Control 1 */
 { 0x71, 0x45 },	/* (0x00) Miscellaneous Control 2 */
 { 0x72, 0x00 },	/* (0x00) Miscellaneous Control 3 */
 { 0x78, 0x00 },	/* (0x00) Key Control/DB Operation */
 { 0x00, 0x00 } 
};


static struct initvalues tvp_initregs[] = 
{
{ 0x6, 0x00},
{ 0x7, 0xe4},
{ 0xf, 0x06},
{ 0x18, 0x80},
{ 0x19, 0x4d},
{ 0x1a, 0x05},
{ 0x1c, 0x00},
{ 0x1d, 0x00},
{ 0x1e, 0x08},
{ 0x30, 0xff},
{ 0x31, 0xff},
{ 0x32, 0xff},
{ 0x33, 0xff},
{ 0x34, 0xff},
{ 0x35, 0xff},
{ 0x36, 0xff},
{ 0x37, 0xff},
{ 0x38, 0x00},
{ TVPIRPLA, 0x00 },
{ TVPIRPPD, 0xc0 },
{ TVPIRPPD, 0xd5 },
{ TVPIRPPD, 0xea },
{ TVPIRPLA, 0x00 },
{ TVPIRMPD, 0xb9 },
{ TVPIRMPD, 0x3a },
{ TVPIRMPD, 0xb1 },
{ TVPIRPLA, 0x00 },
{ TVPIRLPD, 0xc1 },
{ TVPIRLPD, 0x3d },
{ TVPIRLPD, 0xf3 },
{ 0x00, 0x00 } 
};


static struct ims_modevals ims_info =
{
	-1,	// DAC
	-1,	// VRAM
	-1, 	// Monitor;
	0,	// Framebuffer
	0,	// Framebuffer_phys
	0,	// colormap
	0,	// colormap_phys
	0,	// dc
	0,	// dc_phys
	{ ibm_initregs, tvp_initregs},
	{
		NULL,
		NULL,
		NULL,
		NULL,
		&imsmode_6,
		&imsmode_6,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		NULL,
		&imsmode_13,
		NULL,
		NULL,
		NULL,
		&imsmode_17,
		&imsmode_18,
		&imsmode_19,
		&imsmode_20
	},
	{
		{ 0x80, 0x4d, 0xc1 },
		{ 0x44, 0x55, 0xe1 },
		{ 0x46, 0x5d, 0xf1 }
	},
	{
		{ 0x03 },
		{ 0x04 },
		{ 0x06 }
	}	
};	
		
	


// static void set_imstt_clock(unsigned char *params);
static void map_imstt_display(struct device_node *, int);
static int read_imstt_sense(void);
static int imstt_vram_reqd(int vmode, int cmode);


__openfirmware

#if 0
static int get_tvp_ireg(int iaddr)
{
	ims_info.cmap[0] = iaddr & 0xff; eieio();
	return ims_info.cmap[40];
}
#endif

static void set_tvp_ireg(int iaddr,unsigned char value)
{
	ims_info.cmap[0] = iaddr & 0xff; eieio();
	ims_info.cmap[40] = value; eieio();
}
/*
 * Get the monitor sense value.
 * Note that this can be called before calibrate_delay,
 * so we can't use udelay.
 */
static int
read_imstt_sense()
{
#if 0
	int sense;
	unsigned gio, gioe;

	gio = ld_le32(ims_info.dc + GIO) & ~0x0038;
	gioe = ld_le32(dc_
	
	out_le32(ims_info.dc + GIOE, reg);	/* drive all lines high */
	__delay(200);
	out_le32(ims_info.dc + GIOE, 077);	/* turn off drivers */
	__delay(2000);
	sense = (in_le32(ims_info.dc + GIOE) & 0x1c0) << 2;

	/* drive each sense line low in turn and collect the other 2 */
	out_le32(ims_info.dc + GIOE, 033);	/* drive A low */
	__delay(2000);
	sense |= (in_le32(ims_info.dc + GIOE) & 0xc0) >> 2;
	out_le32(ims_info.dc + GIOE, 055);	/* drive B low */
	__delay(2000);
	sense |= ((in_le32(ims_info.dc + GIOE) & 0x100) >> 5)
		| ((in_le32(ims_info.dc + GIOE) & 0x40) >> 4);
	out_le32(ims_info.dc + GIOE, 066);	/* drive C low */
	__delay(2000);
	sense |= (in_le32(ims_info.dc + GIOE) & 0x180) >> 7;

	out_le32(ims_info.dc + GIOE, 077);	/* turn off drivers */
	return sense;
#else
	return 0;
#endif
}

static inline int imstt_vram_reqd(int vmode, int cmode)
{
	return vmode_attrs[vmode-1].vres *
		(ims_info.mode[vmode-1])->pitch * ( 1 << cmode);
}

void
map_imstt_display_tvp(struct device_node *dp)
{
    map_imstt_display(dp,1);
}

void
map_imstt_display_ibm(struct device_node *dp)
{
    map_imstt_display(dp,0);
}

static void
map_imstt_display(struct device_node *dp, int which)
{
	int i, sense;
	unsigned long addr, size, tmp;
	unsigned char bus, devfn;
	unsigned short cmd;

	if (dp->next != 0)
		printk("Warning: only using first imstt display device\n");

#if 0
	printk("pmac_display_init: node = %p, addrs =", dp->node);
	for (i = 0; i < dp->n_addrs; ++i)
		printk(" %x(%x)", dp->addrs[i].address, dp->addrs[i].size);
	printk(", intrs =");
	for (i = 0; i < dp->n_intrs; ++i)
		printk(" %x", dp->intrs[i]);
	printk("\n");
#endif

	/* Map in frame buffer and registers */
	for (i = 0; i < dp->n_addrs; ++i) {
		addr = dp->addrs[i].address;
		size = dp->addrs[i].size;
		if (size >= 0x02000000) {
			ims_info.fb = __ioremap(addr, size, _PAGE_NO_CACHE);
			ims_info.fb_phys = (unsigned char*)addr;
			ims_info.dc = (unsigned*)(ims_info.fb + 0x00800000);
			ims_info.dc_phys = (unsigned*)(ims_info.fb_phys + 0x00800000);
			ims_info.cmap = (unsigned char*)(ims_info.fb + 0x00840000);
			ims_info.cmap_phys = (unsigned char*)(ims_info.fb_phys + 0x00840000);
			printk("mapped ims_info.fb=%x(%x)", (unsigned)ims_info.fb, (unsigned)size);
			printk(" ims_info.dc=%x, ims_info.cmap=%x\n", (unsigned)ims_info.dc, (unsigned)ims_info.cmap);
		}
	}

	/* enable memory-space accesses using config-space command register */
	if (pci_device_loc(dp, &bus, &devfn) == 0) {
	  pcibios_read_config_word(bus, devfn, PCI_COMMAND, &cmd);
	  
	  printk("command word 0x%04X\n", cmd);
	  
	  if (cmd != 0xffff) {
	    cmd |= PCI_COMMAND_MEMORY;
	    pcibios_write_config_word(bus, devfn, PCI_COMMAND, cmd);
	  }
	}
	else
	  printk("unable to find pci device\n");

	tmp = in_le32(ims_info.dc + SSTATUS);
	printk("chip version %ld, ", (tmp & 0x0F00) >> 8);

	tmp = in_le32(ims_info.dc + PRC);
	
	if (0 == which )
	    ims_info.total_vram = (tmp & 0x0004) ? 0x000400000L : 0x000200000L;
	else
	    ims_info.total_vram = 0x000800000L;
	
	printk("VRAM size %ldM\n", ims_info.total_vram / 0x000100000L);
	
	if (ims_info.total_vram == 0x000800000L)
	{
		ims_info.dac = TVPRAMDAC;
		printk("Selecting TVP 3030 RAMDAC\n");
	}
	else
	{
		ims_info.dac = IBMRAMDAC;
		printk("Selecting  IBM RAMDAC\n");
	}

	sense = read_imstt_sense();
	printk("Monitor sense value = 0x%x, ", sense);
#if 0
	if (video_mode == VMODE_NVRAM) {
		video_mode = nvram_read_byte(NV_VMODE);
		if (video_mode <= 0 || video_mode > VMODE_MAX
		    || imstt_reg_init[video_mode-1] == 0)
			video_mode = VMODE_CHOOSE;
	}
	if (video_mode == VMODE_CHOOSE)
		video_mode = map_monitor_sense(sense);
	if (imstt_reg_init[video_mode-1] == 0)
		video_mode = VMODE_640_480_67;

	/*
	 * Reduce the pixel size if we don't have enough VRAM.
	 */
	if (color_mode == CMODE_NVRAM)
		color_mode = nvram_read_byte(NV_CMODE);
	if (color_mode < CMODE_8 || color_mode > CMODE_32)
		color_mode = CMODE_8;
	while (color_mode > CMODE_8
	       && imstt_vram_reqd(video_mode, color_mode) > ims_info.total_vram)
		--color_mode;

#endif
	// Hack Hack Hack !!!
	video_mode = VMODE_640_480_67;
	color_mode = CMODE_8;
}

/*
 * We dont need it ( all is done in ims_init )
static void
set_imstt_clock_tvp(char* tvprv)
{
  int j;
  for (j=0;j<3;j++)
	{
  		set_tvp_ireg(TVPIRPLA,(j << 4) | (j << 2) | j); // Select same value for all plls
		set_tvp_ireg(TVPIRPPD,tvprv[j]);
		set_tvp_ireg(TVPIRMPD,tvprv[3+j]);
		set_tvp_ireg(TVPIRLPD,tvprv[6+j]);
	}
}

static void
set_imstt_clock_ibm(unsigned char *params)
{
  ims_info.cmap[PIDXHI] = 0; eieio();
  ims_info.cmap[PIDXLO] = PM0; eieio();
  ims_info.cmap[PIDXDATA] = params[0]; eieio();

  ims_info.cmap[PIDXLO] = PN0; eieio();
  ims_info.cmap[PIDXDATA] = params[1]; eieio();

  ims_info.cmap[PIDXLO] = PP0; eieio();
  ims_info.cmap[PIDXDATA] = params[2]; eieio();
	
  ims_info.cmap[PIDXLO] = PC0; eieio();
  ims_info.cmap[PIDXDATA] = params[3]; eieio();
}
*/

void
imstt_init()
{
  int	                 i, yoff, hres;
  unsigned long	         ctl, pitch, tmp, scrCmode;
  struct ims_crmodevals   *init;

  if (video_mode <= 0 || video_mode > VMODE_MAX ) panic("imstt: display mode %d not supported(not in valid range)", video_mode);
  if ((init = ims_info.mode[video_mode-1]) == 0) panic("imstt: display mode %d not supported(no mode definition)", video_mode);
  if (init->tt[ims_info.dac].vt == 0) panic("imstt: display mode %d not supported (no timing definition)", video_mode);
	

  n_scanlines = vmode_attrs[video_mode-1].vres;
  hres = vmode_attrs[video_mode-1].hres;
  pixel_size = 1 << color_mode;
  line_pitch = init->pitch * pixel_size;
  row_pitch = line_pitch * 16;

  /* initialize the card */
  tmp = in_le32(ims_info.dc + STGCTL);
  out_le32(ims_info.dc + STGCTL, tmp & ~0x1);
#if 0
  out_le32(ims_info.dc + SCR, 0);
#endif

  switch(ims_info.dac)
  {
  case IBMRAMDAC:
      ims_info.cmap[PPMASK] = 0xFF; eieio();
      ims_info.cmap[PIDXHI] = 0x00; eieio();
      for (i = 0; ims_info.init[IBMRAMDAC][i].addr != 0 && ims_info.init[IBMRAMDAC][i].value != 0 ;i++)
	  {
	      ims_info.cmap[PIDXLO] = ims_info.init[IBMRAMDAC][i].addr; eieio();
	      ims_info.cmap[PIDXDATA] = ims_info.init[IBMRAMDAC][i].value; eieio();
	  }

      ims_info.cmap[PIDXHI] = 0; eieio();
      ims_info.cmap[PIDXLO] = PM0; eieio();
      ims_info.cmap[PIDXDATA] = init->ibm_clock.pclk_m; eieio();

      ims_info.cmap[PIDXLO] = PN0; eieio();
      ims_info.cmap[PIDXDATA] = init->ibm_clock.pclk_n; eieio();

      ims_info.cmap[PIDXLO] = PP0; eieio();
      ims_info.cmap[PIDXDATA] = init->ibm_clock.pclk_p; eieio();
	
      ims_info.cmap[PIDXLO] = PC0; eieio();
      ims_info.cmap[PIDXDATA] = init->ibm_clock.pclk_c; eieio();
      
      ims_info.cmap[PIDXLO] = PPIXREP; eieio();
      ims_info.cmap[PIDXDATA] = ims_info.ibm[color_mode].pformat; eieio();

      break;
  case TVPRAMDAC:
	for (i = 0; ims_info.init[TVPRAMDAC][i].addr != 0 && ims_info.init[TVPRAMDAC][i].value != 0 ;i++)
	{
		set_tvp_ireg(ims_info.init[TVPRAMDAC][i].addr,ims_info.init[TVPRAMDAC][i].value);	
	}
    	set_tvp_ireg(TVPIRPLA,0x00);
    	set_tvp_ireg(TVPIRPPD,init->tvp_clock.pclk_n);
    	set_tvp_ireg(TVPIRPPD,init->tvp_clock.pclk_m);
    	set_tvp_ireg(TVPIRPPD,init->tvp_clock.pclk_p);

    	set_tvp_ireg(TVPIRTCC,ims_info.tvp[color_mode].tcc);
    	set_tvp_ireg(TVPIRMXC,ims_info.tvp[color_mode].mxc);

    	set_tvp_ireg(TVPIRPLA,0x00);
    	set_tvp_ireg(TVPIRLPD,ims_info.tvp[color_mode].lckl_n);

    	set_tvp_ireg(TVPIRPLA,0x15);
    	set_tvp_ireg(TVPIRMLC,(init->tvp[color_mode]).mlc);

    	set_tvp_ireg(TVPIRPLA,0x2a);
    	set_tvp_ireg(TVPIRLPD,init->tvp[color_mode].lckl_p);
	break;
  }


  switch(color_mode) {
  case CMODE_32:
    ctl = 0x1785;
    pitch = init->pitch;
    scrCmode = 0x300;
    break;
  case CMODE_16:
    ctl = 0x1783;
    pitch = init->pitch / 2;
    scrCmode = 0x100;
    break;
  case CMODE_8:
  default:
    ctl = 0x1781;
    pitch = init->pitch / 4;
    scrCmode = 0x000;
    break;
  }

  out_le32(&ims_info.dc[HES], init->tt[ims_info.dac].hes);
  out_le32(&ims_info.dc[HEB], init->tt[ims_info.dac].heb);
  out_le32(&ims_info.dc[HSB], init->tt[ims_info.dac].hsb);
  out_le32(&ims_info.dc[HT], init->tt[ims_info.dac].ht);
  out_le32(&ims_info.dc[VES], init->tt[ims_info.dac].ves);
  out_le32(&ims_info.dc[VEB], init->tt[ims_info.dac].veb);
  out_le32(&ims_info.dc[VSB], init->tt[ims_info.dac].vsb);
  out_le32(&ims_info.dc[VT], init->tt[ims_info.dac].vt);
  out_le32(&ims_info.dc[HCIV], 1);
  out_le32(&ims_info.dc[VCIV], 1);
  out_le32(&ims_info.dc[TCDR], 4);
  out_le32(&ims_info.dc[VIL], 0);
	
  out_le32(&ims_info.dc[SSR], 0);
  out_le32(&ims_info.dc[HRIR], 0x0200);
  out_le32(&ims_info.dc[CMR], 0x01FF);
  out_le32(&ims_info.dc[SRGCTL], 0x0003);
  switch(ims_info.total_vram)
  {
	case 0x000200000:
     		out_le32(&ims_info.dc[SCR], 0x0059D| scrCmode);
		break;
	case 0x000400000:
    		pitch /= 2;
    		out_le32(&ims_info.dc[SCR], 0x00D0DC | scrCmode);
		break;
	case 0x000800000:
    		pitch /= 2;
    		out_le32(&ims_info.dc[SCR], 0x0150DD | scrCmode);
		break;
  }

  out_le32(&ims_info.dc[SPR], pitch);

  if (ims_info.dac == IBMRAMDAC)
  {
	  

  }
  
  pmac_init_palette();	/* Initialize colormap */

  out_le32(&ims_info.dc[STGCTL], ctl);

  yoff = (n_scanlines % 16) / 2;
  fb_start = ims_info.fb + yoff * line_pitch;

  /* Clear screen */
  {
	unsigned long *p;
  	p = (unsigned long*)ims_info.fb;
  	for (i = n_scanlines * line_pitch / sizeof(unsigned); i != 0; --i)
    		*p++ = 0;
  }

  display_info.height = n_scanlines;
  display_info.width = hres;
  display_info.depth = pixel_size * 8;
  display_info.pitch = line_pitch;
  display_info.mode = video_mode;

  if (ims_info.dac == IBMRAMDAC )
      strncpy(display_info.name, "IMS,tt128mb2/4", sizeof(display_info.name));
  else
      strncpy(display_info.name, "IMS,tt128mb8/8A", sizeof(display_info.name));
  
  display_info.fb_address = (unsigned long) ims_info.fb_phys;
  display_info.cmap_adr_address = (unsigned long) &ims_info.cmap_phys[PADDRW];
  display_info.cmap_data_address = (unsigned long) &ims_info.cmap_phys[PDATA];
  display_info.disp_reg_address = (unsigned long) NULL;
}

int
imstt_setmode(struct vc_mode *mode, int doit)
{
  int cmode;
  struct ims_crmodevals   *init;

  if (video_mode <= 0 || video_mode > VMODE_MAX )
		return -EINVAL;
  if ((init = ims_info.mode[video_mode-1]) == 0)
		return -EINVAL;
  if (init->tt[ims_info.dac].vt == 0)
		return -EINVAL;

	if (mode->mode <= 0 || mode->mode > VMODE_MAX
	    || (ims_info.mode[mode->mode-1] == 0))
		return -EINVAL;
	switch (mode->depth) {
	case 24:
	case 32:
		cmode = CMODE_32;
		break;
	case 16:
		cmode = CMODE_16;
		break;
	case 8:
	case 0:		/* (default) */
		cmode = CMODE_8;
		break;
	default:
		return -EINVAL;
	}
	if (imstt_vram_reqd(mode->mode, cmode) > ims_info.total_vram)
		return -EINVAL;
	if (doit) {
		video_mode = mode->mode;
		color_mode = cmode;
		imstt_init();
	}
	return 0;
}

// set palette for TI TVP3030 ramdac (used on 8MB version)
void
imstt_set_palette_tvp(unsigned char red[], unsigned char green[],
		    unsigned char blue[], int index, int ncolors)
{
	int i;
	for (i = 0; i < ncolors; ++i) {
		ims_info.cmap[TVPADDRW] = index + i;	eieio();
		ims_info.cmap[TVPPDATA] = red[i];	eieio();
		ims_info.cmap[TVPPDATA] = green[i];	eieio();
		ims_info.cmap[TVPPDATA] = blue[i];	eieio();
	}
}

// set palette for IBM ramdac (used on 2MB/4MB version)
void
imstt_set_palette_ibm(unsigned char red[], unsigned char green[],
		    unsigned char blue[], int index, int ncolors)
{
	int i;

	for (i = 0; i < ncolors; ++i) {
		ims_info.cmap[PADDRW] = index + i;	eieio();
		ims_info.cmap[PDATA] = red[i];	eieio();
		ims_info.cmap[PDATA] = green[i];	eieio();
		ims_info.cmap[PDATA] = blue[i];	eieio();
	}
}

void
imstt_set_blanking(int blank_mode)
{
	long ctrl;

	ctrl = ld_le32(ims_info.dc + STGCTL) | 0x0030;
	if (blank_mode & VESA_VSYNC_SUSPEND)
		ctrl &= ~0x0020;
	if (blank_mode & VESA_HSYNC_SUSPEND)
		ctrl &= ~0x0010;
	out_le32(ims_info.dc + STGCTL, ctrl);
}


