/* 
    bttv - Bt848 frame grabber driver

    Copyright (C) 1996,97 Ralph Metzler (rjkm@thp.uni-koeln.de)

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
    
    Modified to put the RISC code writer in the kernel and to fit a
    common (and I hope safe) kernel interface. When we have an X extension
    all will now be really sweet.
*/

#include <linux/module.h>
#include <linux/bios32.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/signal.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/sched.h>
#include <asm/segment.h>
#include <linux/types.h>
#include <linux/videodev.h>

#include <linux/version.h>
#include <asm/uaccess.h>

#include "bttv.h"
#include "tuner.h"

#define DEBUG(x)		/* Debug driver */	
#define IDEBUG(x)		/* Debug interrupt handler */

static unsigned int remap=0;
static unsigned int vidmem=0;
static unsigned int tuner=0;	/* Default tuner */

static int find_vga(void);
static void bt848_set_risc_jmps(struct bttv *btv);

/* Anybody who uses more than four? */
#define BTTV_MAX 4

static int bttv_num;
static struct bttv bttvs[BTTV_MAX];

#define I2C_TIMING (0x7<<4)
#define I2C_COMMAND (I2C_TIMING | BT848_I2C_SCL | BT848_I2C_SDA)

#define AUDIO_MUTE_DELAY 10000
#define FREQ_CHANGE_DELAY 20000
#define EEPROM_WRITE_DELAY 20000


/*******************************/
/* Memory management functions */
/*******************************/

/* convert virtual user memory address to physical address */
/* (virt_to_phys only works for kmalloced kernel memory) */

static inline ulong uvirt_to_phys(ulong adr)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *ptep, pte;
  
/*  printk("adr: 0x%08x\n",adr);*/
	pgd = pgd_offset(current->mm, adr);
/*  printk("pgd: 0x%08x\n",pgd);*/
	if (pgd_none(*pgd))
		return 0;
	pmd = pmd_offset(pgd, adr);
/*  printk("pmd: 0x%08x\n",pmd); */
	if (pmd_none(*pmd))
		return 0;
	ptep = pte_offset(pmd, adr&(~PGDIR_MASK));
	pte = *ptep;
	if(pte_present(pte))
		return (pte_page(pte)|(adr&(PAGE_SIZE-1)));
	return 0;
}

/* convert virtual kernel memory address to physical address */
/* (virt_to_phys only works for kmalloced kernel memory) */

static inline ulong kvirt_to_phys(ulong adr) 
{
	return uvirt_to_phys(VMALLOC_VMADDR(adr));
}

static inline ulong kvirt_to_bus(ulong adr) 
{
	return virt_to_bus(phys_to_virt(kvirt_to_phys(adr)));
}


/*****************/
/* I2C functions */
/*****************/

static int I2CRead(struct bttv *btv, int addr)
{
	u32 i;
	u32 stat;
  
	/* clear status bit ; BT848_INT_RACK is ro */
	btwrite(BT848_INT_I2CDONE, BT848_INT_STAT);
  
	btwrite(((addr & 0xff) << 24) | I2C_COMMAND, BT848_I2C);
  
	for (i=0x7fffffff; i; i--)
	{
		stat=btread(BT848_INT_STAT);
		if (stat & BT848_INT_I2CDONE)
			break;
	}
  
	if (!i)
		return -1;
	if (!(stat & BT848_INT_RACK))
		return -2;
  
	i=(btread(BT848_I2C)>>8)&0xff;
	return i;
}

/* set both to write both bytes, reset it to write only b1 */

static int I2CWrite(struct bttv *btv, unchar addr, unchar b1,
		    unchar b2, int both)
{
	u32 i;
	u32 data;
	u32 stat;
  
	/* clear status bit; BT848_INT_RACK is ro */
	btwrite(BT848_INT_I2CDONE, BT848_INT_STAT);
  
	data=((addr & 0xff) << 24) | ((b1 & 0xff) << 16) | I2C_COMMAND;
	if (both)
	{
		data|=((b2 & 0xff) << 8);
		data|=BT848_I2C_W3B;
	}
  
	btwrite(data, BT848_I2C);

	for (i=0x7fffffff; i; i--)
	{
		stat=btread(BT848_INT_STAT);
		if (stat & BT848_INT_I2CDONE)
			break;
	}
  
	if (!i)
		return -1;
	if (!(stat & BT848_INT_RACK))
		return -2;
  
	return 0;
}

static void readee(struct bttv *btv, unchar *eedata)
{
	int i, k;
  
	if (I2CWrite(btv, 0xa0, 0, -1, 0)<0)
	{
		printk(KERN_WARNING "bttv: readee error\n");
		return;
	}

	for (i=0; i<256; i++)
	{
		k=I2CRead(btv, 0xa1);
		if (k<0)
		{
			printk(KERN_WARNING "bttv: readee error\n");
			break;
		}
		eedata[i]=k;
	}
}

static void writeee(struct bttv *btv, unchar *eedata)
{
	int i;
  
	for (i=0; i<256; i++)
	{
		if (I2CWrite(btv, 0xa0, i, eedata[i], 1)<0)
		{
			printk(KERN_WARNING "bttv: writeee error (%d)\n", i);
			break;
		}
		udelay(EEPROM_WRITE_DELAY);
	}
}

/*
 *	Tuner, internal, external and mute 
 */
 
static unchar audiomuxs[][4] = 
{
	{ 0x00, 0x00, 0x00, 0x00}, /* unknown */
	{ 0x02, 0x00, 0x00, 0x0a}, /* MIRO */
	{ 0x00, 0x02, 0x03, 0x04}, /* Hauppauge */
	{ 0x04, 0x02, 0x03, 0x01}, /* STB */
	{ 0x01, 0x02, 0x03, 0x04}, /* Intel??? */
	{ 0x01, 0x02, 0x03, 0x04}, /* Diamond DTV2000??? */
};

static void audio(struct bttv *btv, int mode)
{
	btwrite(0x0f, BT848_GPIO_OUT_EN);
	btwrite(0x00, BT848_GPIO_REG_INP);

	switch (mode)
	{
		case AUDIO_UNMUTE:
			btv->audio&=~AUDIO_MUTE;
			mode=btv->audio;
			break;
		case AUDIO_OFF:
			mode=AUDIO_OFF;
			break;
		case AUDIO_ON:
			mode=btv->audio;
			break;
		default:
			btv->audio&=AUDIO_MUTE;
			btv->audio|=mode;
			break;
	}
	if ((btv->audio&AUDIO_MUTE) || !(btread(BT848_DSTATUS)&BT848_DSTATUS_HLOC))
		mode=AUDIO_OFF;
	btaor(audiomuxs[btv->type][mode] , ~0x0f, BT848_GPIO_DATA);
}


extern inline void bt848_dma(struct bttv *btv, uint state)
{
	if (state)
		btor(3, BT848_GPIO_DMA_CTL);
	else
		btand(~3, BT848_GPIO_DMA_CTL);
}


static void bt848_cap(struct bttv *btv, uint state)
{
	if (state) 
	{
		btv->cap|=3;
		bt848_set_risc_jmps(btv);
	}
	else
	{
		btv->cap&=~3;
		bt848_set_risc_jmps(btv);
	}
}

static void bt848_muxsel(struct bttv *btv, uint input)
{
	input&=3;

	/* This seems to get rid of some synchronization problems */
	btand(~(3<<5), BT848_IFORM);
	udelay(10000); 

	if (input==3) 
	{
		btor(BT848_CONTROL_COMP, BT848_E_CONTROL);
		btor(BT848_CONTROL_COMP, BT848_O_CONTROL);
	}
	else
	{
		btand(~BT848_CONTROL_COMP, BT848_E_CONTROL);
		btand(~BT848_CONTROL_COMP, BT848_O_CONTROL);
	}
	if (input==2)
		input=3;
	btaor(((input+2)&3)<<5, ~(3<<5), BT848_IFORM);
	audio(btv, input ? AUDIO_EXTERN : AUDIO_TUNER);
}


#define VBIBUF_SIZE 65536

static void make_vbitab(struct bttv *btv)
{
	int i;
	dword *po=(dword *) btv->vbi_odd;
	dword *pe=(dword *) btv->vbi_even;
  
	DEBUG(printk(KERN_DEBUG "vbiodd: 0x%08x\n",(int)btv->vbi_odd));
	DEBUG(printk(KERN_DEBUG "vbievn: 0x%08x\n",(int)btv->vbi_even));
	DEBUG(printk(KERN_DEBUG "po: 0x%08x\n",(int)po));
	DEBUG(printk(KERN_DEBUG "pe: 0x%08x\n",(int)pe));

	*(po++)=BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1; *(po++)=0;
	for (i=0; i<16; i++) 
	{
		*(po++)=BT848_RISC_WRITE|2044|BT848_RISC_EOL|BT848_RISC_SOL|(13<<20);
		*(po++)=kvirt_to_bus((ulong)btv->vbibuf+i*2048);
	}
	*(po++)=BT848_RISC_JUMP;
	*(po++)=virt_to_bus(btv->risc_jmp+4);

	*(pe++)=BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1; *(pe++)=0;
	for (i=16; i<32; i++) 
	{
		*(pe++)=BT848_RISC_WRITE|2044|BT848_RISC_EOL|BT848_RISC_SOL;
		*(pe++)=kvirt_to_bus((ulong)btv->vbibuf+i*2048);
	}
	*(pe++)=BT848_RISC_JUMP|BT848_RISC_IRQ|(0x01<<16);
	*(pe++)=virt_to_bus(btv->risc_jmp+10);
}

/*
 *	Set the registers for the size we have specified. Don't bother
 *	trying to understand this without the BT848 manual in front of
 *	you [AC]. 
 *
 *	PS: The manual is free for download in .pdf format from 
 *	www.brooktree.com - nicely done those folks.
 */
 
static void bt848_set_size(struct bttv *btv)
{
	u16 vscale, hscale;
	u32 xsf, sr;
	u16 hdelay, vdelay;
	u16 hactive, vactive;
	u16 inter;
	u8 crop;

	/*
	 *	No window , no try...
	 */
	 
	if (!btv->win.width)
		return;
	if (!btv->win.height)
		return;
    
	inter=(btv->win.interlace&1)^1;

	switch (btv->win.bpp) 
	{
		/*
		 * RGB8 seems to be a 9x5x5 GRB color cube starting at color 16
		 * Why the h... can't they even mention this in the datasheet???
		 */
		case 1: 
			btwrite(BT848_COLOR_FMT_RGB8, BT848_COLOR_FMT);
			btand(~0x10, BT848_CAP_CTL); // Dithering looks much better in this mode
			break;
		case 2: 
			btwrite(BT848_COLOR_FMT_RGB16, BT848_COLOR_FMT);
			btor(0x10, BT848_CAP_CTL);
			break;
		case 3: 
			btwrite(BT848_COLOR_FMT_RGB24, BT848_COLOR_FMT);
			btor(0x10, BT848_CAP_CTL);
			break;
		case 4: 
			btwrite(BT848_COLOR_FMT_RGB32, BT848_COLOR_FMT);
			btor(0x10, BT848_CAP_CTL);
			break;
	}
	
	/*
	 *	Set things up according to the final picture width.
	 */
	 
	hactive=btv->win.width;
	if (hactive < 193) 
	{
		btwrite (2, BT848_E_VTC);
		btwrite (2, BT848_O_VTC);
	}
	else if (hactive < 385) 
	{
		btwrite (1, BT848_E_VTC);
		btwrite (1, BT848_O_VTC);
	}
	else 
	{
		btwrite (0, BT848_E_VTC);
		btwrite (0, BT848_O_VTC);
	}

	/*
	 *	Ok are we doing Never The Same Color or PAL ?
	 */
	 
	if (btv->win.norm==1) 
	{ 
		btv->win.cropwidth=640;
		btv->win.cropheight=480;
		btwrite(0x68, BT848_ADELAY);
		btwrite(0x5d, BT848_BDELAY);
		btaor(BT848_IFORM_NTSC, ~7, BT848_IFORM);
		btaor(BT848_IFORM_XT0, ~0x18, BT848_IFORM);
		xsf = (btv->win.width*365625UL)/300000UL;
		hscale = ((910UL*4096UL)/xsf-4096);
		vdelay=btv->win.cropy+0x16;
		hdelay=((hactive*135)/754+btv->win.cropx)&0x3fe;
	}
	else
	{
		btv->win.cropwidth=768;
		btv->win.cropheight=576;
		if (btv->win.norm==0)
		{ 
			btwrite(0x7f, BT848_ADELAY);
			btwrite(0x72, BT848_BDELAY);
			btaor(BT848_IFORM_PAL_BDGHI, ~BT848_IFORM_NORM, BT848_IFORM);
		}
		else
		{
			btwrite(0x7f, BT848_ADELAY);
			btwrite(0x00, BT848_BDELAY);
			btaor(BT848_IFORM_SECAM, ~BT848_IFORM_NORM, BT848_IFORM);
		}
		btaor(BT848_IFORM_XT1, ~0x18, BT848_IFORM);
		xsf = (btv->win.width*36875UL)/30000UL;
		hscale = ((1135UL*4096UL)/xsf-4096);
		vdelay=btv->win.cropy+0x20;
		hdelay=((hactive*186)/922+btv->win.cropx)&0x3fe;
	}
	sr=((btv->win.cropheight>>inter)*512)/btv->win.height-512;
	vscale=(0x10000UL-sr)&0x1fff;
	vactive=btv->win.cropheight;

#if 0
	printk("bttv: hscale=0x%04x, ",hscale);
	printk("bttv: vscale=0x%04x\n",vscale);

	printk("bttv: hdelay =0x%04x\n",hdelay);
	printk("bttv: hactive=0x%04x\n",hactive);
	printk("bttv: vdelay =0x%04x\n",vdelay);
	printk("bttv: vactive=0x%04x\n",vactive);
#endif

	/*
	 *	Interlace is set elsewhere according to the final image 
	 *	size we desire
	 */
	 
	if (btv->win.interlace) 
	{ 
		btor(BT848_VSCALE_INT, BT848_E_VSCALE_HI);
		btor(BT848_VSCALE_INT, BT848_O_VSCALE_HI);
	}
	else
	{
		btand(~BT848_VSCALE_INT, BT848_E_VSCALE_HI);
		btand(~BT848_VSCALE_INT, BT848_O_VSCALE_HI);
	}

	/*
	 *	Load her up
	 */
	 
	btwrite(hscale>>8, BT848_E_HSCALE_HI);
	btwrite(hscale>>8, BT848_O_HSCALE_HI);
	btwrite(hscale&0xff, BT848_E_HSCALE_LO);
	btwrite(hscale&0xff, BT848_O_HSCALE_LO);

	btwrite((vscale>>8)|(btread(BT848_E_VSCALE_HI)&0xe0), BT848_E_VSCALE_HI);
	btwrite((vscale>>8)|(btread(BT848_O_VSCALE_HI)&0xe0), BT848_O_VSCALE_HI);
	btwrite(vscale&0xff, BT848_E_VSCALE_LO);
	btwrite(vscale&0xff, BT848_O_VSCALE_LO);

	btwrite(hactive&0xff, BT848_E_HACTIVE_LO);
	btwrite(hactive&0xff, BT848_O_HACTIVE_LO);
	btwrite(hdelay&0xff, BT848_E_HDELAY_LO);
	btwrite(hdelay&0xff, BT848_O_HDELAY_LO);

	btwrite(vactive&0xff, BT848_E_VACTIVE_LO);
	btwrite(vactive&0xff, BT848_O_VACTIVE_LO);
	btwrite(vdelay&0xff, BT848_E_VDELAY_LO);
	btwrite(vdelay&0xff, BT848_O_VDELAY_LO);

	crop=((hactive>>8)&0x03)|((hdelay>>6)&0x0c)|
		((vactive>>4)&0x30)|((vdelay>>2)&0xc0);
	btwrite(crop, BT848_E_CROP);
	btwrite(crop, BT848_O_CROP);
}


/*
 *	The floats in the tuner struct are computed at compile time
 *	by gcc and cast back to integers. Thus we don't violate the
 *	"no float in kernel" rule.
 */
 
static struct tunertype tuners[] = {
	{"Temic PAL", TEMIC, PAL,
		16*140.25,16*463.25,0x02,0x04,0x01,0x8e,0xc2},
	{"Philips PAL_I", Philips, PAL_I,
		16*140.25,16*463.25,0x00,0x00,0x00,0x00,0x00},
	{"Philips NTSC", Philips, NTSC,
		16*157.25,16*451.25,0xA0,0x90,0x30,0x8e,0xc0},
	{"Philips SECAM", Philips, SECAM,
		16*168.25,16*447.25,0xA3,0x93,0x33,0x8e,0xc0},
	{"NoTuner", NoTuner, NOTUNER,
		0        ,0        ,0x00,0x00,0x00,0x00,0x00},
	{"Philips PAL", Philips, PAL,
		16*168.25,16*447.25,0xA0,0x90,0x30,0x8e,0xc0},
	{"Temic NTSC", TEMIC, NTSC,
		16*157.25,16*463.25,0x02,0x04,0x01,0x8e,0xc2},
	{"TEMIC PAL_I", TEMIC, PAL_I,
		0        ,0        ,0x00,0x00,0x00,0x00,0xc2},
}; 

/*
 *	Set TSA5522 synthesizer frequency in 1/16 Mhz steps
 */

static void set_freq(struct bttv *btv, ushort freq)
{
	u8 config;
	u16 div;
	struct tunertype *tun=&tuners[btv->tuner];
	int oldAudio = btv->audio;

	audio(btv, AUDIO_MUTE);
	udelay(AUDIO_MUTE_DELAY);
	if (freq < tun->thresh1) 
		config = tun->VHF_L;
	else if (freq < tun->thresh2) 
		config = tun->VHF_H;
	else
		config = tun->UHF;

	div=freq+623; /* div=((freq+16*38.9));*/
  
	div&=0x7fff;
	if (I2CWrite(btv, btv->tuneradr, (div>>8)&0x7f, div&0xff, 1)<0)
		return;
	I2CWrite(btv, btv->tuneradr, tun->config, config, 1);
	if (!(oldAudio & AUDIO_MUTE))
	{
		udelay(FREQ_CHANGE_DELAY);
		audio(btv, AUDIO_UNMUTE);
	}
}

static long bttv_write(struct video_device *v, const char *buf, unsigned long count, int nonblock)
{
	return -EINVAL;
}

static long bttv_read(struct video_device *v, char *buf, unsigned long count, int nonblock)
{
	struct bttv *btv= (struct bttv *)v;
	int q,todo;

	todo=count;
	while (todo && todo>(q=VBIBUF_SIZE-btv->vbip)) 
	{
		if(copy_to_user((void *) buf, (void *) btv->vbibuf+btv->vbip, q))
			return -EFAULT;
		todo-=q;
		buf+=q;

/*		btv->vbip=0; */
		cli();
		if (todo && q==VBIBUF_SIZE-btv->vbip) 
		{
			if(nonblock)
			{
				sti();
				if(count==todo)
					return -EWOULDBLOCK;
				return count-todo;
			}
			interruptible_sleep_on(&btv->vbiq);
			sti();
			if(current->signal & ~current->blocked)
			{
				if(todo==count)
					return -EINTR;
				else
					return count-todo;
			}
		}
	}
	if (todo) 
	{
		if(copy_to_user((void *) buf, (void *) btv->vbibuf+btv->vbip, todo))
			return -EFAULT;
		btv->vbip+=todo;
	}
	return count;
}

/*
 *	Open a bttv card. Right now the flags stuff is just playing
 */
 
static int bttv_open(struct video_device *dev, int flags)
{
	struct bttv *btv = (struct bttv *)dev;
	int users, i;

	switch (flags)
	{
		case 0:
			if (btv->user)
				return -EBUSY;
			btv->user++;
			audio(btv, AUDIO_UNMUTE);
			for (i=users=0; i<bttv_num; i++)
				users+=bttvs[i].user;
			if (users==1)
				find_vga();
			break;
		case 1:
			break;
		case 2:
			btv->vbip=VBIBUF_SIZE;
			btv->cap|=0x0c;
			bt848_set_risc_jmps(btv);
			break;
	}
	MOD_INC_USE_COUNT;
	return 0;   
}

static void bttv_close(struct video_device *dev)
{
	struct bttv *btv=(struct bttv *)dev;
  
	btv->user--;
	audio(btv, AUDIO_MUTE);
	btv->cap&=~3;
#if 0 /* FIXME */	
	if(minor&0x20) 
	{
		btv->cap&=~0x0c;
	}
#endif	
	bt848_set_risc_jmps(btv);

	MOD_DEC_USE_COUNT;  
}

/***********************************/
/* ioctls and supporting functions */
/***********************************/

extern inline void bt848_bright(struct bttv *btv, uint bright)
{
	btwrite(bright&0xff, BT848_BRIGHT);
}

extern inline void bt848_hue(struct bttv *btv, uint hue)
{
	btwrite(hue&0xff, BT848_HUE);
}

extern inline void bt848_contrast(struct bttv *btv, uint cont)
{
	unsigned int conthi;

	conthi=(cont>>6)&4;
	btwrite(cont&0xff, BT848_CONTRAST_LO);
	btaor(conthi, ~4, BT848_E_CONTROL);
	btaor(conthi, ~4, BT848_O_CONTROL);
}

extern inline void bt848_sat_u(struct bttv *btv, ulong data)
{
	u32 datahi;

	datahi=(data>>7)&2;
	btwrite(data&0xff, BT848_SAT_U_LO);
	btaor(datahi, ~2, BT848_E_CONTROL);
	btaor(datahi, ~2, BT848_O_CONTROL);
}

static inline void bt848_sat_v(struct bttv *btv, ulong data)
{
	u32 datahi;

	datahi=(data>>8)&1;
	btwrite(data&0xff, BT848_SAT_V_LO);
	btaor(datahi, ~1, BT848_E_CONTROL);
	btaor(datahi, ~1, BT848_O_CONTROL);
}

/*
 *	Cliprect -> risc table.
 *
 *	FIXME: This is generating wrong code when we have some kinds of
 *	rectangle lists. I don't currently understand why.
 */
 
static void write_risc_data(struct bttv *btv, struct video_clip *vp, int count)
{
	int i;
	u32 yy, y, x, dx, ox;
	u32 *rmem, *rmem2;
	struct video_clip first, *cur, *cur2, *nx, first2, *prev, *nx2;
	u32 *rp, rpo=0, rpe=0, p, bpsl;
	u32 *rpp;
	u32 mask;
	int interlace;
	int depth;
  
	rmem=(u32 *)btv->risc_odd;
	rmem2=(u32 *)btv->risc_even;
	depth=btv->win.bpp;
  
	/* create y-sorted list  */
	
	first.next=NULL;
	for (i=0; i<count; i++) 
	{
		cur=&first;
		while ((nx=cur->next) && (vp[i].y > cur->next->y))
			cur=nx; 
		cur->next=&(vp[i]);
		vp[i].next=nx;
	}
	first2.next=NULL;
	
	rmem[rpo++]=BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1; rmem[rpo++]=0;

	rmem2[rpe++]=BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1; rmem2[rpe++]=0;


	/*
	 *	32bit depth frame buffers need extra flags setting
	 */
	 
	if (depth==4)
		mask=BT848_RISC_BYTE3;
	else
		mask=0;
		
	bpsl=btv->win.width*btv->win.bpp;
	p=btv->win.vidadr+btv->win.x*btv->win.bpp+
		btv->win.y*btv->win.bpl;

	interlace=btv->win.interlace;

	/*
	 *	Loop through all lines 
	 */
	 
	for (yy=0; yy<(btv->win.height<<(1^interlace)); yy++) 
	{
		y=yy>>(1^interlace);
		
		/*
		 *	Even or odd frame generation. We have to 
		 *	write the RISC instructions to the right stream.
		 */
		 
		if(!(y&1))
		{
			rp=&rpo;
			rpp=rmem;
		}
		else
		{
			rp=&rpe;
			rpp=rmem2;
		}
		
		
		/*
		 *	first2 is the header of a list of "active" rectangles. We add
		 *	rectangles as we hit their top and remove them as they fall off
		 *	the bottom
		 */
		
		 /* remove rects with y2 > y */
		if ((cur=first2.next)) 
		{
			prev=&first2;
			do 
			{
				if (cur->y+cur->height < y) 
					prev->next=cur->next;
				else
					prev=cur;
			}
			while ((cur=cur->next));
		}

		/* add rect to second (x-sorted) list if rect.y == y  */
		if ((cur=first.next)) 
		{
			while ((cur) && (cur->y == y))
			{ 
				first.next=cur->next;
				cur2=&first2;
				while ((nx2=cur2->next) && (cur->x > cur2->next->x)) 
					cur2=nx2; 
				cur2->next=cur;
				cur->next=nx2;
				cur=first.next;
			}
		}
		
		
		/*
		 *	Begin writing the RISC script
		 */
    
		dx=x=0;
		
		/*
		 *	Starting at x position 0 on a new scan line
		 *	write to location p, don't yet write the number
		 *	of pixels for the instruction
		 */
		 
		rpp[(*rp)++]=BT848_RISC_WRITE|BT848_RISC_SOL;
		rpp[(*rp)++]=p;
		
		/*
		 *	For each rectangle we have in the "active" list - sorted left to
		 *	right..
		 */
		 
		for (cur2=first2.next; cur2; cur2=cur2->next) 
		{
			/*
			 *	If we are to the left of the first drawing area
			 */
			 
			if (x+dx < cur2->x) 
			{
				/* Bytes pending ? */
				if (dx) 
				{
					/* For a delta away from the start we need to write a SKIP */
					if (x) 
						rpp[(*rp)++]=BT848_RISC_SKIP|(dx*depth);
					else
					/* Rewrite the start of line WRITE to a SKIP */
						rpp[(*rp)-2]|=BT848_RISC_BYTE_ALL|(dx*depth);
					/* Move X to the next point (drawing start) */
					x=x+dx;
				}
				/* Ok how far are we from the start of the next rectangle ? */
				dx=cur2->x-x;
				/* dx is now the size of data to write */
				
				/* If this isnt the left edge generate a "write continue" */
				if (x) 
					rpp[(*rp)++]=BT848_RISC_WRITEC|(dx*depth)|mask;
				else
					/* Fill in the byte count on the initial WRITE */
					rpp[(*rp)-2]|=(dx*depth)|mask;
				/* Move to the start of the rectangle */
				x=cur2->x;
				/* x is our left dx is byte size of hole */
				dx=cur2->width+1;
			}
			else
			/* Already in a clip zone.. set dx */
				if (x+dx < cur2->x+cur2->width) 
					dx=cur2->x+cur2->width-x+1;
		}
		/* now treat the rest of the line */
		ox=x;
		if (dx) 
		{
			/* Complete the SKIP to eat to the end of the gap */
			if (x) 
				rpp[(*rp)++]=BT848_RISC_SKIP|(dx*depth);
			else
			/* Rewrite to SKIP start to this point */
				rpp[(*rp)-2]|=BT848_RISC_BYTE_ALL|(dx*depth);
			x=x+dx;
		}
		
		/*
		 *	Not at the right hand edge ?
		 */
		 
		if ((dx=btv->win.width-x)!=0) 
		{
			/* Write to edge of display */
			if (x)
				rpp[(*rp)++]=BT848_RISC_WRITEC|(dx*depth)|BT848_RISC_EOL|mask;
			else
			/* Entire frame is a write - patch first order */
				rpp[(*rp)-2]|=(dx*depth)|BT848_RISC_EOL|mask;
		}
		else
		{
			/* End of line if needed */
			if (ox)
				rpp[(*rp)-1]|=BT848_RISC_EOL|mask;
			else 
			{
				/* Skip the line : write a SKIP + start/end of line marks */
				(*rp)--;
				rpp[(*rp)-1]=BT848_RISC_SKIP|(btv->win.width*depth)|
					BT848_RISC_EOL|BT848_RISC_SOL;
			}
		}
		/*
		 *	Move the video render pointer on a line 
		 */
		if (interlace||(y&1))
			p+=btv->win.bpl;
	}
	
	/*
	 *	Attach the interframe jumps
	 */

	rmem[rpo++]=BT848_RISC_JUMP; 
	rmem[rpo++]=btv->bus_vbi_even;

	rmem2[rpe++]=BT848_RISC_JUMP;
	rmem2[rpe++]=btv->bus_vbi_odd;
}

/*
 *	Helper for adding clips.
 */

static void new_risc_clip(struct video_window *vw, struct video_clip *vcp, int x, int y, int w, int h)
{
	vcp[vw->clipcount].x=x;
	vcp[vw->clipcount].y=y;
	vcp[vw->clipcount].width=w;
	vcp[vw->clipcount].height=h;
	vw->clipcount++;
}

/*
 *	ioctl routine
 */
 
static int bttv_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	unsigned char eedata[256];
/*	unsigned long data;*/
/*	static ushort creg;*/
	struct bttv *btv=(struct bttv *)dev;
  	static int lastchan=0;
  	
	switch (cmd)
	{	
		case VIDIOCGCAP:
		{
			struct video_capability b;
			strcpy(b.name,btv->video_dev.name);
			b.type = VID_TYPE_CAPTURE|
			 	VID_TYPE_TUNER|
				VID_TYPE_TELETEXT|
				VID_TYPE_OVERLAY|
				VID_TYPE_CLIPPING|
				VID_TYPE_FRAMERAM|
				VID_TYPE_SCALES;
			b.channels = 4; /* tv  , input, svhs */
			b.audios = 4; /* tv, input, svhs */
			b.maxwidth = 768;
			b.maxheight = 576;
			b.minwidth = 32;
			b.minheight = 32;
			if(copy_to_user(arg,&b,sizeof(b)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCGCHAN:
		{
			struct video_channel v;
			if(copy_from_user(&v, arg,sizeof(v)))
				return -EFAULT;
			v.flags=VIDEO_VC_AUDIO;
			v.tuners=0;
			v.type=VIDEO_TYPE_CAMERA;
			switch(v.channel)
			{
				case 0:
					strcpy(v.name,"Television");
					v.flags|=VIDEO_VC_TUNER;
					v.type=VIDEO_TYPE_TV;
					v.tuners=1;
					break;
				case 1:
					strcpy(v.name,"Composite1");
					break;
				case 2:
					strcpy(v.name,"Composite2");
					break;
				case 3:
					strcpy(v.name,"SVHS");
					break;
				default:
					return -EINVAL;
			}
			if(copy_to_user(arg,&v,sizeof(v)))
				return -EFAULT;
			return 0;
		}
		/*
		 *	Each channel has 1 tuner
		 */
		case VIDIOCSCHAN:
		{
			int v;
			if(copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			bt848_muxsel(btv, v);
			lastchan=v;
			return 0;
		}
		case VIDIOCGTUNER:
		{
			struct video_tuner v;
			if(copy_from_user(&v,arg,sizeof(v))!=0)
				return -EFAULT;
			if(v.tuner||lastchan)	/* Only tuner 0 */
				return -EINVAL;
			strcpy(v.name, "Television");
			v.rangelow=0;
			v.rangehigh=0xFFFFFFFF;
			v.flags=VIDEO_TUNER_PAL|VIDEO_TUNER_NTSC;
			v.mode = btv->win.norm;
			if(copy_to_user(arg,&v,sizeof(v)))
				return -EFAULT;
			return 0;
		}
		/* We have but tuner 0 */
		case VIDIOCSTUNER:
		{
			struct video_tuner v;
			if(copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			/* Only channel 0 has a tuner */
			if(v.tuner!=0 || lastchan)
				return -EINVAL;
			if(v.mode!=VIDEO_MODE_PAL||v.mode!=VIDEO_MODE_NTSC)
				return -EOPNOTSUPP;
			btv->win.norm = v.mode;
			bt848_set_size(btv);
			return 0;
		}
		case VIDIOCGPICT:
		{
			struct video_picture p=btv->picture;
			if(btv->win.bpp==8)
				p.palette=VIDEO_PALETTE_HI240;
			if(btv->win.bpp==16)
				p.palette=VIDEO_PALETTE_RGB565;
			if(btv->win.bpp==24)
				p.palette=VIDEO_PALETTE_RGB24;
			if(btv->win.bpp==32)
				p.palette=VIDEO_PALETTE_RGB32;
			if(copy_to_user(arg, &p, sizeof(p)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSPICT:
		{
			struct video_picture p;
			if(copy_from_user(&p, arg,sizeof(p)))
				return -EFAULT;
			/* We want -128 to 127 we get 0-65535 */
			bt848_bright(btv, (p.brightness>>8)-128);
			/* 0-511 for the colour */
			bt848_sat_u(btv, p.colour>>7);
			bt848_sat_v(btv, ((p.colour>>7)*201L)/237);
			/* -128 to 127 */
			bt848_hue(btv, (p.hue>>8)-128);
			/* 0-511 */
			bt848_contrast(btv, p.contrast>>7);
			btv->picture=p;
			return 0;
		}
		case VIDIOCSWIN:
		{
			struct video_window vw;
			struct video_clip *vcp;
			int on;
			
			if(copy_from_user(&vw,arg,sizeof(vw)))
				return -EFAULT;
				
			if(vw.flags)
				return -EINVAL;
				
			btv->win.x=vw.x;
			btv->win.y=vw.y;
			btv->win.width=vw.width;
			btv->win.height=vw.height;

			if(btv->win.height>btv->win.cropheight/2)
				btv->win.interlace=1;
			else
				btv->win.interlace=0;

			on=(btv->cap&3)?1:0;
			
			bt848_cap(btv,0);
			bt848_set_size(btv);

			if(vw.clipcount>256)
				return -EDOM;	/* Too many! */

			/*
			 *	Do any clips.
			 */

			vcp=vmalloc(sizeof(struct video_clip)*(vw.clipcount+4));
			if(vcp==NULL)
				return -ENOMEM;
			if(vw.clipcount && copy_from_user(vcp,vw.clips,sizeof(struct video_clip)*vw.clipcount))
				return -EFAULT;
			/*
			 *	Impose display clips
			 */
			if(btv->win.x<0)
				new_risc_clip(&vw, vcp, 0, 0, -btv->win.x, btv->win.height-1);
			if(btv->win.y<0)
				new_risc_clip(&vw, vcp, 0, 0, btv->win.width-1,-btv->win.y);
			if(btv->win.x+btv->win.width> btv->win.swidth)
				new_risc_clip(&vw, vcp, btv->win.swidth-btv->win.x, 0, btv->win.width-1, btv->win.height-1);
			if(btv->win.y+btv->win.height > btv->win.sheight)
				new_risc_clip(&vw, vcp, 0, btv->win.sheight-btv->win.y, btv->win.width-1, btv->win.height-1);
			/*
			 *	Question: Do we need to walk the clip list
			 *	and saw off any clips outside the window 
			 *	frame or will write_risc_tab do the right
			 *	thing ?
			 */
			write_risc_data(btv,vcp, vw.clipcount);
			vfree(vcp);
			if(on)
				bt848_cap(btv,1);
			return 0;
		}
		case VIDIOCGWIN:
		{
			struct video_window vw;
			/* Oh for a COBOL move corresponding .. */
			vw.x=btv->win.x;
			vw.y=btv->win.y;
			vw.width=btv->win.width;
			vw.height=btv->win.height;
			vw.chromakey=0;
			vw.flags=0;
			if(btv->win.interlace)
				vw.flags|=VIDEO_WINDOW_INTERLACE;
			if(copy_to_user(arg,&vw,sizeof(vw)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCCAPTURE:
		{
			int v;
			if(copy_from_user(&v, arg,sizeof(v)))
				return -EFAULT;
			if(btv->win.vidadr==0 || btv->win.width==0 || btv->win.height==0)
				return -EINVAL;
			if(v==0)
			{
				bt848_cap(btv,0);
			}
			else
			{
				bt848_cap(btv,1);
			}
			return 0;
		}
		case VIDIOCGFBUF:
		{
			struct video_buffer v;
			v.base=(void *)btv->win.vidadr;
			v.height=btv->win.sheight;
			v.width=btv->win.swidth;
			v.depth=btv->win.bpp*8;
			v.bytesperline=btv->win.bpl;
			if(copy_to_user(arg, &v,sizeof(v)))
				return -EFAULT;
			return 0;
			
		}
		case VIDIOCSFBUF:
		{
			struct video_buffer v;
			if(!suser())
				return -EPERM;
			if(copy_from_user(&v, arg,sizeof(v)))
				return -EFAULT;
			if(v.depth!=8 && v.depth!=16 && v.depth!=24 && v.depth!=32)
				return -EINVAL;
			btv->win.vidadr=(int)v.base;
			btv->win.sheight=v.height;
			btv->win.swidth=v.width;
			btv->win.bpp=v.depth/8;
			btv->win.bpl=v.bytesperline;
			
			DEBUG(printk("Display at %p is %d by %d, bytedepth %d, bpl %d\n",
				v.base, v.width,v.height, btv->win.bpp, btv->win.bpl));
			bt848_set_size(btv);
			return 0;		
		}
		case VIDIOCKEY:
		{
			/* Will be handled higher up .. */
			return 0;
		}
		case VIDIOCGFREQ:
		{
			unsigned long v=btv->win.freq;
			if(copy_to_user(arg,&v,sizeof(v)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSFREQ:
		{
			unsigned long v;
			if(copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;
			btv->win.freq=v;
			set_freq(btv, btv->win.freq);
			return 0;
		}
	
		case VIDIOCGAUDIO:
		{
			struct video_audio vp;
			vp=btv->audio_dev;
			vp.flags&=~(VIDEO_AUDIO_MUTE|VIDEO_AUDIO_MUTABLE);
			vp.flags|=VIDEO_AUDIO_MUTABLE;
			return 0;
		}
		case VIDIOCSAUDIO:
		{
			struct video_audio v;
			if(copy_from_user(&v,arg, sizeof(v)))
				return -EFAULT;
			if(v.flags&VIDEO_AUDIO_MUTE)
				audio(btv, AUDIO_MUTE);
			if(v.audio<0||v.audio>2)
				return -EINVAL;
			bt848_muxsel(btv,v.audio);
			if(!(v.flags&VIDEO_AUDIO_MUTE))
				audio(btv, AUDIO_UNMUTE);
			btv->audio_dev=v;
			return 0;
		}

		case BTTV_WRITEEE:
			if(copy_from_user((void *) eedata, (void *) arg, 256))
				return -EFAULT;
			writeee(btv, eedata);
			break;

		case BTTV_READEE:
			readee(btv, eedata);
			if(copy_to_user((void *) arg, (void *) eedata, 256))
				return -EFAULT;
			break;

		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static int bttv_init_done(struct video_device *dev)
{
	return 0;
}

static struct video_device bttv_template=
{
	"UNSET",
	VID_TYPE_TUNER|VID_TYPE_CAPTURE|VID_TYPE_OVERLAY|VID_TYPE_TELETEXT,
	VID_HARDWARE_BT848,
	bttv_open,
	bttv_close,
	bttv_read,
	bttv_write,
	bttv_ioctl,
	NULL,	/* no mmap yet */
	bttv_init_done,
	NULL,
	0,
	0
};

struct vidbases 
{
	ushort vendor, device;
	char *name;
	uint badr;
};

static struct vidbases vbs[] = {
	{ PCI_VENDOR_ID_TSENG, 0, "TSENG", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_MATROX, PCI_DEVICE_ID_MATROX_MIL,
		"Matrox Millennium", PCI_BASE_ADDRESS_1},
	{ PCI_VENDOR_ID_MATROX, 0x051a, "Matrox Mystique", PCI_BASE_ADDRESS_1},
	{ PCI_VENDOR_ID_S3, 0, "S3", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_210888GX,
		"ATI MACH64 Winturbo", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_CIRRUS, 0, "Cirrus Logic", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_N9, PCI_DEVICE_ID_N9_I128, 
		"Number Nine Imagine 128", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_TGA,
		"DEC DC21030", PCI_BASE_ADDRESS_0},
};


/* DEC TGA offsets stolen from XFree-3.2 */

static uint dec_offsets[4] = {
	0x200000,
	0x804000,
	0,
	0x1004000
};

#define NR_CARDS (sizeof(vbs)/sizeof(struct vidbases))

/* Scan for PCI display adapter
   if more than one card is present the last one is used for now */

static int find_vga(void)
{
	unsigned int devfn, class, vendev;
	ushort vendor, device, badr;
	int found=0, bus=0, i, tga_type;
	unsigned int vidadr=0;


	for (devfn = 0; devfn < 0xff; devfn++) 
	{
		if (PCI_FUNC(devfn) != 0)
			continue;
		pcibios_read_config_dword(bus, devfn, PCI_VENDOR_ID, &vendev);
		if (vendev == 0xffffffff || vendev == 0x00000000) 
			continue;
		pcibios_read_config_word(bus, devfn, PCI_VENDOR_ID, &vendor);
		pcibios_read_config_word(bus, devfn, PCI_DEVICE_ID, &device);
		pcibios_read_config_dword(bus, devfn, PCI_CLASS_REVISION, &class);
		class = class >> 16;
/*		if (class == PCI_CLASS_DISPLAY_VGA) {*/
		if ((class>>8) == PCI_BASE_CLASS_DISPLAY ||
			/* Number 9 GXE64Pro needs this */
			class == PCI_CLASS_NOT_DEFINED_VGA) 
		{
			badr=0;
			printk(KERN_INFO "bttv: PCI display adapter: ");
			for (i=0; i<NR_CARDS; i++) 
			{
				if (vendor==vbs[i].vendor) 
				{
					if (vbs[i].device) 
						if (vbs[i].device!=device)
							continue;
					printk("%s.\n", vbs[i].name);
					badr=vbs[i].badr;
					break;
				}
			}
			if (!badr) 
			{
				printk(KERN_ERR "bttv: Unknown video memory base address.\n");
				continue;
			}
			pcibios_read_config_dword(bus, devfn, badr, &vidadr);
			if (vidadr & PCI_BASE_ADDRESS_SPACE_IO) 
			{
				printk(KERN_ERR "bttv: Memory seems to be I/O memory.\n");
				printk(KERN_ERR "bttv: Check entry for your card type in bttv.c vidbases struct.\n");
				continue;
			}
			vidadr &= PCI_BASE_ADDRESS_MEM_MASK;
			if (!vidadr) 
			{
				printk(KERN_ERR "bttv: Memory @ 0, must be something wrong!");
				continue;
			}
      
			if (vendor==PCI_VENDOR_ID_DEC)
				if (device==PCI_DEVICE_ID_DEC_TGA) 
			{
				tga_type = (readl((unsigned long)vidadr) >> 12) & 0x0f;
				if (tga_type != 0 && tga_type != 1 && tga_type != 3) 
				{
					printk(KERN_ERR "bttv: TGA type (0x%x) unrecognized!\n", tga_type);
					found--;
				}
				vidadr+=dec_offsets[tga_type];
			}

			DEBUG(printk(KERN_DEBUG "bttv: memory @ 0x%08x, ", vidadr));
			DEBUG(printk(KERN_DEBUG "devfn: 0x%04x.\n", devfn));
			found++;
		}
	}
  
	if (vidmem)
	{
		vidadr=vidmem<<20;
		printk(KERN_INFO "bttv: Video memory override: 0x%08x\n", vidadr);
		found=1;
	}
	for (i=0; i<BTTV_MAX; i++)
		bttvs[i].win.vidadr=vidadr;

	return found;
}

#define  TRITON_PCON	           0x50 
#define  TRITON_BUS_CONCURRENCY   (1<<0)
#define  TRITON_STREAMING	  (1<<1)
#define  TRITON_WRITE_BURST	  (1<<2)
#define  TRITON_PEER_CONCURRENCY  (1<<3)
  
static void handle_chipset(void)
{
	int index;
  
	for (index = 0; index < 8; index++)
	{
		unsigned char bus, devfn;
		unsigned char b, bo;
    
		/* nothing wrong with this one, just checking buffer control config */

		if (!pcibios_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82441,
			    index, &bus, &devfn)) 
		{
			pcibios_read_config_byte(bus, devfn, 0x53, &b);
			DEBUG(printk(KERN_INFO "bttv: Host bridge: 82441FX Natoma, "));
			DEBUG(printk("bufcon=0x%02x\n",b));
		}

		if (!pcibios_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82437,
			    index, &bus, &devfn)) 
		{
			printk(KERN_INFO "bttv: Host bridge 82437FX Triton PIIX\n");
			pcibios_read_config_byte(bus, devfn, TRITON_PCON, &b);
			bo=b;
			DEBUG(printk(KERN_DEBUG "bttv: 82437FX: PCON: 0x%x\n",b));

			/* 430FX (Triton I) freezes with bus concurrency on -> switch it off */
			if(!(b & TRITON_BUS_CONCURRENCY)) 
			{
				printk(KERN_WARNING "bttv: 82437FX: disabling bus concurrency\n");
				b |= TRITON_BUS_CONCURRENCY;
			}

			/* still freezes on other boards -> switch off even more */
			if(b & TRITON_PEER_CONCURRENCY) 
			{
				printk(KERN_WARNING "bttv: 82437FX: disabling peer concurrency\n");
				b &= ~TRITON_PEER_CONCURRENCY;
			}
			if(!(b & TRITON_STREAMING)) 
			{
				printk(KERN_WARNING "bttv: 82437FX: disabling streaming\n");
				b |=  TRITON_STREAMING;
			}

			if (b!=bo) 
			{
				pcibios_write_config_byte(bus, devfn, TRITON_PCON, b); 
				printk(KERN_DEBUG "bttv: 82437FX: PCON changed to: 0x%x\n",b);
			}
		}
	}
}

static void init_tda9850(struct bttv *btv)
{
	I2CWrite(btv, I2C_TDA9850, TDA9850_CON3, 0, 1);
}

/* Figure out card and tuner type */

static void idcard(struct bttv *btv)
{
	int i;

	btwrite(0, BT848_GPIO_OUT_EN);
	DEBUG(printk(KERN_DEBUG "bttv: GPIO: 0x%08x\n", btread(BT848_GPIO_DATA)));

	btv->type=BTTV_MIRO;
	btv->tuner=tuner;
  
	if (I2CRead(btv, I2C_HAUPEE)>=0)
		btv->type=BTTV_HAUPPAUGE;
	else if (I2CRead(btv, I2C_STBEE)>=0)
		btv->type=BTTV_STB;

	for (i=0xc0; i<0xd0; i+=2)
	{
		if (I2CRead(btv, i)>=0) 
		{
			btv->tuneradr=i;
			break;
		}
	}

	btv->dbx = I2CRead(btv, I2C_TDA9850) ? 0 : 1;

	if (btv->dbx)
		init_tda9850(btv);

	/* How do I detect the tuner type for other cards but Miro ??? */
	printk(KERN_INFO "bttv: model: ");
	switch (btv->type) 
	{
		case BTTV_MIRO:
			btv->tuner=((btread(BT848_GPIO_DATA)>>10)-1)&7;
			printk("MIRO");
			strcpy(btv->video_dev.name,"BT848(Miro)");
			break;
		case BTTV_HAUPPAUGE:
			printk("HAUPPAUGE");
			strcpy(btv->video_dev.name,"BT848(Hauppauge)");
			break;
		case BTTV_STB: 
			printk("STB");
			strcpy(btv->video_dev.name,"BT848(STB)");
			break;
		case BTTV_INTEL: 
			printk("Intel");
			strcpy(btv->video_dev.name,"BT848(Intel)");
			break;
		case BTTV_DIAMOND: 
			printk("Diamond");
			strcpy(btv->video_dev.name,"BT848(Diamond)");
			break;
	}
	printk(" (%s @ 0x%02x)\n", tuners[btv->tuner].name, btv->tuneradr);
	audio(btv, AUDIO_MUTE);
}


static void bt848_set_risc_jmps(struct bttv *btv)
{
	int flags=btv->cap;
  
	btv->risc_jmp[0]=BT848_RISC_SYNC|BT848_RISC_RESYNC|BT848_FIFO_STATUS_VRE;
	btv->risc_jmp[1]=0;

	btv->risc_jmp[2]=BT848_RISC_JUMP;
	if (flags&8)
		btv->risc_jmp[3]=virt_to_bus(btv->vbi_odd);
	else
		btv->risc_jmp[3]=virt_to_bus(btv->risc_jmp+4);

	btv->risc_jmp[4]=BT848_RISC_JUMP;
	if (flags&2)
		btv->risc_jmp[5]=virt_to_bus(btv->risc_odd);
	else
		btv->risc_jmp[5]=virt_to_bus(btv->risc_jmp+6);

	btv->risc_jmp[6]=BT848_RISC_SYNC|BT848_RISC_RESYNC|BT848_FIFO_STATUS_VRO;
	btv->risc_jmp[7]=0;

	btv->risc_jmp[8]=BT848_RISC_JUMP;
	if (flags&4)
		btv->risc_jmp[9]=virt_to_bus(btv->vbi_even);
	else
		btv->risc_jmp[9]=virt_to_bus(btv->risc_jmp+10);

	btv->risc_jmp[10]=BT848_RISC_JUMP;
	if (flags&1)
		btv->risc_jmp[11]=virt_to_bus(btv->risc_even);
	else
		btv->risc_jmp[11]=virt_to_bus(btv->risc_jmp);

	btaor(flags, ~0x0f, BT848_CAP_CTL);
	if (flags&0x0f)
		bt848_dma(btv, 3);
	else
		bt848_dma(btv, 0);
}


static int init_bt848(struct bttv *btv)
{
	/* reset the bt848 */
	btwrite(0,BT848_SRESET);
	btv->user=0; 

	DEBUG(printk(KERN_DEBUG "bttv: bt848_mem: 0x%08x\n",(unsigned int) btv->bt848_mem));

	/* default setup for max. PAL size in a 1024xXXX hicolor framebuffer */

	btv->win.norm=0; /* change this to 1 for NTSC, 2 for SECAM */
	btv->win.interlace=1;
	btv->win.x=0;
	btv->win.y=0;
	btv->win.width=768; /* 640 */
	btv->win.height=576; /* 480 */
	btv->win.cropwidth=768; /* 640 */
	btv->win.cropheight=576; /* 480 */
	btv->win.cropx=0;
	btv->win.cropy=0;
	btv->win.bpp=2;
	btv->win.bpl=1024*btv->win.bpp;
	btv->win.swidth=1024;
	btv->win.sheight=768;
	btv->cap=0;

	if (!(btv->risc_odd=(dword *) kmalloc(RISCMEM_LEN/2, GFP_KERNEL)))
		return -1;
	if (!(btv->risc_even=(dword *) kmalloc(RISCMEM_LEN/2, GFP_KERNEL)))
		return -1;
	if (!(btv->risc_jmp =(dword *) kmalloc(1024, GFP_KERNEL)))
		return -1;
	btv->vbi_odd=btv->risc_jmp+12;
	btv->vbi_even=btv->vbi_odd+256;
	btv->bus_vbi_odd=virt_to_bus(btv->risc_jmp);
	btv->bus_vbi_even=virt_to_bus(btv->risc_jmp+6);

	btwrite(virt_to_bus(btv->risc_jmp+2), BT848_RISC_STRT_ADD);
	btv->vbibuf=(unchar *) vmalloc(VBIBUF_SIZE);
	if (!btv->vbibuf) 
		return -1;

	bt848_muxsel(btv, 1);
	bt848_set_size(btv);

/*	btwrite(0, BT848_TDEC); */
	btwrite(0x10, BT848_COLOR_CTL);
	btwrite(0x00, BT848_CAP_CTL);

	btwrite(0x0ff, BT848_VBI_PACK_SIZE);
	btwrite(1, BT848_VBI_PACK_DEL);

	btwrite(0xfc, BT848_GPIO_DMA_CTL);
	btwrite(BT848_IFORM_MUX1 | BT848_IFORM_XTAUTO | BT848_IFORM_PAL_BDGHI,
		BT848_IFORM);

	bt848_bright(btv, 0x10);
	btwrite(0xd8, BT848_CONTRAST_LO);

	btwrite(0x60, BT848_E_VSCALE_HI);
	btwrite(0x60, BT848_O_VSCALE_HI);
	btwrite(/*BT848_ADC_SYNC_T|*/
		BT848_ADC_RESERVED|BT848_ADC_CRUSH, BT848_ADC);

	btwrite(BT848_CONTROL_LDEC, BT848_E_CONTROL);
	btwrite(BT848_CONTROL_LDEC, BT848_O_CONTROL);
	btwrite(0x00, BT848_E_SCLOOP);
	btwrite(0x00, BT848_O_SCLOOP);

	btwrite(0xffffffUL,BT848_INT_STAT);
/*	  BT848_INT_PABORT|BT848_INT_RIPERR|BT848_INT_PPERR|BT848_INT_FDSR|
	  BT848_INT_FTRGT|BT848_INT_FBUS|*/
	btwrite(BT848_INT_ETBF|
		BT848_INT_SCERR|
		BT848_INT_RISCI|BT848_INT_OCERR|BT848_INT_VPRES|
		BT848_INT_FMTCHG|BT848_INT_HLOCK,
		BT848_INT_MASK);

/*	make_risctab(btv); */
	make_vbitab(btv);
	bt848_set_risc_jmps(btv);
  
	/*
	 *	Now add the template and register the device unit.
	 */

	memcpy(&btv->video_dev,&bttv_template,sizeof(bttv_template));
	idcard(btv);
	if(video_register_device(&btv->video_dev)<0)
		return -1;
	return 0;
}


static void bttv_irq(int irq, void *dev_id, struct pt_regs * regs)
{
	u32 stat,astat;
	u32 dstat;
	int count;
	struct bttv *btv;
 
	btv=(struct bttv *)dev_id;
	count=0;
	while (1) 
	{
		/* get/clear interrupt status bits */
		stat=btread(BT848_INT_STAT);
		astat=stat&btread(BT848_INT_MASK);
		if (!astat)
			return;
		btwrite(astat,BT848_INT_STAT);
		IDEBUG(printk ("bttv: astat %08x\n",astat));
		IDEBUG(printk ("bttv:  stat %08x\n",stat));

		/* get device status bits */
		dstat=btread(BT848_DSTATUS);
    
		if (astat&BT848_INT_FMTCHG) 
		{
			IDEBUG(printk ("bttv: IRQ_FMTCHG\n"));
/*			btv->win.norm&=(dstat&BT848_DSTATUS_NUML) ? (~1) : (~0); */
		}
		if (astat&BT848_INT_VPRES) 
		{
			IDEBUG(printk ("bttv: IRQ_VPRES\n"));
		}
		if (astat&BT848_INT_VSYNC) 
		{
			IDEBUG(printk ("bttv: IRQ_VSYNC\n"));
		}
		if (astat&BT848_INT_SCERR) {
			IDEBUG(printk ("bttv: IRQ_SCERR\n"));
			bt848_dma(btv, 0);
			bt848_dma(btv, 1);
			wake_up_interruptible(&btv->vbiq);
			wake_up_interruptible(&btv->capq);
		}
		if (astat&BT848_INT_RISCI) 
		{
			IDEBUG(printk ("bttv: IRQ_RISCI\n"));
			/* printk ("bttv: IRQ_RISCI%d\n",stat>>28); */
			if (stat&(1<<28)) 
			{
				btv->vbip=0;
				wake_up_interruptible(&btv->vbiq);
			}
			if (stat&(2<<28)) 
			{
				bt848_set_risc_jmps(btv);
				wake_up_interruptible(&btv->capq);
				break;
			}
		}
		if (astat&BT848_INT_OCERR) 
		{
			IDEBUG(printk ("bttv: IRQ_OCERR\n"));
		}
		if (astat&BT848_INT_PABORT) 
		{
			IDEBUG(printk ("bttv: IRQ_PABORT\n"));
		}
		if (astat&BT848_INT_RIPERR) 
		{
			IDEBUG(printk ("bttv: IRQ_RIPERR\n"));
		}
		if (astat&BT848_INT_PPERR) 
		{
			IDEBUG(printk ("bttv: IRQ_PPERR\n"));
		}
		if (astat&BT848_INT_FDSR) 
		{
			IDEBUG(printk ("bttv: IRQ_FDSR\n"));
		}
		if (astat&BT848_INT_FTRGT) 
		{
			IDEBUG(printk ("bttv: IRQ_FTRGT\n"));
		}
		if (astat&BT848_INT_FBUS) 
		{
			IDEBUG(printk ("bttv: IRQ_FBUS\n"));
		}
		if (astat&BT848_INT_HLOCK) 
		{
			if (dstat&BT848_DSTATUS_HLOC)
				audio(btv, AUDIO_ON);
			else
				audio(btv, AUDIO_OFF);
		}
    
		if (astat&BT848_INT_I2CDONE) 
		{
		}
    
		count++;
		if (count > 10)
			printk (KERN_WARNING "bttv: irq loop %d\n", count);
		if (count > 20) 
		{
			btwrite(0, BT848_INT_MASK);
			printk(KERN_ERR "bttv: IRQ lockup, cleared int mask\n");
		}
	}
}


/*
 *	Scan for a Bt848 card, request the irq and map the io memory 
 */
 
static int find_bt848(void)
{
	short pci_index;    
	unsigned char command, latency;
	int result;
	unsigned char bus, devfn;
	struct bttv *btv;

	bttv_num=0;

	if (!pcibios_present()) 
	{
		DEBUG(printk(KERN_DEBUG "bttv: PCI-BIOS not present or not accessable!\n"));
		return 0;
	}

	for (pci_index = 0;
		!pcibios_find_device(PCI_VENDOR_ID_BROOKTREE, PCI_DEVICE_ID_BT848,
			    pci_index, &bus, &devfn);
		++pci_index) 
	{
		btv=&bttvs[bttv_num];
		btv->bus=bus;
		btv->devfn=devfn;
		btv->bt848_mem=NULL;
		btv->vbibuf=NULL;
		btv->risc_jmp=NULL;
		btv->vbi_odd=NULL;
		btv->vbi_even=NULL;
		btv->vbiq=NULL;
		btv->capq=NULL;
		btv->vbip=VBIBUF_SIZE;

		pcibios_read_config_byte(btv->bus, btv->devfn,
			PCI_INTERRUPT_LINE, &btv->irq);
		pcibios_read_config_dword(btv->bus, btv->devfn, PCI_BASE_ADDRESS_0,
			&btv->bt848_adr);
    
		if (remap&&(!bttv_num))
		{ 
			remap<<=20;
			remap&=PCI_BASE_ADDRESS_MEM_MASK;
			printk(KERN_INFO "Remapping to : 0x%08x.\n", remap);
			remap|=btv->bt848_adr&(~PCI_BASE_ADDRESS_MEM_MASK);
			pcibios_write_config_dword(btv->bus, btv->devfn, PCI_BASE_ADDRESS_0,
				 remap);
			pcibios_read_config_dword(btv->bus, btv->devfn, PCI_BASE_ADDRESS_0,
				&btv->bt848_adr);
		}					
    
		btv->bt848_adr&=PCI_BASE_ADDRESS_MEM_MASK;
		pcibios_read_config_byte(btv->bus, btv->devfn, PCI_CLASS_REVISION,
			     &btv->revision);
		printk(KERN_INFO "bttv: Brooktree Bt848 (rev %d) ",btv->revision);
		printk("bus: %d, devfn: %d, ",
			btv->bus, btv->devfn);
		printk("irq: %d, ",btv->irq);
		printk("memory: 0x%08x.\n", btv->bt848_adr);
    
		btv->bt848_mem=ioremap(btv->bt848_adr, 0x1000);

		result = request_irq(btv->irq, bttv_irq,
			SA_SHIRQ | SA_INTERRUPT,"bttv",(void *)btv);
		if (result==-EINVAL) 
		{
			printk(KERN_ERR "bttv: Bad irq number or handler\n");
			return -EINVAL;
		}
		if (result==-EBUSY)
		{
			printk(KERN_ERR "bttv: IRQ %d busy, change your PnP config in BIOS\n",btv->irq);
			return result;
		}
		if (result < 0) 
			return result;

		/* Enable bus-mastering */
		pcibios_read_config_byte(btv->bus, btv->devfn, PCI_COMMAND, &command);
		command|=PCI_COMMAND_MASTER;
		pcibios_write_config_byte(btv->bus, btv->devfn, PCI_COMMAND, command);
		pcibios_read_config_byte(btv->bus, btv->devfn, PCI_COMMAND, &command);
		if (!(command&PCI_COMMAND_MASTER)) 
		{
			printk(KERN_ERR "bttv: PCI bus-mastering could not be enabled\n");
			return -1;
		}
		pcibios_read_config_byte(btv->bus, btv->devfn, PCI_LATENCY_TIMER,
			&latency);
		if (!latency) 
		{
			latency=32;
			pcibios_write_config_byte(btv->bus, btv->devfn, PCI_LATENCY_TIMER,
				latency);
		}
		DEBUG(printk(KERN_DEBUG "bttv: latency: %02x\n", latency));
		bttv_num++;
	}
	if(bttv_num)
		printk(KERN_INFO "bttv: %d Bt848 card(s) found.\n", bttv_num);
	return bttv_num;
}

static void release_bttv(void)
{
	u8 command;
	int i;
	struct bttv *btv;

	for (i=0;i<bttv_num; i++) 
	{
		btv=&bttvs[i];
		/* turn off all capturing, DMA and IRQs */

		/* first disable interrupts before unmapping the memory! */
		btwrite(0, BT848_INT_MASK);
		btwrite(0xffffffffUL,BT848_INT_STAT);
		btwrite(0x0, BT848_GPIO_OUT_EN);

		bt848_cap(btv, 0);
    
		/* disable PCI bus-mastering */
		pcibios_read_config_byte(btv->bus, btv->devfn, PCI_COMMAND, &command);
		command|=PCI_COMMAND_MASTER;
		pcibios_write_config_byte(btv->bus, btv->devfn, PCI_COMMAND, command);
    
		/* unmap and free memory */
		if (btv->risc_odd)
			kfree((void *) btv->risc_odd);
			
		if (btv->risc_even)
			kfree((void *) btv->risc_even);

		DEBUG(printk(KERN_DEBUG "free: risc_jmp: 0x%08x.\n", btv->risc_jmp));
		if (btv->risc_jmp)
			kfree((void *) btv->risc_jmp);

		DEBUG(printk(KERN_DEBUG "bt848_vbibuf: 0x%08x.\n", btv->vbibuf));
		if (btv->vbibuf)
			vfree((void *) btv->vbibuf);
		free_irq(btv->irq,btv);
		DEBUG(printk(KERN_DEBUG "bt848_mem: 0x%08x.\n", btv->bt848_mem));
		if (btv->bt848_mem)
			iounmap(btv->bt848_mem);
		video_unregister_device(&btv->video_dev);
	}
}


#ifdef MODULE

int init_module(void)
{
#else
int init_bttv_cards(struct video_init *unused)
{
#endif
	int i;
  
	handle_chipset();
	if (find_bt848()<0)
		return -EIO;

	for (i=0; i<bttv_num; i++) 
	{
		if (init_bt848(&bttvs[i])<0) 
		{
			release_bttv();
			return -EIO;
		} 
	}  
	return 0;
}

#ifdef MODULE

void cleanup_module(void)
{
	release_bttv();
}

#endif
