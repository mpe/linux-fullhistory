/* 
    bttv - Bt848 frame grabber driver

    Copyright (C) 1996,97,98 Ralph  Metzler (rjkm@thp.uni-koeln.de)
                           & Marcus Metzler (mocm@thp.uni-koeln.de)

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
 
    TODO:  
   
    * move norm from tuner to channel struct!?
      composite source from a satellite tuner can deliver different norms
      depending on tuned channel
    * mmap VBI data?
    * use new PCI routines
    * fix RAW Composite grabbing for NTSC 
    * allow for different VDELAY in RAW grabbing?
    * extra modules for tda9850, tda8425, any volunteers???
    * support 15bpp
*/

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/malloc.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/pci.h>
#include <linux/signal.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/page.h>
#include <linux/sched.h>
#include <asm/segment.h>
#include <linux/types.h>
#include <linux/wrapper.h>

#include <linux/videodev.h>
#include <linux/version.h>
#include <asm/uaccess.h>

#include <linux/i2c.h>
#include "bttv.h"
#include "tuner.h"

#define DEBUG(x)		/* Debug driver */	
#define IDEBUG(x) 		/* Debug interrupt handler */

MODULE_PARM(vidmem,"i");
MODULE_PARM(triton1,"i");
MODULE_PARM(remap,"1-4i");
MODULE_PARM(radio,"1-4i");
MODULE_PARM(card,"1-4i");
MODULE_PARM(pll,"1-4i");

static int find_vga(void);
static void bt848_set_risc_jmps(struct bttv *btv);

/* Anybody who uses more than four? */
#define BTTV_MAX 4

static unsigned int vidmem=0;   /* manually set video mem address */
static int triton1=0;

static unsigned int remap[BTTV_MAX];    /* remap Bt848 */
static unsigned int radio[BTTV_MAX];
static unsigned int card[BTTV_MAX] = { 0, 0, 
                                       0, 0 };
static unsigned int pll[BTTV_MAX] = { 0, 0, 0, 0 };          

static int bttv_num;			/* number of Bt848s in use */
static struct bttv bttvs[BTTV_MAX];

#define I2C_TIMING (0x7<<4)
#define I2C_DELAY   10
#define I2C_SET(CTRL,DATA) \
    { btwrite((CTRL<<1)|(DATA), BT848_I2C); udelay(I2C_DELAY); }
#define I2C_GET()   (btread(BT848_I2C)&1)

#define AUDIO_MUTE_DELAY 10000
#define FREQ_CHANGE_DELAY 20000
#define EEPROM_WRITE_DELAY 20000

/*******************************/
/* Memory management functions */
/*******************************/

/* convert virtual user memory address to physical address */
/* (virt_to_phys only works for kmalloced kernel memory) */

static inline unsigned long uvirt_to_phys(unsigned long adr)
{
	pgd_t *pgd;
	pmd_t *pmd;
	pte_t *ptep, pte;
  
	pgd = pgd_offset(current->mm, adr);
	if (pgd_none(*pgd))
		return 0;
	pmd = pmd_offset(pgd, adr);
	if (pmd_none(*pmd))
		return 0;
	ptep = pte_offset(pmd, adr/*&(~PGDIR_MASK)*/);
	pte = *ptep;
	if(pte_present(pte))
		return 
		  virt_to_phys((void *)(pte_page(pte)|(adr&(PAGE_SIZE-1))));
	return 0;
}

static inline unsigned long uvirt_to_bus(unsigned long adr) 
{
	return virt_to_bus(phys_to_virt(uvirt_to_phys(adr)));
}

/* convert virtual kernel memory address to physical address */
/* (virt_to_phys only works for kmalloced kernel memory) */

static inline unsigned long kvirt_to_phys(unsigned long adr) 
{
	return uvirt_to_phys(VMALLOC_VMADDR(adr));
}

static inline unsigned long kvirt_to_bus(unsigned long adr) 
{
	return uvirt_to_bus(VMALLOC_VMADDR(adr));
}

static void * rvmalloc(unsigned long size)
{
	void * mem;
	unsigned long adr, page;
        
	mem=vmalloc(size);
	if (mem) 
	{
		memset(mem, 0, size); /* Clear the ram out, no junk to the user */
	        adr=(unsigned long) mem;
		while (size > 0) 
                {
	                page = kvirt_to_phys(adr);
			mem_map_reserve(MAP_NR(phys_to_virt(page)));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
	}
	return mem;
}

static void rvfree(void * mem, unsigned long size)
{
        unsigned long adr, page;
        
	if (mem) 
	{
	        adr=(unsigned long) mem;
		while (size > 0) 
                {
	                page = kvirt_to_phys(adr);
			mem_map_unreserve(MAP_NR(phys_to_virt(page)));
			adr+=PAGE_SIZE;
			size-=PAGE_SIZE;
		}
		vfree(mem);
	}
}

/*
 *	Create the giant waste of buffer space we need for now
 *	until we get DMA to user space sorted out (probably 2.3.x)
 *
 *	We only create this as and when someone uses mmap
 */
 
static int fbuffer_alloc(struct bttv *btv)
{
	if(!btv->fbuffer)
		btv->fbuffer=(unsigned char *) rvmalloc(2*BTTV_MAX_FBUF);
	else
		printk(KERN_ERR "bttv%d: Double alloc of fbuffer!\n",
			btv->nr);
	if(!btv->fbuffer)
		return -ENOBUFS;
	return 0;
}


/* ----------------------------------------------------------------------- */
/* I2C functions                                                           */

/* software I2C functions */

static void i2c_setlines(struct i2c_bus *bus,int ctrl,int data)
{
        struct bttv *btv = (struct bttv*)bus->data;
	btwrite((ctrl<<1)|data, BT848_I2C);
	udelay(I2C_DELAY);
}

static int i2c_getdataline(struct i2c_bus *bus)
{
        struct bttv *btv = (struct bttv*)bus->data;
	return btread(BT848_I2C)&1;
}

/* hardware I2C functions */

/* read I2C */
static int I2CRead(struct i2c_bus *bus, unsigned char addr) 
{
	u32 i;
	u32 stat;
	struct bttv *btv = (struct bttv*)bus->data;
  
	/* clear status bit ; BT848_INT_RACK is ro */
	btwrite(BT848_INT_I2CDONE, BT848_INT_STAT);
  
	btwrite(((addr & 0xff) << 24) | btv->i2c_command, BT848_I2C);
  
	/*
	 * Timeout for I2CRead is 1 second (this should be enough, really!)
	 */
	for (i=1000; i; i--)
	{
		stat=btread(BT848_INT_STAT);
		if (stat & BT848_INT_I2CDONE)
                        break;
                mdelay(1);
	}
  
	if (!i) 
	{
		printk(KERN_DEBUG "bttv%d: I2CRead timeout\n",
			btv->nr);
		return -1;
	}
	if (!(stat & BT848_INT_RACK))
		return -2;
  
	i=(btread(BT848_I2C)>>8)&0xff;
	return i;
}

/* set both to write both bytes, reset it to write only b1 */

static int I2CWrite(struct i2c_bus *bus, unsigned char addr, unsigned char b1,
		    unsigned char b2, int both)
{
	u32 i;
	u32 data;
	u32 stat;
	struct bttv *btv = (struct bttv*)bus->data;
  
	/* clear status bit; BT848_INT_RACK is ro */
	btwrite(BT848_INT_I2CDONE, BT848_INT_STAT);
  
	data=((addr & 0xff) << 24) | ((b1 & 0xff) << 16) | btv->i2c_command;
	if (both)
	{
		data|=((b2 & 0xff) << 8);
		data|=BT848_I2C_W3B;
	}
  
	btwrite(data, BT848_I2C);

	for (i=0x1000; i; i--)
	{
		stat=btread(BT848_INT_STAT);
		if (stat & BT848_INT_I2CDONE)
			break;
                mdelay(1);
	}
  
	if (!i) 
	{
		printk(KERN_DEBUG "bttv%d: I2CWrite timeout\n",
			btv->nr);
		return -1;
	}
	if (!(stat & BT848_INT_RACK))
		return -2;
  
	return 0;
}

/* read EEPROM */
static void readee(struct i2c_bus *bus, unsigned char *eedata)
{
	int i, k;
        
	if (I2CWrite(bus, 0xa0, 0, -1, 0)<0)
	{
		printk(KERN_WARNING "bttv: readee error\n");
		return;
	}
        
	for (i=0; i<256; i++)
	{
		k=I2CRead(bus, 0xa1);
		if (k<0)
		{
			printk(KERN_WARNING "bttv: readee error\n");
			break;
		}
		eedata[i]=k;
	}
}

/* write EEPROM */
static void writeee(struct i2c_bus *bus, unsigned char *eedata)
{
	int i;
  
	for (i=0; i<256; i++)
	{
		if (I2CWrite(bus, 0xa0, i, eedata[i], 1)<0)
		{
			printk(KERN_WARNING "bttv: writeee error (%d)\n", i);
			break;
		}
		udelay(EEPROM_WRITE_DELAY);
	}
}

void attach_inform(struct i2c_bus *bus, int id)
{
        struct bttv *btv = (struct bttv*)bus->data;
	int tunertype;
        
	switch (id) 
        {
        	case I2C_DRIVERID_MSP3400:
                	btv->have_msp3400 = 1;
			break;
        	case I2C_DRIVERID_TUNER:
			btv->have_tuner = 1;
			if (btv->tuner_type != -1) 
			{
				tunertype=btv->tuner_type;
 				i2c_control_device(&(btv->i2c), 
                                                   I2C_DRIVERID_TUNER,
                                                   TUNER_SET_TYPE,&tunertype);
 			}
			break;
	}
}

void detach_inform(struct i2c_bus *bus, int id)
{
        struct bttv *btv = (struct bttv*)bus->data;

	switch (id) 
	{
		case I2C_DRIVERID_MSP3400:
		        btv->have_msp3400 = 0;
			break;
		case I2C_DRIVERID_TUNER:
			btv->have_tuner = 0;
			break;
	}
}

static struct i2c_bus bttv_i2c_bus_template = 
{
        "bt848",
        I2C_BUSID_BT848,
	NULL,

	SPIN_LOCK_UNLOCKED,

	attach_inform,
	detach_inform,
	
	i2c_setlines,
	i2c_getdataline,
	I2CRead,
	I2CWrite,
};
 
/* ----------------------------------------------------------------------- */


struct tvcard
{
        int inputs;
        int tuner;
        int svhs;
        u32 gpiomask;
        u32 muxsel[8];
        u32 audiomux[6]; /* Tuner, Radio, internal, external, mute, stereo */
        u32 gpiomask2; /* GPIO MUX mask */
};

static struct tvcard tvcards[] = 
{
        /* default */
        { 3, 0, 2, 0, { 2, 3, 1, 1}, { 0, 0, 0, 0, 0}},
        /* MIRO */
        { 4, 0, 2,15, { 2, 3, 1, 1}, { 2, 0, 0, 0,10}},
        /* Hauppauge */
        { 3, 0, 2, 7, { 2, 3, 1, 1}, { 0, 1, 2, 3, 4}},
	/* STB */
        { 3, 0, 2, 7, { 2, 3, 1, 1}, { 4, 0, 2, 3, 1}},
	/* Intel??? */
        { 3, 0, 2, 7, { 2, 3, 1, 1}, { 0, 1, 2, 3, 4}},
	/* Diamond DTV2000 */
        { 3, 0, 2, 3, { 2, 3, 1, 1}, { 0, 1, 0, 1, 3}},
	/* AVerMedia TVPhone */
        { 3, 0, 3,15, { 2, 3, 1, 1}, {12, 0,11,11, 0}},
        /* Matrix Vision MV-Delta */
        { 5,-1, 3, 0, { 2, 3, 1, 0, 0}},
        /* Fly Video II */
        { 3, 0, 2, 0xc00, { 2, 3, 1, 1}, 
        {0, 0xc00, 0x800, 0x400, 0xc00, 0}},
        /* TurboTV */
        { 3, 0, 2, 3, { 2, 3, 1, 1}, { 1, 1, 2, 3, 0}},
};
#define TVCARDS (sizeof(tvcards)/sizeof(tvcard))


/*
 *	Tuner, Radio, internal, external and mute 
 */
 
static void audio(struct bttv *btv, int mode)
{
	btaor(tvcards[btv->type].gpiomask, ~tvcards[btv->type].gpiomask,
              BT848_GPIO_OUT_EN);

	switch (mode)
	{
	        case AUDIO_MUTE:
                        btv->audio|=AUDIO_MUTE;
			break;
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
        /* if audio mute or not in H-lock, turn audio off */
	if ((btv->audio&AUDIO_MUTE)
#if 0	
	 || 
	    (!btv->radio && !(btread(BT848_DSTATUS)&BT848_DSTATUS_HLOC))
#endif	    
		)
	        mode=AUDIO_OFF;
        if ((mode == 0) && (btv->radio))
		mode = 1;
	btaor(tvcards[btv->type].audiomux[mode],
              ~tvcards[btv->type].gpiomask, BT848_GPIO_DATA);
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


/* If Bt848a or Bt849, use PLL for PAL/SECAM and crystal for NTSC*/

/* Frequency = (F_input / PLL_X) * PLL_I.PLL_F/PLL_C 
   PLL_X = Reference pre-divider (0=1, 1=2) 
   PLL_C = Post divider (0=6, 1=4)
   PLL_I = Integer input 
   PLL_F = Fractional input 
   
   F_input = 28.636363 MHz: 
   PAL (CLKx2 = 35.46895 MHz): PLL_X = 1, PLL_I = 0x0E, PLL_F = 0xDCF9, PLL_C = 0
*/

static void set_pll_freq(struct bttv *btv, unsigned int fin, unsigned int fout)
{
        unsigned char fl, fh, fi;
        
        /* prevent overflows */
        fin/=4;
        fout/=4;

        fout*=12;
        fi=fout/fin;

        fout=(fout-fin*fi)*256;
        fh=fout/fin;

        fout=(fout-fin*fh)*256;
        fl=fout/fin;

        /*printk("0x%02x 0x%02x 0x%02x\n", fi, fh, fl);*/
        btwrite(fl,BT848_PLL_F_LO);
        btwrite(fh,BT848_PLL_F_HI);
        btwrite(fi|BT848_PLL_X,BT848_PLL_XCI);
}

static int set_pll(struct bttv *btv)
{
        int i;
        unsigned long tv;

        if (!btv->pll.pll_crystal)
		return 0;
        if ((btread(BT848_IFORM)&btv->pll.pll_crystal))
        {
                /* printk ("switching PLL off\n");*/
                btwrite(0x00,BT848_TGCTRL);
                btwrite(0x00,BT848_PLL_XCI);
                btv->pll.pll_crystal&=~2;
                return 0;
        }
        
        /* do not set pll again if already active */
        if (btv->pll.pll_crystal&2)
                return 1;
        
        /* printk ("setting PLL for PAL/SECAM\n");*/

	set_pll_freq(btv, btv->pll.pll_ifreq, btv->pll.pll_ofreq);

	/*
	 *	Let other people run while the PLL stabilizes
	 */
	
	tv=jiffies+HZ/10;	/* .1 seconds */
	do
	{
		schedule();
	}
	while(time_before(jiffies,tv));

        for (i=0; i<100; i++) 
        {
                if ((btread(BT848_DSTATUS)&BT848_DSTATUS_PLOCK))
                        btwrite(0,BT848_DSTATUS);
                else
                {
                        btwrite(0x08,BT848_TGCTRL);
                        btv->pll.pll_crystal|=2;
                        return 1;            
                }
                udelay(10000);
        }
        return -1;
}

static void bt848_muxsel(struct bttv *btv, unsigned int input)
{
	btaor(tvcards[btv->type].gpiomask2,~tvcards[btv->type].gpiomask2,
        	BT848_GPIO_OUT_EN);

	/* This seems to get rid of some synchronization problems */
	btand(~(3<<5), BT848_IFORM);
	mdelay(10); 

       	input %= tvcards[btv->type].inputs;
	if (input==tvcards[btv->type].svhs) 
	{
		btor(BT848_CONTROL_COMP, BT848_E_CONTROL);
		btor(BT848_CONTROL_COMP, BT848_O_CONTROL);
	}
	else
	{
		btand(~BT848_CONTROL_COMP, BT848_E_CONTROL);
		btand(~BT848_CONTROL_COMP, BT848_O_CONTROL);
	}
	btaor((tvcards[btv->type].muxsel[input&7]&3)<<5, ~(3<<5), BT848_IFORM);
	audio(btv, (input!=tvcards[btv->type].tuner) ? 
              AUDIO_EXTERN : AUDIO_TUNER);
	btaor(tvcards[btv->type].muxsel[input]>>4,
		~tvcards[btv->type].gpiomask2, BT848_GPIO_DATA);
}


#define VBIBUF_SIZE 65536

/* Maximum sample number per VBI line is 2044, can NTSC deliver this? 
   Note that we write 2048-aligned to keep alignment to memory pages 
*/
#define VBI_SPL 2044

/* RISC command to write one VBI data line */
#define VBI_RISC BT848_RISC_WRITE|VBI_SPL|BT848_RISC_EOL|BT848_RISC_SOL

static void make_vbitab(struct bttv *btv)
{
	int i;
	unsigned int *po=(unsigned int *) btv->vbi_odd;
	unsigned int *pe=(unsigned int *) btv->vbi_even;
  
	DEBUG(printk(KERN_DEBUG "vbiodd: 0x%08x\n",(int)btv->vbi_odd));
	DEBUG(printk(KERN_DEBUG "vbievn: 0x%08x\n",(int)btv->vbi_even));
	DEBUG(printk(KERN_DEBUG "po: 0x%08x\n",(int)po));
	DEBUG(printk(KERN_DEBUG "pe: 0x%08x\n",(int)pe));

	*(po++)=BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1; *(po++)=0;
	for (i=0; i<16; i++) 
	{
		*(po++)=VBI_RISC;
		*(po++)=kvirt_to_bus((unsigned long)btv->vbibuf+i*2048);
	}
	*(po++)=BT848_RISC_JUMP;
	*(po++)=virt_to_bus(btv->risc_jmp+4);

	*(pe++)=BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1; *(pe++)=0;
	for (i=16; i<32; i++) 
	{
		*(pe++)=VBI_RISC;
		*(pe++)=kvirt_to_bus((unsigned long)btv->vbibuf+i*2048);
	}
	*(pe++)=BT848_RISC_JUMP|BT848_RISC_IRQ|(0x01<<16);
	*(pe++)=virt_to_bus(btv->risc_jmp+10);
	DEBUG(printk(KERN_DEBUG "po: 0x%08x\n",(int)po));
	DEBUG(printk(KERN_DEBUG "pe: 0x%08x\n",(int)pe));
}

int fmtbppx2[16] = {
        8, 6, 4, 4, 4, 3, 2, 2, 4, 3, 0, 0, 0, 0, 2, 0 
};

int palette2fmt[] = {
       0,
       BT848_COLOR_FMT_Y8,
       BT848_COLOR_FMT_RGB8,
       BT848_COLOR_FMT_RGB16,
       BT848_COLOR_FMT_RGB24,
       BT848_COLOR_FMT_RGB32,
       BT848_COLOR_FMT_RGB15,
       BT848_COLOR_FMT_YUY2,
       BT848_COLOR_FMT_BtYUV,
       0,
       0,
       0,
       BT848_COLOR_FMT_RAW,
       BT848_COLOR_FMT_YCrCb422,
       BT848_COLOR_FMT_YCrCb411,
};
#define PALETTEFMT_MAX 11

static int make_rawrisctab(struct bttv *btv, unsigned int *ro,
                            unsigned int *re, unsigned int *vbuf)
{
        unsigned long line;
	unsigned long bpl=1024;		/* bytes per line */
	unsigned long vadr=(unsigned long) vbuf;

	*(ro++)=BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1; *(ro++)=0;
	*(re++)=BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1; *(re++)=0;
  
        /* In PAL 650 blocks of 256 DWORDs are sampled, but only if VDELAY
           is 2 and without separate VBI grabbing.
           We'll have to handle this inside the IRQ handler ... */

	for (line=0; line < 640; line++)
	{
                *(ro++)=BT848_RISC_WRITE|bpl|BT848_RISC_SOL|BT848_RISC_EOL;
                *(ro++)=kvirt_to_bus(vadr);
                *(re++)=BT848_RISC_WRITE|bpl|BT848_RISC_SOL|BT848_RISC_EOL;
                *(re++)=kvirt_to_bus(vadr+BTTV_MAX_FBUF/2);
                vadr+=bpl;
	}
	
	*(ro++)=BT848_RISC_JUMP;
	*(ro++)=btv->bus_vbi_even;
	*(re++)=BT848_RISC_JUMP|BT848_RISC_IRQ|(2<<16);
	*(re++)=btv->bus_vbi_odd;
	
	return 0;
}


static int  make_vrisctab(struct bttv *btv, unsigned int *ro,
                          unsigned int *re, 
                          unsigned int *vbuf, unsigned short width,
                          unsigned short height, unsigned short fmt)
{
        unsigned long line;
	unsigned long bpl;  /* bytes per line */
	unsigned long bl;
	unsigned long todo;
	unsigned int **rp;
	int inter;
	unsigned long vadr=(unsigned long) vbuf;

        if (btv->gfmt==BT848_COLOR_FMT_RAW) 
                return make_rawrisctab(btv, ro, re, vbuf);
        
	inter = (height>btv->win.cropheight/2) ? 1 : 0;
	bpl=width*fmtbppx2[fmt&0xf]/2;
	
	*(ro++)=BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1; *(ro++)=0;
	*(re++)=BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1; *(re++)=0;
  
	for (line=0; line < (height<<(1^inter)); line++)
	{
	        if (inter) 
		        rp= (line&1) ? &re : &ro;
		else 
	                rp= (line>height) ? &re : &ro; 

		bl=PAGE_SIZE-((PAGE_SIZE-1)&vadr);
		if (bpl<=bl)
                {
		        *((*rp)++)=BT848_RISC_WRITE|BT848_RISC_SOL|
			        BT848_RISC_EOL|bpl; 
			*((*rp)++)=kvirt_to_bus(vadr);
			vadr+=bpl;
		}
		else
		{
		        todo=bpl;
		        *((*rp)++)=BT848_RISC_WRITE|BT848_RISC_SOL|bl;
			*((*rp)++)=kvirt_to_bus(vadr);
			vadr+=bl;
			todo-=bl;
			while (todo>PAGE_SIZE)
			{
			        *((*rp)++)=BT848_RISC_WRITE|PAGE_SIZE;
				*((*rp)++)=kvirt_to_bus(vadr);
				vadr+=PAGE_SIZE;
				todo-=PAGE_SIZE;
			}
			*((*rp)++)=BT848_RISC_WRITE|BT848_RISC_EOL|todo;
			*((*rp)++)=kvirt_to_bus(vadr);
			vadr+=todo;
		}
	}
	
	*(ro++)=BT848_RISC_JUMP;
	*(ro++)=btv->bus_vbi_even;
	*(re++)=BT848_RISC_JUMP|BT848_RISC_IRQ|(2<<16);
	*(re++)=btv->bus_vbi_odd;
	
	return 0;
}

/* does this really make a difference ???? */
#define BURST_MAX 4096

static inline void write_risc_segment(unsigned int **rp, unsigned long line_adr, unsigned int command,
			int *x, uint dx, uint bpp, uint width)
{
        unsigned int flags, len;
  
	if (!dx)
                return;
	len=dx*bpp;

#ifdef LIMIT_DMA
	if (command==BT848_RISC_WRITEC)
	{
                unsigned int dx2=BURST_MAX/bpp;
		while (len>BURST_MAX)
		{
	                write_risc_segment(rp, line_adr, command,
					   &x,dx2, bpp, width);
			dx-=dx2;
			len=dx*bpp;
		}
	}
#endif

	/* mask upper 8 bits for 24+8 bit overlay modes */
	flags = ((bpp==4) ? BT848_RISC_BYTE3 : 0);
	
	if (*x==0) 
	{
                if (command==BT848_RISC_SKIP) 
		{
	                if (dx<width)
			{
		                flags|=BT848_RISC_BYTE_ALL;
				command=BT848_RISC_WRITE;
			}
		}
		else
	                if (command==BT848_RISC_WRITEC)
                                command=BT848_RISC_WRITE;
		flags|=BT848_RISC_SOL;
        }
	if (*x+dx==width)
                flags|=BT848_RISC_EOL;
	*((*rp)++)=command|flags|len;
	if (command==BT848_RISC_WRITE)
                *((*rp)++)=line_adr+*x*bpp;
	*x+=dx;
}

static void make_clip_tab(struct bttv *btv, struct video_clip *cr, int ncr)
{
	int i,t;
	int yy, y, x, dx;
	struct video_clip first, *cur, *cur2, *nx, first2, *prev, *nx2;
	int bpp, bpl, width, height, inter;
	unsigned int **rp,*ro,*re;
	unsigned long adr;
	int cx,cx2,cy,cy2;

	inter=(btv->win.interlace&1)^1;
	bpp=btv->win.bpp;
	bpl=btv->win.bpl;
	ro=btv->risc_odd;
	re=btv->risc_even;
	width=btv->win.width;
	height=btv->win.height;
	adr=btv->win.vidadr+btv->win.x*bpp+btv->win.y*bpl;

	/* clip clipping rects against viewing window AND screen 
	   so we do not have to rely on the user program
	 */
	cx=(btv->win.x<0) ? (-btv->win.x) : 0;
	cy=(btv->win.y<0) ? (-btv->win.y) : 0;
	cx2=(btv->win.x+width>btv->win.swidth) ? 
	        (btv->win.swidth-btv->win.x) : width;
	cy2=(btv->win.y+height>btv->win.sheight) ? 
	        (btv->win.sheight-btv->win.y) : height;
	first.next=NULL;
	for (i=0; i<ncr; i++)
	{
                if ((t=cy-cr[i].y)>0)
		{
		        if (cr[i].height<=t)
			        continue;
                        cr[i].height-=t;
			cr[i].y=cy;
		} 
		if ((t=cy2-cr[i].y)<cr[i].height) 
		{
		        if (t<=0)
			        continue;
			cr[i].height=t;
		}
                if ((t=cx-cr[i].x)>0)
		{
		        if (cr[i].width<=t)
			        continue;
                        cr[i].width-=t;
			cr[i].x=cx;
		} 
		if ((t=cx2-cr[i].x)<cr[i].width) 
		{
		        if (t<=0)
			        continue;
			cr[i].width=t;
		}
	        cur=&first;
		while ((nx=cur->next) && (cr[i].y > cur->next->y))
		        cur=nx; 
		cur->next=&(cr[i]);
		cr[i].next=nx;
	}
	first2.next=NULL;
	
	*(ro++)=BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1; *(ro++)=0;
	*(re++)=BT848_RISC_SYNC|BT848_FIFO_STATUS_FM1; *(re++)=0;
	
	/* loop through all lines */
	for (yy=0; yy<(height<<inter); yy++) 
        {
                y=yy>>inter;
                rp= (yy&1) ? &re : &ro;
	  
                /* remove rects with y2 > y */
                if ((cur=first2.next))
                {
                        prev=&first2;
                        do
                        {
                                if (cur->y+cur->height <= y) 
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
                x=0;
                if ((btv->win.y+y<=0)||(btv->win.y+y>=btv->win.sheight))
                        write_risc_segment(rp, adr, BT848_RISC_SKIP, &x,
                                           width, bpp, width);
                else 
                {
                        dx=cx;
                        for (cur2=first2.next; cur2; cur2=cur2->next)
                        {
                                if (x+dx < cur2->x)
                                {
                                        write_risc_segment(rp, adr, BT848_RISC_SKIP,
                                                           &x, dx, bpp, width);
                                        dx=cur2->x-x;
                                        write_risc_segment(rp, adr, BT848_RISC_WRITEC,
                                                           &x, dx, bpp, width);
                                        dx=cur2->width;
                                }
                                else if (x+dx < cur2->x+cur2->width) 
                                        dx=cur2->x+cur2->width-x; 
                        }
                        if (cx2<width)
                        {
                                write_risc_segment(rp, adr, BT848_RISC_SKIP,
                                                   &x, dx, bpp, width);
                                write_risc_segment(rp, adr, BT848_RISC_WRITEC,
                                                   &x, cx2-x, bpp, width);
                                dx=width-x;
                        }
                        write_risc_segment(rp, adr, BT848_RISC_SKIP,
                                           &x, dx, bpp, width);
                        write_risc_segment(rp, adr, BT848_RISC_WRITEC,
                                           &x, width-x, bpp, width);
                }
                if ((!inter)||(yy&1))
                        adr+=bpl;
	}
        
        *(ro++)=BT848_RISC_JUMP;
	*(ro++)=btv->bus_vbi_even;
	*(re++)=BT848_RISC_JUMP;
	*(re++)=btv->bus_vbi_odd;
}


/*
 *	Set the registers for the size we have specified. Don't bother
 *	trying to understand this without the BT848 manual in front of
 *	you [AC]. 
 *
 *	PS: The manual is free for download in .pdf format from 
 *	www.brooktree.com - nicely done those folks.
 */
 
struct tvnorm 
{
        u32 Fsc;
        u16 swidth, sheight; /* scaled standard width, height */
	u16 totalwidth;
	u8 adelay, bdelay, iform;
	u32 scaledtwidth;
	u16 hdelayx1, hactivex1;
	u16 vdelay, fporch;
};

static struct tvnorm tvnorms[] = {
	/* PAL-BDGHI */
        /* max. active video is actually 922, but 924 is divisible by 4 and 3! */
        { 35468950,
          924, 576, 1135, 0x7f, 0x72, (BT848_IFORM_PAL_BDGHI|BT848_IFORM_XT1),
          1135, 186, 924, 0x20},
/*
        { 35468950, 
          768, 576, 1135, 0x7f, 0x72, (BT848_IFORM_PAL_BDGHI|BT848_IFORM_XT1),
	  944, 186, 922, 0x20},
*/
	/* NTSC */
	{ 28636363,
          640, 480,  910, 0x68, 0x5d, (BT848_IFORM_NTSC|BT848_IFORM_XT0),
          780, 135, 754, 0x1a},
	/* SECAM */
	{ 28636363, 
          640, 480, 910, 0x68, 0x5d, (BT848_IFORM_PAL_M|BT848_IFORM_XT0),
	  780, 135, 754, 0x16},
	/* PAL-M */
	{ 28636363, 
          640, 480, 910, 0x68, 0x5d, (BT848_IFORM_PAL_M|BT848_IFORM_XT0),
	  780, 135, 754, 0x16},
	/* PAL-N */
	{ 35468950, 
          768, 576, 1135, 0x7f, 0x72, (BT848_IFORM_PAL_N|BT848_IFORM_XT1),
	  944, 186, 922, 0x20},
	/* PAL-NC */
	{ 35468950, 
          768, 576, 1135, 0x7f, 0x72, (BT848_IFORM_PAL_NC|BT848_IFORM_XT0),
	  944, 186, 922, 0x20},
	/* NTSC-Japan */
	{ 28636363,
          640, 480,  910, 0x68, 0x5d, (BT848_IFORM_NTSC_J|BT848_IFORM_XT0),
	  780, 135, 754, 0x16},
};
#define TVNORMS (sizeof(tvnorms)/sizeof(tvnorm))


/* set geometry for even/odd frames 
   just if you are wondering:
   handling of even and odd frames will be separated, e.g. for grabbing
   the even ones as RGB into videomem and the others as YUV in main memory for 
   compressing and sending to the video conferencing partner.

*/
static inline void bt848_set_eogeo(struct bttv *btv, int odd, u8 vtc, 
				   u16 hscale, u16 vscale,
				   u16 hactive, u16 vactive,
				   u16 hdelay, u16 vdelay,
				   u8 crop)
{
        int off = odd ? 0x80 : 0x00;
  
	btwrite(vtc, BT848_E_VTC+off);
	btwrite(hscale>>8, BT848_E_HSCALE_HI+off);
	btwrite(hscale&0xff, BT848_E_HSCALE_LO+off);
	btaor((vscale>>8), 0xe0, BT848_E_VSCALE_HI+off);
	btwrite(vscale&0xff, BT848_E_VSCALE_LO+off);
	btwrite(hactive&0xff, BT848_E_HACTIVE_LO+off);
	btwrite(hdelay&0xff, BT848_E_HDELAY_LO+off);
	btwrite(vactive&0xff, BT848_E_VACTIVE_LO+off);
	btwrite(vdelay&0xff, BT848_E_VDELAY_LO+off);
	btwrite(crop, BT848_E_CROP+off);
}


static void bt848_set_geo(struct bttv *btv, u16 width, u16 height, u16 fmt)
{
        u16 vscale, hscale;
	u32 xsf, sr;
	u16 hdelay, vdelay;
	u16 hactive, vactive;
	u16 inter;
	u8 crop, vtc;  
	struct tvnorm *tvn;
 	
	if (!width || !height)
	        return;

	tvn=&tvnorms[btv->win.norm];
	
        btv->win.cropheight=tvn->sheight;
        btv->win.cropwidth=tvn->swidth;

/*
	if (btv->win.cropwidth>tvn->cropwidth)
                btv->win.cropwidth=tvn->cropwidth;
	if (btv->win.cropheight>tvn->cropheight)
	        btv->win.cropheight=tvn->cropheight;

	if (width>btv->win.cropwidth)
                width=btv->win.cropwidth;
	if (height>btv->win.cropheight)
	        height=btv->win.cropheight;
*/
	btwrite(tvn->adelay, BT848_ADELAY);
	btwrite(tvn->bdelay, BT848_BDELAY);
	btaor(tvn->iform,~(BT848_IFORM_NORM|BT848_IFORM_XTBOTH), BT848_IFORM);
	
	set_pll(btv);

	btwrite(fmt, BT848_COLOR_FMT);
	hactive=width;

        vtc=0;
	/* Some people say interpolation looks bad ... */
	/* vtc = (hactive < 193) ? 2 : ((hactive < 385) ? 1 : 0); */
     
	btv->win.interlace = (height>btv->win.cropheight/2) ? 1 : 0;
	inter=(btv->win.interlace&1)^1;
	vdelay=btv->win.cropy+tvn->vdelay;

	xsf = (hactive*tvn->scaledtwidth)/btv->win.cropwidth;
	hscale = ((tvn->totalwidth*4096UL)/xsf-4096);

	hdelay=tvn->hdelayx1+btv->win.cropx;
	hdelay=(hdelay*hactive)/btv->win.cropwidth;
	hdelay&=0x3fe;

	sr=((btv->win.cropheight>>inter)*512)/height-512;
	vscale=(0x10000UL-sr)&0x1fff;
	vactive=btv->win.cropheight;
	crop=((hactive>>8)&0x03)|((hdelay>>6)&0x0c)|
	        ((vactive>>4)&0x30)|((vdelay>>2)&0xc0);
	vscale|= btv->win.interlace ? (BT848_VSCALE_INT<<8) : 0;
	
	bt848_set_eogeo(btv, 0, vtc, hscale, vscale, hactive, vactive,
			hdelay, vdelay, crop);
	bt848_set_eogeo(btv, 1, vtc, hscale, vscale, hactive, vactive,
			hdelay, vdelay, crop);
	
}


int bpp2fmt[4] = {
        BT848_COLOR_FMT_RGB8, BT848_COLOR_FMT_RGB16, 
        BT848_COLOR_FMT_RGB24, BT848_COLOR_FMT_RGB32 
};

static void bt848_set_winsize(struct bttv *btv)
{
        unsigned short format;

        btv->win.color_fmt = format = 
                (btv->win.depth==15) ? BT848_COLOR_FMT_RGB15 :
                        bpp2fmt[(btv->win.bpp-1)&3];

	/*	RGB8 seems to be a 9x5x5 GRB color cube starting at
	 *	color 16. Why the h... can't they even mention this in the
	 *	data sheet?  [AC - because it's a standard format so I guess
	 *	it never occurred to them]
	 *	Enable dithering in this mode.
	 */

	if (format==BT848_COLOR_FMT_RGB8)
                btand(~BT848_CAP_CTL_DITH_FRAME, BT848_CAP_CTL); 
	else
	        btor(BT848_CAP_CTL_DITH_FRAME, BT848_CAP_CTL);

        bt848_set_geo(btv, btv->win.width, btv->win.height, format);
}

/*
 *	Set TSA5522 synthesizer frequency in 1/16 Mhz steps
 */

static void set_freq(struct bttv *btv, unsigned short freq)
{
	int fixme = freq; /* XXX */
	int oldAudio = btv->audio;
	
	audio(btv, AUDIO_MUTE);
	udelay(AUDIO_MUTE_DELAY);

	if (btv->radio) 
	{
		if (btv->have_tuner)
			i2c_control_device(&(btv->i2c), I2C_DRIVERID_TUNER,
					   TUNER_SET_RADIOFREQ,&fixme);
	
		if (btv->have_msp3400) {
			i2c_control_device(&(btv->i2c),I2C_DRIVERID_MSP3400,
					   MSP_SET_RADIO,0);
			i2c_control_device(&(btv->i2c),I2C_DRIVERID_MSP3400,
					   MSP_NEWCHANNEL,0);
		}
	}
	else
	{
		if (btv->have_tuner)
			i2c_control_device(&(btv->i2c), I2C_DRIVERID_TUNER,
					   TUNER_SET_TVFREQ,&fixme);

		if (btv->have_msp3400) {
			i2c_control_device(&(btv->i2c),I2C_DRIVERID_MSP3400,
					   MSP_SET_TVNORM,&(btv->win.norm));
			i2c_control_device(&(btv->i2c),I2C_DRIVERID_MSP3400,
					   MSP_NEWCHANNEL,0);
		}
	}

 	if (!(oldAudio & AUDIO_MUTE)) {
		udelay(FREQ_CHANGE_DELAY);
		audio(btv, AUDIO_UNMUTE);
	}
}
      

/*
 *	Grab into virtual memory.
 *	Currently only does double buffering. Do we need more?
 */

static int vgrab(struct bttv *btv, struct video_mmap *mp)
{
	unsigned int *ro, *re;
	unsigned int *vbuf;
	
	if(btv->fbuffer==NULL)
	{
		if(fbuffer_alloc(btv))
			return -ENOBUFS;
	}
	if(btv->grabbing >= MAX_GBUFFERS)
		return -ENOBUFS;
	
	/*
	 *	No grabbing past the end of the buffer!
	 */
	 
	if(mp->frame>1 || mp->frame <0)
		return -EINVAL;
		
	if(mp->height <0 || mp->width <0)
		return -EINVAL;
	
/*      This doesn´t work like this for NTSC anyway.
        So, better check the total image size ...
*/
/*
	if(mp->height>576 || mp->width>768)
		return -EINVAL;
*/
	if (mp->height*mp->width*fmtbppx2[mp->format&0x0f]/2>BTTV_MAX_FBUF)
		return -EINVAL;
 
	/*
	 *	FIXME: Check the format of the grab here. This is probably
	 *	also less restrictive than the normal overlay grabs. Stuff
	 *	like QCIF has meaning as a capture.
	 */
	 
	/*
	 *	Ok load up the BT848
	 */
	 
	vbuf=(unsigned int *)(btv->fbuffer+BTTV_MAX_FBUF*mp->frame);
/*	if (!(btread(BT848_DSTATUS)&BT848_DSTATUS_HLOC))
                return -EAGAIN;*/
	ro=btv->grisc+(((btv->grabcount++)&1) ? 4096 :0);
	re=ro+2048;
	btv->gwidth=mp->width;
	btv->gheight=mp->height;

	if (mp->format > PALETTEFMT_MAX)
		return -EINVAL;
	btv->gfmt=palette2fmt[mp->format];
	if(btv->gfmt==0)
		return -EINVAL;
		
	make_vrisctab(btv, ro, re, vbuf, btv->gwidth, btv->gheight, btv->gfmt);
	/* bt848_set_risc_jmps(btv); */
	btor(3, BT848_CAP_CTL);
	btor(3, BT848_GPIO_DMA_CTL);
        btv->frame_stat[mp->frame] = GBUFFER_GRABBING;
        if (btv->grabbing) {
		btv->gro_next=virt_to_bus(ro);
		btv->gre_next=virt_to_bus(re);
                btv->grf_next=mp->frame;
        } else {
		btv->gro=virt_to_bus(ro);
		btv->gre=virt_to_bus(re);
                btv->grf=mp->frame;
        }
	if (!(btv->grabbing++)) 
		btv->risc_jmp[12]=BT848_RISC_JUMP|(0x8<<16)|BT848_RISC_IRQ;
	/* interruptible_sleep_on(&btv->capq); */
	return 0;
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
			if(signal_pending(current))
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
			btv->fbuffer=NULL;
			if (!btv->fbuffer)
				btv->fbuffer=(unsigned char *) rvmalloc(2*BTTV_MAX_FBUF);
			if (!btv->fbuffer)
                        { 
				btv->user--;
				return -EINVAL;
			}
                        btv->grabbing = 0;
                        btv->grab = 0;
                        btv->lastgrab = 0;
                        for (i = 0; i < MAX_GBUFFERS; i++)
				btv->frame_stat[i] = GBUFFER_UNUSED;
                        break;
		case 1:
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
	bt848_set_risc_jmps(btv);

	if(btv->fbuffer)
		rvfree((void *) btv->fbuffer, 2*BTTV_MAX_FBUF);
	btv->fbuffer=0;
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

extern inline void bt848_sat_u(struct bttv *btv, unsigned long data)
{
	u32 datahi;

	datahi=(data>>7)&2;
	btwrite(data&0xff, BT848_SAT_U_LO);
	btaor(datahi, ~2, BT848_E_CONTROL);
	btaor(datahi, ~2, BT848_O_CONTROL);
}

static inline void bt848_sat_v(struct bttv *btv, unsigned long data)
{
	u32 datahi;

	datahi=(data>>8)&1;
	btwrite(data&0xff, BT848_SAT_V_LO);
	btaor(datahi, ~1, BT848_E_CONTROL);
	btaor(datahi, ~1, BT848_O_CONTROL);
}

/*
 *	ioctl routine
 */
 

static int bttv_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	unsigned char eedata[256];
	struct bttv *btv=(struct bttv *)dev;
 	int i;
  	
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
			b.channels = tvcards[btv->type].inputs;
			b.audios = tvcards[btv->type].inputs;
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
			v.norm = btv->win.norm;
                        if (v.channel>=tvcards[btv->type].inputs)
                                return -EINVAL;
                        if(v.channel==tvcards[btv->type].tuner) 
                        {
                                strcpy(v.name,"Television");
                                v.flags|=VIDEO_VC_TUNER;
                                v.type=VIDEO_TYPE_TV;
                                v.tuners=1;
                        } 
                        else if(v.channel==tvcards[btv->type].svhs) 
                                strcpy(v.name,"SVHS");
                        else
                                sprintf(v.name,"Composite%d",v.channel);

			if(copy_to_user(arg,&v,sizeof(v)))
				return -EFAULT;
			return 0;
		}
		/*
		 *	Each channel has 1 tuner
		 */
		case VIDIOCSCHAN:
		{
			struct video_channel v;
			if(copy_from_user(&v, arg,sizeof(v)))
				return -EFAULT;
                        
                        if (v.channel>tvcards[btv->type].inputs)
                                return -EINVAL;
			bt848_muxsel(btv, v.channel);
			if(v.norm!=VIDEO_MODE_PAL&&v.norm!=VIDEO_MODE_NTSC
			   &&v.norm!=VIDEO_MODE_SECAM)
				return -EOPNOTSUPP;
			btv->win.norm = v.norm;
			bt848_set_winsize(btv);
			btv->channel=v.channel;
			return 0;
		}
		case VIDIOCGTUNER:
		{
			struct video_tuner v;
			if(copy_from_user(&v,arg,sizeof(v))!=0)
				return -EFAULT;
			if(v.tuner||btv->channel)	/* Only tuner 0 */
				return -EINVAL;
			strcpy(v.name, "Television");
			v.rangelow=0;
			v.rangehigh=0xFFFFFFFF;
			v.flags=VIDEO_TUNER_PAL|VIDEO_TUNER_NTSC|VIDEO_TUNER_SECAM;
			v.mode = btv->win.norm;
			v.signal = (btread(BT848_DSTATUS)&BT848_DSTATUS_HLOC) ? 0xFFFF : 0;
			if(copy_to_user(arg,&v,sizeof(v)))
				return -EFAULT;
			return 0;
		}
		/* We have but one tuner */
		case VIDIOCSTUNER:
		{
			struct video_tuner v;
			if(copy_from_user(&v, arg, sizeof(v)))
				return -EFAULT;

			/* Only one channel has a tuner */
                        if(v.tuner!=tvcards[btv->type].tuner)
 				return -EINVAL;
 				
			if(v.mode!=VIDEO_MODE_PAL&&v.mode!=VIDEO_MODE_NTSC
			   &&v.mode!=VIDEO_MODE_SECAM)
				return -EOPNOTSUPP;
 			btv->win.norm = v.mode;
 			bt848_set_winsize(btv);
			return 0;
		}
		case VIDIOCGPICT:
		{
			struct video_picture p=btv->picture;
			if(btv->win.depth==8)
				p.palette=VIDEO_PALETTE_HI240;
			if(btv->win.depth==15)
				p.palette=VIDEO_PALETTE_RGB555;
			if(btv->win.depth==16)
				p.palette=VIDEO_PALETTE_RGB565;
			if(btv->win.depth==24)
				p.palette=VIDEO_PALETTE_RGB24;
			if(btv->win.depth==32)
				p.palette=VIDEO_PALETTE_RGB32;
			
			if(copy_to_user(arg, &p, sizeof(p)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSPICT:
		{
			struct video_picture p;
			int format;
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
			btv->picture = p;

                        /* set palette if bpp matches */
                        if (p.palette < sizeof(palette2fmt)/sizeof(int)) {
                                format = palette2fmt[p.palette];
                                if (fmtbppx2[format&0x0f]/2 == btv->win.bpp)
                                        btv->win.color_fmt = format;
                        }
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
			bt848_set_winsize(btv);

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
			make_clip_tab(btv,vcp, vw.clipcount);
			vfree(vcp);
			if(on && btv->win.vidadr!=0)
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
			if(v==0)
			{
				bt848_cap(btv,0);
			}
			else
			{
			        if(btv->win.vidadr==0 || btv->win.width==0
				   || btv->win.height==0)
				  return -EINVAL;
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
			v.depth=btv->win.depth;
			v.bytesperline=btv->win.bpl;
			if(copy_to_user(arg, &v,sizeof(v)))
				return -EFAULT;
			return 0;
			
		}
		case VIDIOCSFBUF:
		{
			struct video_buffer v;
			if(!capable(CAP_SYS_ADMIN))
				return -EPERM;
			if(copy_from_user(&v, arg,sizeof(v)))
				return -EFAULT;
			if(v.depth!=8 && v.depth!=15 && v.depth!=16 && v.depth!=24 && v.depth!=32)
				return -EINVAL;
	                btv->win.vidadr=(unsigned long)v.base;
			btv->win.sheight=v.height;
			btv->win.swidth=v.width;
			btv->win.bpp=((v.depth+7)&0x38)/8;
			btv->win.depth=v.depth;
			btv->win.bpl=v.bytesperline;
			
			DEBUG(printk("Display at %p is %d by %d, bytedepth %d, bpl %d\n",
				v.base, v.width,v.height, btv->win.bpp, btv->win.bpl));
			bt848_set_winsize(btv);
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
			struct video_audio v;
			v=btv->audio_dev;
			v.flags&=~(VIDEO_AUDIO_MUTE|VIDEO_AUDIO_MUTABLE);
			v.flags|=VIDEO_AUDIO_MUTABLE;
			strcpy(v.name,"TV");
			if (btv->have_msp3400) 
			{
                                v.flags|=VIDEO_AUDIO_VOLUME |
                                	VIDEO_AUDIO_BASS |
                                	VIDEO_AUDIO_TREBLE;
                                i2c_control_device(&(btv->i2c),
                                                I2C_DRIVERID_MSP3400,
                                                MSP_GET_VOLUME,&(v.volume));
                                i2c_control_device(&(btv->i2c),
                                                I2C_DRIVERID_MSP3400,
                                                MSP_GET_BASS,&(v.bass));
                                i2c_control_device(&(btv->i2c),
                                                I2C_DRIVERID_MSP3400,
                                                MSP_GET_TREBLE,&(v.treble));
                        	i2c_control_device(&(btv->i2c),
                                		I2C_DRIVERID_MSP3400,
                                                MSP_GET_STEREO,&(v.mode));
			}
			else v.mode = VIDEO_SOUND_MONO;
			if(copy_to_user(arg,&v,sizeof(v)))
				return -EFAULT;
			return 0;
		}
		case VIDIOCSAUDIO:
		{
			struct video_audio v;
			if(copy_from_user(&v,arg, sizeof(v)))
				return -EFAULT;
			if(v.flags&VIDEO_AUDIO_MUTE)
				audio(btv, AUDIO_MUTE);
 			/* One audio source per tuner */
 			if(v.audio!=0)
				return -EINVAL;
			bt848_muxsel(btv,v.audio);
			if(!(v.flags&VIDEO_AUDIO_MUTE))
				audio(btv, AUDIO_UNMUTE);
			if (btv->have_msp3400) 
			{
                                i2c_control_device(&(btv->i2c),
                                                I2C_DRIVERID_MSP3400,
                                                MSP_SET_VOLUME,&(v.volume));
                                i2c_control_device(&(btv->i2c),
                                                I2C_DRIVERID_MSP3400,
                                                MSP_SET_BASS,&(v.bass));
                                i2c_control_device(&(btv->i2c),
                                                I2C_DRIVERID_MSP3400,
                                                MSP_SET_TREBLE,&(v.treble));
                        	i2c_control_device(&(btv->i2c),
                                		I2C_DRIVERID_MSP3400,
                                		MSP_SET_STEREO,&(v.mode));
			}
			btv->audio_dev=v;
			return 0;
		}

	        case VIDIOCSYNC:
			if(copy_from_user((void *)&i,arg,sizeof(int)))
				return -EFAULT;
                        switch (btv->frame_stat[i]) {
                        case GBUFFER_UNUSED:
                                return -EINVAL;
                        case GBUFFER_GRABBING:
 			        interruptible_sleep_on(&btv->capq);
                                /* fall */
                        case GBUFFER_DONE:
                                btv->frame_stat[i] = GBUFFER_UNUSED;
                                break;
                        }
                        return 0;

		case BTTV_WRITEE:
			if(!capable(CAP_SYS_ADMIN))
				return -EPERM;
			if(copy_from_user((void *) eedata, (void *) arg, 256))
				return -EFAULT;
			writeee(&(btv->i2c), eedata);
			return 0;

		case BTTV_READEE:
			if(!capable(CAP_SYS_ADMIN))
				return -EPERM;
			readee(&(btv->i2c), eedata);
			if(copy_to_user((void *) arg, (void *) eedata, 256))
				return -EFAULT;
			break;

                case BTTV_FIELDNR: 
			if(copy_to_user((void *) arg, (void *) &btv->last_field, 
                                        sizeof(btv->last_field)))
				return -EFAULT;
                        break;
      
                case BTTV_PLLSET: {
                        struct bttv_pll_info p;
                        if(copy_from_user(&p , (void *) arg, sizeof(btv->pll)))
				return -EFAULT;
                        btv->pll.pll_ifreq = p.pll_ifreq;
                        btv->pll.pll_ofreq = p.pll_ofreq;
                        btv->pll.pll_crystal = p.pll_crystal;

			break;
                }						
	        case VIDIOCMCAPTURE:
		{
                        struct video_mmap vm;
			if(copy_from_user((void *) &vm, (void *) arg, sizeof(vm)))
				return -EFAULT;
                        if (btv->frame_stat[vm.frame] == GBUFFER_GRABBING)
                                return -EBUSY;
		        return vgrab(btv, &vm);
		}
		
		case VIDIOCGMBUF:
		{
			struct video_mbuf vm;
			memset(&vm, 0 , sizeof(vm));
			vm.size=BTTV_MAX_FBUF*2;
			vm.frames=2;
			vm.offsets[0]=0;
			vm.offsets[1]=BTTV_MAX_FBUF;
			if(copy_to_user((void *)arg, (void *)&vm, sizeof(vm)))
				return -EFAULT;
			return 0;
		}
		
		case VIDIOCGUNIT:
		{
			struct video_unit vu;
			vu.video=btv->video_dev.minor;
			vu.vbi=btv->vbi_dev.minor;
			if(btv->radio_dev.minor!=-1)
				vu.radio=btv->radio_dev.minor;
			else
				vu.radio=VIDEO_NO_UNIT;
			vu.audio=VIDEO_NO_UNIT;
			if(btv->have_msp3400)
			{
				i2c_control_device(&(btv->i2c), I2C_DRIVERID_MSP3400,
					MSP_GET_UNIT, &vu.audio);
			}
			vu.teletext=VIDEO_NO_UNIT;
			if(copy_to_user((void *)arg, (void *)&vu, sizeof(vu)))
				return -EFAULT;
			return 0;
		}
		
		default:
			return -ENOIOCTLCMD;
	}
	return 0;
}

static int bttv_init_done(struct video_device *dev)
{
	return 0;
}

/*
 *	This maps the vmalloced and reserved fbuffer to user space.
 *
 *  FIXME: 
 *  - PAGE_READONLY should suffice!?
 *  - remap_page_range is kind of inefficient for page by page remapping.
 *    But e.g. pte_alloc() does not work in modules ... :-(
 */
 
static int bttv_mmap(struct video_device *dev, const char *adr, unsigned long size)
{
	struct bttv *btv=(struct bttv *)dev;
        unsigned long start=(unsigned long) adr;
	unsigned long page,pos;

	if (size>2*BTTV_MAX_FBUF)
	        return -EINVAL;
	if (!btv->fbuffer)
	{
		if(fbuffer_alloc(btv))
			return -EINVAL;
	}
	pos=(unsigned long) btv->fbuffer;
	while (size > 0) 
	{
	        page = kvirt_to_phys(pos);
		if (remap_page_range(start, page, PAGE_SIZE, PAGE_SHARED))
		        return -EAGAIN;
		start+=PAGE_SIZE;
		pos+=PAGE_SIZE;
		size-=PAGE_SIZE;    
	}
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
	NULL,			/* poll */
	bttv_ioctl,
	bttv_mmap,
	bttv_init_done,
	NULL,
	0,
	-1
};


static long vbi_read(struct video_device *v, char *buf, unsigned long count,
		     int nonblock)
{
	struct bttv *btv=(struct bttv *)(v-2);
	int q,todo;

	todo=count;
	while (todo && todo>(q=VBIBUF_SIZE-btv->vbip)) 
	{
		if(copy_to_user((void *) buf, (void *) btv->vbibuf+btv->vbip, q))
			return -EFAULT;
		todo-=q;
		buf+=q;

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
			if(signal_pending(current))
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

static unsigned int vbi_poll(struct video_device *dev, struct file *file,
	poll_table *wait)
{
	struct bttv *btv=(struct bttv *)(dev-2);
	unsigned int mask = 0;

	poll_wait(file, &btv->vbiq, wait);

	if (btv->vbip < VBIBUF_SIZE)
		mask |= (POLLIN | POLLRDNORM);

	return mask;
}

static int vbi_open(struct video_device *dev, int flags)
{
	struct bttv *btv=(struct bttv *)(dev-2);

	btv->vbip=VBIBUF_SIZE;
	btv->cap|=0x0c;
	bt848_set_risc_jmps(btv);

	MOD_INC_USE_COUNT;
	return 0;   
}

static void vbi_close(struct video_device *dev)
{
	struct bttv *btv=(struct bttv *)(dev-2);
  
	btv->cap&=~0x0c;
	bt848_set_risc_jmps(btv);

	MOD_DEC_USE_COUNT;  
}


static int vbi_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
	return -EINVAL;
}

static struct video_device vbi_template=
{
	"bttv vbi",
	VID_TYPE_CAPTURE|VID_TYPE_TELETEXT,
	VID_HARDWARE_BT848,
	vbi_open,
	vbi_close,
	vbi_read,
	bttv_write,
	vbi_poll,
	vbi_ioctl,
	NULL,	/* no mmap yet */
	bttv_init_done,
	NULL,
	0,
	-1
};


static int radio_open(struct video_device *dev, int flags)
{
	struct bttv *btv = (struct bttv *)(dev-1);

	if (btv->user)
		return -EBUSY;
	btv->user++;
	set_freq(btv,400*16);
	btv->radio = 1;
	bt848_muxsel(btv,0);
	audio(btv, AUDIO_UNMUTE);
	
	MOD_INC_USE_COUNT;
	return 0;   
}

static void radio_close(struct video_device *dev)
{
	struct bttv *btv=(struct bttv *)(dev-1);
  
	btv->user--;
	btv->radio = 0;
	audio(btv, AUDIO_MUTE);
	MOD_DEC_USE_COUNT;  
}

static long radio_read(struct video_device *v, char *buf, unsigned long count, int nonblock)
{
	return -EINVAL;
}

static int radio_ioctl(struct video_device *dev, unsigned int cmd, void *arg)
{
        struct bttv *btv=(struct bttv *)(dev-1);
	switch (cmd) {	
	case VIDIOCGCAP:
	{
		struct video_capability v;
		strcpy(v.name,btv->video_dev.name);
		v.type = VID_TYPE_TUNER;
		v.channels = 1;
		v.audios = 1;
		/* No we don't do pictures */
		v.maxwidth = 0;
		v.maxheight = 0;
		v.minwidth = 0;
		v.minheight = 0;
		if (copy_to_user(arg, &v, sizeof(v)))
			return -EFAULT;
		return 0;
		break;
	}
	case VIDIOCGTUNER:
	{
		struct video_tuner v;
		if(copy_from_user(&v,arg,sizeof(v))!=0)
			return -EFAULT;
		if(v.tuner||btv->channel)	/* Only tuner 0 */
			return -EINVAL;
		strcpy(v.name, "Radio");
		v.rangelow=(int)(87.5*16);
		v.rangehigh=(int)(108*16);
		v.flags= 0; /* XXX */
		v.mode = 0; /* XXX */
		if(copy_to_user(arg,&v,sizeof(v)))
			return -EFAULT;
		return 0;
	}
	case VIDIOCSTUNER:
	{
		struct video_tuner v;
		if(copy_from_user(&v, arg, sizeof(v)))
			return -EFAULT;
		/* Only channel 0 has a tuner */
		if(v.tuner!=0 || btv->channel)
			return -EINVAL;
		/* XXX anything to do ??? */
		return 0;
	}
	case VIDIOCGFREQ:
	case VIDIOCSFREQ:
	case VIDIOCGAUDIO:
	case VIDIOCSAUDIO:
		bttv_ioctl((struct video_device *)btv,cmd,arg);
		break;
	default:
		return -ENOIOCTLCMD;
	}
	return 0;
}

static struct video_device radio_template=
{
	"bttv radio",
	VID_TYPE_TUNER,
	VID_HARDWARE_BT848,
	radio_open,
	radio_close,
	radio_read,          /* just returns -EINVAL */
	bttv_write,          /* just returns -EINVAL */
	NULL,                /* no poll */
	radio_ioctl,
	NULL,	             /* no mmap */
	bttv_init_done,      /* just returns 0 */
	NULL,
	0,
	-1
};


struct vidbases 
{
	unsigned short vendor, device;
	char *name;
	uint badr;
};

static struct vidbases vbs[] = {
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215CT222,
                "ATI MACH64 CT", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_210888GX,
		"ATI MACH64 Winturbo", PCI_BASE_ADDRESS_0},
 	{ PCI_VENDOR_ID_ATI, PCI_DEVICE_ID_ATI_215GT,
 		"ATI MACH64 GT", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_CIRRUS, 0, "Cirrus Logic", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_DEC, PCI_DEVICE_ID_DEC_TGA,
		"DEC DC21030", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_MATROX, PCI_DEVICE_ID_MATROX_MIL,
		"Matrox Millennium", PCI_BASE_ADDRESS_1},
	{ PCI_VENDOR_ID_MATROX, PCI_DEVICE_ID_MATROX_MIL_2,
		"Matrox Millennium II", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_MATROX, PCI_DEVICE_ID_MATROX_MIL_2_AGP,
		"Matrox Millennium II AGP", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_MATROX, 0x051a, "Matrox Mystique", PCI_BASE_ADDRESS_1},
	{ PCI_VENDOR_ID_N9, PCI_DEVICE_ID_N9_I128, 
		"Number Nine Imagine 128", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_N9, PCI_DEVICE_ID_N9_I128_2, 
		"Number Nine Imagine 128 Series 2", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_S3, 0, "S3", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_TSENG, 0, "TSENG", PCI_BASE_ADDRESS_0},
	{ PCI_VENDOR_ID_NVIDIA_SGS, PCI_DEVICE_ID_NVIDIA_SGS_RIVA128,
                "Riva128", PCI_BASE_ADDRESS_1},
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
	unsigned short badr;
	int found = 0, i, tga_type;
	unsigned int vidadr=0;
	struct pci_dev *dev;


	for (dev = pci_devices; dev != NULL; dev = dev->next) 
	{
		if (dev->class != PCI_CLASS_NOT_DEFINED_VGA &&
			((dev->class) >> 16 != PCI_BASE_CLASS_DISPLAY))
		{
			continue;
		}
		if (PCI_FUNC(dev->devfn) != 0)
			continue;

		badr=0;
		printk(KERN_INFO "bttv: PCI display adapter: ");
		for (i=0; i<NR_CARDS; i++) 
		{
			if (dev->vendor == vbs[i].vendor) 
			{
				if (vbs[i].device) 
					if (vbs[i].device!=dev->device)
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
		pci_read_config_dword(dev, badr, &vidadr);
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
     
		if (dev->vendor == PCI_VENDOR_ID_DEC &&
			dev->device == PCI_DEVICE_ID_DEC_TGA) 
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
		DEBUG(printk(KERN_DEBUG "devfn: 0x%04x.\n", dev->devfn));
		found++;
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
	struct pci_dev *dev = NULL;
  
	/*	Just in case some nut set this to something dangerous */
	if (triton1)
		triton1=BT848_INT_ETBF;
	
	while ((dev = pci_find_device(PCI_VENDOR_ID_SI, PCI_DEVICE_ID_SI_496, dev))) 
	{
		/* Beware the SiS 85C496 my friend - rev 49 don't work with a bttv */
		printk(KERN_WARNING "BT848 and SIS 85C496 chipset don't always work together.\n");
	}			

	/* dev == NULL */

	while ((dev = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82441, dev))) 
	{
		unsigned char b;
		pci_read_config_byte(dev, 0x53, &b);
		DEBUG(printk(KERN_INFO "bttv: Host bridge: 82441FX Natoma, "));
		DEBUG(printk("bufcon=0x%02x\n",b));
	}

	while ((dev = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82437, dev))) 
	{
/*		unsigned char b;
		unsigned char bo;*/

		printk(KERN_INFO "bttv: Host bridge 82437FX Triton PIIX\n");
		triton1=BT848_INT_ETBF;
			
#if 0			
		/* The ETBF bit SHOULD make all this unnecessary */
		/* 430FX (Triton I) freezes with bus concurrency on -> switch it off */

		pci_read_config_byte(dev, TRITON_PCON, &b);
		bo=b;
		DEBUG(printk(KERN_DEBUG "bttv: 82437FX: PCON: 0x%x\n",b));
		if(!(b & TRITON_BUS_CONCURRENCY)) 
		{
			printk(KERN_WARNING "bttv: 82437FX: disabling bus concurrency\n");
			b |= TRITON_BUS_CONCURRENCY;
		}
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
			pci_write_config_byte(dev, TRITON_PCON, b); 
			printk(KERN_DEBUG "bttv: 82437FX: PCON changed to: 0x%x\n",b);
		}
#endif
	}
}

static void init_tda8425(struct i2c_bus *bus) 
{
        I2CWrite(bus, I2C_TDA8425, TDA8425_VL, 0xFC, 1); /* volume left 0dB  */
        I2CWrite(bus, I2C_TDA8425, TDA8425_VR, 0xFC, 1); /* volume right 0dB */
        I2CWrite(bus, I2C_TDA8425, TDA8425_BA, 0xF6, 1); /* bass 0dB         */
        I2CWrite(bus, I2C_TDA8425, TDA8425_TR, 0xF6, 1); /* treble 0dB       */
	I2CWrite(bus, I2C_TDA8425, TDA8425_S1, 0xCE, 1); /* mute off         */
}


static void init_tda9850(struct i2c_bus *bus)
{
        I2CWrite(bus, I2C_TDA9850, TDA9850_CON1, 0x08, 1);  /* noise threshold st */
	I2CWrite(bus, I2C_TDA9850, TDA9850_CON2, 0x08, 1);  /* noise threshold sap */
	I2CWrite(bus, I2C_TDA9850, TDA9850_CON3, 0x40, 1);  /* stereo mode */
	I2CWrite(bus, I2C_TDA9850, TDA9850_CON4, 0x07, 1);  /* 0 dB input gain?*/
	I2CWrite(bus, I2C_TDA9850, TDA9850_ALI1, 0x10, 1);  /* wideband alignment? */
	I2CWrite(bus, I2C_TDA9850, TDA9850_ALI2, 0x10, 1);  /* spectral alignment? */
	I2CWrite(bus, I2C_TDA9850, TDA9850_ALI3, 0x03, 1);
}



/* Figure out card and tuner type */

static void idcard(int i)
{
        struct bttv *btv = &bttvs[i];

	int tunertype;
	btwrite(0, BT848_GPIO_OUT_EN);
	DEBUG(printk(KERN_DEBUG "bttv%d: GPIO: 0x%08x\n", i, btread(BT848_GPIO_DATA)));

	/* Default the card to the user-selected one. */
	btv->type=card[i];
        btv->tuner_type=-1; /* use default tuner type */

	/* If we were asked to auto-detect, then do so! 
	   Right now this will only recognize Miro, Hauppauge or STB
	   */
	if (btv->type == BTTV_UNKNOWN) 
	{
	        btv->type=BTTV_MIRO;
    
		if (I2CRead(&(btv->i2c), I2C_HAUPEE)>=0)
		        btv->type=BTTV_HAUPPAUGE;
		else
		        if (I2CRead(&(btv->i2c), I2C_STBEE)>=0)
			        btv->type=BTTV_STB;

                if (btv->type == BTTV_MIRO) {
                        /* auto detect tuner for MIRO cards */
                        btv->tuner_type=((btread(BT848_GPIO_DATA)>>10)-1)&7;
                }
	}

        if (I2CRead(&(btv->i2c), I2C_TDA9850) >=0)
        {
        	btv->audio_chip = TDA9850;
        	printk(KERN_INFO "bttv%d: audio chip: TDA9850\n", i);
        }

        if (I2CRead(&(btv->i2c), I2C_TDA8425) >=0)
        {
            btv->audio_chip = TDA8425;
            printk("bttv%d: audio chip: TDA8425\n", i);
        }
        
        switch(btv->audio_chip)
        {
                case TDA9850:
                        init_tda9850(&(btv->i2c));
                        break;
		case TDA8425:
                        init_tda8425(&(btv->i2c));
                        break;
        }

	/* How do I detect the tuner type for other cards but Miro ??? */
	printk(KERN_INFO "bttv%d: model: ", btv->nr);
	switch (btv->type) 
	{
		case BTTV_MIRO:
			printk("MIRO\n");
			if (btv->have_tuner) 
			{
				tunertype=((btread(BT848_GPIO_DATA)>>10)-1)&7;
				i2c_control_device(&(btv->i2c),
						   I2C_DRIVERID_TUNER,
						   TUNER_SET_TYPE,&tunertype);
			}
			strcpy(btv->video_dev.name,"BT848(Miro)");
			break;
		case BTTV_HAUPPAUGE:
			printk("HAUPPAUGE\n");
			strcpy(btv->video_dev.name,"BT848(Hauppauge)");
			break;
		case BTTV_STB: 
			printk("STB\n");
			strcpy(btv->video_dev.name,"BT848(STB)");
			break;
		case BTTV_INTEL: 
			printk("Intel\n");
			strcpy(btv->video_dev.name,"BT848(Intel)");
			break;
		case BTTV_DIAMOND: 
			printk("Diamond\n");
			strcpy(btv->video_dev.name,"BT848(Diamond)");
			break;
		case BTTV_AVERMEDIA: 
			printk("AVerMedia\n");
			strcpy(btv->video_dev.name,"BT848(AVerMedia)");
			break;
		case BTTV_MATRIX_VISION: 
			printk("MATRIX-Vision\n");
			strcpy(btv->video_dev.name,"BT848(MATRIX-Vision)");
			break;
	}
	audio(btv, AUDIO_MUTE);
}



static void bt848_set_risc_jmps(struct bttv *btv)
{
	int flags=btv->cap;

	/* Sync to start of odd field */
	btv->risc_jmp[0]=BT848_RISC_SYNC|BT848_RISC_RESYNC|BT848_FIFO_STATUS_VRE;
	btv->risc_jmp[1]=0;

	/* Jump to odd vbi sub */
	btv->risc_jmp[2]=BT848_RISC_JUMP|(0x5<<20);
	if (flags&8)
		btv->risc_jmp[3]=virt_to_bus(btv->vbi_odd);
	else
		btv->risc_jmp[3]=virt_to_bus(btv->risc_jmp+4);

        /* Jump to odd sub */
	btv->risc_jmp[4]=BT848_RISC_JUMP|(0x6<<20);
	if (flags&2)
		btv->risc_jmp[5]=virt_to_bus(btv->risc_odd);
	else
		btv->risc_jmp[5]=virt_to_bus(btv->risc_jmp+6);


	/* Sync to start of even field */
	btv->risc_jmp[6]=BT848_RISC_SYNC|BT848_RISC_RESYNC|BT848_FIFO_STATUS_VRO;
	btv->risc_jmp[7]=0;

	/* Jump to even vbi sub */
	btv->risc_jmp[8]=BT848_RISC_JUMP;
	if (flags&4)
		btv->risc_jmp[9]=virt_to_bus(btv->vbi_even);
	else
		btv->risc_jmp[9]=virt_to_bus(btv->risc_jmp+10);

	/* Jump to even sub */
	btv->risc_jmp[10]=BT848_RISC_JUMP|(8<<20);
	if (flags&1)
		btv->risc_jmp[11]=virt_to_bus(btv->risc_even);
	else
		btv->risc_jmp[11]=virt_to_bus(btv->risc_jmp+12);

	btv->risc_jmp[12]=BT848_RISC_JUMP;
	btv->risc_jmp[13]=virt_to_bus(btv->risc_jmp);

	/* enable capturing and DMA */
	btaor(flags, ~0x0f, BT848_CAP_CTL);
	if (flags&0x0f)
		bt848_dma(btv, 3);
	else
		bt848_dma(btv, 0);
}


static int init_bt848(int i)
{
        struct bttv *btv = &bttvs[i];

	btv->user=0; 

	/* reset the bt848 */
	btwrite(0, BT848_SRESET);
	DEBUG(printk(KERN_DEBUG "bttv%d: bt848_mem: 0x%08x\n",i,(unsigned int) btv->bt848_mem));

#ifdef RESET_MSP_HAUPPAUGE
        /* Reset the MSP on some Hauppauge cards */
        /* Thanks to Kyösti Mälkki (kmalkki@cc.hut.fi)! */
        /* Can this hurt cards without one? What about Miros with MSP? */
        btaor(32, ~32, BT848_GPIO_OUT_EN);
        btaor(0, ~32, BT848_GPIO_DATA);
        udelay(2500);
        btaor(32, ~32, BT848_GPIO_DATA);
        btaor(0, ~32, BT848_GPIO_OUT_EN);
#endif

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
	btv->win.depth=16;
	btv->win.color_fmt=BT848_COLOR_FMT_RGB16;
	btv->win.bpl=1024*btv->win.bpp;
	btv->win.swidth=1024;
	btv->win.sheight=768;
	btv->cap=0;

	btv->gmode=0;
	btv->risc_odd=0;
	btv->risc_even=0;
	btv->risc_jmp=0;
	btv->vbibuf=0;
	btv->grisc=0;
	btv->grabbing=0;
	btv->grabcount=0;
	btv->grab=0;
	btv->lastgrab=0;
        btv->field=btv->last_field=0;

	/* i2c */
	memcpy(&(btv->i2c),&bttv_i2c_bus_template,sizeof(struct i2c_bus));
	sprintf(btv->i2c.name,"bt848-%d",i);
	btv->i2c.data = btv;

	if (!(btv->risc_odd=(unsigned int *) kmalloc(RISCMEM_LEN/2, GFP_KERNEL)))
		return -1;
	if (!(btv->risc_even=(unsigned int *) kmalloc(RISCMEM_LEN/2, GFP_KERNEL)))
		return -1;
	if (!(btv->risc_jmp =(unsigned int *) kmalloc(2048, GFP_KERNEL)))
		return -1;
 	DEBUG(printk(KERN_DEBUG "risc_jmp: %p\n",btv->risc_jmp));
	btv->vbi_odd=btv->risc_jmp+16;
	btv->vbi_even=btv->vbi_odd+256;
	btv->bus_vbi_odd=virt_to_bus(btv->risc_jmp+12);
	btv->bus_vbi_even=virt_to_bus(btv->risc_jmp+6);

	btwrite(virt_to_bus(btv->risc_jmp+2), BT848_RISC_STRT_ADD);
	btv->vbibuf=(unsigned char *) vmalloc(VBIBUF_SIZE);
	if (!btv->vbibuf) 
		return -1;
	if (!(btv->grisc=(unsigned int *) kmalloc(32768, GFP_KERNEL)))
		return -1;

	memset(btv->vbibuf, 0, VBIBUF_SIZE); /* We don't want to return random
	                                        memory to the user */
	btv->fbuffer=NULL;

	bt848_muxsel(btv, 1);
	bt848_set_winsize(btv);

/*	btwrite(0, BT848_TDEC); */
	btwrite(0x10, BT848_COLOR_CTL);
	btwrite(0x00, BT848_CAP_CTL);
	btwrite(0xfc, BT848_GPIO_DMA_CTL);

        /* select direct input */
	btwrite(0x00, BT848_GPIO_REG_INP);


	btwrite(0xff, BT848_VBI_PACK_SIZE);
	btwrite(1, BT848_VBI_PACK_DEL);

		
	btwrite(BT848_IFORM_MUX1 | BT848_IFORM_XTAUTO | BT848_IFORM_PAL_BDGHI,
		BT848_IFORM);

	btwrite(0xd8, BT848_CONTRAST_LO);
	bt848_bright(btv, 0x10);

	btwrite(0x20, BT848_E_VSCALE_HI);
	btwrite(0x20, BT848_O_VSCALE_HI);
	btwrite(/*BT848_ADC_SYNC_T|*/
		BT848_ADC_RESERVED|BT848_ADC_CRUSH, BT848_ADC);

	btwrite(BT848_CONTROL_LDEC, BT848_E_CONTROL);
	btwrite(BT848_CONTROL_LDEC, BT848_O_CONTROL);

	btv->picture.colour=254<<7;
	btv->picture.brightness=128<<8;
	btv->picture.hue=128<<8;
	btv->picture.contrast=0xd8<<7;

	btwrite(0x00, BT848_E_SCLOOP);
	btwrite(0x00, BT848_O_SCLOOP);

	/* clear interrupt status */
	btwrite(0xfffffUL, BT848_INT_STAT);
        
	/* set interrupt mask */
	btwrite(btv->triton1|
                /*BT848_INT_PABORT|BT848_INT_RIPERR|BT848_INT_PPERR|
                  BT848_INT_FDSR|BT848_INT_FTRGT|BT848_INT_FBUS|*/
                BT848_INT_VSYNC|
		BT848_INT_SCERR|
		BT848_INT_RISCI|BT848_INT_OCERR|BT848_INT_VPRES|
		BT848_INT_FMTCHG|BT848_INT_HLOCK,
		BT848_INT_MASK);

	make_vbitab(btv);
	bt848_set_risc_jmps(btv);
  
	/*
	 *	Now add the template and register the device unit.
	 */

	memcpy(&btv->video_dev,&bttv_template, sizeof(bttv_template));
	memcpy(&btv->vbi_dev,&vbi_template, sizeof(vbi_template));
	memcpy(&btv->radio_dev,&radio_template,sizeof(radio_template));
	idcard(i);

	if(video_register_device(&btv->video_dev,VFL_TYPE_GRABBER)<0)
		return -1;
	if(video_register_device(&btv->vbi_dev,VFL_TYPE_VBI)<0) 
        {
	        video_unregister_device(&btv->video_dev);
		return -1;
	}
	if (radio[i])
	{
		if(video_register_device(&btv->radio_dev, VFL_TYPE_RADIO)<0) 
                {
		        video_unregister_device(&btv->vbi_dev);
		        video_unregister_device(&btv->video_dev);
			return -1;
		}
	}
	i2c_register_bus(&btv->i2c);

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
		IDEBUG(printk ("bttv%d: astat %08x\n", btv->nr, astat));
		IDEBUG(printk ("bttv%d:  stat %08x\n", btv->nr, stat));

		/* get device status bits */
		dstat=btread(BT848_DSTATUS);
    
		if (astat&BT848_INT_FMTCHG) 
		{
			IDEBUG(printk ("bttv%d: IRQ_FMTCHG\n", btv->nr));
			/*btv->win.norm&=
			  (dstat&BT848_DSTATUS_NUML) ? (~1) : (~0); */
		}
		if (astat&BT848_INT_VPRES) 
		{
			IDEBUG(printk ("bttv%d: IRQ_VPRES\n", btv->nr));
		}
		if (astat&BT848_INT_VSYNC) 
		{
			IDEBUG(printk ("bttv%d: IRQ_VSYNC\n", btv->nr));
		}
		if (astat&BT848_INT_SCERR) {
			IDEBUG(printk ("bttv%d: IRQ_SCERR\n", btv->nr));
			bt848_dma(btv, 0);
			bt848_dma(btv, 1);
			wake_up_interruptible(&btv->vbiq);
			wake_up_interruptible(&btv->capq);

		}
		if (astat&BT848_INT_RISCI) 
		{
			IDEBUG(printk ("bttv%d: IRQ_RISCI\n", btv->nr));

			/* captured VBI frame */
			if (stat&(1<<28)) 
			{
				btv->vbip=0;
				wake_up_interruptible(&btv->vbiq);
			}

			/* captured full frame */
			if (stat&(2<<28)) 
			{
				wake_up_interruptible(&btv->capq);
                                btv->last_field=btv->field;
                                btv->grab++;
                                btv->frame_stat[btv->grf] = GBUFFER_DONE;
			        if ((--btv->grabbing))
				{
					btv->gro = btv->gro_next;
					btv->gre = btv->gre_next;
					btv->grf = btv->grf_next;
                                        btv->risc_jmp[5]=btv->gro;
					btv->risc_jmp[11]=btv->gre;
					bt848_set_geo(btv, btv->gwidth,
						      btv->gheight,
						      btv->gfmt);
				} else {
					bt848_set_risc_jmps(btv);
					bt848_set_geo(btv, btv->win.width, 
						      btv->win.height,
						      btv->win.color_fmt);
				}
				wake_up_interruptible(&btv->capq);
				break;
			}
			if (stat&(8<<28)) 
			{
			        btv->risc_jmp[5]=btv->gro;
				btv->risc_jmp[11]=btv->gre;
				btv->risc_jmp[12]=BT848_RISC_JUMP;
				bt848_set_geo(btv, btv->gwidth, btv->gheight,
					      btv->gfmt);
			}
		}
		if (astat&BT848_INT_OCERR) 
		{
			IDEBUG(printk ("bttv%d: IRQ_OCERR\n", btv->nr));
		}
		if (astat&BT848_INT_PABORT) 
		{
			IDEBUG(printk ("bttv%d: IRQ_PABORT\n", btv->nr));
		}
		if (astat&BT848_INT_RIPERR) 
		{
			IDEBUG(printk ("bttv%d: IRQ_RIPERR\n", btv->nr));
		}
		if (astat&BT848_INT_PPERR) 
		{
			IDEBUG(printk ("bttv%d: IRQ_PPERR\n", btv->nr));
		}
		if (astat&BT848_INT_FDSR) 
		{
			IDEBUG(printk ("bttv%d: IRQ_FDSR\n", btv->nr));
		}
		if (astat&BT848_INT_FTRGT) 
		{
			IDEBUG(printk ("bttv%d: IRQ_FTRGT\n", btv->nr));
		}
		if (astat&BT848_INT_FBUS) 
		{
			IDEBUG(printk ("bttv%d: IRQ_FBUS\n", btv->nr));
		}
		if (astat&BT848_INT_HLOCK) 
		{
			if ((dstat&BT848_DSTATUS_HLOC) || (btv->radio))
				audio(btv, AUDIO_ON);
			else
				audio(btv, AUDIO_OFF);
		}
    
		if (astat&BT848_INT_I2CDONE) 
		{
		}
    
		count++;
		if (count > 10)
			printk (KERN_WARNING "bttv%d: irq loop %d\n", btv->nr, count);
		if (count > 20) 
		{
			btwrite(0, BT848_INT_MASK);
			printk(KERN_ERR 
			       "bttv%d: IRQ lockup, cleared int mask\n", btv->nr);
		}
	}
}



/*
 *	Scan for a Bt848 card, request the irq and map the io memory 
 */

int configure_bt848(struct pci_dev *dev, int bttv_num)
{
	int result;
	unsigned char bus, devfn, command;
	struct bttv *btv;

        btv=&bttvs[bttv_num];
        btv->dev=dev;
        btv->nr = bttv_num;
        btv->bus=bus=dev->bus->number;
        btv->devfn=devfn=dev->devfn;
        btv->bt848_mem=NULL;
        btv->vbibuf=NULL;
        btv->risc_jmp=NULL;
        btv->vbi_odd=NULL;
        btv->vbi_even=NULL;
        btv->vbiq=NULL;
        btv->capq=NULL;
        btv->capqo=NULL;
        btv->capqe=NULL;
        btv->vbip=VBIBUF_SIZE;

        btv->id=dev->device;
        btv->irq=dev->irq;
        btv->bt848_adr=dev->base_address[0];
        if (btv->id >= 878)
                btv->i2c_command = 0x83;                   
        else
                btv->i2c_command=(I2C_TIMING | BT848_I2C_SCL | BT848_I2C_SDA);

        if (remap[bttv_num])
        {
                if (remap[bttv_num] < 0x1000)
                        remap[bttv_num]<<=20;
                remap[bttv_num]&=PCI_BASE_ADDRESS_MEM_MASK;
                printk(KERN_INFO "bttv%d: remapping to : 0x%08x.\n",
                       bttv_num,remap[bttv_num]);
                remap[bttv_num]|=btv->bt848_adr&(~PCI_BASE_ADDRESS_MEM_MASK);
                pci_write_config_dword(dev, PCI_BASE_ADDRESS_0, remap[bttv_num]);
                pci_read_config_dword(dev, PCI_BASE_ADDRESS_0, &btv->bt848_adr);
                btv->dev->base_address[0] = btv->bt848_adr;
        }					
        btv->bt848_adr&=PCI_BASE_ADDRESS_MEM_MASK;
        pci_read_config_byte(dev, PCI_CLASS_REVISION, &btv->revision);
        printk(KERN_INFO "bttv%d: Brooktree Bt%d (rev %d) ",
               bttv_num,btv->id, btv->revision);
        printk("bus: %d, devfn: %d, ",
               btv->bus, btv->devfn);
        printk("irq: %d, ",btv->irq);
        printk("memory: 0x%08x.\n", btv->bt848_adr);
        
        btv->pll.pll_ifreq=0;
        btv->pll.pll_ifreq=0;
        btv->pll.pll_crystal=0;
        if(pll[btv->nr])
                if (!(btv->id==848 && btv->revision==0x11))
                {
                        printk(KERN_INFO "bttv%d: internal PLL, single crystal operation enabled\n",bttv_num);
                        btv->pll.pll_ofreq=28636363;
                        btv->pll.pll_ifreq=35468950;
                        btv->pll.pll_crystal=BT848_IFORM_XT1;
                }
        
        btv->bt848_mem=ioremap(btv->bt848_adr, 0x1000);
        
	/* clear interrupt mask */
	btwrite(0, BT848_INT_MASK);

        result = request_irq(btv->irq, bttv_irq,
                             SA_SHIRQ | SA_INTERRUPT,"bttv",(void *)btv);
        if (result==-EINVAL) 
        {
                printk(KERN_ERR "bttv%d: Bad irq number or handler\n",
                       bttv_num);
                return -EINVAL;
        }
        if (result==-EBUSY)
        {
                printk(KERN_ERR "bttv%d: IRQ %d busy, change your PnP config in BIOS\n",bttv_num,btv->irq);
                return result;
        }
        if (result < 0) 
                return result;
        
        pci_set_master(dev);

        btv->triton1=triton1 ? BT848_INT_ETBF : 0;
        if (triton1 && btv->id >= 878) 
        {
                btv->triton1 = 0;
                printk("bttv: Enabling 430FX compatibilty for bt878\n");
                pci_read_config_byte(dev, BT878_DEVCTRL, &command);
                command|=BT878_EN_TBFX;
                pci_write_config_byte(dev, BT878_DEVCTRL, command);
                pci_read_config_byte(dev, BT878_DEVCTRL, &command);
                if (!(command&BT878_EN_TBFX)) 
                {
                        printk("bttv: 430FX compatibility could not be enabled\n");
                        return -1;
                }
        }
        
        return 0;
}

static int find_bt848(void)
{
        struct pci_dev *dev = pci_devices;
        int result=0;

        bttv_num=0;

        while (dev)
        {
                if (dev->vendor == PCI_VENDOR_ID_BROOKTREE)
                        if ((dev->device == PCI_DEVICE_ID_BT848)||
                            (dev->device == PCI_DEVICE_ID_BT849)||
                            (dev->device == PCI_DEVICE_ID_BT878)||
                            (dev->device == PCI_DEVICE_ID_BT879))
                                result=configure_bt848(dev,bttv_num++);
                if (result)
                        return result;
                dev = dev->next;
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

		btand(~15, BT848_GPIO_DMA_CTL);

		/* first disable interrupts before unmapping the memory! */
		btwrite(0, BT848_INT_MASK);
		btwrite(0xffffffffUL,BT848_INT_STAT);
		btwrite(0x0, BT848_GPIO_OUT_EN);

    		/* unregister i2c_bus */
		i2c_unregister_bus((&btv->i2c));

		/* disable PCI bus-mastering */
		pci_read_config_byte(btv->dev, PCI_COMMAND, &command);
		/* Should this be &=~ ?? */
 		command|=PCI_COMMAND_MASTER;
		pci_write_config_byte(btv->dev, PCI_COMMAND, command);
    
		/* unmap and free memory */
		if (btv->grisc)
		        kfree((void *) btv->grisc);

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

		if(btv->video_dev.minor!=-1)
			video_unregister_device(&btv->video_dev);
		if(btv->vbi_dev.minor!=-1)
			video_unregister_device(&btv->vbi_dev);
		if (radio && btv->radio_dev.minor != -1)
			video_unregister_device(&btv->radio_dev);
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

	/* initialize Bt848s */
	for (i=0; i<bttv_num; i++) 
	{
		if (init_bt848(i)<0) 
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

/*
 * Local variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * indent-tabs-mode: nil
 * tab-width: 8
 * End:
 */
 
