/*
    zr36120.c - Zoran 36120/36125 based framegrabbers

    Copyright (C) 1998-1999 Pauline Middelink <middelin@polyware.nl>

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

#include <linux/config.h>
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

#include <linux/version.h>
#include <asm/uaccess.h>

#include "linux/video_decoder.h"
#include "tuner.h"
#include "zr36120.h"
#include "zr36120_mem.h"

/* sensible default */
#ifndef CARDTYPE
#define CARDTYPE 0
#endif

/* Anybody who uses more than four? */
#define ZORAN_MAX 4

static ulong irq1 = 0;

       unsigned int triton1=0;			/* triton1 chipset? */
static unsigned int cardtype[ZORAN_MAX]={ [ 0 ... ZORAN_MAX-1 ] = CARDTYPE };

MODULE_AUTHOR("Pauline Middelink <middelin@polyware.nl>");
MODULE_DESCRIPTION("Zoran ZR36120 based framegrabber");
MODULE_PARM(triton1,"i");
MODULE_PARM(cardtype,"1-" __MODULE_STRING(ZORAN_MAX) "i");

static int zoran_cards;
static struct zoran zorans[ZORAN_MAX];

/*
 * the meaning of each element can be found in zr36120.h
 * Determining the value of gpdir/gpval can be tricky. The
 * best way is to run the card under the original software
 * and read the values from the general purpose registers
 * 0x28 and 0x2C. How you do that is left as an exercise
 * to the impatient reader :)
 */
#define T 1	/* to seperate the bools from the ints */
#define F 0
static struct tvcard tvcards[] = {
	/* reported working by <middelin@polyware.nl> */
/*0*/	{ "Trust Victor II",
	  2, 0, T, T, T, T, 0x7F, 0x80, { 1, SVHS(6) }, { 0 } },
	/* reported working by <Michael.Paxton@aihw.gov.au>  */
/*1*/   { "Aitech WaveWatcher TV-PCI",
	  3, 0, T, F, T, T, 0x7F, 0x80, { 1, TUNER(3), SVHS(6) }, { 0 } },
	/* reported working by ? */
/*2*/	{ "Genius Video Wonder PCI Video Capture Card",
	  2, 0, T, T, T, T, 0x7F, 0x80, { 1, SVHS(6) }, { 0 } },
	/* reported working by <Pascal.Gabriel@wanadoo.fr> */
/*3*/	{ "Guillemot Maxi-TV PCI",
	  2, 0, T, T, T, T, 0x7F, 0x80, { 1, SVHS(6) }, { 0 } },
	/* reported working by "Craig Whitmore <lennon@igrin.co.nz> */
/*4*/	{ "Quadrant Buster",
	  3, 3, T, F, T, T, 0x7F, 0x80, { SVHS(1), TUNER(2), 3 }, { 1, 2, 3 } },
	/* a debug entry which has all inputs mapped */
/*5*/	{ "ZR36120 based framegrabber (all inputs enabled)",
	  6, 0, T, T, T, T, 0x7F, 0x80, { 1, 2, 3, 4, 5, 6 }, { 0 } }
};
#undef T
#undef F
#define NRTVCARDS (sizeof(tvcards)/sizeof(tvcards[0]))

static struct { const char name[8]; int mode; int bpp; } palette2fmt[] = {
/* n/a     */	{ "n/a",     0, 0 },
/* GREY    */	{ "GRAY",    0, 0 },
/* HI240   */	{ "HI240",   0, 0 },
/* RGB565  */	{ "RGB565",  ZORAN_VFEC_RGB_RGB565|ZORAN_VFEC_LE, 2 },
/* RGB24   */	{ "RGB24",   ZORAN_VFEC_RGB_RGB888|ZORAN_VFEC_LE|ZORAN_VFEC_PACK24, 3 },
/* RGB32   */	{ "RGB32",   ZORAN_VFEC_RGB_RGB888|ZORAN_VFEC_LE, 4 },
/* RGB555  */	{ "RGB555",  ZORAN_VFEC_RGB_RGB555|ZORAN_VFEC_LE, 2 },
/* YUV422  */	{ "YUV422",  ZORAN_VFEC_RGB_YUV422|ZORAN_VFEC_LE, 3 },
/* YUYV    */	{ "YUYV",    0, 0 },
/* UYVY    */	{ "UYVY",    0, 0 },
/* YUV420  */	{ "YUV420",  0, 0 },
/* YUV411  */	{ "YUV411",  0, 0 },
/* RAW     */	{ "RAW",     0, 0 },
/* YUV422P */	{ "YUV422P", 0, 0 },
/* YUV411P */	{ "YUV411P", 0, 0 }};
#define NRPALETTES (sizeof(palette2fmt)/sizeof(palette2fmt[0]))

/* ----------------------------------------------------------------------- */
/* ZORAN chipset detector						   */
/* shamelessly stolen from bttv.c					   */
/* Reason for beeing here: we need to detect if we are running on a        */
/* Triton based chipset, and if so, enable a certain bit                   */
/* ----------------------------------------------------------------------- */

void handle_chipset(void)
{
	struct pci_dev *dev = NULL;
  
	/* Just in case some nut set this to something dangerous */
	if (triton1)
		triton1 = ZORAN_VDC_TRICOM;
	
	while ((dev = pci_find_device(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82437, dev))) 
	{
		printk(KERN_INFO "zoran: Host bridge 82437FX Triton PIIX\n");
		triton1 = ZORAN_VDC_TRICOM;
	}
}

/* ----------------------------------------------------------------------- */
/* ZORAN functions							   */
/* ----------------------------------------------------------------------- */

static void zoran_set_geo(struct zoran* ztv, struct vidinfo* i);

static
void zoran_dump(struct zoran *ztv)
{
	char	str[1024];
	char	*p=str; /* shut up, gcc! */
	int	i;

	for (i=0; i<0x60; i+=4) {
		if ((i % 16) == 0) {
			if (i) printk(/*KERN_DEBUG*/ "%s\n",str);
			p = str;
			p+= sprintf(str, "       %04x: ",i);
		}
		p += sprintf(p, "%08x ",zrread(i));
	}
}

static
void reap_states(struct zoran* ztv)
{
	irq1++;		/* debugging... */

	/*
	 * GRABBING?
	 */
	if ( test_bit(STATE_GRAB, &ztv->state) ) {
		int i;

		/* are we already grabbing? */
		if (test_bit(STATE_GRAB, &ztv->prevstate)) {

			/* did we get a complete grab? */
			if (zrread(ZORAN_VSTR) & ZORAN_VSTR_GRAB)
				goto out;

			/* we are done with this buffer, tell everyone */
			ztv->grabinfo[ztv->lastframe].status = FBUFFER_DONE;
			wake_up_interruptible(&ztv->grabq);
		}

		/* locate a new frame to grab */
		for (i=0; i<ZORAN_MAX_FBUFFERS; i++)
			if (ztv->grabinfo[i].status == FBUFFER_GRABBING) {

				/* there is a buffer more to be grabbed... */
				ztv->lastframe = i;

DEBUG(printk(KERN_DEBUG "irq(%ld): starting grab(%d)\n",irq1,i));

				/* loadup the frame settings */
				read_lock(&ztv->lock);
				zoran_set_geo(ztv,&ztv->grabinfo[i]);
				read_unlock(&ztv->lock);

				zrand(~ZORAN_VDC_VIDEN,ZORAN_VDC);
				zrand(~ZORAN_OCR_OVLEN, ZORAN_OCR);
				zror(ZORAN_VSTR_SNAPSHOT,ZORAN_VSTR);
				zror(ZORAN_VDC_VIDEN,ZORAN_VDC);

				/* start single-shot grab */
				zror(ZORAN_VSTR_GRAB, ZORAN_VSTR);
				goto out;
			}

DEBUG(printk(KERN_DEBUG "irq(%ld): nothing more to grab\n",irq1));

		/* turn grabbing off the next time around */
		clear_bit(STATE_GRAB, &ztv->state);

		/* force re-init of Read or Overlay settings */
		clear_bit(STATE_READ, &ztv->prevstate);
		clear_bit(STATE_OVERLAY, &ztv->prevstate);
	}

	/*
	 * READING?
	 */
	if ( test_bit(STATE_READ, &ztv->state) ) {
		/* are we already reading? */
		if (!test_bit(STATE_READ, &ztv->prevstate)) {

DEBUG(printk(KERN_DEBUG "irq(%ld): starting read\n",irq1));

			read_lock(&ztv->lock);
			zoran_set_geo(ztv,&ztv->readinfo);
			read_unlock(&ztv->lock);

			zrand(~ZORAN_VDC_VIDEN,ZORAN_VDC);
			zrand(~ZORAN_OCR_OVLEN, ZORAN_OCR);
			zror(ZORAN_VSTR_SNAPSHOT,ZORAN_VSTR);
			zror(ZORAN_VDC_VIDEN,ZORAN_VDC);

			/* start single-shot grab */
			zror(ZORAN_VSTR_GRAB, ZORAN_VSTR);
			goto out;
		}

		/* did we get a complete grab? */
		if (zrread(ZORAN_VSTR) & ZORAN_VSTR_GRAB)
			goto out;

DEBUG(printk(KERN_DEBUG "irq(%ld): nothing more to read\n",irq1));

		/* turn reading off the next time around */
		clear_bit(STATE_READ, &ztv->state);
		/* force re-init of Overlay settings */
		clear_bit(STATE_OVERLAY, &ztv->prevstate);

		/* we are done, tell everyone */
		wake_up_interruptible(&ztv->readq);
	}

	/*
	 * OVERLAYING?
	 */
	if ( test_bit(STATE_OVERLAY, &ztv->state) ) {
		/* are we already overlaying? */
		if (!test_bit(STATE_OVERLAY, &ztv->prevstate)) {

DEBUG(printk(KERN_DEBUG "irq(%ld): starting overlay\n",irq1));

			read_lock(&ztv->lock);
			zoran_set_geo(ztv,&ztv->overinfo);
			read_unlock(&ztv->lock);

			zrand(~ZORAN_VDC_VIDEN,ZORAN_VDC);
			zrand(~ZORAN_VSTR_SNAPSHOT,ZORAN_VSTR);
			zror(ZORAN_OCR_OVLEN, ZORAN_OCR);
			zror(ZORAN_VDC_VIDEN,ZORAN_VDC);
		}

		/*
		 * leave overlaying on, but turn interrupts off.
		 */
		zrand(~ZORAN_ICR_EN,ZORAN_ICR);
		goto out;
	}

	/*
	 * THEN WE MUST BE IDLING
	 */
DEBUG(printk(KERN_DEBUG "irq(%ld): turning off\n",irq1));
	/* nothing further to do, disable DMA and further IRQs */
	zrand(~ZORAN_VDC_VIDEN,ZORAN_VDC);
	zrand(~ZORAN_ICR_EN,ZORAN_ICR);
out:
	ztv->prevstate = ztv->state;
}

static
void zoran_irq(int irq, void *dev_id, struct pt_regs * regs)
{
	u32 stat,estat;
	int count = 0;
	struct zoran *ztv = (struct zoran *)dev_id;

	for (;;) {
		/* get/clear interrupt status bits */
		stat=zrread(ZORAN_ISR);
		estat=stat & zrread(ZORAN_ICR);
		if (!estat)
			return;
		zrwrite(estat,ZORAN_ISR);
		IDEBUG(printk(KERN_DEBUG "%s: estat %08x\n",CARD,estat));
		IDEBUG(printk(KERN_DEBUG "%s:  stat %08x\n",CARD,stat));

		if (estat & ZORAN_ISR_CODE)
		{
			IDEBUG(printk(KERN_DEBUG "%s: CodReplIRQ\n",CARD));
		}
		if (estat & ZORAN_ISR_GIRQ0)
		{
			IDEBUG(printk(KERN_DEBUG "%s: GIRQ0\n",CARD));
			if (!ztv->card->usegirq1)
				reap_states(ztv);
		}
		if (estat & ZORAN_ISR_GIRQ1)
		{
			IDEBUG(printk(KERN_DEBUG "%s: GIRQ1\n",CARD));
			if (ztv->card->usegirq1)
				reap_states(ztv);
		}

		count++;
		if (count > 10)
			printk(KERN_ERR "%s: irq loop %d (%x)\n",CARD,count,estat);
		if (count > 20)
		{
			zrwrite(0, ZORAN_ICR);
			printk(KERN_ERR "%s: IRQ lockup, cleared int mask\n",CARD);
		}
	}
}

/*
 *      Scan for a Zoran chip, request the irq and map the io memory
 */
static int find_zoran(void)
{
	unsigned char command, latency;
	int result;
	struct zoran *ztv;
	struct pci_dev *dev;
	int zoran_num=0;

	if (!pcibios_present())
	{
		DEBUG(printk(KERN_DEBUG "zoran: PCI-BIOS not present or not accessible!\n"));
		return 0;
	}

	for (dev = pci_devices; dev != NULL; dev = dev->next)
	{
		if (dev->vendor != PCI_VENDOR_ID_ZORAN)
			continue;
		if (dev->device != PCI_DEVICE_ID_ZORAN_36120)
			continue;

		/* Ok, ZR36120 found! */
		ztv=&zorans[zoran_num];
		ztv->dev=dev;
		ztv->id=dev->device;
		ztv->zoran_mem=NULL;

		ztv->zoran_adr = ztv->dev->resource[0].start;
		pci_read_config_byte(ztv->dev, PCI_CLASS_REVISION,
			     &ztv->revision);
		printk(KERN_INFO "zoran: Zoran %x (rev %d) ",
			ztv->id, ztv->revision);
		printk("bus: %d, devfn: %d, ",
			ztv->dev->bus->number, ztv->dev->devfn);
		printk("irq: %d, ",ztv->dev->irq);
		printk("memory: 0x%08x.\n", ztv->zoran_adr);

		ztv->zoran_mem = ioremap(ztv->zoran_adr, 0x1000);
		DEBUG(printk(KERN_DEBUG "zoran: mapped-memory at 0x%p\n",ztv->zoran_mem));

		result = request_irq(ztv->dev->irq, zoran_irq,
			SA_SHIRQ|SA_INTERRUPT,"zoran",(void *)ztv);
		if (result==-EINVAL)
		{
			printk(KERN_ERR "zoran: Bad irq number or handler\n");
			return -EINVAL;
		}
		if (result==-EBUSY)
		{
			printk(KERN_ERR "zoran: IRQ %d busy, change your PnP config in BIOS\n",ztv->dev->irq);
			return result;
		}
		if (result < 0)
			return result;

		/* Enable bus-mastering */
		pci_read_config_byte(ztv->dev, PCI_COMMAND, &command);
		command|=PCI_COMMAND_MASTER|PCI_COMMAND_MEMORY;
		pci_write_config_byte(ztv->dev, PCI_COMMAND, command);
		pci_read_config_byte(ztv->dev, PCI_COMMAND, &command);
		if (!(command&PCI_COMMAND_MASTER))
		{
			printk(KERN_ERR "zoran: PCI bus-mastering could not be enabled\n");
			return -1;
		}
		pci_read_config_byte(ztv->dev, PCI_LATENCY_TIMER, &latency);
		if (!latency)
		{
			latency=32;
			pci_write_config_byte(ztv->dev, PCI_LATENCY_TIMER, latency);
			DEBUG(printk(KERN_INFO "zoran: latency set to %d\n",latency));
		}
		zoran_num++;
	}
	if(zoran_num)
		printk(KERN_INFO "zoran: %d Zoran card(s) found.\n",zoran_num);
	return zoran_num;
}

static
int zoran_muxsel(struct zoran* ztv, int channel, int norm)
{
	int	rv;

	/* set the new video norm */
	rv = i2c_control_device(&(ztv->i2c), I2C_DRIVERID_VIDEODECODER, DECODER_SET_NORM, &norm);
	if (rv)
		return rv;
	ztv->norm = norm;

	/* map the given channel to the cards decoder's channel */
	channel = ztv->card->video_mux[channel] & CHANNEL_MASK;

	/* set the new channel */
	rv = i2c_control_device(&(ztv->i2c), I2C_DRIVERID_VIDEODECODER, DECODER_SET_INPUT, &channel);
	return rv;
}

/* Tell the interrupt handler what to to.  */
static
void zoran_cap(struct zoran* ztv, int on)
{
	DEBUG(printk(KERN_DEBUG "       zoran_cap(%d) at %ld, state=%x\n",on,irq1,ztv->state));

	if (on) {
		ztv->running = 1;
		/* 
		 * Clear the previous state flag. This way the irq
		 * handler will be forced to re-examine its current
		 * state from scratch, setting up the registers along
		 * the way.
		 */
		clear_bit(STATE_OVERLAY, &ztv->prevstate);
		/*
		 * turn interrupts back on. The DMA will be enabled
		 * inside the irq handler when it detects a restart.
		 */
		zror(ZORAN_ICR_CODE|ZORAN_ICR_GIRQ0|ZORAN_ICR_GIRQ1,ZORAN_ICR);
		zror(ZORAN_ICR_EN,ZORAN_ICR);
	}
	else {
		ztv->running = 0;
		/*
		 * turn interrupts and DMA both off
		 */
		zrand(~ZORAN_VDC_VIDEN,ZORAN_VDC);
		zrand(~ZORAN_ICR_EN,ZORAN_ICR);
	}
}

static ulong dmask[] = {
	0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFFFC, 0xFFFFFFF8,
	0xFFFFFFF0, 0xFFFFFFE0, 0xFFFFFFC0, 0xFFFFFF80,
	0xFFFFFF00, 0xFFFFFE00, 0xFFFFFC00, 0xFFFFF800,
	0xFFFFF000, 0xFFFFE000, 0xFFFFC000, 0xFFFF8000,
	0xFFFF0000, 0xFFFE0000, 0xFFFC0000, 0xFFF80000,
	0xFFF00000, 0xFFE00000, 0xFFC00000, 0xFF800000,
	0xFF000000, 0xFE000000, 0xFC000000, 0xF8000000,
	0xF0000000, 0xE0000000, 0xC0000000, 0x80000000
};

static
void zoran_built_overlay(struct zoran* ztv, int count, struct video_clip *vcp)
{
	ulong*	mtop;
	int	ystep = (ztv->vidXshift + ztv->vidWidth+31)/32;	/* next DWORD */
	int	mult = ztv->interlace;		/* double height? */
	int	i;

	DEBUG(printk(KERN_DEBUG "       overlay at %p, ystep=%d, clips=%d\n",ztv->overinfo.overlay,ystep,count));
	if (ztv->overinfo.overlay == 0) {
		zrand(~ZORAN_OCR_OVLEN, ZORAN_OCR);
		return;
	}

for (i=0; i<count; i++) {
	struct video_clip *vp = vcp+i;
	DEBUG(printk(KERN_DEBUG "       %d: clip(%d,%d,%d,%d)\n",
		i,vp->x,vp->y,vp->width,vp->height));
}

	/* clear entire blob */
/*	memset(ztv->overinfo.overlay, 0, 1024*1024/8); */

	/*
	 * activate the visible portion of the screen
	 * Note we take some shortcuts here, because we
	 * know the width can never be < 32. (I.e. a DWORD)
	 * We also assume the overlay starts somewhere in
	 * the FIRST dword.
	 */
	{
		int start = ztv->vidXshift;
		ulong firstd = dmask[start];
		ulong lastd = ~dmask[(start + ztv->overinfo.w) & 31];
		mtop = ztv->overinfo.overlay;
		for (i=0; i<ztv->overinfo.h; i++) {
			int w = ztv->vidWidth;
			ulong* line = mtop;
			if (start & 31) {
				*line++ = firstd;
				w -= 32-(start&31);
			}
			memset(line, ~0, w/8);
			if (w & 31)
				line[w/32] = lastd;
			mtop += ystep;
		}
	}

	/* process clipping regions */
	for (i=0; i<count; i++) {
		int h;
		if (vcp->x < 0 || vcp->x > ztv->overinfo.w ||
		    vcp->y < 0 || vcp->y > ztv->overinfo.h ||
		    vcp->width < 0 || (vcp->x+vcp->width) > ztv->overinfo.w ||
		    vcp->height < 0 || (vcp->y+vcp->height) > ztv->overinfo.h)
		{
			DEBUG(printk(KERN_DEBUG "%s: illegal clipzone (%d,%d,%d,%d) not in (0,0,%d,%d), adapting\n",CARD,vcp->x,vcp->y,vcp->width,vcp->height,ztv->overinfo.w,ztv->overinfo.h));
			if (vcp->x < 0) vcp->x = 0;
			if (vcp->x > ztv->overinfo.w) vcp->x = ztv->overinfo.w;
			if (vcp->y < 0) vcp->y = 0;
			if (vcp->y > ztv->overinfo.h) vcp->y = ztv->overinfo.h;
			if (vcp->width < 0) vcp->width = 0;
			if (vcp->x+vcp->width > ztv->overinfo.w) vcp->width = ztv->overinfo.w - vcp->x;
			if (vcp->height < 0) vcp->height = 0;
			if (vcp->y+vcp->height > ztv->overinfo.h) vcp->height = ztv->overinfo.h - vcp->y;
//			continue;
		}

		mtop = &ztv->overinfo.overlay[vcp->y*ystep];
		for (h=0; h<=vcp->height; h++) {
			int w;
			int x = ztv->vidXshift + vcp->x;
			for (w=0; w<=vcp->width; w++) {
				clear_bit(x&31, &mtop[x/32]);
				x++;
			}
			mtop += ystep;
		}
		++vcp;
	}

	mtop = ztv->overinfo.overlay;
	zrwrite(virt_to_bus(mtop), ZORAN_MTOP);
	zrwrite(virt_to_bus(mtop+ystep), ZORAN_MBOT);
	zraor((mult*ystep)<<0,~ZORAN_OCR_MASKSTRIDE,ZORAN_OCR);
}

struct tvnorm 
{
	u16 Wt, Wa, Ht, Ha, HStart, VStart;
};

static struct tvnorm tvnorms[] = {
	/* PAL-BDGHI */
/*	{ 864, 720, 625, 576, 131, 21 },*/
/*00*/	{ 864, 768, 625, 576, 81, 17 },
	/* NTSC */
/*01*/	{ 858, 720, 525, 480, 121, 10 },
	/* SECAM */
/*02*/	{ 864, 720, 625, 576, 131, 21 },
	/* BW50 */
/*03*/	{ 864, 720, 625, 576, 131, 21 },
	/* BW60 */
/*04*/	{ 858, 720, 525, 480, 121, 10 }
};
#define TVNORMS (sizeof(tvnorms)/sizeof(tvnorm))

static
void zoran_set_geo(struct zoran* ztv, struct vidinfo* i)
{
	ulong	top, bot;
	int	stride;
	int	winWidth, winHeight;
	int	maxWidth, maxHeight, maxXOffset, maxYOffset;
	int	filter;

	DEBUG(printk(KERN_DEBUG "       set_geo(rect=(%d,%d,%d,%d), norm=%d, format=%d, bpp=%d, bpl=%d, vidadr=%lx, overlay=%p)\n", i->x,i->y,i->w,i->h,ztv->norm,i->format,i->bpp,i->bpl,i->vidadr,i->overlay));

	/*
	 * make sure the DMA transfers are inhibited during our
	 * reprogramming of the chip
	 */
	zrand(~ZORAN_VDC_VIDEN,ZORAN_VDC);

	maxWidth = tvnorms[ztv->norm].Wa;
	maxHeight = tvnorms[ztv->norm].Ha;
	maxXOffset = tvnorms[ztv->norm].HStart;
	maxYOffset = tvnorms[ztv->norm].VStart;

	/*
	 * Set top, bottom ptrs. Since these must be DWORD aligned,
	 * possible adjust the x and the width of the window.
	 * so the endposition stay the same. The vidXshift will make
	 * sure we are not writing pixels before the requested x.
	 */
	ztv->vidXshift = 0;
	winWidth = i->w;
	top = i->vidadr + i->x*i->bpp + i->y*i->bpl;
	if (top & 3) {
		ztv->vidXshift = (top & 3) / i->bpp;
		winWidth += ztv->vidXshift;
		DEBUG(printk(KERN_DEBUG "       window-x shifted %d pixels left\n",ztv->vidXshift));
		top &= ~3;
	}

	/*
	 * bottom points to next frame but in interleaved mode we want
	 * to 'mix' the 2 frames to one capture, so 'bot' points to one
	 * (physical) line below the top line.
	 */
	bot = top + i->bpl;
	zrwrite(top,ZORAN_VTOP);
	zrwrite(bot,ZORAN_VBOT);

	/*
	 * Make sure the winWidth is DWORD aligned too,
	 * thereby automaticly making sure the stride to the
	 * next line is DWORD aligned too (as required by spec).
	 */
	if ((winWidth*i->bpp) & 3) {
		DEBUG(printk(KERN_DEBUG "       window-width enlarged by %d pixels\n",(winWidth*i->bpp) & 3));
		winWidth += (winWidth*i->bpp) & 3;
	}

	/* determine the DispMode and stride */
	if (i->h <= maxHeight/2) {
		/* single frame suffices for this height */
		zror(ZORAN_VFEC_DISPMOD, ZORAN_VFEC);
		ztv->interlace = 0;
		winHeight = i->h;
		if (winHeight < 0)	/* can happen for read's! */
			winHeight = -winHeight;
		stride = i->bpl - (winWidth*i->bpp);
	}
	else {
		/* interleaving needed for this height */
		zrand(~ZORAN_VFEC_DISPMOD, ZORAN_VFEC);
		ztv->interlace = 1;
		winHeight = i->h/2;
		stride = i->bpl*2 - (winWidth*i->bpp);
	}
	/* safety net, sometimes bpl is too short??? */
	if (stride<0) {
		DEBUG(printk(KERN_DEBUG "%s: WARNING stride = %d\n",CARD,stride));
		stride = 0;
	}

	zraor((winHeight<<12)|(winWidth<<0),~(ZORAN_VDC_VIDWINHT|ZORAN_VDC_VIDWINWID), ZORAN_VDC);
	zraor(stride<<16,~ZORAN_VSTR_DISPSTRIDE,ZORAN_VSTR);

	/* remember vidWidth, vidHeight for overlay calculations */
	ztv->vidWidth = winWidth;
	ztv->vidHeight = winHeight;
DEBUG(printk(KERN_DEBUG "       top=%08lx, bottom=%08lx, winWidth=%d, winHeight=%d, maxWidth=%d, maxHeight=%d, stride=%d\n",top,bot,winWidth,winHeight,maxWidth,maxHeight,stride));

	/* determine scales and crops */
	if (1) {
		int Wa, X, We, HorDcm, hcrop1, hcrop2, Hstart, Hend;

A:		Wa = maxWidth;
		X = (winWidth*64+Wa-1)/Wa;
		We = winWidth*64/X;
		HorDcm = 64-X;
		hcrop1 = 2*(Wa-We)/4;
		hcrop2 = Wa-We-hcrop1;
		Hstart = maxXOffset + hcrop1;
		Hend = maxXOffset + Wa-1-hcrop2;

		/*
		 * BUGFIX: Juha Nurmela <junki@qn-lpr2-165.quicknet.inet.fi> 
		 * found the solution to the color phase shift.
		 * See ChangeLog for the full explanation)
		 */
		if (!(Hstart & 1)) {
DEBUG(printk(KERN_DEBUG "       correcting horizontal start/end by one\n"));
			winWidth--;
			goto A;
		}

DEBUG(printk(KERN_DEBUG "       X: scale=%d, start=%d, end=%d\n", HorDcm, Hstart, Hend));

		zraor((Hstart<<10)|(Hend<<0),~(ZORAN_VFEH_HSTART|ZORAN_VFEH_HEND),ZORAN_VFEH);
		zraor((HorDcm<<14),~ZORAN_VFEC_HORDCM, ZORAN_VFEC);

		filter = ZORAN_VFEC_HFILTER_1;
		if (HorDcm >= 48)
			filter = ZORAN_VFEC_HFILTER_5; /* 5 tap filter */
		else if (HorDcm >= 32)
			filter = ZORAN_VFEC_HFILTER_4; /* 4 tap filter */
		else if (HorDcm >= 16)
			filter = ZORAN_VFEC_HFILTER_3; /* 3 tap filter */
		zraor(filter, ~ZORAN_VFEC_HFILTER, ZORAN_VFEC);
	}
	/* when height is negative, we want to read from line 0 */
	if (i->h < 0) {
		int Vstart = 0;
		int Vend = Vstart + winHeight;
		int VerDcm = 0;
DEBUG(printk(KERN_DEBUG "       Y: scale=%d, start=%d, end=%d\n", VerDcm, Vstart, Vend));
		zraor((Vstart<<10)|(Vend<<0),~(ZORAN_VFEV_VSTART|ZORAN_VFEV_VEND),ZORAN_VFEV);
		zraor((VerDcm<<8),~ZORAN_VFEC_VERDCM, ZORAN_VFEC);
	}
	else {
		int Ha = maxHeight/2;
		int Y = (winHeight*64+Ha-1)/Ha;
		int He = winHeight*64/Y;
		int VerDcm = 64-Y;
		int vcrop1 = 2*(Ha-He)/4;
		int vcrop2 = Ha-He-vcrop1;
		int Vstart = maxYOffset + vcrop1;
		int Vend = maxYOffset + Ha-1-vcrop2;

DEBUG(printk(KERN_DEBUG "       Y: scale=%d, start=%d, end=%d\n", VerDcm, Vstart, Vend));
		zraor((Vstart<<10)|(Vend<<0),~(ZORAN_VFEV_VSTART|ZORAN_VFEV_VEND),ZORAN_VFEV);
		zraor((VerDcm<<8),~ZORAN_VFEC_VERDCM, ZORAN_VFEC);
	}

DEBUG(printk(KERN_DEBUG "       F: format=%d(=%s)\n",i->format,palette2fmt[i->format].name));
	/* setup the requested format */
	zraor(palette2fmt[i->format].mode, ~(ZORAN_VFEC_RGB|ZORAN_VFEC_LE|ZORAN_VFEC_PACK24), ZORAN_VFEC);
}

#if LINUX_VERSION_CODE >= 0x020100
static
unsigned int zoran_poll(struct video_device *dev, struct file *file, poll_table *wait)
{
	struct zoran *ztv = (struct zoran *)dev;

	poll_wait(file, &ztv->readq, wait);

	return (POLLIN | POLLRDNORM);
}
#endif

/*
 * Open a zoran card. Right now the flags are just a hack
 */
static int zoran_open(struct video_device *dev, int flags)
{
	struct zoran *ztv = (struct zoran*)dev;
	int	i;

	DEBUG(printk(KERN_DEBUG "%s: open(dev,%d)\n",CARD,flags));

	switch (flags) {
	 case 0:
		/* already active? */
		if (ztv->user)
			return -EBUSY;
		ztv->user++;

		/* unmute audio */
		/* /what/ audio? */

/******************************************
 We really should be doing lazy allocing...
 ******************************************/
		/* allocate a frame buffer */
		if (!ztv->fbuffer)
			ztv->fbuffer = bmalloc(ZORAN_MAX_FBUFSIZE);
		if (!ztv->fbuffer) {
			/* could not get a buffer, bail out */
			ztv->user--;
			return -ENOBUFS;
		}
		/* at this time we _always_ have a framebuffer */
		memset(ztv->fbuffer,0,ZORAN_MAX_FBUFSIZE);

		if (!ztv->overinfo.overlay)
			ztv->overinfo.overlay = (void*)kmalloc(1024*1024/8, GFP_KERNEL);
		if (!ztv->overinfo.overlay) {
			/* could not get an overlay buffer, bail out */
			ztv->user--;
			bfree(ztv->fbuffer, ZORAN_MAX_FBUFSIZE);
			return -ENOBUFS;
		}
		/* at this time we _always_ have a overlay */

		/* clear buffer status */
		for (i=0; i<ZORAN_MAX_FBUFFERS; i++)
			ztv->grabinfo[i].status = FBUFFER_UNUSED;
		ztv->state = 0;
		ztv->prevstate = 0;
		ztv->lastframe = -1;

		/* setup the encoder to the initial values */
		i2c_control_device(&ztv->i2c, I2C_DRIVERID_VIDEODECODER, DECODER_SET_PICTURE, &ztv->picture);

		/* default to the compisite input since my camera is there */
		zoran_muxsel(ztv, 0, VIDEO_MODE_PAL);
		break;
	 case 1:
		break;
	}
	MOD_INC_USE_COUNT;
	return 0;
}

static
void zoran_close(struct video_device* dev)
{
	struct zoran *ztv = (struct zoran*)dev;

	DEBUG(printk(KERN_DEBUG "%s: close(dev)\n",CARD));

	/* we are no longer active, goodbye */
	ztv->user--;

	/* mute audio */
	/* stop the chip */
	zoran_cap(ztv, 0);

	/* free the allocated framebuffer */
	if (ztv->fbuffer)
		bfree( ztv->fbuffer, ZORAN_MAX_FBUFSIZE );
	ztv->fbuffer = 0;
	if (ztv->overinfo.overlay)
		kfree( ztv->overinfo.overlay );
	ztv->overinfo.overlay = 0;

	MOD_DEC_USE_COUNT;
}

static
long zoran_write(struct video_device* dev, const char* buf, unsigned long count, int nonblock)
{
	DEBUG(printk(KERN_DEBUG "zoran_write\n"));
	return -EINVAL;
}

static
long zoran_read(struct video_device* dev, char* buf, unsigned long count, int nonblock)
{
	struct zoran *ztv = (struct zoran*)dev;
	int	max;

	DEBUG(printk(KERN_DEBUG "zoran_read(%p,%ld,%d)\n",buf,count,nonblock));

	/* tell the state machine we want in too */
	write_lock_irq(&ztv->lock);
	ztv->readinfo.vidadr = virt_to_bus(phys_to_virt((ulong)ztv->fbuffer));
	set_bit(STATE_READ, &ztv->state);
	write_unlock_irq(&ztv->lock);
	zoran_cap(ztv, 1);

	/* wait for data to arrive */
	interruptible_sleep_on(&ztv->readq);

	/* see if a signal did it */
	if (signal_pending(current))
		return -ERESTARTSYS;

	/* give the user what he requested */
	max = ztv->readinfo.w*ztv->readinfo.bpp - ztv->readinfo.h*ztv->readinfo.bpl;
	if (count > max)
		count = max;
	if (copy_to_user((void*)buf, (void*)ztv->fbuffer, count))
		return -EFAULT;

	/* goodbye */
	return count;
}

/* append a new clipregion to the vector of video_clips */
static
void new_clip(struct video_window* vw, struct video_clip* vcp, int x, int y, int w, int h)
{
	vcp[vw->clipcount].x = x;
	vcp[vw->clipcount].y = y;
	vcp[vw->clipcount].width = w;
	vcp[vw->clipcount].height = h;
	vw->clipcount++;
}

static
int zoran_ioctl(struct video_device* dev, unsigned int cmd, void *arg)
{
	struct zoran* ztv = (struct zoran*)dev;

	switch (cmd) {
	 case VIDIOCGCAP:
	 {	/* get video capabilities */
		struct video_capability c;
		struct video_decoder_capability dc;
		int rv;
		DEBUG(printk(KERN_DEBUG "%s: GetCapabilities\n",CARD));

		/* fetch the capabilites of the decoder */
		dc.flags = 0;
		dc.inputs = -1;
		dc.outputs = -1;
		rv = i2c_control_device(&ztv->i2c, I2C_DRIVERID_VIDEODECODER, DECODER_GET_CAPABILITIES, &dc);
		if (rv)
			return rv;
		DEBUG(printk(KERN_DEBUG "%s: capabilities %d %d %d\n",CARD,dc.flags,dc.inputs,dc.outputs));

		strcpy(c.name,ztv->video_dev.name);
		c.type = VID_TYPE_CAPTURE|
			 VID_TYPE_OVERLAY|
			 VID_TYPE_CLIPPING|
			 VID_TYPE_FRAMERAM|
			 VID_TYPE_SCALES;
		c.channels = ztv->card->video_inputs;
		c.audios = ztv->card->audio_inputs;
		c.maxwidth = 768;
		c.maxheight = 576;
		c.minwidth = 32;
		c.minheight = 32;
		if (copy_to_user(arg,&c,sizeof(c)))
			return -EFAULT;
		return 0;
	 }

	 case VIDIOCGCHAN:
	 {
		struct video_channel v;
		int mux;

		if (copy_from_user(&v, arg,sizeof(v)))
			return -EFAULT;
		DEBUG(printk(KERN_DEBUG "%s: GetChannel(%d)\n",CARD,v.channel));
		v.flags=VIDEO_VC_AUDIO
#ifdef VIDEO_VC_NORM
			|VIDEO_VC_NORM
#endif
			;
		v.tuners=0;
		v.type=VIDEO_TYPE_CAMERA;
#ifdef I_EXPECT_POSSIBLE_NORMS_IN_THE_API
		v.norm=VIDEO_MODE_PAL|
		       VIDEO_MODE_NTSC|
		       VIDEO_MODE_SECAM;
#else
		v.norm=VIDEO_MODE_PAL;
#endif
		/* too many inputs? */
		if (v.channel >= ztv->card->video_inputs)
			return -EINVAL;

		/* now determine the name of the channel */
		mux = ztv->card->video_mux[v.channel];
		if (mux & IS_TUNER) {
			/* lets assume only one tuner, yes? */
			strcpy(v.name,"Television");
			v.type = VIDEO_TYPE_TV;
			if (ztv->have_tuner) {
				v.flags |= VIDEO_VC_TUNER;
				v.tuners = 1;
			}
		}
		else if (mux & IS_SVHS)
			sprintf(v.name,"S-Video-%d",v.channel);
		else
			sprintf(v.name,"CVBS-%d",v.channel);

		if (copy_to_user(arg,&v,sizeof(v)))
			return -EFAULT;
		return 0;
	 }
	 case VIDIOCSCHAN:
	 {	/* set video channel */
		struct video_channel v;
		if (copy_from_user(&v, arg,sizeof(v)))
			return -EFAULT;
		DEBUG(printk(KERN_DEBUG "%s: SetChannel(%d,%d)\n",CARD,v.channel,v.norm));
		if (v.channel >= ztv->card->video_inputs)
			return -EINVAL;

		if (v.norm != VIDEO_MODE_PAL &&
		    v.norm != VIDEO_MODE_NTSC &&
		    v.norm != VIDEO_MODE_SECAM &&
		    v.norm != VIDEO_MODE_AUTO)
			return -EOPNOTSUPP;

		/* make it happen, nr1! */
		return zoran_muxsel(ztv,v.channel,v.norm);
	 }

	 case VIDIOCGTUNER:
	 {
		struct video_tuner v;
		if (copy_from_user(&v, arg,sizeof(v)))
			return -EFAULT;

		/* Only one tuner for now */
		if (!ztv->have_tuner && v.tuner)
			return -EINVAL;

		strcpy(v.name,"Television");
		v.rangelow  = 0;
		v.rangehigh = ~0;
		v.flags     = VIDEO_TUNER_PAL|VIDEO_TUNER_NTSC|VIDEO_TUNER_SECAM;
		v.mode      = ztv->norm;
		v.signal    = 0xFFFF; /* unknown */

		if (copy_to_user(arg,&v,sizeof(v)))
			return -EFAULT;
		return 0;
	 }

	 case VIDIOCSTUNER:
	 {
		struct video_tuner v;
		if (copy_from_user(&v, arg, sizeof(v)))
			return -EFAULT;

		/* Only one tuner for now */
		if (!ztv->have_tuner && v.tuner)
			return -EINVAL;

		/* and it only has certain valid modes */
		if( v.mode != VIDEO_MODE_PAL &&
		    v.mode != VIDEO_MODE_NTSC &&
		    v.mode != VIDEO_MODE_SECAM)
			return -EOPNOTSUPP;

		/* engage! */
		return zoran_muxsel(ztv,v.tuner,v.mode);
	 }

	 case VIDIOCGPICT:
	 {
		struct video_picture p = ztv->picture;
		DEBUG(printk(KERN_DEBUG "%s: GetPicture\n",CARD));
		p.depth = ztv->depth;
		switch (p.depth) {
		 case  8: p.palette=VIDEO_PALETTE_YUV422;
			  break;
		 case 15: p.palette=VIDEO_PALETTE_RGB555;
			  break;
		 case 16: p.palette=VIDEO_PALETTE_RGB565;
			  break;
		 case 24: p.palette=VIDEO_PALETTE_RGB24;
			  break;
		 case 32: p.palette=VIDEO_PALETTE_RGB32;
			  break;
		}
		if (copy_to_user(arg, &p, sizeof(p)))
			return -EFAULT;
		return 0;
	 }
	 case VIDIOCSPICT:
	 {
		struct video_picture p;
		DEBUG(printk(KERN_DEBUG "%s: SetPicture\n",CARD));
		if (copy_from_user(&p, arg,sizeof(p)))
			return -EFAULT;

		/* depth must match with framebuffer */
		if (p.depth != ztv->depth)
			return -EINVAL;

		/* check if palette matches this bpp */
		if (p.palette<1 || p.palette>NRPALETTES ||
		    palette2fmt[p.palette].bpp != ztv->overinfo.bpp)
			return -EINVAL;

		write_lock_irq(&ztv->lock);
		ztv->overinfo.format = p.palette;
		ztv->picture = p;
		write_unlock_irq(&ztv->lock);

		/* tell the decoder */
		i2c_control_device(&ztv->i2c, I2C_DRIVERID_VIDEODECODER, DECODER_SET_PICTURE, &p);
		return 0;
	 }

	 case VIDIOCGWIN:
	 {
		struct video_window vw;
		DEBUG(printk(KERN_DEBUG "%s: GetWindow\n",CARD));
		read_lock(&ztv->lock);
		vw.x      = ztv->overinfo.x;
		vw.y      = ztv->overinfo.y;
		vw.width  = ztv->overinfo.w;
		vw.height = ztv->overinfo.h;
		vw.chromakey= 0;
		vw.flags  = 0;
		if (ztv->interlace)
			vw.flags|=VIDEO_WINDOW_INTERLACE;
		read_unlock(&ztv->lock);
		if (copy_to_user(arg,&vw,sizeof(vw)))
			return -EFAULT;
		return 0;
	 }
	 case VIDIOCSWIN:
	 {
		struct video_window vw;
		struct video_clip *vcp;
		int on;

		if (copy_from_user(&vw,arg,sizeof(vw)))
			return -EFAULT;

		DEBUG(printk(KERN_DEBUG "%s: SetWindow(%d,%d,%d,%d,%x,%d)\n",CARD,vw.x,vw.y,vw.width,vw.height,vw.flags,vw.clipcount));

		if (vw.flags)
			return -EINVAL;

		if (vw.clipcount>256)
			return -EDOM;   /* Too many! */

		/*
		 *      Do any clips.
		 */
		vcp = vmalloc(sizeof(struct video_clip)*(vw.clipcount+4));
		if (vcp==NULL)
			return -ENOMEM;
		if (vw.clipcount && copy_from_user(vcp,vw.clips,sizeof(struct video_clip)*vw.clipcount))
			return -EFAULT;

		on = ztv->running;
		if (on)
			zoran_cap(ztv, 0);

		/* by now we are committed to the new data... */
		write_lock_irq(&ztv->lock);
		ztv->overinfo.x = vw.x;
		ztv->overinfo.y = vw.y;
		ztv->overinfo.w = vw.width;
		ztv->overinfo.h = vw.height;
		write_unlock_irq(&ztv->lock);

		/*
		 *      Impose display clips
		 */
		if (vw.x<0)
			new_clip(&vw, vcp, 0, 0, -vw.x, vw.height-1);
		if (vw.y<0)
			new_clip(&vw, vcp, 0, 0, vw.width-1,-vw.y);
		if (vw.x+vw.width > ztv->swidth)
			new_clip(&vw, vcp, ztv->swidth-vw.x, 0, vw.width-1, vw.height-1);
		if (vw.y+vw.height > ztv->sheight)
			new_clip(&vw, vcp, 0, ztv->sheight-vw.y, vw.width-1, vw.height-1);

		/* built the requested clipping zones */
		zoran_set_geo(ztv, &ztv->overinfo);
		zoran_built_overlay(ztv, vw.clipcount, vcp);
		vfree(vcp);

		/* if we were on, restart the video engine */
		if (on) zoran_cap(ztv, on);
		return 0;
	 }
	 case VIDIOCCAPTURE:
	 {
		int v;
		get_user_ret(v,(int*)arg, -EFAULT);
		DEBUG(printk(KERN_DEBUG "%s: Capture(%d)\n",CARD,v));

		if (v==0) {
			zoran_cap(ztv, 0);
			clear_bit(STATE_OVERLAY, &ztv->state);
		}
		else {
			/* is VIDIOCSFBUF, VIDIOCSWIN done? */
			if (ztv->overinfo.vidadr==0 || ztv->overinfo.w==0 || ztv->overinfo.h==0)
				return -EINVAL;

			set_bit(STATE_OVERLAY, &ztv->state);
			zoran_cap(ztv, 1);
		}
		return 0;
	 }

	 case VIDIOCGFBUF:
	 {
		struct video_buffer v;
		DEBUG(printk(KERN_DEBUG "%s: GetFramebuffer\n",CARD));
		read_lock(&ztv->lock);
		v.base   = (void *)ztv->overinfo.vidadr;
		v.height = ztv->sheight;
		v.width  = ztv->swidth;
		v.depth  = ztv->depth;
		v.bytesperline = ztv->overinfo.bpl;
		read_unlock(&ztv->lock);
		if(copy_to_user(arg, &v,sizeof(v)))
			return -EFAULT;
		return 0;
	 }
	 case VIDIOCSFBUF:
	 {
		struct video_buffer v;
#if LINUX_VERSION_CODE >= 0x020100
			if(!capable(CAP_SYS_ADMIN))
#else
			if(!suser())
#endif
			return -EPERM;
		if (copy_from_user(&v, arg,sizeof(v)))
			return -EFAULT;
		if (v.depth!=15 && v.depth!=16 && v.depth!=24 && v.depth!=32)
			return -EINVAL;
		if (v.bytesperline<1)
			return -EINVAL;
		if (ztv->running)
			return -EBUSY;
		write_lock_irq(&ztv->lock);
		ztv->overinfo.vidadr  = (unsigned long)v.base;
		ztv->sheight      = v.height;
		ztv->swidth       = v.width;
		ztv->overinfo.bpp = ((v.depth+1)&0x38)/8;/* bytes per pixel */
		ztv->depth        = v.depth;		/* bits per pixel */
		ztv->overinfo.bpl = v.bytesperline;
		write_unlock_irq(&ztv->lock);

		DEBUG(printk(KERN_DEBUG "%s: SetFrameBuffer(%p,%dx%d, bpp %d, bpl %d)\n",CARD,v.base, v.width,v.height, ztv->overinfo.bpp, ztv->overinfo.bpl));
		return 0;
	 }

	 case VIDIOCSYNC:
	 {
		int i;
		get_user_ret(i,(int*)arg, -EFAULT);
		DEBUG(printk(KERN_DEBUG "%s: VIDEOCSYNC(%d)\n",CARD,i));
		if (i<0 || i>ZORAN_MAX_FBUFFERS)
			return -EINVAL;
		switch (ztv->grabinfo[i].status) {
		 case FBUFFER_UNUSED:
			return -EINVAL;
		 case FBUFFER_GRABBING:
			/* wait till this buffer gets grabbed */
			while (ztv->grabinfo[i].status == FBUFFER_GRABBING) {
				interruptible_sleep_on(&ztv->grabq);
				/* see if a signal did it */
				if (signal_pending(current))
					return -ERESTARTSYS;
			}
			/* fall through */
		 case FBUFFER_DONE:
			ztv->grabinfo[i].status = FBUFFER_UNUSED;
			break;
		}
		return 0;
	 }

	 case VIDIOCKEY:
	 {
		/* Will be handled higher up .. */
		return 0;
	 }

	 case VIDIOCMCAPTURE:
	 {
		struct video_mmap vm;
		struct vidinfo* frame;
		if (copy_from_user(&vm,arg,sizeof(vm)))
			return -EFAULT;
		if (vm.frame<0 || vm.frame>ZORAN_MAX_FBUFFERS ||
		    vm.width<32 || vm.width>768 ||
		    vm.height<32 || vm.height>576 ||
		    vm.format<0 || vm.format>NRPALETTES ||
		    palette2fmt[vm.format].mode == 0)
			return -EINVAL;

		DEBUG(printk(KERN_DEBUG "%s: Mcapture(%d,(%d,%d),%d=%s)\n",CARD,vm.frame,vm.width,vm.height,vm.format,palette2fmt[vm.format].name));
		frame = &ztv->grabinfo[vm.frame];
		if (frame->status == FBUFFER_GRABBING)
			return -EBUSY;

		/* setup the other parameters if they are given */
		write_lock_irq(&ztv->lock);
		if (vm.width)
			frame->w = vm.width;
		if (vm.height)
			frame->h = vm.height;
		if (vm.format)
			frame->format = vm.format;
		frame->bpp = palette2fmt[frame->format].bpp;
		frame->bpl = frame->w*frame->bpp;
		frame->vidadr = virt_to_bus(phys_to_virt((ulong)ztv->fbuffer+vm.frame*ZORAN_MAX_FBUFFER));
		frame->status = FBUFFER_GRABBING;
		set_bit(STATE_GRAB, &ztv->state);
		write_unlock_irq(&ztv->lock);

		zoran_cap(ztv, 1);
		return 0;
	 }

	 case VIDIOCGMBUF:
	 {
		struct video_mbuf mb;
		int i;
		DEBUG(printk(KERN_DEBUG "%s: GetMemoryBuffer\n",CARD));
		mb.size = ZORAN_MAX_FBUFSIZE;
		mb.frames = ZORAN_MAX_FBUFFERS;
		for (i=0; i<ZORAN_MAX_FBUFFERS; i++)
			mb.offsets[i] = i*ZORAN_MAX_FBUFFER;
		if(copy_to_user(arg, &mb,sizeof(mb)))
			return -EFAULT;
		return 0;
	 }

	 case VIDIOCGUNIT:
	 {
		struct video_unit vu;
		DEBUG(printk(KERN_DEBUG "%s: GetUnit\n",CARD));
		vu.video = ztv->video_dev.minor;
		vu.vbi = VIDEO_NO_UNIT;
		vu.radio = VIDEO_NO_UNIT;
		vu.audio = VIDEO_NO_UNIT;
		vu.teletext = VIDEO_NO_UNIT;
		if(copy_to_user(arg, &vu,sizeof(vu)))
			return -EFAULT;
		return 0;
	 }

	 case VIDIOCGFREQ:
	 {
		unsigned long v = ztv->tuner_freq;
		if (copy_to_user(arg,&v,sizeof(v)))
			return -EFAULT;
		return 0;
	 }

	 case VIDIOCSFREQ:
	 {
		unsigned long v;
		if (copy_from_user(&v, arg, sizeof(v)))
			return -EFAULT;

		if (ztv->have_tuner) {
			int fixme = v;
			if (i2c_control_device(&(ztv->i2c), I2C_DRIVERID_TUNER, TUNER_SET_TVFREQ, &fixme) < 0)
				return -EAGAIN;
		}
		ztv->tuner_freq = v;
		return 0;
	 }

	 case VIDIOCGAUDIO:
	 case VIDIOCSAUDIO:
	 case VIDIOCGCAPTURE:
	 case VIDIOCSCAPTURE:
		DEBUG(printk(KERN_DEBUG "%s: unhandled video ioctl(%x)\n",CARD,cmd));
		return -EINVAL;

	 default:
		DEBUG(printk(KERN_DEBUG "%s: bad ioctl(%x)\n",CARD,cmd));
	}
	return -EPERM;
}

static
int zoran_mmap(struct video_device* dev, const char* adr, unsigned long size)
{
	struct zoran* ztv = (struct zoran*)dev;
	unsigned long start = (unsigned long)adr;
	unsigned long pos;

	DEBUG(printk(KERN_DEBUG "zoran_mmap(0x%p,%ld)\n",adr,size));

	/* sanity checks */
	if (size > ZORAN_MAX_FBUFSIZE || !ztv->fbuffer)
		return -EINVAL;

	/* start mapping the whole shabang to user memory */
	pos = (unsigned long)ztv->fbuffer;
	while (size>0) {
#ifdef CONFIG_BIGPHYS_AREA
		unsigned long page = virt_to_phys((void*)pos);
#else
		unsigned long page = kvirt_to_phys(pos);
#endif
		if (remap_page_range(start, page, PAGE_SIZE, PAGE_SHARED))
			return -EAGAIN;
		start += PAGE_SIZE;
		pos += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
	return 0;
}

static struct video_device zoran_template=
{
	"UNSET",
	VID_TYPE_TUNER|VID_TYPE_CAPTURE|VID_TYPE_OVERLAY,
	VID_HARDWARE_ZR36120,

	zoran_open,
	zoran_close,
	zoran_read,
	zoran_write,
#if LINUX_VERSION_CODE >= 0x020100
	zoran_poll,		/* poll */
#endif
	zoran_ioctl,
	zoran_mmap,
	NULL,			/* initialize */
	NULL,
	0,
	-1
};

static
int init_zoran(int card)
{
	struct zoran *ztv = &zorans[card];
	int	i;

	/* if the given cardtype valid? */
	if (cardtype[card]<0 || cardtype[card]>=NRTVCARDS) {
		printk(KERN_INFO "invalid cardtype(%d) detected\n",cardtype[card]);
		return -1;
	}

	/* reset the zoran */
	zrand(~ZORAN_PCI_SOFTRESET,ZORAN_PCI);
	udelay(10);
	zror(ZORAN_PCI_SOFTRESET,ZORAN_PCI);
	udelay(10);

	/* default setup for max. PAL size in a 1024xXXX hicolor framebuffer */

	/* framegrabber details */
	ztv->swidth=800;
	ztv->sheight=600;
	ztv->depth=16;

	/* channel details */
	ztv->norm=0;				/* PAL */
	ztv->card=tvcards+cardtype[card];	/* point to the selected card */
	ztv->tuner_freq = 0;

	ztv->overinfo.status = FBUFFER_UNUSED;
	ztv->overinfo.x = 0;
	ztv->overinfo.y = 0;
	ztv->overinfo.w = 768; /* 640 */
	ztv->overinfo.h = 576; /* 480 */
	ztv->overinfo.format = VIDEO_PALETTE_RGB565;
	ztv->overinfo.bpp = palette2fmt[ztv->overinfo.format].bpp;
	ztv->overinfo.bpl = ztv->overinfo.bpp*ztv->swidth;
	ztv->overinfo.vidadr = 0;
	ztv->overinfo.overlay = 0;

	ztv->readinfo = ztv->overinfo;
	ztv->readinfo.w = 768;
	ztv->readinfo.h = -22;
	ztv->readinfo.format = VIDEO_PALETTE_YUV422;
	ztv->readinfo.bpp = palette2fmt[ztv->readinfo.format].bpp;
	ztv->readinfo.bpl = ztv->readinfo.w*ztv->readinfo.bpp;

	/* grabbing details */
	for (i=0; i<ZORAN_MAX_FBUFFERS; i++) {
		ztv->grabinfo[i] = ztv->overinfo;
		ztv->grabinfo[i].format = VIDEO_PALETTE_RGB24;
	}

	/* maintenance data */
	ztv->fbuffer = NULL;
	ztv->user = 0;
	ztv->have_decoder = 0;
	ztv->have_tuner = 0;
	ztv->running = 0;
	init_waitqueue_head(&ztv->grabq);
	init_waitqueue_head(&ztv->readq);
	ztv->lock = RW_LOCK_UNLOCKED;
	ztv->state = 0;
	ztv->prevstate = 0;
	ztv->lastframe = -1;

	/* picture details */
	ztv->picture.colour=254<<7;
	ztv->picture.brightness=128<<8;
	ztv->picture.hue=128<<8;
	ztv->picture.contrast=216<<7;

	if (triton1)
		zrand(~ZORAN_VDC_TRICOM, ZORAN_VDC);

	/* external FL determines TOP frame */
	zror(ZORAN_VFEC_EXTFL, ZORAN_VFEC); 

	/* set HSpol */
	if (ztv->card->hsync_pos)
		zrwrite(ZORAN_VFEH_HSPOL, ZORAN_VFEH);
	/* set VSpol */
	if (ztv->card->vsync_pos)
		zrwrite(ZORAN_VFEV_VSPOL, ZORAN_VFEV);

	/* Set the proper General Purpuse register bits */
	/* implicit: no softreset, 0 waitstates */
	zrwrite(ZORAN_PCI_SOFTRESET|(ztv->card->gpdir<<0),ZORAN_PCI);
	/* implicit: 3 duration and recovery PCI clocks on guest 0-3 */
	zrwrite(ztv->card->gpval<<24,ZORAN_GUEST);
	
	/* clear interrupt status */
	zrwrite(~0, ZORAN_ISR);

	/*
	 * i2c template
	 */
	ztv->i2c = zoran_i2c_bus_template;
	sprintf(ztv->i2c.name,"zoran-%d",card);
	ztv->i2c.data = ztv;

	/*
	 * Now add the template and register the device unit
	 */
	ztv->video_dev = zoran_template;
	strcpy(ztv->video_dev.name, ztv->i2c.name);
	if (video_register_device(&ztv->video_dev, VFL_TYPE_GRABBER) < 0)
		return -1;
	i2c_register_bus(&ztv->i2c);

	/* set interrupt mask - the PIN enable will be set later */
	zrwrite(ZORAN_ICR_GIRQ0|ZORAN_ICR_GIRQ1|ZORAN_ICR_CODE, ZORAN_ICR);

	printk(KERN_INFO "%s: installed %s\n",CARD,ztv->card->name);
	return 0;
}

static
void release_zoran(int max)
{
	u8 command;
	struct zoran *ztv;
	int i;

	for (i=0;i<max; i++) 
	{
		ztv=&zorans[i];

		/* turn off all capturing, DMA and IRQs */
		/* reset the zoran */
		zrand(~ZORAN_PCI_SOFTRESET,ZORAN_PCI);
		udelay(10);
		zror(ZORAN_PCI_SOFTRESET,ZORAN_PCI);
		udelay(10);

		/* first disable interrupts before unmapping the memory! */
		zrwrite(0, ZORAN_ICR);
		zrwrite(0xffffffffUL,ZORAN_ISR);

		/* free it */
		free_irq(ztv->dev->irq,ztv);
 
    		/* unregister i2c_bus */
		i2c_unregister_bus((&ztv->i2c));

		/* disable PCI bus-mastering */
		pci_read_config_byte(ztv->dev, PCI_COMMAND, &command);
 		command&=PCI_COMMAND_MASTER;
		pci_write_config_byte(ztv->dev, PCI_COMMAND, command);
    
		/* unmap and free memory */
		if (ztv->zoran_mem)
			iounmap(ztv->zoran_mem);

		video_unregister_device(&ztv->video_dev);
	}
}

#ifdef MODULE
void cleanup_module(void)
{
	release_zoran(zoran_cards);
}

int init_module(void)
{
#else
int init_zr36120_cards(struct video_init *unused)
{
#endif
	int	card;
 
	handle_chipset();
	zoran_cards = find_zoran();
	if (zoran_cards<0)
		/* no cards found, no need for a driver */
		return -EIO;

	/* initialize Zorans */
	for (card=0; card<zoran_cards; card++) {
		if (init_zoran(card)<0) {
			/* only release the zorans we have registered */
			release_zoran(card);
			return -EIO;
		} 
	}
	return 0;
}
