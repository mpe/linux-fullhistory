/* $Id: floppy.h,v 1.7 1997/09/07 03:34:08 davem Exp $
 * asm-sparc64/floppy.h: Sparc specific parts of the Floppy driver.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 * Copyright (C) 1997 Jakub Jelinek (jj@sunsite.mff.cuni.cz)
 *
 * Ultra/PCI support added: Sep 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#ifndef __ASM_SPARC64_FLOPPY_H
#define __ASM_SPARC64_FLOPPY_H

#include <linux/config.h>

#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/idprom.h>
#include <asm/oplib.h>
#include <asm/auxio.h>
#include <asm/sbus.h>
#include <asm/irq.h>

/* References:
 * 1) Netbsd Sun floppy driver.
 * 2) NCR 82077 controller manual
 * 3) Intel 82077 controller manual
 */
struct sun_flpy_controller {
	volatile unsigned char status1_82077; /* Auxiliary Status reg. 1 */
	volatile unsigned char status2_82077; /* Auxiliary Status reg. 2 */
	volatile unsigned char dor_82077;     /* Digital Output reg. */
	volatile unsigned char tapectl_82077; /* What the? Tape control reg? */
	volatile unsigned char status_82077;  /* Main Status Register. */
#define drs_82077              status_82077   /* Digital Rate Select reg. */
	volatile unsigned char data_82077;    /* Data fifo. */
	volatile unsigned char ___unused;
	volatile unsigned char dir_82077;     /* Digital Input reg. */
#define dcr_82077              dir_82077      /* Config Control reg. */
};

/* You'll only ever find one controller on a SparcStation anyways. */
static struct sun_flpy_controller *sun_fdc = NULL;
volatile unsigned char *fdc_status;
static struct linux_sbus_device *floppy_sdev = NULL;

struct sun_floppy_ops {
	unsigned char	(*fd_inb) (unsigned long port);
	void		(*fd_outb) (unsigned char value, unsigned long port);
	void		(*fd_enable_dma) (void);
	void		(*fd_disable_dma) (void);
	void		(*fd_set_dma_mode) (int);
	void		(*fd_set_dma_addr) (char *);
	void		(*fd_set_dma_count) (int);
	unsigned int	(*get_dma_residue) (void);
	void		(*fd_enable_irq) (void);
	void		(*fd_disable_irq) (void);
	int		(*fd_request_irq) (void);
	void		(*fd_free_irq) (void);
	int		(*fd_eject) (int);
};

static struct sun_floppy_ops sun_fdops;

#define fd_inb(port)              sun_fdops.fd_inb(port)
#define fd_outb(value,port)       sun_fdops.fd_outb(value,port)
#define fd_enable_dma()           sun_fdops.fd_enable_dma()
#define fd_disable_dma()          sun_fdops.fd_disable_dma()
#define fd_request_dma()          (0) /* nothing... */
#define fd_free_dma()             /* nothing... */
#define fd_clear_dma_ff()         /* nothing... */
#define fd_set_dma_mode(mode)     sun_fdops.fd_set_dma_mode(mode)
#define fd_set_dma_addr(addr)     sun_fdops.fd_set_dma_addr(addr)
#define fd_set_dma_count(count)   sun_fdops.fd_set_dma_count(count)
#define get_dma_residue(x)        sun_fdops.get_dma_residue()
#define fd_enable_irq()           sun_fdops.fd_enable_irq()
#define fd_disable_irq()          sun_fdops.fd_disable_irq()
#define fd_cacheflush(addr, size) /* nothing... */
#define fd_request_irq()          sun_fdops.fd_request_irq()
#define fd_free_irq()             sun_fdops.fd_free_irq()
#define fd_eject(drive)           sun_fdops.fd_eject(drive)

static int FLOPPY_MOTOR_MASK = 0x10;

#define FLOPPY0_TYPE  4
#define FLOPPY1_TYPE  0

/* Super paranoid... */
#undef HAVE_DISABLE_HLT

/* Here is where we catch the floppy driver trying to initialize,
 * therefore this is where we call the PROM device tree probing
 * routine etc. on the Sparc.
 */
#define FDC1                      sun_floppy_init()

static int FDC2 = -1;

#define N_FDC    1
#define N_DRIVE  8

/* No 64k boundary crossing problems on the Sparc. */
#define CROSS_64KB(a,s) (0)

static unsigned char sun_82077_fd_inb(unsigned long port)
{
	switch(port & 7) {
	default:
		printk("floppy: Asked to read unknown port %lx\n", port);
		panic("floppy: Port bolixed.");
	case 4: /* FD_STATUS */
		return sun_fdc->status_82077 & ~STATUS_DMA;
	case 5: /* FD_DATA */
		return sun_fdc->data_82077;
	case 7: /* FD_DIR */
		/* XXX: Is DCL on 0x80 in sun4m? */
		return sun_fdc->dir_82077;
	};
	panic("sun_82072_fd_inb: How did I get here?");
}

static void sun_82077_fd_outb(unsigned char value, unsigned long port)
{
	switch(port & 7) {
	default:
		printk("floppy: Asked to write to unknown port %lx\n", port);
		panic("floppy: Port bolixed.");
	case 2: /* FD_DOR */
		/* Happily, the 82077 has a real DOR register. */
		sun_fdc->dor_82077 = value;
		break;
	case 5: /* FD_DATA */
		sun_fdc->data_82077 = value;
		break;
	case 7: /* FD_DCR */
		sun_fdc->dcr_82077 = value;
		break;
	case 4: /* FD_STATUS */
		sun_fdc->status_82077 = value;
		break;
	};
	return;
}

/* For pseudo-dma (Sun floppy drives have no real DMA available to
 * them so we must eat the data fifo bytes directly ourselves) we have
 * three state variables.  doing_pdma tells our inline low-level
 * assembly floppy interrupt entry point whether it should sit and eat
 * bytes from the fifo or just transfer control up to the higher level
 * floppy interrupt c-code.  I tried very hard but I could not get the
 * pseudo-dma to work in c-code without getting many overruns and
 * underruns.  If non-zero, doing_pdma encodes the direction of
 * the transfer for debugging.  1=read 2=write
 */
char *pdma_vaddr;
unsigned long pdma_size;
volatile int doing_pdma = 0;

/* This is software state */
char *pdma_base = 0;
unsigned long pdma_areasize;

/* Common routines to all controller types on the Sparc. */
static __inline__ void virtual_dma_init(void)
{
	/* nothing... */
}

static void sun_fd_disable_dma(void)
{
	doing_pdma = 0;
	if (pdma_base) {
		mmu_unlockarea(pdma_base, pdma_areasize);
		pdma_base = 0;
	}
}

static void sun_fd_set_dma_mode(int mode)
{
	switch(mode) {
	case DMA_MODE_READ:
		doing_pdma = 1;
		break;
	case DMA_MODE_WRITE:
		doing_pdma = 2;
		break;
	default:
		printk("Unknown dma mode %d\n", mode);
		panic("floppy: Giving up...");
	}
}

static void sun_fd_set_dma_addr(char *buffer)
{
	pdma_vaddr = buffer;
}

static void sun_fd_set_dma_count(int length)
{
	pdma_size = length;
}

static void sun_fd_enable_dma(void)
{
	pdma_vaddr = mmu_lockarea(pdma_vaddr, pdma_size);
	pdma_base = pdma_vaddr;
	pdma_areasize = pdma_size;
}

/* Our low-level entry point in arch/sparc/kernel/entry.S */
extern void floppy_hardint(int irq, void *unused, struct pt_regs *regs);

static int sun_fd_request_irq(void)
{
	static int once = 0;
	int error;

	if(!once) {
		struct devid_cookie dcookie;

		once = 1;

		dcookie.real_dev_id = NULL;
		dcookie.imap = dcookie.iclr = 0;
		dcookie.pil = -1;
		dcookie.bus_cookie = floppy_sdev->my_bus;

		error = request_fast_irq(FLOPPY_IRQ, floppy_hardint,
					 (SA_INTERRUPT | SA_SBUS | SA_DCOOKIE),
					 "floppy", &dcookie);

		if(error == 0)
			FLOPPY_IRQ = dcookie.ret_ino;

		return ((error == 0) ? 0 : -1);
	}
	return 0;
}

static void sun_fd_enable_irq(void)
{
}

static void sun_fd_disable_irq(void)
{
}

static void sun_fd_free_irq(void)
{
}

static unsigned int sun_get_dma_residue(void)
{
	/* XXX This isn't really correct. XXX */
	return 0;
}

static int sun_fd_eject(int drive)
{
	set_dor(0x00, 0xff, 0x90);
	udelay(500);
	set_dor(0x00, 0x6f, 0x00);
	udelay(500);
	return 0;
}

#ifdef CONFIG_PCI
#include <asm/ebus.h>

static struct linux_ebus_dma *sun_fd_ebus_dma;

extern void floppy_interrupt(int irq, void *dev_id, struct pt_regs *regs);

static unsigned char sun_pci_fd_inb(unsigned long port)
{
	return inb(port);
}

static void sun_pci_fd_outb(unsigned char val, unsigned long port)
{
	outb(val, port);
}

static void sun_pci_fd_enable_dma(void)
{
	unsigned int dcsr;

	dcsr = readl((unsigned long)&sun_fd_ebus_dma->dcsr);
	dcsr |= (EBUS_DCSR_EN_DMA | EBUS_DCSR_EN_CNT);
	writel(dcsr, (unsigned long)&sun_fd_ebus_dma->dcsr);
}

static void sun_pci_fd_disable_dma(void)
{
	unsigned int dcsr;

	dcsr = readl((unsigned long)&sun_fd_ebus_dma->dcsr);
	dcsr &= ~(EBUS_DCSR_EN_DMA | EBUS_DCSR_EN_CNT);
	writel(dcsr, (unsigned long)&sun_fd_ebus_dma->dcsr);
}

static void sun_pci_fd_set_dma_mode(int mode)
{
	unsigned int dcsr;

	dcsr = readl((unsigned long)&sun_fd_ebus_dma->dcsr);
	/*
	 * For EBus WRITE means to system memory, which is
	 * READ for us.
	 */
	if (mode == DMA_MODE_WRITE)
		dcsr &= ~(EBUS_DCSR_WRITE);
	else
		dcsr |= EBUS_DCSR_WRITE;
	writel(dcsr, (unsigned long)&sun_fd_ebus_dma->dcsr);
}

static void sun_pci_fd_set_dma_count(int length)
{
	writel(length, (unsigned long)&sun_fd_ebus_dma->dbcr);
}

static void sun_pci_fd_set_dma_addr(char *buffer)
{
	unsigned int addr;

	addr = virt_to_bus(buffer);
	writel(addr, (unsigned long)&sun_fd_ebus_dma->dacr);
}

static void sun_pci_fd_enable_irq(void)
{
	unsigned int dcsr;

	dcsr = readl((unsigned long)&sun_fd_ebus_dma->dcsr);
	dcsr |= EBUS_DCSR_INT_EN;
	writel(dcsr, (unsigned long)&sun_fd_ebus_dma->dcsr);
}

static void sun_pci_fd_disable_irq(void)
{
	unsigned int dcsr;

	dcsr = readl((unsigned long)&sun_fd_ebus_dma->dcsr);
	dcsr &= ~(EBUS_DCSR_INT_EN);
	writel(dcsr, (unsigned long)&sun_fd_ebus_dma->dcsr);
}

static int sun_pci_fd_request_irq(void)
{
	int error;

	error = request_irq(FLOPPY_IRQ, floppy_interrupt, SA_SHIRQ, "floppy", sun_fdc);
	return ((error == 0) ? 0 : -1);
}

static void sun_pci_fd_free_irq(void)
{
	free_irq(FLOPPY_IRQ, sun_fdc);
}

static unsigned int sun_pci_get_dma_residue(void)
{
	unsigned int res;

	res = readl((unsigned long)&sun_fd_ebus_dma->dbcr);
	return res;
}

static int sun_pci_fd_eject(int drive)
{
	return -EINVAL;
}
#endif

static struct linux_prom_registers fd_regs[2];

static unsigned long sun_floppy_init(void)
{
	char state[128];
	int fd_node, num_regs;
	struct linux_sbus *bus;
	struct linux_sbus_device *sdev = NULL;

	for_all_sbusdev (sdev, bus) {
		if (!strcmp(sdev->prom_name, "SUNW,fdtwo")) 
			break;
	}
	if(sdev) {
		floppy_sdev = sdev;
		FLOPPY_IRQ = sdev->irqs[0].pri;
	} else {
#ifdef CONFIG_PCI
		struct linux_ebus *ebus;
		struct linux_ebus_device *edev;

		for_all_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_name, "fdthree"))
				break;
		}
		if (!edev)
			return -1;

		if (check_region(edev->base_address[1], sizeof(struct linux_ebus_dma))) {
			printk("sun_floppy_init: can't get region %016lx (%d)\n",
			       edev->base_address[1], (int)sizeof(struct linux_ebus_dma));
			return -1;
		}
		request_region(edev->base_address[1], sizeof(struct linux_ebus_dma), "floppy DMA");

		sun_fdc = (struct sun_flpy_controller *)edev->base_address[0];
		FLOPPY_IRQ = edev->irqs[0];

		sun_fd_ebus_dma = (struct linux_ebus_dma *)edev->base_address[1];
		writel(EBUS_DCSR_BURST_SZ_16, (unsigned long)&sun_fd_ebus_dma->dcsr);

		sun_fdops.fd_inb = sun_pci_fd_inb;
		sun_fdops.fd_outb = sun_pci_fd_outb;

		use_virtual_dma = 0;
		sun_fdops.fd_enable_dma = sun_pci_fd_enable_dma;
		sun_fdops.fd_disable_dma = sun_pci_fd_disable_dma;
		sun_fdops.fd_set_dma_mode = sun_pci_fd_set_dma_mode;
		sun_fdops.fd_set_dma_addr = sun_pci_fd_set_dma_addr;
		sun_fdops.fd_set_dma_count = sun_pci_fd_set_dma_count;
		sun_fdops.get_dma_residue = sun_pci_get_dma_residue;

		sun_fdops.fd_enable_irq = sun_pci_fd_enable_irq;
		sun_fdops.fd_disable_irq = sun_pci_fd_disable_irq;
		sun_fdops.fd_request_irq = sun_pci_fd_request_irq;
		sun_fdops.fd_free_irq = sun_pci_fd_free_irq;

		sun_fdops.fd_eject = sun_pci_fd_eject;

        	fdc_status = &sun_fdc->status_82077;
		FLOPPY_MOTOR_MASK = 0xf0;

		return (unsigned long)sun_fdc;
#else
		return -1;
#endif
	}
	fd_node = sdev->prom_node;
	prom_getproperty(fd_node, "status", state, sizeof(state));
	if(!strncmp(state, "disabled", 8))
		return -1;
	num_regs = prom_getproperty(fd_node, "reg", (char *) fd_regs, sizeof(fd_regs));
	num_regs = (num_regs / sizeof(fd_regs[0]));
	prom_apply_sbus_ranges(sdev->my_bus, fd_regs, num_regs, sdev);
	sun_fdc = (struct sun_flpy_controller *) sparc_alloc_io(fd_regs[0].phys_addr,
								0x0,
								fd_regs[0].reg_size,
								"floppy",
								fd_regs[0].which_io,
								0x0);
	/* Last minute sanity check... */
	if(sun_fdc->status1_82077 == 0xff) {
		sun_fdc = NULL;
		return -1;
	}

        sun_fdops.fd_inb = sun_82077_fd_inb;
        sun_fdops.fd_outb = sun_82077_fd_outb;

	use_virtual_dma = 1;
	sun_fdops.fd_enable_dma = sun_fd_enable_dma;
	sun_fdops.fd_disable_dma = sun_fd_disable_dma;
	sun_fdops.fd_set_dma_mode = sun_fd_set_dma_mode;
	sun_fdops.fd_set_dma_addr = sun_fd_set_dma_addr;
	sun_fdops.fd_set_dma_count = sun_fd_set_dma_count;
	sun_fdops.get_dma_residue = sun_get_dma_residue;

	sun_fdops.fd_enable_irq = sun_fd_enable_irq;
	sun_fdops.fd_disable_irq = sun_fd_disable_irq;
	sun_fdops.fd_request_irq = sun_fd_request_irq;
	sun_fdops.fd_free_irq = sun_fd_free_irq;

	sun_fdops.fd_eject = sun_fd_eject;

        fdc_status = &sun_fdc->status_82077;
        /* printk("DOR @0x%p\n", &sun_fdc->dor_82077); */ /* P3 */

	/* Success... */
	return (unsigned long)sun_fdc;
}

#endif /* !(__ASM_SPARC64_FLOPPY_H) */
