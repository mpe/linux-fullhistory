/*
 *	linux/arch/alpha/kernel/core_mcpcia.c
 *
 * Based on code written by David A Rusling (david.rusling@reo.mts.dec.com).
 *
 * Code common to all MCbus-PCI Adaptor core logic chipsets
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/pci.h>
#include <asm/hwrpb.h>

#define __EXTERN_INLINE inline
#include <asm/io.h>
#include <asm/core_mcpcia.h>
#undef __EXTERN_INLINE

#include "proto.h"
#include "pci_impl.h"

/*
 * NOTE: Herein lie back-to-back mb instructions.  They are magic. 
 * One plausible explanation is that the i/o controller does not properly
 * handle the system transaction.  Another involves timing.  Ho hum.
 */

/*
 * BIOS32-style PCI interface:
 */

#define DEBUG_CFG 0

#if DEBUG_CFG
# define DBG_CFG(args)	printk args
#else
# define DBG_CFG(args)
#endif

#define MCPCIA_MAX_HOSES 2

/* Dodge has PCI0 and PCI1 at MID 4 and 5 respectively.  Durango adds
   PCI2 and PCI3 at MID 6 and 7 respectively.  */

#define hose2mid(h)	((h) + 4)


/*
 * Given a bus, device, and function number, compute resulting
 * configuration space address and setup the MCPCIA_HAXR2 register
 * accordingly.  It is therefore not safe to have concurrent
 * invocations to configuration space access routines, but there
 * really shouldn't be any need for this.
 *
 * Type 0:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | |D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|0|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:11	Device select bit.
 * 	10:8	Function number
 * 	 7:2	Register number
 *
 * Type 1:
 *
 *  3 3|3 3 2 2|2 2 2 2|2 2 2 2|1 1 1 1|1 1 1 1|1 1 
 *  3 2|1 0 9 8|7 6 5 4|3 2 1 0|9 8 7 6|5 4 3 2|1 0 9 8|7 6 5 4|3 2 1 0
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 * | | | | | | | | | | |B|B|B|B|B|B|B|B|D|D|D|D|D|F|F|F|R|R|R|R|R|R|0|1|
 * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
 *
 *	31:24	reserved
 *	23:16	bus number (8 bits = 128 possible buses)
 *	15:11	Device number (5 bits)
 *	10:8	function number
 *	 7:2	register number
 *  
 * Notes:
 *	The function number selects which function of a multi-function device 
 *	(e.g., SCSI and Ethernet).
 * 
 *	The register selects a DWORD (32 bit) register offset.  Hence it
 *	doesn't get shifted by 2 bits as we want to "drop" the bottom two
 *	bits.
 */

static unsigned int
conf_read(unsigned long addr, unsigned char type1,
	  struct pci_controler *hose)
{
	unsigned long flags;
	unsigned long mid = hose2mid(hose->index);
	unsigned int stat0, value, temp, cpu;

	cpu = smp_processor_id();

	__save_and_cli(flags);

	DBG_CFG(("conf_read(addr=0x%lx, type1=%d, hose=%d)\n",
		 addr, type1, mid));

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)MCPCIA_CAP_ERR(mid);
	*(vuip)MCPCIA_CAP_ERR(mid) = stat0;
	mb();
	temp = *(vuip)MCPCIA_CAP_ERR(mid);
	DBG_CFG(("conf_read: MCPCIA_CAP_ERR(%d) was 0x%x\n", mid, stat0));

	mb();
	draina();
	mcheck_expected(cpu) = 1;
	mcheck_taken(cpu) = 0;
	mcheck_extra(cpu) = mid;
	mb();

	/* Access configuration space.  */
	value = *((vuip)addr);
	mb();
	mb();  /* magic */

	if (mcheck_taken(cpu)) {
		mcheck_taken(cpu) = 0;
		value = 0xffffffffU;
		mb();
	}
	mcheck_expected(cpu) = 0;
	mb();

	DBG_CFG(("conf_read(): finished\n"));

	__restore_flags(flags);
	return value;
}

static void
conf_write(unsigned long addr, unsigned int value, unsigned char type1,
	   struct pci_controler *hose)
{
	unsigned long flags;
	unsigned long mid = hose2mid(hose->index);
	unsigned int stat0, temp, cpu;

	cpu = smp_processor_id();

	__save_and_cli(flags);	/* avoid getting hit by machine check */

	/* Reset status register to avoid losing errors.  */
	stat0 = *(vuip)MCPCIA_CAP_ERR(mid);
	*(vuip)MCPCIA_CAP_ERR(mid) = stat0; mb();
	temp = *(vuip)MCPCIA_CAP_ERR(mid);
	DBG_CFG(("conf_write: MCPCIA CAP_ERR(%d) was 0x%x\n", mid, stat0));

	draina();
	mcheck_expected(cpu) = 1;
	mcheck_extra(cpu) = mid;
	mb();

	/* Access configuration space.  */
	*((vuip)addr) = value;
	mb();
	mb();  /* magic */
	temp = *(vuip)MCPCIA_CAP_ERR(mid); /* read to force the write */
	mcheck_expected(cpu) = 0;
	mb();

	DBG_CFG(("conf_write(): finished\n"));
	__restore_flags(flags);
}

static int
mk_conf_addr(struct pci_dev *dev, int where, struct pci_controler *hose,
	     unsigned long *pci_addr, unsigned char *type1)
{
	u8 bus = dev->bus->number;
	u8 devfn = dev->devfn;
	unsigned long addr;

	DBG_CFG(("mk_conf_addr(bus=%d,devfn=0x%x,hose=%d,where=0x%x,"
		 " pci_addr=0x%p, type1=0x%p)\n",
		 bus, devfn, hose->index, where, pci_addr, type1));

	/* Type 1 configuration cycle for *ALL* busses.  */
	*type1 = 1;

	if (dev->bus->number == hose->first_busno)
		bus = 0;
	addr = (bus << 16) | (devfn << 8) | (where);
	addr <<= 5; /* swizzle for SPARSE */
	addr |= hose->config_space;

	*pci_addr = addr;
	DBG_CFG(("mk_conf_addr: returning pci_addr 0x%lx\n", addr));
	return 0;
}

static int
mcpcia_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	struct pci_controler *hose = dev->sysdata ? : probing_hose;
	unsigned long addr, w;
	unsigned char type1;

	if (mk_conf_addr(dev, where, hose, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x00;
	w = conf_read(addr, type1, hose);
	*value = __kernel_extbl(w, where & 3);
	return PCIBIOS_SUCCESSFUL;
}

static int
mcpcia_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	struct pci_controler *hose = dev->sysdata ? : probing_hose;
	unsigned long addr, w;
	unsigned char type1;

	if (mk_conf_addr(dev, where, hose, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x08;
	w = conf_read(addr, type1, hose);
	*value = __kernel_extwl(w, where & 3);
	return PCIBIOS_SUCCESSFUL;
}

static int
mcpcia_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	struct pci_controler *hose = dev->sysdata ? : probing_hose;
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, hose, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= 0x18;
	*value = conf_read(addr, type1, hose);
	return PCIBIOS_SUCCESSFUL;
}

static int
mcpcia_write_config(struct pci_dev *dev, int where, u32 value, long mask)
{
	struct pci_controler *hose = dev->sysdata ? : probing_hose;
	unsigned long addr;
	unsigned char type1;

	if (mk_conf_addr(dev, where, hose, &addr, &type1))
		return PCIBIOS_DEVICE_NOT_FOUND;

	addr |= mask;
	value = __kernel_insql(value, where & 3);
	conf_write(addr, value, type1, hose);
	return PCIBIOS_SUCCESSFUL;
}

static int
mcpcia_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	return mcpcia_write_config(dev, where, value, 0x00);
}

static int
mcpcia_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	return mcpcia_write_config(dev, where, value, 0x08);
}

static int
mcpcia_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	return mcpcia_write_config(dev, where, value, 0x18);
}

struct pci_ops mcpcia_pci_ops = 
{
	read_byte:	mcpcia_read_config_byte,
	read_word:	mcpcia_read_config_word,
	read_dword:	mcpcia_read_config_dword,
	write_byte:	mcpcia_write_config_byte,
	write_word:	mcpcia_write_config_word,
	write_dword:	mcpcia_write_config_dword
};

static int __init
mcpcia_probe_hose(int h)
{
	int cpu = smp_processor_id();
	int mid = hose2mid(h);
	unsigned int pci_rev;

	/* Gotta be REAL careful.  If hose is absent, we get an mcheck.  */

	mb();
	mb();
	draina();
	mcheck_expected(cpu) = 1;
	mcheck_taken(cpu) = 0;
	mcheck_extra(cpu) = mid;
	mb();

	/* Access the bus revision word. */
	pci_rev = *(vuip)MCPCIA_REV(mid);

	mb();
	mb();  /* magic */
	if (mcheck_taken(cpu)) {
		mcheck_taken(cpu) = 0;
		pci_rev = 0xffffffff;
		mb();
	}
	mcheck_expected(cpu) = 0;
	mb();

	return (pci_rev >> 16) == PCI_CLASS_BRIDGE_HOST;
}

static void __init
mcpcia_new_hose(unsigned long *mem_start, int h)
{
	struct pci_controler *hose;
	struct resource *io, *mem, *hae_mem;
	int mid = hose2mid(h);

	hose = alloc_pci_controler(mem_start);
	io = alloc_resource(mem_start);
	mem = alloc_resource(mem_start);
	hae_mem = alloc_resource(mem_start);
			
	hose->io_space = io;
	hose->mem_space = hae_mem;
	hose->config_space = MCPCIA_CONF(mid);
	hose->index = h;

	io->start = MCPCIA_IO(mid) - MCPCIA_IO_BIAS;
	io->end = io->start + 0xffff;
	io->name = pci_io_names[h];

	mem->start = MCPCIA_DENSE(mid) - MCPCIA_MEM_BIAS;
	mem->end = mem->start + 0xffffffff;
	mem->name = pci_mem_names[h];

	hae_mem->start = mem->start;
	hae_mem->end = mem->start + MCPCIA_MEM_MASK;
	hae_mem->name = pci_hae0_name;

	request_resource(&ioport_resource, io);
	request_resource(&iomem_resource, mem);
	request_resource(mem, hae_mem);
}

static void __init
mcpcia_startup_hose(struct pci_controler *hose)
{
	int mid = hose2mid(hose->index);
	unsigned int tmp;

	/* 
	 * Set up error reporting. Make sure CPU_PE is OFF in the mask.
	 */
#if 0
	tmp = *(vuip)MCPCIA_ERR_MASK(mid);
	tmp &= ~4;
	*(vuip)MCPCIA_ERR_MASK(mid) = tmp;
	mb();
	tmp = *(vuip)MCPCIA_ERR_MASK(mid);
#endif

	tmp = *(vuip)MCPCIA_CAP_ERR(mid);
	tmp |= 0x0006;   /* master/target abort */
	*(vuip)MCPCIA_CAP_ERR(mid) = tmp;
	mb();
	tmp = *(vuip)MCPCIA_CAP_ERR(mid);

	/*
	 * Set up the PCI->physical memory translation windows.
	 * For now, windows 1,2 and 3 are disabled.  In the
	 * future, we may want to use them to do scatter/
	 * gather DMA.
	 *
	 * Window 0 goes at 2 GB and is 2 GB large.
	 */

	*(vuip)MCPCIA_W0_BASE(mid) = 1U | (MCPCIA_DMA_WIN_BASE & 0xfff00000U);
	*(vuip)MCPCIA_W0_MASK(mid) = (MCPCIA_DMA_WIN_SIZE - 1) & 0xfff00000U;
	*(vuip)MCPCIA_T0_BASE(mid) = 0;

	*(vuip)MCPCIA_W1_BASE(mid) = 0x0;
	*(vuip)MCPCIA_W2_BASE(mid) = 0x0;
	*(vuip)MCPCIA_W3_BASE(mid) = 0x0;

	*(vuip)MCPCIA_HBASE(mid) = 0x0;
	mb();

#if 0
	tmp = *(vuip)MCPCIA_INT_CTL(mid);
	printk("mcpcia_init_arch: INT_CTL was 0x%x\n", tmp);
	*(vuip)MCPCIA_INT_CTL(mid) = 1U;
	mb();
	tmp = *(vuip)MCPCIA_INT_CTL(mid);
#endif

	*(vuip)MCPCIA_HAE_MEM(mid) = 0U;
	mb();
	*(vuip)MCPCIA_HAE_MEM(mid); /* read it back. */
	*(vuip)MCPCIA_HAE_IO(mid) = 0;
	mb();
	*(vuip)MCPCIA_HAE_IO(mid);  /* read it back. */
}

void __init
mcpcia_init_arch(unsigned long *mem_start, unsigned long *mem_end)
{
	extern asmlinkage void entInt(void);
	struct pci_controler *hose;
	int h, hose_count = 0;

	/* Ho hum.. init_arch is called before init_IRQ, but we need to be
	   able to handle machine checks.  So install the handler now.  */
	wrent(entInt, 0);

	/* With multiple PCI busses, we play with I/O as physical addrs.  */
	ioport_resource.end = ~0UL;
	iomem_resource.end = ~0UL;

	/* First, find how many hoses we have.  */
	for (h = 0; h < MCPCIA_MAX_HOSES; ++h) {
		if (mcpcia_probe_hose(h)) {
			mcpcia_new_hose(mem_start, h);
			hose_count++;
		}
	}

	printk("mcpcia_init_arch: found %d hoses\n", hose_count);

	/* Now do init for each hose.  */
	for (hose = hose_head; hose; hose = hose->next)
		mcpcia_startup_hose(hose);
}

static void
mcpcia_pci_clr_err(int mid)
{
	*(vuip)MCPCIA_CAP_ERR(mid);
	*(vuip)MCPCIA_CAP_ERR(mid) = 0xffffffff;   /* Clear them all.  */
	mb();
	*(vuip)MCPCIA_CAP_ERR(mid);  /* Re-read for force write.  */
}

static void
mcpcia_print_uncorrectable(struct el_MCPCIA_uncorrected_frame_mcheck *logout)
{
	struct el_common_EV5_uncorrectable_mcheck *frame;
	int i;

	frame = &logout->procdata;

	/* Print PAL fields */
	for (i = 0; i < 24; i += 2) {
		printk("\tpal temp[%d-%d]\t\t= %16lx %16lx\n\r",
		       i, i+1, frame->paltemp[i], frame->paltemp[i+1]);
	}
	for (i = 0; i < 8; i += 2) {
		printk("\tshadow[%d-%d]\t\t= %16lx %16lx\n\r",
		       i, i+1, frame->shadow[i], 
		       frame->shadow[i+1]);
	}
	printk("\tAddr of excepting instruction\t= %16lx\n\r",
	       frame->exc_addr);
	printk("\tSummary of arithmetic traps\t= %16lx\n\r",
	       frame->exc_sum);
	printk("\tException mask\t\t\t= %16lx\n\r",
	       frame->exc_mask);
	printk("\tBase address for PALcode\t= %16lx\n\r",
	       frame->pal_base);
	printk("\tInterrupt Status Reg\t\t= %16lx\n\r",
	       frame->isr);
	printk("\tCURRENT SETUP OF EV5 IBOX\t= %16lx\n\r",
	       frame->icsr);
	printk("\tI-CACHE Reg %s parity error\t= %16lx\n\r",
	       (frame->ic_perr_stat & 0x800L) ? 
	       "Data" : "Tag", 
	       frame->ic_perr_stat); 
	printk("\tD-CACHE error Reg\t\t= %16lx\n\r",
	       frame->dc_perr_stat);
	if (frame->dc_perr_stat & 0x2) {
		switch (frame->dc_perr_stat & 0x03c) {
		case 8:
			printk("\t\tData error in bank 1\n\r");
			break;
		case 4:
			printk("\t\tData error in bank 0\n\r");
			break;
		case 20:
			printk("\t\tTag error in bank 1\n\r");
			break;
		case 10:
			printk("\t\tTag error in bank 0\n\r");
			break;
		}
	}
	printk("\tEffective VA\t\t\t= %16lx\n\r",
	       frame->va);
	printk("\tReason for D-stream\t\t= %16lx\n\r",
	       frame->mm_stat);
	printk("\tEV5 SCache address\t\t= %16lx\n\r",
	       frame->sc_addr);
	printk("\tEV5 SCache TAG/Data parity\t= %16lx\n\r",
	       frame->sc_stat);
	printk("\tEV5 BC_TAG_ADDR\t\t\t= %16lx\n\r",
	       frame->bc_tag_addr);
	printk("\tEV5 EI_ADDR: Phys addr of Xfer\t= %16lx\n\r",
	       frame->ei_addr);
	printk("\tFill Syndrome\t\t\t= %16lx\n\r",
	       frame->fill_syndrome);
	printk("\tEI_STAT reg\t\t\t= %16lx\n\r",
	       frame->ei_stat);
	printk("\tLD_LOCK\t\t\t\t= %16lx\n\r",
	       frame->ld_lock);
}

void
mcpcia_machine_check(unsigned long vector, unsigned long la_ptr,
		     struct pt_regs * regs)
{
	struct el_common *mchk_header;
	struct el_MCPCIA_uncorrected_frame_mcheck *mchk_logout;
	unsigned int cpu = smp_processor_id();

	mchk_header = (struct el_common *)la_ptr;
	mchk_logout = (struct el_MCPCIA_uncorrected_frame_mcheck *)la_ptr;

	mb();
	mb();  /* magic */
	draina();
	if (mcheck_expected(cpu)) {
		mcpcia_pci_clr_err(mcheck_extra(cpu));
	} else {
		/* FIXME: how do we figure out which hose the error was on?  */	
		struct pci_controler *hose;
		for (hose = hose_head; hose; hose = hose->next)
			mcpcia_pci_clr_err(hose2mid(hose->index));
	}
	wrmces(0x7);
	mb();

	if (mcheck_expected(cpu)) {
		process_mcheck_info(vector, la_ptr, regs, "MCPCIA", 1);
	} else {
		process_mcheck_info(vector, la_ptr, regs, "MCPCIA", 0);
		if (vector != 0x620 && vector != 0x630)
			mcpcia_print_uncorrectable(mchk_logout);
	}
}
