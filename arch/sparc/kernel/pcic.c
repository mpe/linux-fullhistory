/* $Id: pcic.c,v 1.7 1999/07/23 01:56:07 davem Exp $
 * pcic.c: Sparc/PCI controller support
 *
 * Copyright (C) 1998 V. Roganov and G. Raiko
 *
 * Code is derived from Ultra/PCI PSYCHO controller support, see that
 * for author info.
 *
 * Support for diverse IIep based platforms by Pete Zaitcev.
 * CP-1200 by Eric Brower.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/malloc.h>

#include <asm/ebus.h>
#include <asm/sbus.h> /* for sanity check... */
#include <asm/swift.h> /* for cache flushing. */
#include <asm/io.h>

#undef PROM_DEBUG
#undef FIXUP_REGS_DEBUG
#undef FIXUP_IRQ_DEBUG
#undef FIXUP_VMA_DEBUG

#ifdef PROM_DEBUG
#define dprintf	prom_printf
#else
#define dprintf printk
#endif

#include <linux/ctype.h>
#include <linux/pci.h>
#include <linux/timex.h>
#include <linux/interrupt.h>

#include <asm/irq.h>
#include <asm/oplib.h>
#include <asm/pcic.h>
#include <asm/timer.h>
#include <asm/uaccess.h>

#ifndef CONFIG_PCI

int pcibios_present(void)
{
	return 0;
}

asmlinkage int sys_pciconfig_read(unsigned long bus,
				  unsigned long dfn,
				  unsigned long off,
				  unsigned long len,
				  unsigned char *buf)
{
	return 0;
}

asmlinkage int sys_pciconfig_write(unsigned long bus,
				   unsigned long dfn,
				   unsigned long off,
				   unsigned long len,
				   unsigned char *buf)
{
	return 0;
}

#else

unsigned int pcic_pin_to_irq(unsigned int pin, char *name);

/*
 * I studied different documents and many live PROMs both from 2.30
 * family and 3.xx versions. I came to the amazing conclusion: there is
 * absolutely no way to route interrupts in IIep systems relying on
 * information which PROM presents. We must hardcode interrupt routing
 * schematics. And this actually sucks.   -- zaitcev 1999/05/12
 *
 * To find irq for a device we determine which routing map
 * is in effect or, in other words, on which machine we are running.
 * We use PROM name for this although other techniques may be used
 * in special cases (Gleb reports a PROMless IIep based system).
 * Once we know the map we take device configuration address and
 * find PCIC pin number where INT line goes. Then we may either program
 * preferred irq into the PCIC or supply the preexisting irq to the device.
 *
 * XXX Entries for JE-1 are completely bogus. Gleb, Vladimir, please fill them.
 */
struct pcic_ca2irq {
	unsigned char busno;		/* PCI bus number */
	unsigned char devfn;		/* Configuration address */
	unsigned char pin;		/* PCIC external interrupt pin */
	unsigned char irq;		/* Preferred IRQ (mappable in PCIC) */
	unsigned int force;		/* Enforce preferred IRQ */
};

struct pcic_sn2list {
	char *sysname;
	struct pcic_ca2irq *intmap;
	int mapdim;
};

/*
 * XXX JE-1 is a little known beast.
 * One rumor has the map this way: pin 0 - parallel, audio;
 * pin 1 - Ethernet; pin 2 - su; pin 3 - PS/2 kbd and mouse.
 * All other comparable systems tie serial and keyboard together,
 * so we do not code this rumor just yet.
 */
static struct pcic_ca2irq pcic_i_je1[] = {
	{ 0, 0x01, 1,  6, 1 },		/* Happy Meal */
};

/* XXX JS-E entry is incomplete - PCI Slot 2 address (pin 7)? */
static struct pcic_ca2irq pcic_i_jse[] = {
	{ 0, 0x00, 0, 13, 0 },		/* Ebus - serial and keyboard */
	{ 0, 0x01, 1,  6, 0 },		/* hme */
	{ 0, 0x08, 2,  9, 0 },		/* VGA - we hope not used :) */
	{ 0, 0x18, 6,  8, 0 },		/* PCI INTA# in Slot 1 */
	{ 0, 0x38, 4,  9, 0 },		/* All ISA devices. Read 8259. */
	{ 0, 0x80, 5, 11, 0 },		/* EIDE */
	/* {0,0x88, 0,0,0} - unknown device... PMU? Probably no interrupt. */
	{ 0, 0xA0, 4,  9, 0 },		/* USB */
	/*
	 * Some pins belong to non-PCI devices, we hardcode them in drivers.
	 * sun4m timers - irq 10, 14
	 * PC style RTC - pin 7, irq 4 ?
	 * Smart card, Parallel - pin 4 shared with USB, ISA
	 * audio - pin 3, irq 5 ?
	 */
};

/* SPARCengine-6 was the original release name of CP1200.
 * The documentation differs between the two versions
 */
static struct pcic_ca2irq pcic_i_se6[] = {
	{ 0, 0x08, 0,  2, 0 },		/* SCSI	*/
	{ 0, 0x01, 1,  6, 0 },		/* HME	*/
	{ 0, 0x00, 3, 13, 0 },		/* EBus	*/
};

/*
 * Several entries in this list may point to the same routing map
 * as several PROMs may be installed on the same physical board.
 */
#define SN2L_INIT(name, map)	\
  { name, map, sizeof(map)/sizeof(struct pcic_ca2irq) }

static struct pcic_sn2list pcic_known_sysnames[] = {
	SN2L_INIT("JE-1-name", pcic_i_je1),  /* XXX Gleb, put name here, pls */
	SN2L_INIT("SUNW,JS-E", pcic_i_jse),	/* PROLL JavaStation-E */
	SN2L_INIT("SUNW,SPARCengine-6", pcic_i_se6), /* SPARCengine-6/CP-1200 */
	{ NULL, NULL, 0 }
};

static struct linux_pcic PCIC;
static struct linux_pcic *pcic = NULL;

unsigned int pcic_regs;
volatile int pcic_speculative;
volatile int pcic_trapped;

static void pci_do_gettimeofday(struct timeval *tv);
static void pci_do_settimeofday(struct timeval *tv);

__initfunc(void pcic_probe(void))
{
	struct linux_prom_registers regs[PROMREG_MAX];
	struct linux_pbm_info* pbm;
	char namebuf[64];
	int node;
	int err;

	if (pcibios_present()) {
		prom_printf("PCIC: called twice!\n");
		prom_halt();
	}

	node = prom_getchild (prom_root_node);
	node = prom_searchsiblings (node, "pci");
	if (node == 0)
		return;
	/*
	 * Map in PCIC register set, config space, and IO base
	 */
	err = prom_getproperty(node, "reg", (char*)regs, sizeof(regs));
	if (err == 0 || err == -1) {
		prom_printf("PCIC: Error, cannot get PCIC registers "
			    "from PROM.\n");
		prom_halt();
	}
	
	pcic = &PCIC;

	pcic->pcic_regs = (unsigned long)sparc_alloc_io(regs[0].phys_addr, NULL,
					      regs[0].reg_size,
					      "PCIC Registers", 0, 0);
	if (!pcic->pcic_regs) {
		prom_printf("PCIC: Error, cannot map PCIC registers.\n");
		prom_halt();
	}

	pcic->pcic_io_phys = regs[1].phys_addr;
	pcic->pcic_io = (unsigned long)sparc_alloc_io(regs[1].phys_addr, NULL,
					    regs[1].reg_size,
					    "PCIC IO Base", 0, 0);
	if (pcic->pcic_io == 0UL) {
		prom_printf("PCIC: Error, cannot map PCIC IO Base.\n");
		prom_halt();
	}

	pcic->pcic_config_space_addr =
			(unsigned long)sparc_alloc_io (regs[2].phys_addr, NULL,
					     regs[2].reg_size * 2,
					     "PCI Config Space Address", 0, 0);
	if (pcic->pcic_config_space_addr == 0UL) {
		prom_printf("PCIC: Error, cannot map" 
			    "PCI Configuration Space Address.\n");
		prom_halt();
	}

	/*
	 * Docs say three least significant bits in address and data
	 * must be the same. Thus, we need adjust size of data.
	 */
	pcic->pcic_config_space_data =
			(unsigned long)sparc_alloc_io (regs[3].phys_addr, NULL,
					     regs[3].reg_size * 2,
					     "PCI Config Space Data", 0, 0);
	if (pcic->pcic_config_space_data == 0UL) {
		prom_printf("PCIC: Error, cannot map" 
			    "PCI Configuration Space Data.\n");
		prom_halt();
	}

	pbm = &pcic->pbm;
	pbm->prom_node = node;
	prom_getstring(node, "name", namebuf, sizeof(namebuf));
	strcpy(pbm->prom_name, namebuf);

	{
		extern volatile int t_nmi[1];
		extern int pcic_nmi_trap_patch[1];

		t_nmi[0] = pcic_nmi_trap_patch[0];
		t_nmi[1] = pcic_nmi_trap_patch[1];
		t_nmi[2] = pcic_nmi_trap_patch[2];
		t_nmi[3] = pcic_nmi_trap_patch[3];
		swift_flush_dcache();
		pcic_regs = pcic->pcic_regs;
	}

	prom_getstring(prom_root_node, "name", namebuf, sizeof(namebuf));
	{
		struct pcic_sn2list *p;

		for (p = pcic_known_sysnames; p->sysname != NULL; p++) {
			if (strcmp(namebuf, p->sysname) == 0)
				break;
		}
		pcic->pcic_imap = p->intmap;
		pcic->pcic_imdim = p->mapdim;
	}
	if (pcic->pcic_imap == NULL) {
		/*
		 * We do not panic here for the sake of embedded systems.
		 */
		printk("PCIC: System %s is unknown, cannot route interrupts\n",
		    namebuf);
	}
}

__initfunc(void pcibios_init(void))
{
	/*
	 * PCIC should be initialized at start of the timer.
	 * So, here we report the presence of PCIC and do some magic passes.
	 */
	if(!pcic)
		return;

	printk("PCIC MAP: config addr=0x%lx; config data=0x%lx, "
	       "regs=0x%lx io=0x%lx\n",
	       pcic->pcic_config_space_addr, pcic->pcic_config_space_data,
	       pcic->pcic_regs, pcic->pcic_io);

	/*
	 *      Switch off IOTLB translation.
	 */
	writeb(PCI_DVMA_CONTROL_IOTLB_DISABLE, 
	       pcic->pcic_regs+PCI_DVMA_CONTROL);

	/*
	 *      Increase mapped size for PCI memory space (DMA access).
	 *      Should be done in that order (size first, address second).
	 *      Why we couldn't set up 4GB and forget about it? XXX
	 */
	writel(0xF0000000UL, pcic->pcic_regs+PCI_SIZE_0);
	writel(0+PCI_BASE_ADDRESS_SPACE_MEMORY, 
	       pcic->pcic_regs+PCI_BASE_ADDRESS_0);
}

int pcibios_present(void)
{
	return pcic != NULL;
}

__initfunc(static int pdev_to_pnode(struct linux_pbm_info *pbm, 
				    struct pci_dev *pdev))
{
	struct linux_prom_pci_registers regs[PROMREG_MAX];
	int err;
	int node = prom_getchild(pbm->prom_node);

	while(node) {
		err = prom_getproperty(node, "reg", 
				       (char *)&regs[0], sizeof(regs));
		if(err != 0 && err != -1) {
			unsigned long devfn = (regs[0].which_io >> 8) & 0xff;
			if(devfn == pdev->devfn)
				return node;
		}
		node = prom_getsibling(node);
	}
	return 0;
}

static inline struct pcidev_cookie *pci_devcookie_alloc(void)
{
	return kmalloc(sizeof(struct pcidev_cookie), GFP_ATOMIC);
}

static void pcic_map_pci_device (struct pci_dev *dev, int node) {
	struct linux_prom_pci_assigned_addresses addrs[6];
	int addrlen;
	int i, j;

	/* Is any valid address present ? */
	i = 0;
	for(j = 0; j < 6; j++)
		if (dev->base_address[j]) i++;
	if (!i) return; /* nothing to do */

	if (node == 0 || node == -1) {
		printk("PCIC: no prom node for device ID (%x,%x)\n",
		    dev->device, dev->vendor);
		return;
	}

	/*
	 * find related address and get it's window length
	 */
	addrlen = prom_getproperty(node,"assigned-addresses",
					       (char*)addrs, sizeof(addrs));
	if (addrlen == -1) {
		printk("PCIC: no \"assigned-addresses\" for device (%x,%x)\n",
		    dev->device, dev->vendor);
		return;
	}

	addrlen /= sizeof(struct linux_prom_pci_assigned_addresses);
	for (i = 0; i < addrlen; i++ )
	    for (j = 0; j < 6; j++) {
		if (!dev->base_address[j] || !addrs[i].phys_lo)
			continue;
		if (addrs[i].phys_lo == dev->base_address[j]) {
			unsigned long address = dev->base_address[j];
			int length  = addrs[i].size_lo;
			char namebuf[128] = { 0, };
			unsigned long mapaddr, addrflags;

			prom_getstring(node, "name", namebuf, sizeof(namebuf));

			/*
			 *      failure in allocation too large space
			 */
			if (length > 0x200000) {
				length = 0x200000;
				prom_printf("PCIC: map window for device '%s' "
					    "reduced to 2MB !\n", namebuf);
			}

			/*
			 *  Be careful with MEM/IO address flags
			 */
			if ((address & PCI_BASE_ADDRESS_SPACE) ==
				 PCI_BASE_ADDRESS_SPACE_IO) {
				mapaddr = address & PCI_BASE_ADDRESS_IO_MASK;
			} else {
				mapaddr = address & PCI_BASE_ADDRESS_MEM_MASK;
			}
			addrflags = address ^ mapaddr;

			dev->base_address[j] =
				(unsigned long)sparc_alloc_io(address, 0, 
							      length,
							      namebuf, 0, 0);
			if ( dev->base_address[j] == 0 )
				panic("PCIC: failed make mapping for "
				      "pci device '%s' with address %lx\n",
				       namebuf, address);

			dev->base_address[j] ^= addrflags;
			return;
		}
	    }

	printk("PCIC: unable to match addresses for device (%x,%x)\n",
	    dev->device, dev->vendor);
}

static void pcic_fill_irq(struct pci_dev *dev, int node) {
	struct pcic_ca2irq *p;
	int i, ivec;
	char namebuf[64];  /* P3 remove */

	if (node == -1) {
		strcpy(namebuf, "???");
	} else {
		prom_getstring(node, "name", namebuf, sizeof(namebuf)); /* P3 remove */
	}

	if ((p = pcic->pcic_imap) == 0) {
		dev->irq = 0;
		return;
	}
	for (i = 0; i < pcic->pcic_imdim; i++) {
		if (p->busno == dev->bus->number && p->devfn == dev->devfn)
			break;
		p++;
	}
	if (i >= pcic->pcic_imdim) {
		printk("PCIC: device %s devfn %02x:%02x not found in %d\n",
		    namebuf, dev->bus->number, dev->devfn, pcic->pcic_imdim);
		dev->irq = 0;
		return;
	}

	i = p->pin;
	if (i >= 0 && i < 4) {
		ivec = readw(pcic->pcic_regs+PCI_INT_SELECT_LO);
		dev->irq = ivec >> (i << 2) & 0xF;
	} else if (i >= 4 && i < 8) {
		ivec = readw(pcic->pcic_regs+PCI_INT_SELECT_HI);
		dev->irq = ivec >> ((i-4) << 2) & 0xF;
	} else {					/* Corrupted map */
		printk("PCIC: BAD PIN %d\n", i); for (;;) {}
	}
/* P3 remove later */ printk("PCIC: device %s pin %d ivec 0x%x irq %x\n", namebuf, i, ivec, dev->irq);

	/*
	 * dev->irq=0 means PROM did not bothered to program the upper
	 * half of PCIC. This happens on JS-E with PROM 3.11, for instance.
	 */
	if (dev->irq == 0 || p->force) {
		if (p->irq == 0 || p->irq >= 15) {	/* Corrupted map */
			printk("PCIC: BAD IRQ %d\n", p->irq); for (;;) {}
		}
		printk("PCIC: setting irq %x for device (%x,%x)\n",
		    p->irq, dev->device, dev->vendor);
		dev->irq = p->irq;

		ivec = readw(pcic->pcic_regs+PCI_INT_SELECT_HI);
		ivec &= ~(0xF << ((p->pin - 4) << 2));
		ivec |= p->irq << ((p->pin - 4) << 2);
		writew(ivec, pcic->pcic_regs+PCI_INT_SELECT_HI);
	}

	return;
}

/*
 * Assign IO space for a device.
 * This is a chance for devices which have the same IO and Mem Space to
 * fork access to IO and Mem.
 *
 * Now, we assume there is one such device only (IGA 1682) but code below
 * should work in cases when space of all such devices is less then 16MB.
 */
unsigned long pcic_alloc_io( unsigned long* addr )
{
	unsigned long paddr = *addr;
	unsigned long offset;

	if(pcic->pcic_mapped_io == 0) {
		pcic->pcic_mapped_io = paddr & ~(PCI_SPACE_SIZE-1) ;
		writeb((pcic->pcic_mapped_io>>24) & 0xff, 
		       pcic->pcic_regs+PCI_PIBAR);
		writeb((pcic->pcic_io_phys>>24) & PCI_SIBAR_ADDRESS_MASK,
		       pcic->pcic_regs+PCI_SIBAR);
		writeb(PCI_ISIZE_16M, pcic->pcic_regs+PCI_ISIZE);

	}
	if(paddr < pcic->pcic_mapped_io ||
	   paddr >= pcic->pcic_mapped_io + 0x10000)
		return 0;
	offset = paddr - pcic->pcic_mapped_io;
	*addr = pcic->pcic_io_phys + offset;
	return pcic->pcic_io + offset;
}

/*
 * Stolen from both i386 and sparc64 branch 
 */
__initfunc(void pcibios_fixup(void))
{
  struct pci_dev *dev;
  int i, has_io, has_mem;
  unsigned short cmd;
	struct linux_pbm_info* pbm = &pcic->pbm;
	int node;
	struct pcidev_cookie *pcp;

	if(pcic == NULL) {
		prom_printf("PCI: Error, PCIC not found.\n");
		prom_halt();
	}

	for (dev = pci_devices; dev; dev=dev->next) {
		/*
		 * Comment from i386 branch:
		 *     There are buggy BIOSes that forget to enable I/O and memory
		 *     access to PCI devices. We try to fix this, but we need to
		 *     be sure that the BIOS didn't forget to assign an address
		 *     to the device. [mj]
		 * OBP is a case of such BIOS :-)
		 */
		has_io = has_mem = 0;
		for(i=0; i<6; i++) {
			unsigned long a = dev->base_address[i];
			if (a & PCI_BASE_ADDRESS_SPACE_IO) {
				has_io = 1;
			} else if (a & PCI_BASE_ADDRESS_MEM_MASK)
				has_mem = 1;
		}
		pci_read_config_word(dev, PCI_COMMAND, &cmd);
		if (has_io && !(cmd & PCI_COMMAND_IO)) {
			printk("PCIC: Enabling I/O for device %02x:%02x\n",
				dev->bus->number, dev->devfn);
			cmd |= PCI_COMMAND_IO;
			pci_write_config_word(dev, PCI_COMMAND, cmd);
		}
		if (has_mem && !(cmd & PCI_COMMAND_MEMORY)) {
			printk("PCIC: Enabling memory for device %02x:%02x\n",
				dev->bus->number, dev->devfn);
			cmd |= PCI_COMMAND_MEMORY;
			pci_write_config_word(dev, PCI_COMMAND, cmd);
		}    

		node = pdev_to_pnode(pbm, dev);
		if(node == 0)
			node = -1;

		/* cookies */
		pcp = pci_devcookie_alloc();
		pcp->pbm = pbm;
		pcp->prom_node = node;
		dev->sysdata = pcp;

		/* memory mapping */
		if ((dev->class>>16) != PCI_BASE_CLASS_BRIDGE)
			pcic_map_pci_device(dev, node);

		pcic_fill_irq(dev, node);
	}

	ebus_init();
}

/*
 * pcic_pin_to_irq() is exported to ebus.c.
 */
unsigned int
pcic_pin_to_irq(unsigned int pin, char *name)
{
	unsigned int irq;
	unsigned int ivec;

	if (pin < 4) {
		ivec = readw(pcic->pcic_regs+PCI_INT_SELECT_LO);
		irq = ivec >> (pin << 2) & 0xF;
	} else if (pin < 8) {
		ivec = readw(pcic->pcic_regs+PCI_INT_SELECT_HI);
		irq = ivec >> ((pin-4) << 2) & 0xF;
	} else {					/* Corrupted map */
		printk("PCIC: BAD PIN %d FOR %s\n", pin, name);
		for (;;) {}	/* XXX Cannot panic properly in case of PROLL */
	}
/* P3 remove later */ printk("PCIC: dev %s pin %d ivec 0x%x irq %x\n", name, pin, ivec, irq);
	return irq;
}

/* Makes compiler happy */
static volatile int pcic_timer_dummy;

static void pcic_clear_clock_irq(void)
{
	pcic_timer_dummy = readl(pcic->pcic_regs+PCI_SYS_LIMIT);
}

static void pcic_timer_handler (int irq, void *h, struct pt_regs *regs)
{
	pcic_clear_clock_irq();
	do_timer(regs);
}

#define USECS_PER_JIFFY  10000  /* We have 100HZ "standard" timer for sparc */
#define TICK_TIMER_LIMIT ((100*1000000/4)/100)

__initfunc(void pci_time_init(void))
{
	unsigned long v;
	int timer_irq, irq;

	do_get_fast_time = pci_do_gettimeofday;
	/* A hack until do_gettimeofday prototype is moved to arch specific headers
	   and btfixupped. Patch do_gettimeofday with ba pci_do_gettimeofday; nop */
	((unsigned int *)do_gettimeofday)[0] = 
		0x10800000 | ((((unsigned long)pci_do_gettimeofday - (unsigned long)do_gettimeofday) >> 2) & 0x003fffff);
	((unsigned int *)do_gettimeofday)[1] =
		0x01000000;
	BTFIXUPSET_CALL(bus_do_settimeofday, pci_do_settimeofday, BTFIXUPCALL_NORM);
	btfixup();

	writel (TICK_TIMER_LIMIT, pcic->pcic_regs+PCI_SYS_LIMIT);
	/* PROM should set appropriate irq */
	v = readb(pcic->pcic_regs+PCI_COUNTER_IRQ);
	timer_irq = PCI_COUNTER_IRQ_SYS(v);
	writel (PCI_COUNTER_IRQ_SET(timer_irq, 0),
		pcic->pcic_regs+PCI_COUNTER_IRQ);
	irq = request_irq(timer_irq, pcic_timer_handler,
			  (SA_INTERRUPT | SA_STATIC_ALLOC), "timer", NULL);
	if (irq) {
		prom_printf("time_init: unable to attach IRQ%d\n", timer_irq);
		prom_halt();
	}
	__sti();
}

static __inline__ unsigned long do_gettimeoffset(void)
{
	unsigned long offset = 0;

	/* 
	 * We devide all to 100
	 * to have microsecond resolution and to avoid overflow
	 */
	unsigned long count = 
	    readl(pcic->pcic_regs+PCI_SYS_COUNTER) & ~PCI_SYS_COUNTER_OVERFLOW;
	count = ((count/100)*USECS_PER_JIFFY) / (TICK_TIMER_LIMIT/100);

	if(test_bit(TIMER_BH, &bh_active))
		offset = 1000000;
	return offset + count;
}

extern volatile unsigned long lost_ticks;

static void pci_do_gettimeofday(struct timeval *tv)
{
 	unsigned long flags;

	save_and_cli(flags);
	*tv = xtime;
	tv->tv_usec += do_gettimeoffset();

	/*
	 * xtime is atomically updated in timer_bh. lost_ticks is
	 * nonzero if the timer bottom half hasnt executed yet.
	 */
	if (lost_ticks)
		tv->tv_usec += USECS_PER_JIFFY;

	restore_flags(flags);

	if (tv->tv_usec >= 1000000) {
		tv->tv_usec -= 1000000;
		tv->tv_sec++;
	}       
}

static void pci_do_settimeofday(struct timeval *tv)
{
	cli();
	tv->tv_usec -= do_gettimeoffset();
	if(tv->tv_usec < 0) {
		tv->tv_usec += 1000000;
		tv->tv_sec--;
	}
	xtime = *tv;
	time_adjust = 0;		/* stop active adjtime() */
	time_status |= STA_UNSYNC;
	time_maxerror = NTP_PHASE_LIMIT;
	time_esterror = NTP_PHASE_LIMIT;
	sti();
}

#if 0
static void watchdog_reset() {
	writeb(0, pcic->pcic_regs+PCI_SYS_STATUS);
}
#endif

#define CONFIG_CMD(bus, device_fn, where) (0x80000000 | (((unsigned int)bus) << 16) | (((unsigned int)device_fn) << 8) | (where & ~3))

int pcibios_read_config_byte(unsigned char bus, unsigned char device_fn,
			     unsigned char where, unsigned char *value)
{
	unsigned int v;

	pcibios_read_config_dword (bus, device_fn, where&~3, &v);
	*value = 0xff & (v >> (8*(where & 3)));
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_read_config_word (unsigned char bus,
			      unsigned char device_fn, 
			      unsigned char where, unsigned short *value)
{
	unsigned int v;
	if (where&1) return PCIBIOS_BAD_REGISTER_NUMBER;
  
	pcibios_read_config_dword (bus, device_fn, where&~3, &v);
	*value = 0xffff & (v >> (8*(where & 3)));
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_read_config_dword (unsigned char bus, unsigned char device_fn,
			       unsigned char where, unsigned int *value)
{
	unsigned long flags;

	if (where&3) return PCIBIOS_BAD_REGISTER_NUMBER;

	save_and_cli(flags);
#if 0
	pcic_speculative = 1;
	pcic_trapped = 0;
#endif
	writel(CONFIG_CMD(bus,device_fn,where), pcic->pcic_config_space_addr);
#if 0
	nop();
	if (pcic_trapped) {
		restore_flags(flags);
		*value = ~0;
		return PCIBIOS_SUCCESSFUL;
	}
#endif
	pcic_speculative = 2;
	pcic_trapped = 0;
	*value = readl(pcic->pcic_config_space_data + (where&4));
	nop();
	if (pcic_trapped) {
		pcic_speculative = 0;
		restore_flags(flags);
		*value = ~0;
		return PCIBIOS_SUCCESSFUL;
	}
	pcic_speculative = 0;
	restore_flags(flags);
	return PCIBIOS_SUCCESSFUL;
}

int pcibios_write_config_byte (unsigned char bus, unsigned char devfn,
			       unsigned char where, unsigned char value)
{
	unsigned int v;

	pcibios_read_config_dword (bus, devfn, where&~3, &v);
	v = (v & ~(0xff << (8*(where&3)))) | 
	    ((0xff&(unsigned)value) << (8*(where&3)));
	return pcibios_write_config_dword (bus, devfn, where&~3, v);
}

int pcibios_write_config_word (unsigned char bus, unsigned char devfn,
			       unsigned char where, unsigned short value)
{
	unsigned int v;
	if (where&1) return PCIBIOS_BAD_REGISTER_NUMBER;

	pcibios_read_config_dword (bus, devfn, where&~3, &v);
	v = (v & ~(0xffff << (8*(where&3)))) | 
	    ((0xffff&(unsigned)value) << (8*(where&3)));
	return pcibios_write_config_dword (bus, devfn, where&~3, v);
}
  
int pcibios_write_config_dword (unsigned char bus, unsigned char devfn,
				unsigned char where, unsigned int value)
{
	unsigned long flags;

	if (where&3) return PCIBIOS_BAD_REGISTER_NUMBER;

	save_and_cli(flags);
	writel(CONFIG_CMD(bus,devfn,where),pcic->pcic_config_space_addr);
	writel(value, pcic->pcic_config_space_data + (where&4));
	restore_flags(flags);
	return PCIBIOS_SUCCESSFUL;
}

__initfunc(char *pcibios_setup(char *str))
{
	return str;
}

/*
 * NMI
 */
void pcic_nmi(unsigned int pend, struct pt_regs *regs)
{

	pend = flip_dword(pend);

	if (!pcic_speculative || (pend & PCI_SYS_INT_PENDING_PIO) == 0) {
		/*
		 * XXX On CP-1200 PCI #SERR may happen, we do not know
		 * what to do about it yet.
		 */
		printk("Aiee, NMI pend 0x%x pc 0x%x spec %d, hanging\n",
		    pend, (int)regs->pc, pcic_speculative);
		for (;;) { }
	}
	pcic_speculative = 0;
	pcic_trapped = 1;
	regs->pc = regs->npc;
	regs->npc += 4;
}

/*
 *  Following code added to handle extra PCI-related system calls 
 */
asmlinkage int sys_pciconfig_read(unsigned long bus,
				  unsigned long dfn,
				  unsigned long off,
				  unsigned long len,
				  unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	int err = 0;

	if(!suser())
		return -EPERM;

	lock_kernel();
	switch(len) {
	case 1:
		pcibios_read_config_byte(bus, dfn, off, &ubyte);
		put_user(ubyte, (unsigned char *)buf);
		break;
	case 2:
		pcibios_read_config_word(bus, dfn, off, &ushort);
		put_user(ushort, (unsigned short *)buf);
		break;
	case 4:
		pcibios_read_config_dword(bus, dfn, off, &uint);
		put_user(uint, (unsigned int *)buf);
		break;

	default:
		err = -EINVAL;
		break;
	};
	unlock_kernel();

	return err;
}
   
asmlinkage int sys_pciconfig_write(unsigned long bus,
				   unsigned long dfn,
				   unsigned long off,
				   unsigned long len,
				   unsigned char *buf)
{
	unsigned char ubyte;
	unsigned short ushort;
	unsigned int uint;
	int err = 0;

	if(!suser())
		return -EPERM;

	lock_kernel();
	switch(len) {
	case 1:
		err = get_user(ubyte, (unsigned char *)buf);
		if(err)
			break;
		pcibios_write_config_byte(bus, dfn, off, ubyte);
		break;

	case 2:
		err = get_user(ushort, (unsigned short *)buf);
		if(err)
			break;
		pcibios_write_config_byte(bus, dfn, off, ushort);
		break;

	case 4:
		err = get_user(uint, (unsigned int *)buf);
		if(err)
			break;
		pcibios_write_config_byte(bus, dfn, off, uint);
		break;

	default:
		err = -EINVAL;
		break;

	};
	unlock_kernel();

	return err;
}			   

static inline unsigned long get_irqmask(int irq_nr)
{
	return 1 << irq_nr;
}

static inline char *pcic_irq_itoa(unsigned int irq)
{
	static char buff[16];
	sprintf(buff, "%d", irq);
	return buff;
}

static void pcic_disable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;

	mask = get_irqmask(irq_nr);
	save_and_cli(flags);
	writel(mask, pcic->pcic_regs+PCI_SYS_INT_TARGET_MASK_SET);
	restore_flags(flags);
}

static void pcic_enable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;

	mask = get_irqmask(irq_nr);
	save_and_cli(flags);
	writel(mask, pcic->pcic_regs+PCI_SYS_INT_TARGET_MASK_CLEAR);
	restore_flags(flags);
}

static void pcic_clear_profile_irq(int cpu)
{
	printk("PCIC: unimplemented code: FILE=%s LINE=%d", __FILE__, __LINE__);
}

static void pcic_load_profile_irq(int cpu, unsigned int limit)
{
	printk("PCIC: unimplemented code: FILE=%s LINE=%d", __FILE__, __LINE__);
}

/* We assume the caller is local cli()'d when these are called, or else
 * very bizarre behavior will result.
 */
static void pcic_disable_pil_irq(unsigned int pil)
{
	writel(get_irqmask(pil), pcic->pcic_regs+PCI_SYS_INT_TARGET_MASK_SET);
}

static void pcic_enable_pil_irq(unsigned int pil)
{
	writel(get_irqmask(pil), pcic->pcic_regs+PCI_SYS_INT_TARGET_MASK_CLEAR);
}

__initfunc(void sun4m_pci_init_IRQ(void))
{
	BTFIXUPSET_CALL(enable_irq, pcic_enable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_irq, pcic_disable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(enable_pil_irq, pcic_enable_pil_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_pil_irq, pcic_disable_pil_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_clock_irq, pcic_clear_clock_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_profile_irq, pcic_clear_profile_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(load_profile_irq, pcic_load_profile_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(__irq_itoa, pcic_irq_itoa, BTFIXUPCALL_NORM);
}

__initfunc(void pcibios_fixup_bus(struct pci_bus *bus))
{
}

#endif
