/* $Id: ebus.c,v 1.2 1997/08/15 06:44:13 davem Exp $
 * ebus.c: PCI to EBus bridge device.
 *
 * Copyright (C) 1997  Eddie C. Dost  (ecd@skynet.be)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/string.h>

#include <asm/system.h>
#include <asm/pbm.h>
#include <asm/ebus.h>
#include <asm/oplib.h>
#include <asm/bpp.h>

struct linux_ebus *ebus_chain = 0;

static char lbuf[128];

extern void prom_ebus_ranges_init(struct linux_ebus *);

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

__initfunc(void fill_ebus_device(int node, struct linux_ebus_device *dev))
{
	int irqs[PROMINTR_MAX];
	int i, len;

	dev->prom_node = node;
	prom_getstring(node, "name", lbuf, sizeof(lbuf));
	strcpy(dev->prom_name, lbuf);

	len = prom_getproperty(node, "reg", (void *)dev->regs,
			       sizeof(dev->regs));
	if (len % sizeof(struct linux_prom_registers)) {
		prom_printf("UGH: proplen for %s was %d, need multiple of %d\n",
			    dev->prom_name, len,
			    (int)sizeof(struct linux_prom_registers));
		panic(__FUNCTION__);
	}
	dev->num_registers = len / sizeof(struct linux_prom_registers);

	prom_apply_ebus_ranges(dev->parent, dev->regs, dev->num_registers);
#if 0 /* XXX No longer exists/needed in new framework... */
	prom_apply_pbm_ranges(dev->parent->parent, dev->regs,
			      dev->num_registers);
#endif

	len = prom_getproperty(node, "interrupts", (char *)&irqs, sizeof(irqs));
	if ((len == -1) || (len == 0)) {
		dev->irqs[0].pri = 0;
		dev->num_irqs = 0;
	} else {
		dev->num_irqs = len / sizeof(irqs[0]);
		for (i = 0; i < dev->num_irqs; i++)
			dev->irqs[i].pri = irqs[i];
	}

	printk("Found '%s' at %x.%08x", dev->prom_name,
	       dev->regs[0].which_io, dev->regs[0].phys_addr);
	if (dev->num_irqs) {
		printk(" IRQ%s", dev->num_irqs > 1 ? "s" : "");
		for (i = 0; i < dev->num_irqs; i++)
			printk(" %03x", dev->irqs[i].pri);
	}
	printk("\n");
}

__initfunc(unsigned long ebus_init(unsigned long memory_start,
				   unsigned long memory_end))
{
	struct linux_ebus_device *dev;
	struct linux_ebus *ebus;
	int nd, ebusnd, topnd;
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
		printk("ebus%d: ", num_ebus);

		prom_getstring(ebusnd, "name", lbuf, sizeof(lbuf));
		ebus->prom_node = ebusnd;
		strcpy(ebus->prom_name, lbuf);
		ebus->parent = &psycho_root->pbm_B;

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
