/* $Id: ebus.c,v 1.7 1997/08/28 02:23:17 ecd Exp $
 * ebus.c: PCI to EBus bridge device.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/string.h>

#include <asm/system.h>
#include <asm/page.h>
#include <asm/pbm.h>
#include <asm/ebus.h>
#include <asm/oplib.h>
#include <asm/bpp.h>

#undef DEBUG_FILL_EBUS_DEV

struct linux_ebus *ebus_chain = 0;

extern void prom_ebus_ranges_init(struct linux_ebus *);
extern unsigned long pci_console_init(unsigned long memory_start);

#ifdef CONFIG_SUN_OPENPROMIO
extern int openprom_init(void);
#endif
#ifdef CONFIG_SUN_MOSTEK_RTC
extern int rtc_init(void);
#endif
#ifdef CONFIG_SPARCAUDIO
extern int sparcaudio_init(void);
#endif
#ifdef CONFIG_SUN_AUXIO
extern void auxio_probe(void);
#endif

extern unsigned int psycho_irq_build(unsigned int full_ino);

__initfunc(void fill_ebus_device(int node, struct linux_ebus_device *dev))
{
	struct linux_prom_registers regs[PROMREG_MAX];
	int irqs[PROMINTR_MAX];
	char lbuf[128];
	int i, n, len;

#ifndef CONFIG_PCI
	return;
#endif
	dev->prom_node = node;
	prom_getstring(node, "name", lbuf, sizeof(lbuf));
	strcpy(dev->prom_name, lbuf);

	len = prom_getproperty(node, "reg", (void *)regs, sizeof(regs));
	if (len % sizeof(struct linux_prom_registers)) {
		prom_printf("UGH: proplen for %s was %d, need multiple of %d\n",
			    dev->prom_name, len,
			    (int)sizeof(struct linux_prom_registers));
		panic(__FUNCTION__);
	}
	dev->num_addrs = len / sizeof(struct linux_prom_registers);

	for (i = 0; i < dev->num_addrs; i++) {
		n = (regs[i].which_io - 0x10) >> 2;

		dev->base_address[i] = dev->parent->self->base_address[n];
		dev->base_address[i] += (unsigned long)regs[i].phys_addr;
	}

	len = prom_getproperty(node, "interrupts", (char *)&irqs, sizeof(irqs));
	if ((len == -1) || (len == 0)) {
		dev->num_irqs = 0;
	} else {
		dev->num_irqs = len / sizeof(irqs[0]);
		for (i = 0; i < dev->num_irqs; i++)
			dev->irqs[i] = psycho_irq_build(irqs[i]);
	}

#ifdef DEBUG_FILL_EBUS_DEV
	printk("'%s': address%s\n", dev->prom_name,
	       dev->num_addrs > 1 ? "es" : "");
	for (i = 0; i < dev->num_addrs; i++)
		printk("  %016lx\n", dev->base_address[i]);
	if (dev->num_irqs) {
		printk("  IRQ%s", dev->num_irqs > 1 ? "s" : "");
		for (i = 0; i < dev->num_irqs; i++)
			printk(" %08x", dev->irqs[i]);
		printk("\n");
	}
#endif
}

__initfunc(unsigned long ebus_init(unsigned long memory_start,
				   unsigned long memory_end))
{
	struct linux_prom_pci_registers regs[PROMREG_MAX];
	struct linux_pbm_info *pbm;
	struct linux_ebus_device *dev;
	struct linux_ebus *ebus;
	struct pci_dev *pdev;
	char lbuf[128];
	unsigned long addr, *base;
	int nd, len, ebusnd, topnd;
	int reg, rng, nreg;
	int devfn;
	int num_ebus = 0;

#ifndef CONFIG_PCI
	return memory_start;
#endif

	memory_start = ((memory_start + 7) & (~7));

	topnd = psycho_root->pbm_B.prom_node;
	if (!topnd)
		return memory_start;

	ebusnd = prom_searchsiblings(prom_getchild(topnd), "ebus");
	if (ebusnd == 0) {
		printk("EBUS: No EBUS's found.\n");
		return memory_start;
	}

	ebus_chain = ebus = (struct linux_ebus *)memory_start;
	memory_start += sizeof(struct linux_ebus);
	ebus->next = 0;

	while (ebusnd) {
		printk("ebus%d:\n", num_ebus);

		prom_getstring(ebusnd, "name", lbuf, sizeof(lbuf));
		ebus->prom_node = ebusnd;
		strcpy(ebus->prom_name, lbuf);
		ebus->parent = pbm = &psycho_root->pbm_B;

		len = prom_getproperty(ebusnd, "reg", (void *)regs,
				       sizeof(regs));
		if (len == 0 || len == -1) {
			prom_printf("%s: can't find reg property\n",
				    __FUNCTION__);
			prom_halt();
		}
		nreg = len / sizeof(struct linux_prom_pci_registers);

		devfn = (regs[0].phys_hi >> 8) & 0xff;
		for (pdev = pbm->pci_bus.devices; pdev; pdev = pdev->sibling)
			if (pdev->devfn == devfn)
				break;
		if (!pdev) {
			prom_printf("%s: can't find PCI device\n",
				    __FUNCTION__);
			prom_halt();
		}
		ebus->self = pdev;

		base = &ebus->self->base_address[0];
		for (reg = 0; reg < nreg; reg++) {
			if (!(regs[reg].phys_hi & 0x03000000))
				continue;

			for (rng = 0; rng < pbm->num_pbm_ranges; rng++) {
				struct linux_prom_pci_ranges *rp =
						&pbm->pbm_ranges[rng];

				if ((rp->child_phys_hi ^ regs[reg].phys_hi)
								& 0x03000000)
					continue;

				addr = (u64)regs[reg].phys_lo;
				addr += (u64)regs[reg].phys_mid << 32UL;
				addr += (u64)rp->parent_phys_lo;
				addr += (u64)rp->parent_phys_hi << 32UL;
				*base++ = (unsigned long)__va(addr);

				break;
			}
		}

		prom_ebus_ranges_init(ebus);

		nd = prom_getchild(ebusnd);
		ebus->devices = (struct linux_ebus_device *)memory_start;
		memory_start += sizeof(struct linux_ebus_device);

		dev = ebus->devices;
		dev->next = 0;
		dev->parent = ebus;
		fill_ebus_device(nd, dev);

		while ((nd = prom_getsibling(nd))) {
			dev->next = (struct linux_ebus_device *)memory_start;
			memory_start += sizeof(struct linux_ebus_device);

			dev = dev->next;
			dev->next = 0;
			dev->parent = ebus;
			fill_ebus_device(nd, dev);
		}

		ebusnd = prom_searchsiblings(prom_getsibling(ebusnd), "ebus");
		++num_ebus;
	}

	memory_start = pci_console_init(memory_start);

#ifdef CONFIG_SUN_OPENPROMIO
	openprom_init();
#endif
#ifdef CONFIG_SUN_MOSTEK_RTC
	rtc_init();
#endif
#ifdef CONFIG_SPARCAUDIO
	sparcaudio_init();
#endif
#ifdef CONFIG_SUN_BPP
	bpp_init();
#endif
#ifdef CONFIG_SUN_AUXIO
	if (sparc_cpu_model == sun4u)
		auxio_probe();
#endif
#ifdef __sparc_v9__
	if (sparc_cpu_model == sun4u) {
		extern void sun4u_start_timers(void);
		extern void clock_probe(void);

		sun4u_start_timers();
		clock_probe();
	}
#endif
	return memory_start;
}
