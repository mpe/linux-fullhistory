/* Low-level parallel port routines for Archimedes onboard hardware
 *
 * Author: Phil Blundell <Philip.Blundell@pobox.com>
 */

/* This driver is for the parallel port hardware found on Acorn's old
 * range of Archimedes machines.  The A5000 and newer systems have PC-style
 * I/O hardware and should use the parport_pc driver instead.
 *
 * The Acorn printer port hardware is very simple.  There is a single 8-bit
 * write-only latch for the data port and control/status bits are handled
 * with various auxilliary input and output lines.  The port is not
 * bidirectional, does not support any modes other than SPP, and has only
 * a subset of the standard printer control lines connected.
 */

#include <linux/tasks.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/parport.h>

#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/arch/oldlatches.h>
#include <asm/arch/irqs.h>

#define DATA_LATCH    0x3350010

/* ARC can't read from the data latch, so we must use a soft copy. */
static unsigned char data_copy;

static void arc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	parport_generic_irq(irq, (struct parport *) dev_id, regs);
}

static void arc_write_data(struct parport *p, unsigned char data)
{
	data_copy = data;
	outb(data, DATA_LATCH);
}

static unsigned char arc_read_data(struct parport *p)
{
	return data_copy;
}

static void arc_inc_use_count(void)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

static void arc_dec_use_count(void)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}

static void arc_fill_inode(struct inode *inode, int fill)
{
#ifdef MODULE
	if (fill)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
#endif
}

static struct parport_operations parport_arc_ops = 
{
	arc_write_data,
	arc_read_data,

	arc_write_control,
	arc_read_control,
	arc_frob_control,

	NULL, /* write_econtrol */
	NULL, /* read_econtrol */
	NULL, /* frob_econtrol */

	arc_write_status,
	arc_read_status,

	NULL, /* write_fifo */
	NULL, /* read_fifo */
	
	NULL, /* change_mode */
	
	arc_release_resources,
	arc_claim_resources,
	
	NULL, /* epp_write_data */
	NULL, /* epp_read_data */
	NULL, /* epp_write_addr */
	NULL, /* epp_read_addr */
	NULL, /* epp_check_timeout */

	NULL, /* epp_write_block */
	NULL, /* epp_read_block */

	NULL, /* ecp_write_block */
	NULL, /* epp_write_block */
	
	arc_init_state,
	arc_save_state,
	arc_restore_state,

	arc_enable_irq,
	arc_disable_irq,
	arc_interrupt,

	arc_inc_use_count,
	arc_dec_use_count,
	arc_fill_inode
};

/* --- Initialisation code -------------------------------- */

int parport_arc_init(void)
{
	/* Archimedes hardware provides only one port, at a fixed address */
	struct parport *p;

	if (check_region(DATA_LATCH, 4))
		return 0;
	
       	if (!(p = parport_register_port(base, IRQ_PRINTERACK, 
					PARPORT_DMA_NONE, &parport_arc_ops))) 
		return 0;

	p->modes = PARPORT_MODE_ARCSPP;
	p->size = 4;

	printk(KERN_INFO "%s: Archimedes on-board port, using irq %d\n",
	       p->irq);
	parport_proc_register(p);
	p->flags |= PARPORT_FLAG_COMA;

	if (parport_probe_hook)
		(*parport_probe_hook)(p);

	return 1;
}
