
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/prom.h>
#include "pmac_pic.h"

/* pmac */struct pmac_irq_hw {
        unsigned int    flag;
        unsigned int    enable;
        unsigned int    ack;
        unsigned int    level;
};

/* XXX these addresses should be obtained from the device tree */
static volatile struct pmac_irq_hw *pmac_irq_hw[4] = {
        (struct pmac_irq_hw *) 0xf3000020,
        (struct pmac_irq_hw *) 0xf3000010,
        (struct pmac_irq_hw *) 0xf4000020,
        (struct pmac_irq_hw *) 0xf4000010,
};

static int max_irqs;
static int max_real_irqs;

#define MAXCOUNT 10000000

#define GATWICK_IRQ_POOL_SIZE        10
static struct interrupt_info gatwick_int_pool[GATWICK_IRQ_POOL_SIZE];

static void __pmac pmac_mask_and_ack_irq(unsigned int irq_nr)
{
        unsigned long bit = 1UL << (irq_nr & 0x1f);
        int i = irq_nr >> 5;

        if ((unsigned)irq_nr >= max_irqs)
                return;

        clear_bit(irq_nr, ppc_cached_irq_mask);
        if (test_and_clear_bit(irq_nr, ppc_lost_interrupts))
                atomic_dec(&ppc_n_lost_interrupts);
        out_le32(&pmac_irq_hw[i]->ack, bit);
        out_le32(&pmac_irq_hw[i]->enable, ppc_cached_irq_mask[i]);
        out_le32(&pmac_irq_hw[i]->ack, bit);
        do {
                /* make sure ack gets to controller before we enable
                   interrupts */
                mb();
        } while(in_le32(&pmac_irq_hw[i]->flag) & bit);
}

static void __pmac pmac_set_irq_mask(unsigned int irq_nr)
{
        unsigned long bit = 1UL << (irq_nr & 0x1f);
        int i = irq_nr >> 5;

        if ((unsigned)irq_nr >= max_irqs)
                return;

        /* enable unmasked interrupts */
        out_le32(&pmac_irq_hw[i]->enable, ppc_cached_irq_mask[i]);

        do {
                /* make sure mask gets to controller before we
                   return to user */
                mb();
        } while((in_le32(&pmac_irq_hw[i]->enable) & bit)
                != (ppc_cached_irq_mask[i] & bit));

        /*
         * Unfortunately, setting the bit in the enable register
         * when the device interrupt is already on *doesn't* set
         * the bit in the flag register or request another interrupt.
         */
        if ((bit & ppc_cached_irq_mask[i])
            && (ld_le32(&pmac_irq_hw[i]->level) & bit)
            && !(ld_le32(&pmac_irq_hw[i]->flag) & bit)) {
                if (!test_and_set_bit(irq_nr, ppc_lost_interrupts))
                        atomic_inc(&ppc_n_lost_interrupts);
        }
}

static void __pmac pmac_mask_irq(unsigned int irq_nr)
{
        clear_bit(irq_nr, ppc_cached_irq_mask);
        pmac_set_irq_mask(irq_nr);
        mb();
}

static void __pmac pmac_unmask_irq(unsigned int irq_nr)
{
        set_bit(irq_nr, ppc_cached_irq_mask);
        pmac_set_irq_mask(irq_nr);
}

struct hw_interrupt_type pmac_pic = {
        " PMAC-PIC ",
        NULL,
        NULL,
        NULL,
        pmac_unmask_irq,
        pmac_mask_irq,
        pmac_mask_and_ack_irq,
        0
};

struct hw_interrupt_type gatwick_pic = {
	" GATWICK  ",
	NULL,
	NULL,
	NULL,
	pmac_unmask_irq,
	pmac_mask_irq,
	pmac_mask_and_ack_irq,
	0
};

static void gatwick_action(int cpl, void *dev_id, struct pt_regs *regs)
{
	int irq, bits;
	
	for (irq = max_irqs - 1; irq > max_real_irqs; irq -= 32) {
		int i = irq >> 5;
		bits = ld_le32(&pmac_irq_hw[i]->flag)
			| ppc_lost_interrupts[i];
		if (bits == 0)
			continue;
		irq -= cntlzw(bits);
		break;
	}
	/* The previous version of this code allowed for this case, we
	 * don't.  Put this here to check for it.
	 * -- Cort
	 */
	if ( irq_desc[irq].ctl != &gatwick_pic )
		printk("gatwick irq not from gatwick pic\n");
	else
		ppc_irq_dispatch_handler( regs, irq );
}

void
pmac_do_IRQ(struct pt_regs *regs,
	    int            cpu,
            int            isfake)
{
	int irq;
	unsigned long bits = 0;

#ifdef __SMP__
        /* IPI's are a hack on the powersurge -- Cort */
        if ( cpu != 0 )
        {
                if (!isfake)
                {
#ifdef CONFIG_XMON
                        static int xmon_2nd;
                        if (xmon_2nd)
                                xmon(regs);
#endif
                        smp_message_recv();
                        goto out;
                }
                /* could be here due to a do_fake_interrupt call but we don't
                   mess with the controller from the second cpu -- Cort */
                goto out;
        }

        {
                unsigned int loops = MAXCOUNT;
                while (test_bit(0, &global_irq_lock)) {
                        if (smp_processor_id() == global_irq_holder) {
                                printk("uh oh, interrupt while we hold global irq lock!\n");
#ifdef CONFIG_XMON
                                xmon(0);
#endif
                                break;
                        }
                        if (loops-- == 0) {
                                printk("do_IRQ waiting for irq lock (holder=%d)\n", global_irq_holder);
#ifdef CONFIG_XMON
                                xmon(0);
#endif
                        }
                }
        }
#endif /* __SMP__ */

        for (irq = max_real_irqs - 1; irq > 0; irq -= 32) {
                int i = irq >> 5;
                bits = ld_le32(&pmac_irq_hw[i]->flag)
                        | ppc_lost_interrupts[i];
                if (bits == 0)
                        continue;
                irq -= cntlzw(bits);
                break;
        }

        if (irq < 0)
        {
                printk(KERN_DEBUG "Bogus interrupt %d from PC = %lx\n",
                       irq, regs->nip);
                ppc_spurious_interrupts++;
        }
        else
        {
                ppc_irq_dispatch_handler( regs, irq );
        }
#ifdef CONFIG_SMP	
out:
#endif /* CONFIG_SMP */
}

/* This routine will fix some missing interrupt values in the device tree
 * on the gatwick mac-io controller used by some PowerBooks
 */
static void __init pmac_fix_gatwick_interrupts(struct device_node *gw, int irq_base)
{
	struct device_node *node;
	int count;
	
	memset(gatwick_int_pool, 0, sizeof(gatwick_int_pool));
	node = gw->child;
	count = 0;
	while(node)
	{
		/* Fix SCC */
		if (strcasecmp(node->name, "escc") == 0)
			if (node->child) {
				if (node->child->n_intrs < 3) {
					node->child->intrs = &gatwick_int_pool[count];
					count += 3;
				}
				node->child->n_intrs = 3;				
				node->child->intrs[0].line = 15+irq_base;
				node->child->intrs[1].line =  4+irq_base;
				node->child->intrs[2].line =  5+irq_base;
				printk(KERN_INFO "irq: fixed SCC on second controller (%d,%d,%d)\n",
					node->child->intrs[0].line,
					node->child->intrs[1].line,
					node->child->intrs[2].line);
			}
		/* Fix media-bay & left SWIM */
		if (strcasecmp(node->name, "media-bay") == 0) {
			struct device_node* ya_node;

			if (node->n_intrs == 0)
				node->intrs = &gatwick_int_pool[count++];
			node->n_intrs = 1;
			node->intrs[0].line = 29+irq_base;
			printk(KERN_INFO "irq: fixed media-bay on second controller (%d)\n",
					node->intrs[0].line);
			
			ya_node = node->child;
			while(ya_node)
			{
				if (strcasecmp(ya_node->name, "floppy") == 0) {
					if (ya_node->n_intrs < 2) {
						ya_node->intrs = &gatwick_int_pool[count];
						count += 2;
					}
					ya_node->n_intrs = 2;
					ya_node->intrs[0].line = 19+irq_base;
					ya_node->intrs[1].line =  1+irq_base;
					printk(KERN_INFO "irq: fixed floppy on second controller (%d,%d)\n",
						ya_node->intrs[0].line, ya_node->intrs[1].line);
				} 
				if (strcasecmp(ya_node->name, "ata4") == 0) {
					if (ya_node->n_intrs < 2) {
						ya_node->intrs = &gatwick_int_pool[count];
						count += 2;
					}
					ya_node->n_intrs = 2;
					ya_node->intrs[0].line = 14+irq_base;
					ya_node->intrs[1].line =  3+irq_base;
					printk(KERN_INFO "irq: fixed ide on second controller (%d,%d)\n",
						ya_node->intrs[0].line, ya_node->intrs[1].line);
				} 
				ya_node = ya_node->sibling;
			}
		}
		node = node->sibling;
	}
	if (count > 10) {
		printk("WARNING !! Gatwick interrupt pool overflow\n");
		printk("  GATWICK_IRQ_POOL_SIZE = %d\n", GATWICK_IRQ_POOL_SIZE);
		printk("              requested = %d\n", count);
	}
}

__initfunc(void
pmac_pic_init(void))
{
        int i;
        struct device_node *irqctrler;
        unsigned long addr;
	int second_irq = -999;


	/* G3 powermacs have 64 interrupts, G3 Series PowerBook have 128, 
	   others have 32 */
	max_irqs = max_real_irqs = 32;
	irqctrler = find_devices("mac-io");
	if (irqctrler)
	{
		max_real_irqs = 64;
		if (irqctrler->next)
			max_irqs = 128;
		else
			max_irqs = 64;
	}
	for ( i = 0; i < max_real_irqs ; i++ )
		irq_desc[i].ctl = &pmac_pic;

	/* get addresses of first controller */
	if (irqctrler) {
		if  (irqctrler->n_addrs > 0) {
			addr = (unsigned long) 
				ioremap(irqctrler->addrs[0].address, 0x40);
			for (i = 0; i < 2; ++i)
				pmac_irq_hw[i] = (volatile struct pmac_irq_hw*)
					(addr + (2 - i) * 0x10);
		}
		
		/* get addresses of second controller */
		irqctrler = (irqctrler->next) ? irqctrler->next : NULL;
		if (irqctrler && irqctrler->n_addrs > 0) {
			addr = (unsigned long) 
				ioremap(irqctrler->addrs[0].address, 0x40);
			for (i = 2; i < 4; ++i)
				pmac_irq_hw[i] = (volatile struct pmac_irq_hw*)
					(addr + (4 - i) * 0x10);
		}
	}

	/* disable all interrupts in all controllers */
	for (i = 0; i * 32 < max_irqs; ++i)
		out_le32(&pmac_irq_hw[i]->enable, 0);
	
	/* get interrupt line of secondary interrupt controller */
	if (irqctrler) {
		second_irq = irqctrler->intrs[0].line;
		printk(KERN_INFO "irq: secondary controller on irq %d\n",
			(int)second_irq);
		if (device_is_compatible(irqctrler, "gatwick"))
			pmac_fix_gatwick_interrupts(irqctrler, max_real_irqs);
		for ( i = max_real_irqs ; i < max_irqs ; i++ )
			irq_desc[i].ctl = &gatwick_pic;
		request_irq( second_irq, gatwick_action, SA_INTERRUPT,
			     "gatwick cascade", 0 );
	}
	printk("System has %d possible interrupts\n", max_irqs);
	if (max_irqs != max_real_irqs)
		printk(KERN_DEBUG "%d interrupts on main controller\n",
			max_real_irqs);

#ifdef CONFIG_XMON
	request_irq(20, xmon_irq, 0, "NMI - XMON", 0);
#endif	/* CONFIG_XMON */
}
