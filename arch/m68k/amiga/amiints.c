/*
 * linux/arch/m68k/amiga/amiints.c -- Amiga Linux interrupt handling code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * 11/07/96: rewritten interrupt handling, irq lists are exists now only for
 *           this sources where it makes sense (VERTB/PORTS/EXTER) and you must
 *           be careful that dev_id for this sources is unique since this the
 *           only possibility to distinguish between different handlers for
 *           free_irq. irq lists also have different irq flags:
 *           - IRQ_FLG_FAST: handler is inserted at top of list (after other
 *                           fast handlers)
 *           - IRQ_FLG_SLOW: handler is inserted at bottom of list and before
 *                           they're executed irq level is set to the previous
 *                           one, but handlers don't need to be reentrant, if
 *                           reentrance occurred, slow handlers will be just
 *                           called again.
 *           The whole interrupt handling for CIAs is moved to cia.c
 *           /Roman Zippel
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/amipcmcia.h>

extern int cia_request_irq(struct ciabase *base,int irq,
                           void (*handler)(int, void *, struct pt_regs *),
                           unsigned long flags, const char *devname, void *dev_id);
extern void cia_free_irq(struct ciabase *base, unsigned int irq, void *dev_id);
extern void cia_init_IRQ(struct ciabase *base);
extern int cia_get_irq_list(struct ciabase *base, char *buf);

/* irq node variables for amiga interrupt sources */
static irq_node_t *ami_irq_list[AMI_STD_IRQS];

unsigned short ami_intena_vals[AMI_STD_IRQS] = {
	IF_VERTB, IF_COPER, IF_AUD0, IF_AUD1, IF_AUD2, IF_AUD3, IF_BLIT,
	IF_DSKSYN, IF_DSKBLK, IF_RBF, IF_TBE, IF_SOFT, IF_PORTS, IF_EXTER
};
static const unsigned char ami_servers[AMI_STD_IRQS] = {
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1
};

static short ami_ablecount[AMI_IRQS];

static void ami_badint(int irq, void *dev_id, struct pt_regs *fp)
{
	num_spurious += 1;
}

/*
 * void amiga_init_IRQ(void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function should be called during kernel startup to initialize
 * the amiga IRQ handling routines.
 */

__initfunc(void amiga_init_IRQ(void))
{
	int i;

	/* initialize handlers */
	for (i = 0; i < AMI_STD_IRQS; i++) {
		if (ami_servers[i]) {
			ami_irq_list[i] = NULL;
		} else {
			ami_irq_list[i] = new_irq_node();
			ami_irq_list[i]->handler = ami_badint;
			ami_irq_list[i]->flags   = IRQ_FLG_STD;
			ami_irq_list[i]->dev_id  = NULL;
			ami_irq_list[i]->devname = NULL;
			ami_irq_list[i]->next    = NULL;
		}
	}
	for (i = 0; i < AMI_IRQS; i++)
		ami_ablecount[i] = 0;

	/* turn off PCMCIA interrupts */
	if (AMIGAHW_PRESENT(PCMCIA))
		pcmcia_disable_irq();

	/* turn off all interrupts and enable the master interrupt bit */
	custom.intena = 0x7fff;
	custom.intreq = 0x7fff;
	custom.intena = IF_SETCLR | IF_INTEN;

	cia_init_IRQ(&ciaa_base);
	cia_init_IRQ(&ciab_base);
}

static inline void amiga_insert_irq(irq_node_t **list, irq_node_t *node)
{
	unsigned long flags;
	irq_node_t *cur;

	if (!node->dev_id)
		printk("%s: Warning: dev_id of %s is zero\n",
		       __FUNCTION__, node->devname);

	save_flags(flags);
	cli();

	cur = *list;

	if (node->flags & IRQ_FLG_FAST) {
		node->flags &= ~IRQ_FLG_SLOW;
		while (cur && cur->flags & IRQ_FLG_FAST) {
			list = &cur->next;
			cur = cur->next;
		}
	} else if (node->flags & IRQ_FLG_SLOW) {
		while (cur) {
			list = &cur->next;
			cur = cur->next;
		}
	} else {
		while (cur && !(cur->flags & IRQ_FLG_SLOW)) {
			list = &cur->next;
			cur = cur->next;
		}
	}

	node->next = cur;
	*list = node;

	restore_flags(flags);
}

static inline void amiga_delete_irq(irq_node_t **list, void *dev_id)
{
	unsigned long flags;
	irq_node_t *node;

	save_flags(flags);
	cli();

	for (node = *list; node; list = &node->next, node = *list) {
		if (node->dev_id == dev_id) {
			*list = node->next;
			/* Mark it as free. */
			node->handler = NULL;
			restore_flags(flags);
			return;
		}
	}
	restore_flags(flags);
	printk ("%s: tried to remove invalid irq\n", __FUNCTION__);
}

/*
 * amiga_request_irq : add an interrupt service routine for a particular
 *                     machine specific interrupt source.
 *                     If the addition was successful, it returns 0.
 */

int amiga_request_irq(unsigned int irq, void (*handler)(int, void *, struct pt_regs *),
                      unsigned long flags, const char *devname, void *dev_id)
{
	irq_node_t *node;

	if (irq >= AMI_IRQS) {
		printk ("%s: Unknown IRQ %d from %s\n", __FUNCTION__, irq, devname);
		return -ENXIO;
	}

	if (irq >= IRQ_AMIGA_AUTO)
		return sys_request_irq(irq - IRQ_AMIGA_AUTO, handler,
		                       flags, devname, dev_id);

	if (irq >= IRQ_AMIGA_CIAB)
		return cia_request_irq(&ciab_base, irq - IRQ_AMIGA_CIAB,
		                       handler, flags, devname, dev_id);

	if (irq >= IRQ_AMIGA_CIAA)
		return cia_request_irq(&ciaa_base, irq - IRQ_AMIGA_CIAA,
		                       handler, flags, devname, dev_id);

	if (ami_servers[irq]) {
		if (!(node = new_irq_node()))
			return -ENOMEM;
		node->handler = handler;
		node->flags   = flags;
		node->dev_id  = dev_id;
		node->devname = devname;
		node->next    = NULL;
		amiga_insert_irq(&ami_irq_list[irq], node);
	} else {
		if (!(ami_irq_list[irq]->flags & IRQ_FLG_STD)) {
			if (ami_irq_list[irq]->flags & IRQ_FLG_LOCK) {
				printk("%s: IRQ %d from %s is not replaceable\n",
				       __FUNCTION__, irq, ami_irq_list[irq]->devname);
				return -EBUSY;
			}
			if (!(flags & IRQ_FLG_REPLACE)) {
				printk("%s: %s can't replace IRQ %d from %s\n",
				       __FUNCTION__, devname, irq, ami_irq_list[irq]->devname);
				return -EBUSY;
			}
		}
		ami_irq_list[irq]->handler = handler;
		ami_irq_list[irq]->flags   = flags;
		ami_irq_list[irq]->dev_id  = dev_id;
		ami_irq_list[irq]->devname = devname;
	}

	/* enable the interrupt */
	if (irq < IRQ_AMIGA_PORTS && !ami_ablecount[irq])
		custom.intena = IF_SETCLR | ami_intena_vals[irq];

	return 0;
}

void amiga_free_irq(unsigned int irq, void *dev_id)
{
	if (irq >= AMI_IRQS) {
		printk ("%s: Unknown IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (irq >= IRQ_AMIGA_AUTO)
		sys_free_irq(irq - IRQ_AMIGA_AUTO, dev_id);

	if (irq >= IRQ_AMIGA_CIAB) {
		cia_free_irq(&ciab_base, irq - IRQ_AMIGA_CIAB, dev_id);
		return;
	}

	if (irq >= IRQ_AMIGA_CIAA) {
		cia_free_irq(&ciaa_base, irq - IRQ_AMIGA_CIAA, dev_id);
		return;
	}

	if (ami_servers[irq]) {
		amiga_delete_irq(&ami_irq_list[irq], dev_id);
		/* if server list empty, disable the interrupt */
		if (!ami_irq_list[irq] && irq < IRQ_AMIGA_PORTS)
			custom.intena = ami_intena_vals[irq];
	} else {
		if (ami_irq_list[irq]->dev_id != dev_id)
			printk("%s: removing probably wrong IRQ %d from %s\n",
			       __FUNCTION__, irq, ami_irq_list[irq]->devname);
		ami_irq_list[irq]->handler = ami_badint;
		ami_irq_list[irq]->flags   = IRQ_FLG_STD;
		ami_irq_list[irq]->dev_id  = NULL;
		ami_irq_list[irq]->devname = NULL;
		custom.intena = ami_intena_vals[irq];
	}
}

/*
 * Enable/disable a particular machine specific interrupt source.
 * Note that this may affect other interrupts in case of a shared interrupt.
 * This function should only be called for a _very_ short time to change some
 * internal data, that may not be changed by the interrupt at the same time.
 * ami_(enable|disable)_irq calls may also be nested.
 */

void amiga_enable_irq(unsigned int irq)
{
	if (irq >= AMI_IRQS) {
		printk("%s: Unknown IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (--ami_ablecount[irq])
		return;

	/* No action for auto-vector interrupts */
	if (irq >= IRQ_AMIGA_AUTO){
		printk("%s: Trying to enable auto-vector IRQ %i\n",
		       __FUNCTION__, irq - IRQ_AMIGA_AUTO);
		return;
	}

	if (irq >= IRQ_AMIGA_CIAB) {
		cia_set_irq(&ciab_base, (1 << (irq - IRQ_AMIGA_CIAB)));
		cia_able_irq(&ciab_base, CIA_ICR_SETCLR |
		             (1 << (irq - IRQ_AMIGA_CIAB)));
		return;
	}

	if (irq >= IRQ_AMIGA_CIAA) {
		cia_set_irq(&ciaa_base, (1 << (irq - IRQ_AMIGA_CIAA)));
		cia_able_irq(&ciaa_base, CIA_ICR_SETCLR |
		             (1 << (irq - IRQ_AMIGA_CIAA)));
		return;
	}

	/* enable the interrupt */
	custom.intena = IF_SETCLR | ami_intena_vals[irq];
}

void amiga_disable_irq(unsigned int irq)
{
	if (irq >= AMI_IRQS) {
		printk("%s: Unknown IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (ami_ablecount[irq]++)
		return;

	/* No action for auto-vector interrupts */
	if (irq >= IRQ_AMIGA_AUTO) {
		printk("%s: Trying to disable auto-vector IRQ %i\n",
		       __FUNCTION__, irq - IRQ_AMIGA_AUTO);
		return;
	}

	if (irq >= IRQ_AMIGA_CIAB) {
		cia_able_irq(&ciab_base, 1 << (irq - IRQ_AMIGA_CIAB));
		return;
	}

	if (irq >= IRQ_AMIGA_CIAA) {
		cia_able_irq(&ciaa_base, 1 << (irq - IRQ_AMIGA_CIAA));
		return;
	}

	/* disable the interrupt */
	custom.intena = ami_intena_vals[irq];
}

inline void amiga_do_irq(int irq, struct pt_regs *fp)
{
	kstat.irqs[0][SYS_IRQS + irq]++;
	ami_irq_list[irq]->handler(irq, ami_irq_list[irq]->dev_id, fp);
}

void amiga_do_irq_list(int irq, struct pt_regs *fp, struct irq_server *server)
{
	irq_node_t *node, *slow_nodes;
	unsigned short flags;

	kstat.irqs[0][SYS_IRQS + irq]++;
	if (server->count++)
		server->reentrance = 1;
	/* serve first fast and normal handlers */
	for (node = ami_irq_list[irq];
	     node && (!(node->flags & IRQ_FLG_SLOW));
	     node = node->next)
		node->handler(irq, node->dev_id, fp);
	custom.intreq = ami_intena_vals[irq];
	if (!node) {
		server->count--;
		return;
	}
	save_flags(flags);
	restore_flags((flags & ~0x0700) | (fp->sr & 0x0700));
	/* if slow handlers exists, serve them now */
	slow_nodes = node;
	for (;;) {
		for (; node; node = node->next)
			node->handler(irq, node->dev_id, fp);
		/* if reentrance occurred, serve slow handlers again */
		custom.intena = ami_intena_vals[irq];
		if (!server->reentrance) {
			server->count--;
			custom.intena = IF_SETCLR | ami_intena_vals[irq];
			return;
		}
		server->reentrance = 0;
		custom.intena = IF_SETCLR | ami_intena_vals[irq];
		node = slow_nodes;
	}
}

/*
 * The builtin Amiga hardware interrupt handlers.
 */

static void ami_int1(int irq, void *dev_id, struct pt_regs *fp)
{
	unsigned short ints = custom.intreqr & custom.intenar;

	/* if serial transmit buffer empty, interrupt */
	if (ints & IF_TBE) {
		custom.intreq = IF_TBE;
		amiga_do_irq(IRQ_AMIGA_TBE, fp);
	}

	/* if floppy disk transfer complete, interrupt */
	if (ints & IF_DSKBLK) {
		custom.intreq = IF_DSKBLK;
		amiga_do_irq(IRQ_AMIGA_DSKBLK, fp);
	}

	/* if software interrupt set, interrupt */
	if (ints & IF_SOFT) {
		custom.intreq = IF_SOFT;
		amiga_do_irq(IRQ_AMIGA_SOFT, fp);
	}
}

static void ami_int3(int irq, void *dev_id, struct pt_regs *fp)
{
	unsigned short ints = custom.intreqr & custom.intenar;
	static struct irq_server server = {0, 0};

	/* if a blitter interrupt */
	if (ints & IF_BLIT) {
		custom.intreq = IF_BLIT;
		amiga_do_irq(IRQ_AMIGA_BLIT, fp);
	}

	/* if a copper interrupt */
	if (ints & IF_COPER) {
		custom.intreq = IF_COPER;
		amiga_do_irq(IRQ_AMIGA_COPPER, fp);
	}

	/* if a vertical blank interrupt */
	if (ints & IF_VERTB)
		amiga_do_irq_list(IRQ_AMIGA_VERTB, fp, &server);
}

static void ami_int4(int irq, void *dev_id, struct pt_regs *fp)
{
	unsigned short ints = custom.intreqr & custom.intenar;

	/* if audio 0 interrupt */
	if (ints & IF_AUD0) {
		custom.intreq = IF_AUD0;
		amiga_do_irq(IRQ_AMIGA_AUD0, fp);
	}

	/* if audio 1 interrupt */
	if (ints & IF_AUD1) {
		custom.intreq = IF_AUD1;
		amiga_do_irq(IRQ_AMIGA_AUD1, fp);
	}

	/* if audio 2 interrupt */
	if (ints & IF_AUD2) {
		custom.intreq = IF_AUD2;
		amiga_do_irq(IRQ_AMIGA_AUD2, fp);
	}

	/* if audio 3 interrupt */
	if (ints & IF_AUD3) {
		custom.intreq = IF_AUD3;
		amiga_do_irq(IRQ_AMIGA_AUD3, fp);
	}
}

static void ami_int5(int irq, void *dev_id, struct pt_regs *fp)
{
	unsigned short ints = custom.intreqr & custom.intenar;

	/* if serial receive buffer full interrupt */
	if (ints & IF_RBF) {
		/* acknowledge of IF_RBF must be done by the serial interrupt */
		amiga_do_irq(IRQ_AMIGA_RBF, fp);
	}

	/* if a disk sync interrupt */
	if (ints & IF_DSKSYN) {
		custom.intreq = IF_DSKSYN;
		amiga_do_irq(IRQ_AMIGA_DSKSYN, fp);
	}
}

static void ami_int7(int irq, void *dev_id, struct pt_regs *fp)
{
	panic ("level 7 interrupt received\n");
}

void (*amiga_default_handler[SYS_IRQS])(int, void *, struct pt_regs *) = {
	ami_badint, ami_int1, ami_badint, ami_int3,
	ami_int4, ami_int5, ami_badint, ami_int7
};

int amiga_get_irq_list(char *buf)
{
	int i, len = 0;
	irq_node_t *node;

	for (i = 0; i < AMI_STD_IRQS; i++) {
		if (!(node = ami_irq_list[i]))
			continue;
		if (node->flags & IRQ_FLG_STD)
			continue;
		len += sprintf(buf+len, "ami  %2d: %10u ", i,
		               kstat.irqs[0][SYS_IRQS + i]);
		do {
			if (ami_servers[i]) {
				if (node->flags & IRQ_FLG_FAST)
					len += sprintf(buf+len, "F ");
				else if (node->flags & IRQ_FLG_SLOW)
					len += sprintf(buf+len, "S ");
				else
					len += sprintf(buf+len, "  ");
			} else {
				if (node->flags & IRQ_FLG_LOCK)
					len += sprintf(buf+len, "L ");
				else
					len += sprintf(buf+len, "  ");
			}
			len += sprintf(buf+len, "%s\n", node->devname);
			if ((node = node->next))
				len += sprintf(buf+len, "                    ");
		} while (node);
	}

	len += cia_get_irq_list(&ciaa_base, buf+len);
	len += cia_get_irq_list(&ciab_base, buf+len);
	return len;
}
