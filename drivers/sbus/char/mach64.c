/* $Id: mach64.c,v 1.8 1997/08/25 07:50:34 jj Exp $
 * mach64.c: Ultra/PCI Mach64 console driver.
 *
 * Just about all of this is from the PPC/mac driver, see that for
 * author info.  I'm only responsible for grafting it into working
 * on PCI Ultra's.  The two drivers should be merged.
 *
 * Copyright (C) 1997 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/bios32.h>
#include <linux/string.h>
#include <linux/tty.h>
#include <linux/console.h>
#include <linux/selection.h>
#include <linux/proc_fs.h>

#include <asm/oplib.h>
#include <asm/pbm.h>
#include <asm/fbio.h>
#include <asm/sbus.h>

#include "pcicons.h"
#include "mach64.h"
#include "fb.h"

#define MACH64_REGOFF	0x7ffc00
#define MACH64_FBOFF	0x800000

static inline void mach64_waitq(int entries)
{
	unsigned short base = (0x8000 >> entries);

	while((pcivga_readl(MACH64_REGOFF + FIFO_STAT) & 0xffff) > base)
		barrier();
}

static inline void mach64_idle(void)
{
	mach64_waitq(16);
	while(pcivga_readl(MACH64_REGOFF + GUI_STAT) & 1)
		barrier();
}

#if 0	/* not used yet */
static void mach64_st_514(int offset, char val)
{
	mach64_waitq(5);
	pcivga_writeb(1,			MACH64_REGOFF + DAC_CNTL);
	pcivga_writeb((offset & 0xff),		MACH64_REGOFF + DAC_W_INDEX);
	pcivga_writeb(((offset>>8)&0xff),	MACH64_REGOFF + DAC_DATA);
	pcivga_writeb(val,			MACH64_REGOFF + DAC_MASK);
	pcivga_writeb(0,			MACH64_REGOFF + DAC_CNTL);
}

static void mach64_st_pll(int offset, char val)
{
	mach64_waitq(3);
	pcivga_writeb(((offset<<2)|PLL_WR_EN),	MACH64_REGOFF + CLOCK_CNTL + 1);
	pcivga_writeb(val,			MACH64_REGOFF + CLOCK_CNTL + 2);
	pcivga_writeb(((offset<<2)&~PLL_WR_EN),	MACH64_REGOFF + CLOCK_CNTL + 1);
}
#endif

static int
mach64_mmap(struct inode *inode, struct file *file, struct vm_area_struct *vma,
	    long base, fbinfo_t *fb)
{
	return -ENOSYS;
}

static void
mach64_loadcmap(fbinfo_t *fb, int index, int count)
{
	unsigned char tmp;
	int i;

	mach64_waitq(2);
	tmp = pcivga_readb(MACH64_REGOFF + DAC_CNTL);
	pcivga_writeb(tmp & 0xfc, MACH64_REGOFF + DAC_CNTL);
	pcivga_writeb(0xff, MACH64_REGOFF + DAC_MASK);
	for(i = index; count--; i++) {
		mach64_waitq(4);
		pcivga_writeb(i, MACH64_REGOFF + DAC_W_INDEX);
		pcivga_writeb(fb->color_map CM(i, 0), MACH64_REGOFF + DAC_DATA);
		pcivga_writeb(fb->color_map CM(i, 1), MACH64_REGOFF + DAC_DATA);
		pcivga_writeb(fb->color_map CM(i, 2), MACH64_REGOFF + DAC_DATA);
	}
}

static void
mach64_blank(fbinfo_t *fb)
{
	unsigned char gen_cntl;

	gen_cntl = pcivga_readb(MACH64_REGOFF + CRTC_GEN_CNTL);
	gen_cntl |= 0x40;
	pcivga_writeb(gen_cntl, MACH64_REGOFF + CRTC_GEN_CNTL);
}

static void
mach64_unblank(fbinfo_t *fb)
{
	unsigned char gen_cntl;

	gen_cntl = pcivga_readb(MACH64_REGOFF + CRTC_GEN_CNTL);
	gen_cntl &= ~(0x4c);
	pcivga_writeb(gen_cntl, MACH64_REGOFF + CRTC_GEN_CNTL);
}

static struct mach64_info mach64;

int mach64_init(fbinfo_t *fb)
{
	struct pci_dev *pdev;
	struct pcidev_cookie *cookie;
	unsigned long addr;

	memset(&mach64, 0, sizeof(mach64));
	for(pdev = pci_devices; pdev; pdev = pdev->next) {
		if((pdev->vendor == PCI_VENDOR_ID_ATI) &&
		   (pdev->device == PCI_DEVICE_ID_ATI_264VT))
			break;
	}
	if(!pdev)
		return -1;

	addr = pdev->base_address[0];
	pcivga_iobase = pcivga_membase = 0;
	if((addr & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO)
		pcivga_iobase = addr & PCI_BASE_ADDRESS_IO_MASK;
	else
		pcivga_membase = addr & PCI_BASE_ADDRESS_MEM_MASK;

	addr = pdev->base_address[1];
	if((addr & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO)
		pcivga_iobase = addr & PCI_BASE_ADDRESS_IO_MASK;
	else
		pcivga_membase = addr & PCI_BASE_ADDRESS_MEM_MASK;

	if(!pcivga_iobase || !pcivga_membase) {
		prom_printf("mach64_init: I/O or MEM baseaddr is missing\n");
		prom_printf("mach64_init: ba[0]=%016lx ba[1]=%016lx\n",
			    pdev->base_address[0], pdev->base_address[1]);
		prom_halt();
	}

	printk("mach64_init: IOBASE[%016lx] MEMBASE[%016lx]\n",
		pcivga_iobase, pcivga_membase);

	cookie = (struct pcidev_cookie *)pdev->sysdata;
	fb->prom_node = cookie->prom_node;
	fb->proc_entry.node = cookie->pbm->prom_node;

	fb->type.fb_type = FBTYPE_PCI_MACH64;
	fb->type.fb_cmsize = 256;
	fb->info.private = (void *)&mach64;
	fb->base = pcivga_membase + MACH64_FBOFF;

	switch(pcivga_readl(MACH64_REGOFF + MEM_CNTL) & MEM_SIZE_ALIAS) {
	case MEM_SIZE_512K:
		mach64.total_vram = 0x80000;
		break;
	case MEM_SIZE_1M:
		mach64.total_vram = 0x100000;
		break;
	case MEM_SIZE_2M:
		mach64.total_vram = 0x200000;
		break;
	case MEM_SIZE_4M:
		mach64.total_vram = 0x400000;
		break;
	case MEM_SIZE_6M:
		mach64.total_vram = 0x600000;
		break;
	case MEM_SIZE_8M:
		mach64.total_vram = 0x800000;
		break;
	default:
		mach64.total_vram = 0x80000;
		break;
	}

	if ((pcivga_readl(MACH64_REGOFF + CONFIG_CHIP_ID)
					& CFG_CHIP_TYPE) == MACH64_VT_ID)
		mach64.flags |= MACH64_MASK_VT;

	printk("mach64_init: total_vram[%08x] is_vt_chip[%d]\n",
		mach64.total_vram, mach64.flags & MACH64_MASK_VT ? 1 : 0);

	fb->mmap = mach64_mmap;
	fb->loadcmap = mach64_loadcmap;
	fb->ioctl = 0;
	fb->reset = 0;
	fb->blank = mach64_blank;
	fb->unblank = mach64_unblank;
	fb->setcursor = 0;

	return 0;
}
