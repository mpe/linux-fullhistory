/* $Id: parport_ax.c,v 1.14 1998/11/16 04:48:02 davem Exp $
 * Parallel-port routines for Sun Ultra/AX architecture
 * 
 * Author: Eddie C. Dost <ecd@skynet.be>
 *
 * based on work by:
 *          Phil Blundell <Philip.Blundell@pobox.com>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *	    Jose Renau <renau@acm.org>
 *          David Campbell <campbell@tirian.che.curtin.edu.au>
 *          Grant Guenther <grant@torque.net>
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>
#include <linux/init.h>

#include <linux/parport.h>

#include <asm/ptrace.h>
#include <linux/interrupt.h>

#include <asm/io.h>
#include <asm/ebus.h>
#include <asm/ns87303.h>
#include <asm/irq.h>


/*
 * Define this if you have Devices which don't support short
 * host read/write cycles.
 */
#undef HAVE_SLOW_DEVICES


#define DATA		0x00
#define STATUS		0x01
#define CONTROL		0x02
#define EPPADDR		0x03
#define EPPDATA		0x04

#define CFIFO		0x400
#define DFIFO		0x400
#define TFIFO		0x400
#define CONFIGA		0x400
#define CONFIGB		0x401
#define ECONTROL	0x402

static void parport_ax_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	parport_generic_irq(irq, (struct parport *) dev_id, regs);
}

void
parport_ax_write_epp(struct parport *p, unsigned char d)
{
	outb(d, p->base + EPPDATA);
}

unsigned char
parport_ax_read_epp(struct parport *p)
{
	return inb(p->base + EPPDATA);
}

void
parport_ax_write_epp_addr(struct parport *p, unsigned char d)
{
	outb(d, p->base + EPPADDR);
}

unsigned char
parport_ax_read_epp_addr(struct parport *p)
{
	return inb(p->base + EPPADDR);
}

int parport_ax_epp_clear_timeout(struct parport *pb);

int 
parport_ax_check_epp_timeout(struct parport *p)
{
	if (!(inb(p->base+STATUS) & 1))
		return 0;
	parport_ax_epp_clear_timeout(p);
	return 1;
}

unsigned char
parport_ax_read_configb(struct parport *p)
{
	return inb(p->base + CONFIGB);
}

void
parport_ax_write_data(struct parport *p, unsigned char d)
{
	outb(d, p->base + DATA);
}

unsigned char
parport_ax_read_data(struct parport *p)
{
	return inb(p->base + DATA);
}

void
parport_ax_write_control(struct parport *p, unsigned char d)
{
	outb(d, p->base + CONTROL);
}

unsigned char
parport_ax_read_control(struct parport *p)
{
	return inb(p->base + CONTROL);
}

unsigned char
parport_ax_frob_control(struct parport *p, unsigned char mask,  unsigned char val)
{
	unsigned char old = inb(p->base + CONTROL);
	outb(((old & ~mask) ^ val), p->base + CONTROL);
	return old;
}

void
parport_ax_write_status(struct parport *p, unsigned char d)
{
	outb(d, p->base + STATUS);
}

unsigned char
parport_ax_read_status(struct parport *p)
{
	return inb(p->base + STATUS);
}

void
parport_ax_write_econtrol(struct parport *p, unsigned char d)
{
	outb(d, p->base + ECONTROL);
}

unsigned char
parport_ax_read_econtrol(struct parport *p)
{
	return inb(p->base + ECONTROL);
}

unsigned char
parport_ax_frob_econtrol(struct parport *p, unsigned char mask, unsigned char val)
{
	unsigned char old = inb(p->base + ECONTROL);
	outb(((old & ~mask) ^ val), p->base + ECONTROL);
	return old;
}

void
parport_ax_change_mode(struct parport *p, int m)
{
	parport_ax_frob_econtrol(p, 0xe0, m << 5);
}

void
parport_ax_write_fifo(struct parport *p, unsigned char v)
{
	outb(v, p->base + DFIFO);
}

unsigned char
parport_ax_read_fifo(struct parport *p)
{
	return inb(p->base + DFIFO);
}

void
parport_ax_disable_irq(struct parport *p)
{
	struct linux_ebus_dma *dma = p->private_data;
	unsigned int dcsr;

	dcsr = readl((unsigned long)&dma->dcsr);
	dcsr &= ~(EBUS_DCSR_INT_EN);
	writel(dcsr, (unsigned long)&dma->dcsr);
}

void
parport_ax_enable_irq(struct parport *p)
{
	struct linux_ebus_dma *dma = p->private_data;
	unsigned int dcsr;

	dcsr = readl((unsigned long)&dma->dcsr);
	dcsr |= EBUS_DCSR_INT_EN;
	writel(dcsr, (unsigned long)&dma->dcsr);
}

void
parport_ax_release_resources(struct parport *p)
{
	if (p->irq != PARPORT_IRQ_NONE) {
		parport_ax_disable_irq(p);
		free_irq(p->irq, p);
	}
	release_region(p->base, p->size);
	if (p->modes & PARPORT_MODE_PCECR)
		release_region(p->base+0x400, 3);
	release_region((unsigned long)p->private_data,
		       sizeof(struct linux_ebus_dma));
}

int
parport_ax_claim_resources(struct parport *p)
{
	/* FIXME check that resources are free */
	int err;

	if (p->irq != PARPORT_IRQ_NONE)
		if ((err = request_irq(p->irq, parport_ax_interrupt,
				       0, p->name, p)) != 0)
			return err;
		else
			parport_ax_enable_irq(p);

	request_region(p->base, p->size, p->name);
	if (p->modes & PARPORT_MODE_PCECR)
		request_region(p->base+0x400, 3, p->name);
	request_region((unsigned long)p->private_data,
		       sizeof(struct linux_ebus_dma), p->name);
	return 0;
}

void
parport_ax_init_state(struct parport_state *s)
{
	s->u.pc.ctr = 0xc;
	s->u.pc.ecr = 0x0;
}

void
parport_ax_save_state(struct parport *p, struct parport_state *s)
{
	s->u.pc.ctr = parport_ax_read_control(p);
	s->u.pc.ecr = parport_ax_read_econtrol(p);
}

void
parport_ax_restore_state(struct parport *p, struct parport_state *s)
{
	parport_ax_write_control(p, s->u.pc.ctr);
	parport_ax_write_econtrol(p, s->u.pc.ecr);
}

size_t
parport_ax_epp_read_block(struct parport *p, void *buf, size_t length)
{
	return 0; /* FIXME */
}

size_t
parport_ax_epp_write_block(struct parport *p, void *buf, size_t length)
{
	return 0; /* FIXME */
}

int
parport_ax_ecp_read_block(struct parport *p, void *buf, size_t length,
			  void (*fn)(struct parport *, void *, size_t),
			  void *handle)
{
	return 0; /* FIXME */
}

int
parport_ax_ecp_write_block(struct parport *p, void *buf, size_t length,
			   void (*fn)(struct parport *, void *, size_t),
			   void *handle)
{
	return 0; /* FIXME */
}

void
parport_ax_inc_use_count(void)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

void
parport_ax_dec_use_count(void)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}

static void parport_ax_fill_inode(struct inode *inode, int fill)
{
#ifdef MODULE
	if (fill)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
#endif
}

static struct parport_operations parport_ax_ops = 
{
	parport_ax_write_data,
	parport_ax_read_data,

	parport_ax_write_control,
	parport_ax_read_control,
	parport_ax_frob_control,

	parport_ax_write_econtrol,
	parport_ax_read_econtrol,
	parport_ax_frob_econtrol,

	parport_ax_write_status,
	parport_ax_read_status,

	parport_ax_write_fifo,
	parport_ax_read_fifo,
	
	parport_ax_change_mode,
	
	parport_ax_release_resources,
	parport_ax_claim_resources,
	
	parport_ax_write_epp,
	parport_ax_read_epp,
	parport_ax_write_epp_addr,
	parport_ax_read_epp_addr,
	parport_ax_check_epp_timeout,

	parport_ax_epp_write_block,
	parport_ax_epp_read_block,

	parport_ax_ecp_write_block,
	parport_ax_ecp_read_block,
	
	parport_ax_init_state,
	parport_ax_save_state,
	parport_ax_restore_state,

	parport_ax_enable_irq,
	parport_ax_disable_irq,
	parport_ax_interrupt,

	parport_ax_inc_use_count,
	parport_ax_dec_use_count,
	parport_ax_fill_inode
};


/******************************************************
 *  MODE detection section:
 */

/*
 * Clear TIMEOUT BIT in EPP MODE
 */
int parport_ax_epp_clear_timeout(struct parport *pb)
{
	unsigned char r;

	if (!(parport_ax_read_status(pb) & 0x01))
		return 1;

	/* To clear timeout some chips require double read */
	parport_ax_read_status(pb);
	r = parport_ax_read_status(pb);
	parport_ax_write_status(pb, r | 0x01); /* Some reset by writing 1 */
	parport_ax_write_status(pb, r & 0xfe); /* Others by writing 0 */
	r = parport_ax_read_status(pb);

	return !(r & 0x01);
}

/* Check for ECP
 *
 * Old style XT ports alias io ports every 0x400, hence accessing ECONTROL
 * on these cards actually accesses the CTR.
 *
 * Modern cards don't do this but reading from ECONTROL will return 0xff
 * regardless of what is written here if the card does NOT support
 * ECP.
 *
 * We will write 0x2c to ECONTROL and 0xcc to CTR since both of these
 * values are "safe" on the CTR since bits 6-7 of CTR are unused.
 */
static int parport_ECR_present(struct parport *pb)
{
	unsigned int r;
	unsigned char octr = pb->ops->read_control(pb), 
	  oecr = pb->ops->read_econtrol(pb);

	r = pb->ops->read_control(pb);	
	if ((pb->ops->read_econtrol(pb) & 0x3) == (r & 0x3)) {
		pb->ops->write_control(pb, r ^ 0x2 ); /* Toggle bit 1 */

		r = pb->ops->read_control(pb);	
		if ((pb->ops->read_econtrol(pb) & 0x2) == (r & 0x2)) {
			pb->ops->write_control(pb, octr);
			return 0; /* Sure that no ECONTROL register exists */
		}
	}
	
	if ((pb->ops->read_econtrol(pb) & 0x3 ) != 0x1)
		return 0;

	pb->ops->write_econtrol(pb, 0x34);
	if (pb->ops->read_econtrol(pb) != 0x35)
		return 0;

	pb->ops->write_econtrol(pb, oecr);
	pb->ops->write_control(pb, octr);
	
	return PARPORT_MODE_PCECR;
}

static int parport_ECP_supported(struct parport *pb)
{
	int i;
	unsigned char oecr = pb->ops->read_econtrol(pb);
	
	/* If there is no ECONTROL, we have no hope of supporting ECP. */
	if (!(pb->modes & PARPORT_MODE_PCECR))
		return 0;

	/*
	 * Using LGS chipset it uses ECONTROL register, but
	 * it doesn't support ECP or FIFO MODE
	 */
	
	pb->ops->write_econtrol(pb, 0xc0); /* TEST FIFO */
	for (i=0; i < 1024 && (pb->ops->read_econtrol(pb) & 0x01); i++)
		pb->ops->write_fifo(pb, 0xaa);

	pb->ops->write_econtrol(pb, oecr);
	return (i == 1024) ? 0 : PARPORT_MODE_PCECP;
}

/* Detect PS/2 support.
 *
 * Bit 5 (0x20) sets the PS/2 data direction; setting this high
 * allows us to read data from the data lines.  In theory we would get back
 * 0xff but any peripheral attached to the port may drag some or all of the
 * lines down to zero.  So if we get back anything that isn't the contents
 * of the data register we deem PS/2 support to be present. 
 *
 * Some SPP ports have "half PS/2" ability - you can't turn off the line
 * drivers, but an external peripheral with sufficiently beefy drivers of
 * its own can overpower them and assert its own levels onto the bus, from
 * where they can then be read back as normal.  Ports with this property
 * and the right type of device attached are likely to fail the SPP test,
 * (as they will appear to have stuck bits) and so the fact that they might
 * be misdetected here is rather academic. 
 */

static int parport_PS2_supported(struct parport *pb)
{
	int ok = 0;
	unsigned char octr = pb->ops->read_control(pb);
  
	pb->ops->write_control(pb, octr | 0x20);  /* try to tri-state buffer */
	
	pb->ops->write_data(pb, 0x55);
	if (pb->ops->read_data(pb) != 0x55) ok++;

	pb->ops->write_data(pb, 0xaa);
	if (pb->ops->read_data(pb) != 0xaa) ok++;
	
	pb->ops->write_control(pb, octr);          /* cancel input mode */

	return ok ? PARPORT_MODE_PCPS2 : 0;
}

static int parport_ECPPS2_supported(struct parport *pb)
{
	int mode;
	unsigned char oecr = pb->ops->read_econtrol(pb);

	if (!(pb->modes & PARPORT_MODE_PCECR))
		return 0;
	
	pb->ops->write_econtrol(pb, 0x20);
	
	mode = parport_PS2_supported(pb);

	pb->ops->write_econtrol(pb, oecr);
	return mode ? PARPORT_MODE_PCECPPS2 : 0;
}

#define printmode(x)					\
{							\
	if (p->modes & PARPORT_MODE_PC##x) {		\
		printk("%s%s", f ? "," : "", #x);	\
		f++;					\
	}						\
}

int
init_one_port(struct linux_ebus_device *dev)
{
	struct parport tmpport, *p;
	unsigned long base;
	unsigned long config;
	unsigned char tmp;
	int irq, dma;

	/* Pointer to NS87303 Configuration Registers */
	config = dev->base_address[1];

	/* Setup temporary access to Device operations */
	tmpport.base = dev->base_address[0];
	tmpport.ops = &parport_ax_ops;

	/* Enable ECP mode, set bit 2 of the CTR first */
	tmpport.ops->write_control(&tmpport, 0x04);
	tmp = ns87303_readb(config, PCR);
	tmp |= (PCR_EPP_IEEE | PCR_ECP_ENABLE | PCR_ECP_CLK_ENA);
	ns87303_writeb(config, PCR, tmp);

	/* LPT CTR bit 5 controls direction of parallel port */
	tmp = ns87303_readb(config, PTR);
	tmp |= PTR_LPT_REG_DIR;
	ns87303_writeb(config, PTR, tmp);

	/* Configure IRQ to Push Pull, Level Low */
	tmp = ns87303_readb(config, PCR);
	tmp &= ~(PCR_IRQ_ODRAIN);
	tmp |= PCR_IRQ_POLAR;
	ns87303_writeb(config, PCR, tmp);

#ifndef HAVE_SLOW_DEVICES
	/* Enable Zero Wait State for ECP */
	tmp = ns87303_readb(config, FCR);
	tmp |= FCR_ZWS_ENA;
	ns87303_writeb(config, FCR, tmp);
#endif

	/*
	 * Now continue initializing the port
	 */
	base = dev->base_address[0];
	irq = dev->irqs[0];
	dma = PARPORT_DMA_AUTO;

	if (!(p = parport_register_port(base, irq, dma, &parport_ax_ops)))
		return 0;

	/* Save away pointer to our EBus DMA */
	p->private_data = (void *)dev->base_address[2];

	p->modes = PARPORT_MODE_PCSPP | parport_PS2_supported(p);
	if (!check_region(p->base + 0x400, 3)) {
		p->modes |= parport_ECR_present(p);
		p->modes |= parport_ECP_supported(p);
		p->modes |= parport_ECPPS2_supported(p);
	}
	p->size = 3;

	if (p->dma == PARPORT_DMA_AUTO)
		p->dma = (p->modes & PARPORT_MODE_PCECP) ? 0 : PARPORT_DMA_NONE;

	printk(KERN_INFO "%s: PC-style at 0x%lx", p->name, p->base);
	if (p->irq != PARPORT_IRQ_NONE)
		printk(", irq %s", __irq_itoa(p->irq));
	if (p->dma != PARPORT_DMA_NONE)
		printk(", dma %d", p->dma);
	printk(" [");
	{
		int f = 0;
		printmode(SPP);
		printmode(PS2);
		printmode(ECP);
		printmode(ECPPS2);
	}
	printk("]\n");
	parport_proc_register(p);
	p->flags |= PARPORT_FLAG_COMA;

	p->ops->write_control(p, 0x0c);
	p->ops->write_data(p, 0);

	if (parport_probe_hook)
		(*parport_probe_hook)(p);

	return 1;
}

EXPORT_NO_SYMBOLS;

#ifdef MODULE
int init_module(void)
#else
__initfunc(int parport_ax_init(void))
#endif
{
	struct linux_ebus *ebus;
	struct linux_ebus_device *edev;
	int count = 0;

	for_each_ebus(ebus) {
		for_each_ebusdev(edev, ebus) {
			if (!strcmp(edev->prom_name, "ecpp"))
				count += init_one_port(edev);
		}
	}
	return count ? 0 : -ENODEV;
}

#ifdef MODULE
void
cleanup_module(void)
{
	struct parport *p = parport_enumerate(), *tmp;
	while (p) {
		tmp = p->next;
		if (p->modes & PARPORT_MODE_PCSPP) { 
			if (!(p->flags & PARPORT_FLAG_COMA)) 
				parport_quiesce(p);
			parport_proc_unregister(p);
			parport_unregister_port(p);
		}
		p = tmp;
	}
}
#endif
