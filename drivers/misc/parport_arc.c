/* $Id$
 * Parallel-port routines for ARC onboard hardware.
 *
 * Author: Phil Blundell <pjb27@cam.ac.uk>
 */

#include <linux/tasks.h>

#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>

#include <linux/parport.h>

#include <linux/arch/oldlatches.h>

#define DATA_LATCH    0x3350010

/* ARC can't read from the data latch, so we must use a soft copy. */
static unsigned int data_copy;

static void arc_write_data(struct parport *p, unsigned int data)
{
	data_copy = data;
	outb(data, DATA_LATCH);
}

static unsigned int arc_read_data(struct parport *p)
{
	return data_copy;
}

static struct parport_operations arc_ops = 
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
	
	NULL, /* epp_write_block */
	NULL, /* epp_read_block */

	NULL, /* ecp_write_block */
	NULL, /* epp_write_block */
	
	arc_save_state,
	arc_restore_state,

	arc_enable_irq,
	arc_disable_irq,
	arc_examine_irq 
};
