/* Low-level parallel port routines for the Atari builtin port
 *
 * Author: Andreas Schwab <schwab@issan.informatik.uni-dortmund.de>
 *
 * Based on parport_amiga.c.
 *
 * The built-in Atari parallel port provides one port at a fixed address
 * with 8 output data lines (D0 - D7), 1 output control line (STROBE)
 * and 1 input status line (BUSY) able to cause an interrupt.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/parport.h>
#include <asm/setup.h>
#include <asm/atarihw.h>
#include <asm/irq.h>
#include <asm/atariints.h>

static struct parport *this_port = NULL;

static unsigned char
parport_atari_read_data(struct parport *p)
{
	unsigned long flags;
	unsigned char data;

	save_flags(flags);
	cli();
	sound_ym.rd_data_reg_sel = 15;
	data = sound_ym.rd_data_reg_sel;
	restore_flags(flags);
	return data;
}

static void
parport_atari_write_data(struct parport *p, unsigned char data)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	sound_ym.rd_data_reg_sel = 15;
	sound_ym.wd_data = data;
	restore_flags(flags);
}

static unsigned char
parport_atari_read_control(struct parport *p)
{
	unsigned long flags;
	unsigned char control = 0;

	save_flags(flags);
	cli();
	sound_ym.rd_data_reg_sel = 14;
	if (!(sound_ym.rd_data_reg_sel & (1 << 5)))
		control = PARPORT_CONTROL_STROBE;
	restore_flags(flags);
	return control;
}

static void
parport_atari_write_control(struct parport *p, unsigned char control)
{
	unsigned long flags;

	save_flags(flags);
	cli();
	sound_ym.rd_data_reg_sel = 14;
	if (control & PARPORT_CONTROL_STROBE)
		sound_ym.wd_data = sound_ym.rd_data_reg_sel & ~(1 << 5);
	else
		sound_ym.wd_data = sound_ym.rd_data_reg_sel | (1 << 5);
	restore_flags(flags);
}

static unsigned char
parport_atari_frob_control(struct parport *p, unsigned char mask,
			   unsigned char val)
{
	unsigned char old = parport_atari_read_control(p);
	parport_atari_write_control(p, (old & ~mask) ^ val);
	return old;
}

static unsigned char
parport_atari_read_status(struct parport *p)
{
	return ((mfp.par_dt_reg & 1 ? 0 : PARPORT_STATUS_BUSY) |
		PARPORT_STATUS_SELECT | PARPORT_STATUS_ERROR);
}

static void
parport_atari_write_status(struct parport *p, unsigned char status)
{
}

static void
parport_atari_init_state(struct parport_state *s)
{
}

static void
parport_atari_save_state(struct parport *p, struct parport_state *s)
{
}

static void
parport_atari_restore_state(struct parport *p, struct parport_state *s)
{
}

static void
parport_atari_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	parport_generic_irq(irq, (struct parport *) dev_id, regs);
}

static void
parport_atari_inc_use_count(void)
{
	MOD_INC_USE_COUNT;
}

static void
parport_atari_dec_use_count(void)
{
	MOD_DEC_USE_COUNT;
}

static void
parport_atari_fill_inode(struct inode *inode, int fill)
{
#ifdef MODULE
	if (fill)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
#endif
}

static struct parport_operations parport_atari_ops = {
	parport_atari_write_data,
	parport_atari_read_data,

	parport_atari_write_control,
	parport_atari_read_control,
	parport_atari_frob_control,

	NULL, /* write_econtrol */
	NULL, /* read_econtrol */
	NULL, /* frob_econtrol */

	parport_atari_write_status,
	parport_atari_read_status,

	NULL, /* write fifo */
	NULL, /* read fifo */

	NULL, /* change_mode */

	NULL, /* epp_write_data */
	NULL, /* epp_read_data */
	NULL, /* epp_write_addr */
	NULL, /* epp_read_addr */
	NULL, /* epp_check_timeout */

	NULL, /* epp_write_block */
	NULL, /* epp_read_block */

	NULL, /* ecp_write_block */
	NULL, /* ecp_read_block */

	parport_atari_init_state,
	parport_atari_save_state,
	parport_atari_restore_state,

	NULL, /* enable_irq */
	NULL, /* disable_irq */
	parport_atari_interrupt,

	parport_atari_inc_use_count,
	parport_atari_dec_use_count,
	parport_atari_fill_inode
};


int __init
parport_atari_init(void)
{
	struct parport *p;
	unsigned long flags;

	if (MACH_IS_ATARI) {
		save_flags(flags);
		cli();
		/* Soundchip port A/B as output. */
		sound_ym.rd_data_reg_sel = 7;
		sound_ym.wd_data = (sound_ym.rd_data_reg_sel & 0x3f) | 0xc0;
		/* STROBE high. */
		sound_ym.rd_data_reg_sel = 14;
		sound_ym.wd_data = sound_ym.rd_data_reg_sel | (1 << 5);
		restore_flags(flags);
		/* MFP port I0 as input. */
		mfp.data_dir &= ~1;
		/* MFP port I0 interrupt on high->low edge. */
		mfp.active_edge &= ~1;
		p = parport_register_port((unsigned long)&sound_ym.wd_data,
					  IRQ_MFP_BUSY, PARPORT_DMA_NONE,
					  &parport_atari_ops);
		if (!p)
			return 0;
		if (request_irq(IRQ_MFP_BUSY, parport_atari_interrupt,
				IRQ_TYPE_SLOW, p->name, p)) {
			parport_unregister_port (p);
			return 0;
		}

		this_port = p;
		printk(KERN_INFO "%s: Atari built-in port using irq\n", p->name);
		parport_proc_register(p);

		if (parport_probe_hook)
			(*parport_probe_hook)(p);

		parport_announce_port (p);

		return 1;
	}
	return 0;
}

#ifdef MODULE

MODULE_AUTHOR("Andreas Schwab");
MODULE_DESCRIPTION("Parport Driver for Atari builtin Port");
MODULE_SUPPORTED_DEVICE("Atari builtin Parallel Port");

int
init_module(void)
{
	return parport_atari_init() ? 0 : -ENODEV;
}

void
cleanup_module(void)
{
	if (p->irq != PARPORT_IRQ_NONE)
		free_irq(IRQ_MFP_BUSY, p);
	parport_proc_unregister(this_port);
	parport_unregister_port(this_port);
}
#endif
