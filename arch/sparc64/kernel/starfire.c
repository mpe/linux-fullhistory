/* $Id: starfire.c,v 1.3 1999/08/30 10:01:13 davem Exp $
 * starfire.c: Starfire/E10000 support.
 *
 * Copyright (C) 1998 David S. Miller (davem@dm.cobaltmicro.com)
 */

#include <linux/kernel.h>
#include <linux/malloc.h>

#include <asm/page.h>
#include <asm/oplib.h>
#include <asm/smp.h>

/* A few places around the kernel check this to see if
 * they need to call us to do things in a Starfire specific
 * way.
 */
int this_is_starfire = 0;

void starfire_check(void)
{
	int ssnode = prom_finddevice("/ssp-serial");

	if(ssnode != 0 && ssnode != -1) {
		int i;

		this_is_starfire = 1;

		/* Now must fixup cpu MIDs.  OBP gave us a logical
		 * linear cpuid number, not the real upaid.
		 */
		for(i = 0; i < linux_num_cpus; i++) {
			unsigned int mid = linux_cpus[i].mid;

			mid = (((mid & 0x3c) << 1) |
			       ((mid & 0x40) >> 4) |
			       (mid & 0x3));

			linux_cpus[i].mid = mid;
		}
	}
}

int starfire_hard_smp_processor_id(void)
{
	return *((volatile unsigned int *) __va(0x1fff40000d0));
}

/* Each Starfire board has 32 registers which perform translation
 * and delivery of traditional interrupt packets into the extended
 * Starfire hardware format.  Essentially UPAID's now have 2 more
 * bits than in all previous Sun5 systems.
 */
struct starfire_irqinfo {
	volatile unsigned int *imap_slots[32];
	volatile unsigned int *tregs[32];
	struct starfire_irqinfo *next;
	int upaid, hwmid;
};

static struct starfire_irqinfo *sflist = NULL;

/* Beam me up Scott(McNeil)y... */
void *starfire_hookup(int upaid)
{
	struct starfire_irqinfo *p;
	unsigned long treg_base, hwmid, i;

	p = kmalloc(sizeof(*p), GFP_KERNEL);
	if(!p) {
		prom_printf("starfire_hookup: No memory, this is insane.\n");
		prom_halt();
	}
	treg_base = 0x100fc000000UL;
	hwmid = ((upaid & 0x3c) << 1) |
		((upaid & 0x40) >> 4) |
		(upaid & 0x3);
	p->hwmid = hwmid;
	treg_base += (hwmid << 33UL);
	treg_base += 0x200UL;
	for(i = 0; i < 32; i++) {
		p->imap_slots[i] = NULL;
		p->tregs[i] = (volatile unsigned int *)__va(treg_base + (i * 0x10));
	}
	p->upaid = upaid;
	p->next = sflist;
	sflist = p;

	return (void *) p;
}

unsigned int starfire_translate(volatile unsigned int *imap,
				unsigned int upaid)
{
	struct starfire_irqinfo *p;
	unsigned int bus_hwmid;
	unsigned int i;

	bus_hwmid = (((unsigned long)imap) >> 33) & 0x7f;
	for(p = sflist; p != NULL; p = p->next)
		if(p->hwmid == bus_hwmid)
			break;
	if(p == NULL) {
		prom_printf("XFIRE: Cannot find irqinfo for imap %016lx\n",
			    ((unsigned long)imap));
		prom_halt();
	}
	for(i = 0; i < 32; i++) {
		if(p->imap_slots[i] == imap ||
		   p->imap_slots[i] == NULL)
			break;
	}
	if(i == 32) {
		printk("starfire_translate: Are you kidding me?\n");
		panic("Lucy in the sky....");
	}
	p->imap_slots[i] = imap;
	*(p->tregs[i]) = upaid;

	return i;
}
