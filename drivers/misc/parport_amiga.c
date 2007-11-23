/* Low-level parallel port routines for the Amiga buildin port
 *
 * Author: Joerg Dorchain <dorchain@wirbel.com>
 *
 * This is a complete rewrite of the code, but based heaviy upon the old
 * lp_intern. code.
 *
 * The built-in Amiga parallel port provides one port at a fixed address
 * with 8 bisdirecttional data lines (D0 - D7) and 3 bidirectional status
 * lines (BUSY, POUT, SEL), 1 output control line /STROBE (raised automatically in
 * hardware when the data register is accessed), and 1 input control line
 * /ACK, able to cause an interrupt, but both not directly settable by
 * software.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/parport.h>
#include <asm/setup.h>
#include <asm/amigahw.h>
#include <asm/irq.h>
#include <asm/amigaints.h>

#undef DEBUG
#ifdef DEBUG
#define DPRINTK printk
#else
static inline int DPRINTK() {return 0;}
#endif

static struct parport *this_port = NULL;

static void amiga_write_data(struct parport *p, unsigned char data)
{
DPRINTK("write_data %c\n",data);
	/* Triggers also /STROBE. This behavior cannot be changed */
	ciaa.prb = data;
}

static unsigned char amiga_read_data(struct parport *p)
{
	/* Triggers also /STROBE. This behavior cannot be changed */
	return ciaa.prb;
}

#if 0
static unsigned char control_pc_to_amiga(unsigned char control)
{
	unsigned char ret = 0;

	if (control & PARPORT_CONTROL_DIRECTION) /* XXX: What is this? */
		;
	if (control & PARPORT_CONTROL_INTEN) /* XXX: What is INTEN? */
		;
	if (control & PARPORT_CONTROL_SELECT) /* XXX: What is SELECP? */
		;
	if (control & PARPORT_CONTROL_INIT) /* INITP */
		/* reset connected to cpu reset pin */;
	if (control & PARPORT_CONTROL_AUTOFD) /* AUTOLF */
		/* Not connected */;
	if (control & PARPORT_CONTROL_STROBE) /* Strobe */
		/* Handled only directly by hardware */;
	return ret;
}
#endif

static unsigned char control_amiga_to_pc(unsigned char control)
{
	return PARPORT_CONTROL_INTEN | PARPORT_CONTROL_SELECT |
	      PARPORT_CONTROL_AUTOFD | PARPORT_CONTROL_STROBE;
	/* fake value: interrupt enable, select in, no reset,
	no autolf, no strobe - seems to be closest the wiring diagram */
}

static void amiga_write_control(struct parport *p, unsigned char control)
{
DPRINTK("write_control %02x\n",control);
	/* No implementation possible */
}
	
static unsigned char amiga_read_control( struct parport *p)
{
DPRINTK("read_control \n");
	return control_amiga_to_pc(0);
}

static unsigned char amiga_frob_control( struct parport *p, unsigned char mask, unsigned char val)
{
	unsigned char old;

DPRINTK("frob_control mask %02x, value %02x\n",mask,val);
	old = amiga_read_control(p);
	amiga_write_control(p, (old & ~mask) ^ val);
	return old;
}


static unsigned char status_pc_to_amiga(unsigned char status)
{
	unsigned char ret = 1;

	if (status & PARPORT_STATUS_BUSY) /* Busy */
		ret &= ~1;
	if (status & PARPORT_STATUS_ACK) /* Ack */
		/* handled in hardware */;
	if (status & PARPORT_STATUS_PAPEROUT) /* PaperOut */
		ret |= 2;
	if (status & PARPORT_STATUS_SELECT) /* select */
		ret |= 4;
	if (status & PARPORT_STATUS_ERROR) /* error */
		/* not connected */;
	return ret;
}

static unsigned char status_amiga_to_pc(unsigned char status)
{
	unsigned char ret = PARPORT_STATUS_BUSY | PARPORT_STATUS_ACK | PARPORT_STATUS_ERROR;

	if (status & 1) /* Busy */
		ret &= ~PARPORT_STATUS_BUSY;
	if (status & 2) /* PaperOut */
		ret |= PARPORT_STATUS_PAPEROUT;
	if (status & 4) /* Selected */
		ret |= PARPORT_STATUS_SELECT;
	/* the rest is not connected or handled autonomously in hardware */

	return ret;
}

static void amiga_write_status( struct parport *p, unsigned char status)
{
DPRINTK("write_status %02x\n",status);
	ciab.pra |= (ciab.pra & 0xf8) | status_pc_to_amiga(status);
}

static unsigned char amiga_read_status(struct parport *p)
{
	unsigned char status;

	status = status_amiga_to_pc(ciab.pra & 7);
DPRINTK("read_status %02x\n", status);
	return status;
}

static void amiga_change_mode( struct parport *p, int m)
{
	/* XXX: This port only has one mode, and I am
	not sure about the corresponding PC-style mode*/
}

/* as this ports irq handling is already done, we use a generic funktion */
static void amiga_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	parport_generic_irq(irq, (struct parport *) dev_id, regs);
}


static void amiga_release_resources(struct parport *p)
{
DPRINTK("realease_resources\n");
	if (p->irq != PARPORT_IRQ_NONE)
		free_irq(IRQ_AMIGA_CIAA_FLG, p);
}

static int amiga_claim_resources(struct parport *p)
{
DPRINTK("claim_resources\n");
	return request_irq(IRQ_AMIGA_CIAA_FLG, amiga_interrupt, 0, p->name, p);
}

static void amiga_init_state(struct parport_state *s)
{
	s->u.amiga.data = 0;
	s->u.amiga.datadir = 255;
	s->u.amiga.status = 0;
	s->u.amiga.statusdir = 0;
}

static void amiga_save_state(struct parport *p, struct parport_state *s)
{
	s->u.amiga.data = ciaa.prb;
	s->u.amiga.datadir = ciaa.ddrb;
	s->u.amiga.status = ciab.pra & 7;
	s->u.amiga.statusdir = ciab.ddra & 7;
}

static void amiga_restore_state(struct parport *p, struct parport_state *s)
{
	ciaa.prb = s->u.amiga.data;
	ciaa.ddrb = s->u.amiga.datadir;
	ciab.pra |= (ciab.pra & 0xf8) | s->u.amiga.status;
	ciab.ddra |= (ciab.ddra & 0xf8) | s->u.amiga.statusdir;
}

static void amiga_enable_irq(struct parport *p)
{
	enable_irq(IRQ_AMIGA_CIAA_FLG);
}

static void amiga_disable_irq(struct parport *p)
{
	disable_irq(IRQ_AMIGA_CIAA_FLG);
}

static void amiga_inc_use_count(void)
{
	MOD_INC_USE_COUNT;
}

static void amiga_dec_use_count(void)
{
	MOD_DEC_USE_COUNT;
}

static void amiga_fill_inode(struct inode *inode, int fill)
{
#ifdef MODULE
	if (fill)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
#endif
}

static struct parport_operations pp_amiga_ops = {
	amiga_write_data,
	amiga_read_data,

	amiga_write_control,
	amiga_read_control,
	amiga_frob_control,

	NULL, /* write_econtrol */
	NULL, /* read_econtrol */
	NULL, /* frob_econtrol */

	amiga_write_status,
	amiga_read_status,

	NULL, /* write fifo */
	NULL, /* read fifo */

	amiga_change_mode,


	amiga_release_resources,
	amiga_claim_resources,


	NULL, /* epp_write_data */
	NULL, /* epp_read_data */
	NULL, /* epp_write_addr */
	NULL, /* epp_read_addr */
	NULL, /* epp_check_timeout */

	NULL, /* epp_write_block */
	NULL, /* epp_read_block */

	NULL, /* ecp_write_block */
	NULL, /* ecp_read_block */

	amiga_init_state,
	amiga_save_state,
	amiga_restore_state,

	amiga_enable_irq,
	amiga_disable_irq,
	amiga_interrupt, 

	amiga_inc_use_count,
	amiga_dec_use_count,
	amiga_fill_inode
};

/* ----------- Initialisation code --------------------------------- */

__initfunc(int parport_amiga_init(void))
{
	struct parport *p;

	if (MACH_IS_AMIGA && AMIGAHW_PRESENT(AMI_PARALLEL)) {
		ciaa.ddrb = 0xff;
		ciab.ddra &= 0xf8;
		if (!(p = parport_register_port((unsigned long)&ciaa.prb,
					IRQ_AMIGA_CIAA_FLG, PARPORT_DMA_NONE,
					&pp_amiga_ops)))
			return 0;
		this_port = p;
		printk(KERN_INFO "%s: Amiga built-in port using irq\n", p->name);
		/* XXX: set operating mode */
		parport_proc_register(p);
		p->flags |= PARPORT_FLAG_COMA;

		if (parport_probe_hook)
			(*parport_probe_hook)(p);
		return 1;

	}
	return 0;
}

#ifdef MODULE

MODULE_AUTHOR("Joerg Dorchain");
MODULE_DESCRIPTION("Parport Driver for Amiga builtin Port");
MODULE_SUPPORTED_DEVICE("Amiga builtin Parallel Port");

int init_module(void)
{
	return ! parport_amiga_init();
}

void cleanup_module(void)
{
	if (!(this_port->flags & PARPORT_FLAG_COMA))
		parport_quiesce(this_port);
	parport_proc_unregister(this_port);
	parport_unregister_port(this_port);
}
#endif


