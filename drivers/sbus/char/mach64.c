/* $Id: mach64.c,v 1.11 1997/10/17 04:13:35 davem Exp $
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
#include <asm/pgtable.h>

#include "pcicons.h"
#include "mach64.h"
#include "fb.h"

static unsigned int mach64_pci_membase;
static unsigned int mach64_pci_iobase;

#define MACH64_LE_FBOFF	0x000000
#define MACH64_REGOFF	0x7ffc00
#define MACH64_BE_FBOFF	0x800000

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
	unsigned long addr, size;

	size = vma->vm_end - vma->vm_start;
	if (vma->vm_offset & ~PAGE_MASK)
		return -ENXIO;

	if (vma->vm_offset == mach64_pci_iobase) {
		addr = __pa(pcivga_iobase);
		size = PAGE_SIZE;
	} else if (vma->vm_offset >= (mach64_pci_membase + 0x800000)) {
		addr = __pa(pcivga_membase) - mach64_pci_membase
			+ vma->vm_offset;
		pgprot_val(vma->vm_page_prot) |= _PAGE_IE;
	} else if (vma->vm_offset >= mach64_pci_membase) {
		addr = __pa(pcivga_membase) - mach64_pci_membase
			+ vma->vm_offset;
	} else {
		return -EINVAL;
	}

	pgprot_val(vma->vm_page_prot) &= ~(_PAGE_CACHE);
	pgprot_val(vma->vm_page_prot) |= _PAGE_E;
	vma->vm_flags |= (VM_SHM | VM_LOCKED);

	if (remap_page_range(vma->vm_start, addr, size, vma->vm_page_prot))
		return -EAGAIN;

	vma->vm_dentry = dget(file->f_dentry);
	return 0;
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

void mach64_test(fbinfo_t *fb)
{
	unsigned int x;
	int i;

	for (i = 0; i < mach64.total_vram; i += 4)
		writel(i, pcivga_membase + i);

	for (i = 0; i < mach64.total_vram; i += 4)
		if ((x = readl(pcivga_membase + i)) != i) {
			printk("vga mem read error @ %08x: exp %x, rd %x\n",
			       i, i, x);
			i = (i & ~(0xffff)) + 0x10000;
		}
}

int mach64_init(fbinfo_t *fb)
{
	struct pci_dev *pdev;
	struct pcidev_cookie *cookie;
	struct linux_pbm_info *pbm;
	unsigned long addr;
	unsigned int tmp;

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

	pcibios_read_config_dword(pdev->bus->number, pdev->devfn,
				  PCI_BASE_ADDRESS_0, &mach64_pci_membase);
	mach64_pci_membase &= PCI_BASE_ADDRESS_MEM_MASK;

	pcibios_read_config_dword(pdev->bus->number, pdev->devfn,
				  PCI_BASE_ADDRESS_1, &mach64_pci_iobase);
	mach64_pci_iobase &= PCI_BASE_ADDRESS_IO_MASK;

	printk("mach64_init: IOBASE[%016lx] MEMBASE[%016lx]\n",
		pcivga_iobase, pcivga_membase);

	cookie = pdev->sysdata;
	pbm = cookie->pbm;

	fb->prom_node = cookie->prom_node;
	fb->proc_entry.node = pbm->prom_node;

	fb->type.fb_type = FBTYPE_PCI_MACH64;
	fb->type.fb_cmsize = 256;
	fb->info.private = (void *)&mach64;
	fb->base = pcivga_membase + MACH64_BE_FBOFF;

	if ((pcivga_readl(MACH64_REGOFF + CONFIG_CHIP_ID)
					& CFG_CHIP_TYPE) == MACH64_VT_ID)
		mach64.flags |= MACH64_MASK_VT;

	/*
	 * Fix the PROM's idea of MEM_CNTL settings...
	 */
	tmp = pcivga_readl(MACH64_REGOFF + MEM_CNTL);
	switch (tmp & 0xf) {
		case 3:
			tmp = (tmp & ~(0xf)) | 2;
			break;
		case 7:
			tmp = (tmp & ~(0xf)) | 3;
			break;
		case 9:
			tmp = (tmp & ~(0xf)) | 4;
			break;
		case 11:
			tmp = (tmp & ~(0xf)) | 5;
			break;
		default:
			break;
	}
	tmp &= ~(0x00f00000);
	pcivga_writel(tmp, MACH64_REGOFF + MEM_CNTL);

	switch(tmp & MEM_SIZE_ALIAS) {
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

	printk("mach64_init: total_vram[%08x] is_vt_chip[%d]\n",
		mach64.total_vram, mach64.flags & MACH64_MASK_VT ? 1 : 0);

#if 0
	mach64_test(fb);
#endif

	fb->mmap = mach64_mmap;
	fb->loadcmap = mach64_loadcmap;
	fb->ioctl = 0;
	fb->reset = 0;
	fb->blank = mach64_blank;
	fb->unblank = mach64_unblank;
	fb->setcursor = 0;

	return 0;
}
