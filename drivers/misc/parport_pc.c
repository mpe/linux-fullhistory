/* Low-level parallel-port routines for 8255-based PC-style hardware.
 * 
 * Authors: Phil Blundell <Philip.Blundell@pobox.com>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *	    Jose Renau <renau@acm.org>
 *          David Campbell <campbell@torque.net>
 *          Andrea Arcangeli
 *
 * based on work by Grant Guenther <grant@torque.net> and Phil Blundell.
 *
 * Cleaned up include files - Russell King <linux@arm.uk.linux.org>
 */

/* This driver should work with any hardware that is broadly compatible
 * with that in the IBM PC.  This applies to the majority of integrated
 * I/O chipsets that are commonly available.  The expected register
 * layout is:
 *
 *	base+0		data
 *	base+1		status
 *	base+2		control
 *
 * In addition, there are some optional registers:
 *
 *	base+3		EPP address
 *	base+4		EPP data
 *	base+0x400	ECP config A
 *	base+0x401	ECP config B
 *	base+0x402	ECP control
 *
 * All registers are 8 bits wide and read/write.  If your hardware differs
 * only in register addresses (eg because your registers are on 32-bit
 * word boundaries) then you can alter the constants in parport_pc.h to
 * accomodate this.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>

#include <asm/io.h>

#include <linux/parport.h>
#include <linux/parport_pc.h>

/* Maximum number of ports to support.  It is useless to set this greater
   than PARPORT_MAX (in <linux/parport.h>).  */
#define PARPORT_PC_MAX_PORTS  8

static int user_specified __initdata = 0;

static void parport_pc_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	parport_generic_irq(irq, (struct parport *) dev_id, regs);
}

void parport_pc_write_epp(struct parport *p, unsigned char d)
{
	outb(d, EPPDATA(p));
}

unsigned char parport_pc_read_epp(struct parport *p)
{
	return inb(EPPDATA(p));
}

void parport_pc_write_epp_addr(struct parport *p, unsigned char d)
{
	outb(d, EPPADDR(p));
}

unsigned char parport_pc_read_epp_addr(struct parport *p)
{
	return inb(EPPADDR(p));
}

int parport_pc_check_epp_timeout(struct parport *p)
{
	if (!(inb(STATUS(p)) & 1))
		return 0;
	parport_pc_epp_clear_timeout(p);
	return 1;
}

unsigned char parport_pc_read_configb(struct parport *p)
{
	return inb(CONFIGB(p));
}

void parport_pc_write_data(struct parport *p, unsigned char d)
{
	outb(d, DATA(p));
}

unsigned char parport_pc_read_data(struct parport *p)
{
	return inb(DATA(p));
}

void parport_pc_write_control(struct parport *p, unsigned char d)
{
	struct parport_pc_private *priv = p->private_data;
	priv->ctr = d;/* update soft copy */
	outb(d, CONTROL(p));
}

unsigned char parport_pc_read_control(struct parport *p)
{
	struct parport_pc_private *priv = p->private_data;
	return priv->ctr;
}

unsigned char parport_pc_frob_control(struct parport *p, unsigned char mask,  unsigned char val)
{
	struct parport_pc_private *priv = p->private_data;
	unsigned char ctr = priv->ctr;
	ctr = (ctr & ~mask) ^ val;
	outb (ctr, CONTROL(p));
	return priv->ctr = ctr; /* update soft copy */
}

void parport_pc_write_status(struct parport *p, unsigned char d)
{
	outb(d, STATUS(p));
}

unsigned char parport_pc_read_status(struct parport *p)
{
	return inb(STATUS(p));
}

void parport_pc_write_econtrol(struct parport *p, unsigned char d)
{
	outb(d, ECONTROL(p));
}

unsigned char parport_pc_read_econtrol(struct parport *p)
{
	return inb(ECONTROL(p));
}

unsigned char parport_pc_frob_econtrol(struct parport *p, unsigned char mask,  unsigned char val)
{
	unsigned char old = inb(ECONTROL(p));
	outb(((old & ~mask) ^ val), ECONTROL(p));
	return old;
}

void parport_pc_change_mode(struct parport *p, int m)
{
	/* FIXME */
}

void parport_pc_write_fifo(struct parport *p, unsigned char v)
{
	outb (v, CONFIGA(p));
}

unsigned char parport_pc_read_fifo(struct parport *p)
{
	return inb (CONFIGA(p));
}

void parport_pc_disable_irq(struct parport *p)
{
	parport_pc_frob_control(p, 0x10, 0);
}

void parport_pc_enable_irq(struct parport *p)
{
	parport_pc_frob_control(p, 0x10, 0x10);
}

void parport_pc_release_resources(struct parport *p)
{
	if (p->irq != PARPORT_IRQ_NONE)
		free_irq(p->irq, p);
	release_region(p->base, p->size);
	if (p->modes & PARPORT_MODE_PCECR)
		release_region(p->base_hi, 3);
}

int parport_pc_claim_resources(struct parport *p)
{
	int err;
	if (p->irq != PARPORT_IRQ_NONE)
		if ((err = request_irq(p->irq, parport_pc_interrupt,
				       0, p->name, p)) != 0)
			return err;
	request_region(p->base, p->size, p->name);
	if (p->modes & PARPORT_MODE_PCECR)
		request_region(p->base_hi, 3, p->name);
	return 0;
}

void parport_pc_init_state(struct parport_state *s)
{
	s->u.pc.ctr = 0xc;
	s->u.pc.ecr = 0x0;
}

void parport_pc_save_state(struct parport *p, struct parport_state *s)
{
	s->u.pc.ctr = parport_pc_read_control(p);
	if (p->modes & PARPORT_MODE_PCECR)
		s->u.pc.ecr = parport_pc_read_econtrol(p);
}

void parport_pc_restore_state(struct parport *p, struct parport_state *s)
{
	parport_pc_write_control(p, s->u.pc.ctr);
	if (p->modes & PARPORT_MODE_PCECR)
		parport_pc_write_econtrol(p, s->u.pc.ecr);
}

size_t parport_pc_epp_read_block(struct parport *p, void *buf, size_t length)
{
	size_t got = 0;
	for (; got < length; got++) {
		*((char*)buf)++ = inb (EPPDATA(p));
		if (inb (STATUS(p)) & 0x01)
			break;
	}
	return got;
}

size_t parport_pc_epp_write_block(struct parport *p, void *buf, size_t length)
{
	size_t written = 0;
	for (; written < length; written++) {
		outb (*((char*)buf)++, EPPDATA(p));
		if (inb (STATUS(p)) & 0x01)
			break;
	}
	return written;
}

int parport_pc_ecp_read_block(struct parport *p, void *buf, size_t length, void (*fn)(struct parport *, void *, size_t), void *handle)
{
	return -ENOSYS; /* FIXME */
}

int parport_pc_ecp_write_block(struct parport *p, void *buf, size_t length, void (*fn)(struct parport *, void *, size_t), void *handle)
{
	return -ENOSYS; /* FIXME */
}

void parport_pc_inc_use_count(void)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

void parport_pc_dec_use_count(void)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}

static void parport_pc_fill_inode(struct inode *inode, int fill)
{
#ifdef MODULE
	if (fill)
		MOD_INC_USE_COUNT;
	else
		MOD_DEC_USE_COUNT;
#endif
}

struct parport_operations parport_pc_ops = 
{
	parport_pc_write_data,
	parport_pc_read_data,

	parport_pc_write_control,
	parport_pc_read_control,
	parport_pc_frob_control,

	parport_pc_write_econtrol,
	parport_pc_read_econtrol,
	parport_pc_frob_econtrol,

	parport_pc_write_status,
	parport_pc_read_status,

	parport_pc_write_fifo,
	parport_pc_read_fifo,
	
	parport_pc_change_mode,
	
	parport_pc_release_resources,
	parport_pc_claim_resources,
	
	parport_pc_write_epp,
	parport_pc_read_epp,
	parport_pc_write_epp_addr,
	parport_pc_read_epp_addr,
	parport_pc_check_epp_timeout,

	parport_pc_epp_write_block,
	parport_pc_epp_read_block,

	parport_pc_ecp_write_block,
	parport_pc_ecp_read_block,
	
	parport_pc_init_state,
	parport_pc_save_state,
	parport_pc_restore_state,

	parport_pc_enable_irq,
	parport_pc_disable_irq,
	parport_pc_interrupt,

	parport_pc_inc_use_count,
	parport_pc_dec_use_count,
	parport_pc_fill_inode
};

/* --- Mode detection ------------------------------------- */

/*
 * Clear TIMEOUT BIT in EPP MODE
 */
int parport_pc_epp_clear_timeout(struct parport *pb)
{
	unsigned char r;

	if (!(parport_pc_read_status(pb) & 0x01))
		return 1;

	/* To clear timeout some chips require double read */
	parport_pc_read_status(pb);
	r = parport_pc_read_status(pb);
	parport_pc_write_status(pb, r | 0x01); /* Some reset by writing 1 */
	parport_pc_write_status(pb, r & 0xfe); /* Others by writing 0 */
	r = parport_pc_read_status(pb);

	return !(r & 0x01);
}


/*
 * Checks for port existence, all ports support SPP MODE
 */
static int __init parport_SPP_supported(struct parport *pb)
{
	unsigned char r, w;

	/*
	 * first clear an eventually pending EPP timeout 
	 * I (sailer@ife.ee.ethz.ch) have an SMSC chipset
	 * that does not even respond to SPP cycles if an EPP
	 * timeout is pending
	 */
	parport_pc_epp_clear_timeout(pb);

	/* Do a simple read-write test to make sure the port exists. */
	w = 0xc;
	parport_pc_write_control(pb, w);

	/* Can we read from the control register?  Some ports don't
	 * allow reads, so read_control just returns a software
	 * copy. Some ports _do_ allow reads, so bypass the software
	 * copy here.  In addition, some bits aren't writable. */
	r = inb (CONTROL (pb));
	if ((r & 0x3f) == w) {
		w = 0xe;
		parport_pc_write_control (pb, w);
		r = inb (CONTROL(pb));
		parport_pc_write_control (pb, 0xc);
		if ((r & 0x3f) == w)
			return PARPORT_MODE_PCSPP;
	}

	if (user_specified)
		/* That didn't work, but the user thinks there's a
		 * port here. */
		printk (KERN_DEBUG "0x%lx: CTR: wrote 0x%02x, read 0x%02x\n",
			pb->base, w, r);

	/* Try the data register.  The data lines aren't tri-stated at
	 * this stage, so we expect back what we wrote. */
	w = 0xaa;
	parport_pc_write_data (pb, w);
	r = parport_pc_read_data (pb);
	if (r == w) {
		w = 0x55;
		parport_pc_write_data (pb, w);
		r = parport_pc_read_data (pb);
		if (r == w)
			return PARPORT_MODE_PCSPP;
	}

	if (user_specified)
		/* Didn't work with 0xaa, but the user is convinced
		 * this is the place. */
		printk (KERN_DEBUG "0x%lx: DATA: wrote 0x%02x, read 0x%02x\n",
			pb->base, w, r);

	/* It's possible that we can't read the control register or
	   the data register.  In that case just believe the user. */
	if (user_specified)
		return PARPORT_MODE_PCSPP;

	return 0;
}

/* Check for ECP
 *
 * Old style XT ports alias io ports every 0x400, hence accessing ECR
 * on these cards actually accesses the CTR.
 *
 * Modern cards don't do this but reading from ECR will return 0xff
 * regardless of what is written here if the card does NOT support
 * ECP.
 *
 * We will write 0x2c to ECR and 0xcc to CTR since both of these
 * values are "safe" on the CTR since bits 6-7 of CTR are unused.
 */
static int __init parport_ECR_present(struct parport *pb)
{
	unsigned char r;

	parport_pc_write_control (pb, 0xc);
	r = parport_pc_read_control(pb);	
	if ((parport_pc_read_econtrol(pb) & 0x3) == (r & 0x3)) {
		parport_pc_write_control(pb, r ^ 0x2 ); /* Toggle bit 1 */

		r = parport_pc_read_control(pb);	
		if ((parport_pc_read_econtrol(pb) & 0x2) == (r & 0x2))
			goto no_reg; /* Sure that no ECR register exists */
	}
	
	if ((parport_pc_read_econtrol(pb) & 0x3 ) != 0x1)
		goto no_reg;

	parport_pc_write_econtrol(pb, 0x34);
	if (parport_pc_read_econtrol(pb) != 0x35)
		goto no_reg;

	parport_pc_write_control(pb, 0xc);

	/* Go to mode 000; SPP, reset FIFO */
	parport_pc_frob_econtrol (pb, 0xe0, 0x00);
	
	return PARPORT_MODE_PCECR;

 no_reg:
	parport_pc_write_control (pb, 0xc);
	return 0;
}

static int __init parport_ECP_supported(struct parport *pb)
{
	int i;
	unsigned char oecr;
	
	/* If there is no ECR, we have no hope of supporting ECP. */
	if (!(pb->modes & PARPORT_MODE_PCECR))
		return 0;

	oecr = parport_pc_read_econtrol(pb);
	/*
	 * Using LGS chipset it uses ECR register, but
	 * it doesn't support ECP or FIFO MODE
	 */
	
	parport_pc_write_econtrol(pb, 0xc0); /* TEST FIFO */
	for (i=0; i < 1024 && (parport_pc_read_econtrol(pb) & 0x01); i++)
		parport_pc_write_fifo(pb, 0xaa);

	parport_pc_write_econtrol(pb, oecr);
	return (i==1024)?0:PARPORT_MODE_PCECP;
}

/* EPP mode detection
 * Theory:
 *	Bit 0 of STR is the EPP timeout bit, this bit is 0
 *	when EPP is possible and is set high when an EPP timeout
 *	occurs (EPP uses the HALT line to stop the CPU while it does
 *	the byte transfer, an EPP timeout occurs if the attached
 *	device fails to respond after 10 micro seconds).
 *
 *	This bit is cleared by either reading it (National Semi)
 *	or writing a 1 to the bit (SMC, UMC, WinBond), others ???
 *	This bit is always high in non EPP modes.
 */
static int __init parport_EPP_supported(struct parport *pb)
{
	/* If EPP timeout bit clear then EPP available */
	if (!parport_pc_epp_clear_timeout(pb))
		return 0;  /* No way to clear timeout */

	parport_pc_write_control(pb, parport_pc_read_control(pb) | 0x20);
	parport_pc_write_control(pb, parport_pc_read_control(pb) | 0x10);
	parport_pc_epp_clear_timeout(pb);
	
	parport_pc_read_epp(pb);
	udelay(30);  /* Wait for possible EPP timeout */
	
	if (parport_pc_read_status(pb) & 0x01) {
		parport_pc_epp_clear_timeout(pb);
		return PARPORT_MODE_PCEPP;
	}

	return 0;
}

static int __init parport_ECPEPP_supported(struct parport *pb)
{
	int mode;
	unsigned char oecr;

	if (!(pb->modes & PARPORT_MODE_PCECR))
		return 0;

	oecr = parport_pc_read_econtrol(pb);
	/* Search for SMC style EPP+ECP mode */
	parport_pc_write_econtrol(pb, 0x80);
	
	mode = parport_EPP_supported(pb);

	parport_pc_write_econtrol(pb, oecr);
	
	return mode?PARPORT_MODE_PCECPEPP:0;
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

static int __init parport_PS2_supported(struct parport *pb)
{
	int ok = 0;
	unsigned char octr = parport_pc_read_control(pb);
  
	parport_pc_epp_clear_timeout(pb);

	parport_pc_write_control(pb, octr | 0x20);  /* try to tri-state the buffer */
	
	parport_pc_write_data(pb, 0x55);
	if (parport_pc_read_data(pb) != 0x55) ok++;

	parport_pc_write_data(pb, 0xaa);
	if (parport_pc_read_data(pb) != 0xaa) ok++;
	
	parport_pc_write_control(pb, octr);          /* cancel input mode */

	return ok?PARPORT_MODE_PCPS2:0;
}

static int __init parport_ECPPS2_supported(struct parport *pb)
{
	int mode;
	unsigned char oecr;

	if (!(pb->modes & PARPORT_MODE_PCECR))
		return 0;

	oecr = parport_pc_read_econtrol(pb);
	parport_pc_write_econtrol(pb, 0x20);
	
	mode = parport_PS2_supported(pb);

	parport_pc_write_econtrol(pb, oecr);
	return mode?PARPORT_MODE_PCECPPS2:0;
}

/* --- IRQ detection -------------------------------------- */

/* Only if supports ECP mode */
static int __init programmable_irq_support(struct parport *pb)
{
	int irq, intrLine;
	unsigned char oecr = parport_pc_read_econtrol(pb);
	static const int lookup[8] = {
		PARPORT_IRQ_NONE, 7, 9, 10, 11, 14, 15, 5
	};

	parport_pc_write_econtrol(pb,0xE0); /* Configuration MODE */
	
	intrLine = (parport_pc_read_configb(pb) >> 3) & 0x07;
	irq = lookup[intrLine];

	parport_pc_write_econtrol(pb, oecr);
	return irq;
}

static int __init irq_probe_ECP(struct parport *pb)
{
	int irqs, i;

	sti();
	irqs = probe_irq_on();
		
	parport_pc_write_econtrol(pb, 0x00);	/* Reset FIFO */
	parport_pc_write_econtrol(pb, 0xd0);	/* TEST FIFO + nErrIntrEn */

	/* If Full FIFO sure that WriteIntrThresold is generated */
	for (i=0; i < 1024 && !(parport_pc_read_econtrol(pb) & 0x02) ; i++) 
		parport_pc_write_fifo(pb, 0xaa);
		
	pb->irq = probe_irq_off(irqs);
	parport_pc_write_econtrol(pb, 0x00);

	if (pb->irq <= 0)
		pb->irq = PARPORT_IRQ_NONE;

	return pb->irq;
}

/*
 * This detection seems that only works in National Semiconductors
 * This doesn't work in SMC, LGS, and Winbond 
 */
static int __init irq_probe_EPP(struct parport *pb)
{
	int irqs;
	unsigned char octr = parport_pc_read_control(pb);
	unsigned char oecr;

#ifndef ADVANCED_DETECT
	return PARPORT_IRQ_NONE;
#endif

	if (pb->modes & PARPORT_MODE_PCECR)
		oecr = parport_pc_read_econtrol(pb);

	sti();
	irqs = probe_irq_on();

	if (pb->modes & PARPORT_MODE_PCECR)
		parport_pc_frob_econtrol (pb, 0x10, 0x10);
	
	parport_pc_epp_clear_timeout(pb);
	parport_pc_frob_control (pb, 0x20, 0x20);
	parport_pc_frob_control (pb, 0x10, 0x10);
	parport_pc_epp_clear_timeout(pb);

	/* Device isn't expecting an EPP read
	 * and generates an IRQ.
	 */
	parport_pc_read_epp(pb);
	udelay(20);

	pb->irq = probe_irq_off (irqs);
	if (pb->modes & PARPORT_MODE_PCECR)
		parport_pc_write_econtrol(pb, oecr);
	parport_pc_write_control(pb, octr);

	if (pb->irq <= 0)
		pb->irq = PARPORT_IRQ_NONE;

	return pb->irq;
}

static int __init irq_probe_SPP(struct parport *pb)
{
	int irqs;
	unsigned char octr = parport_pc_read_control(pb);
	unsigned char oecr;

#ifndef ADVANCED_DETECT
	return PARPORT_IRQ_NONE;
#endif

	if (pb->modes & PARPORT_MODE_PCECR)
		oecr = parport_pc_read_econtrol(pb);
	probe_irq_off(probe_irq_on());	/* Clear any interrupts */
	irqs = probe_irq_on();

	if (pb->modes & PARPORT_MODE_PCECR)
		parport_pc_write_econtrol(pb, 0x10);

	parport_pc_write_data(pb,0x00);
	parport_pc_write_control(pb,0x00);
	parport_pc_write_control(pb,0x0c);
	udelay(5);
	parport_pc_write_control(pb,0x0d);
	udelay(5);
	parport_pc_write_control(pb,0x0c);
	udelay(25);
	parport_pc_write_control(pb,0x08);
	udelay(25);
	parport_pc_write_control(pb,0x0c);
	udelay(50);

	pb->irq = probe_irq_off(irqs);
	if (pb->irq <= 0)
		pb->irq = PARPORT_IRQ_NONE;	/* No interrupt detected */
	
	if (pb->modes & PARPORT_MODE_PCECR)
		parport_pc_write_econtrol(pb, oecr);
	parport_pc_write_control(pb, octr);
	return pb->irq;
}

/* We will attempt to share interrupt requests since other devices
 * such as sound cards and network cards seem to like using the
 * printer IRQs.
 *
 * When ECP is available we can autoprobe for IRQs.
 * NOTE: If we can autoprobe it, we can register the IRQ.
 */
static int __init parport_irq_probe(struct parport *pb)
{
	if (pb->modes & PARPORT_MODE_PCECR) {
		pb->irq = programmable_irq_support(pb);
		if (pb->irq != PARPORT_IRQ_NONE)
			goto out;
	}

	if (pb->modes & PARPORT_MODE_PCECP)
		pb->irq = irq_probe_ECP(pb);

	if (pb->irq == PARPORT_IRQ_NONE && 
	    (pb->modes & PARPORT_MODE_PCECPEPP))
		pb->irq = irq_probe_EPP(pb);

	parport_pc_epp_clear_timeout(pb);

	if (pb->irq == PARPORT_IRQ_NONE && (pb->modes & PARPORT_MODE_PCEPP))
		pb->irq = irq_probe_EPP(pb);

	parport_pc_epp_clear_timeout(pb);

	if (pb->irq == PARPORT_IRQ_NONE)
		pb->irq = irq_probe_SPP(pb);

out:
	return pb->irq;
}

/* --- Initialisation code -------------------------------- */

static int __init probe_one_port(unsigned long int base,
				 unsigned long int base_hi,
				 int irq, int dma)
{
	struct parport *p;
	int probedirq = PARPORT_IRQ_NONE;
	if (check_region(base, 3)) return 0;
	if (!(p = parport_register_port(base, irq, dma, &parport_pc_ops)))
		return 0;
	p->private_data = kmalloc (sizeof (struct parport_pc_private),
				   GFP_KERNEL);
	if (!p->private_data) {
		/* Not enough memory. */
		printk (KERN_DEBUG "parport (0x%lx): no memory!\n", base);
		parport_unregister_port (p);
		return 0;
	}
	((struct parport_pc_private *) (p->private_data))->ctr = 0xc;
	p->base_hi = base_hi;
	if (p->base != 0x3bc) {
		if (base_hi && !check_region(base_hi,3)) {
			p->modes |= parport_ECR_present(p);	
			p->modes |= parport_ECP_supported(p);
			p->modes |= parport_ECPPS2_supported(p);
		}
		if (!check_region(base+0x3, 5)) {
			p->modes |= parport_EPP_supported(p);
			p->modes |= parport_ECPEPP_supported(p);
		}
	}
	if (!parport_SPP_supported(p)) {
		/* No port. */
		kfree (p->private_data);
		parport_unregister_port (p);
		return 0;
	}
	p->modes |= PARPORT_MODE_PCSPP | parport_PS2_supported(p);
	p->size = (p->modes & (PARPORT_MODE_PCEPP 
			       | PARPORT_MODE_PCECPEPP))?8:3;
	printk(KERN_INFO "%s: PC-style at 0x%lx", p->name, p->base);
	if (p->base_hi && (p->modes & PARPORT_MODE_PCECR))
		printk (" (0x%lx)", p->base_hi);
	if (p->irq == PARPORT_IRQ_AUTO) {
		p->irq = PARPORT_IRQ_NONE;
		parport_irq_probe(p);
	} else if (p->irq == PARPORT_IRQ_PROBEONLY) {
		p->irq = PARPORT_IRQ_NONE;
		parport_irq_probe(p);
		probedirq = p->irq;
		p->irq = PARPORT_IRQ_NONE;
	}
	if (p->irq != PARPORT_IRQ_NONE)
		printk(", irq %d", p->irq);
	if (p->dma == PARPORT_DMA_AUTO)		
		p->dma = PARPORT_DMA_NONE;
	if (p->dma != PARPORT_DMA_NONE)
		printk(", dma %d", p->dma);
	printk(" [");
#define printmode(x) {if(p->modes&PARPORT_MODE_PC##x){printk("%s%s",f?",":"",#x);f++;}}
	{
		int f = 0;
		printmode(SPP);
		printmode(PS2);
		printmode(EPP);
		printmode(ECP);
		printmode(ECPEPP);
		printmode(ECPPS2);
	}
#undef printmode
	printk("]\n");
#ifdef	CONFIG_PROC_FS
	if (probedirq != PARPORT_IRQ_NONE) 
		printk("%s: detected irq %d; use procfs to enable interrupt-driven operation.\n", p->name, probedirq);
#endif
	parport_proc_register(p);
	p->flags |= PARPORT_FLAG_COMA;

	/* Done probing.  Now put the port into a sensible start-up state. */
	if (p->modes & PARPORT_MODE_PCECR)
		/*
		 * Put the ECP detected port in PS2 mode.
		 */
		parport_pc_write_econtrol(p, 0x24);
	parport_pc_write_data(p, 0);
	parport_pc_write_control(p, 0x8);
	udelay (50);
	parport_pc_write_control(p, 0xc);
	udelay (50);

	if (parport_probe_hook)
		(*parport_probe_hook)(p);

	return 1;
}

int __init parport_pc_init(int *io, int *io_hi, int *irq, int *dma)
{
	int count = 0, i = 0;
	if (io && *io) {
		/* Only probe the ports we were given. */
		user_specified = 1;
		do {
			if (!*io_hi) *io_hi = 0x400 + *io;
			count += probe_one_port(*(io++), *(io_hi++),
						*(irq++), *(dma++));
		} while (*io && (++i < PARPORT_PC_MAX_PORTS));
	} else {
		/* Probe all the likely ports. */
		count += probe_one_port(0x3bc, 0x7bc, irq[0], dma[0]);
		count += probe_one_port(0x378, 0x778, irq[0], dma[0]);
		count += probe_one_port(0x278, 0x678, irq[0], dma[0]);
	}

	return count;
}

#ifdef MODULE
static int io[PARPORT_PC_MAX_PORTS+1] = { [0 ... PARPORT_PC_MAX_PORTS] = 0 };
static int io_hi[PARPORT_PC_MAX_PORTS+1] = { [0 ... PARPORT_PC_MAX_PORTS] = 0 };
static int dma[PARPORT_PC_MAX_PORTS] = { [0 ... PARPORT_PC_MAX_PORTS-1] = PARPORT_DMA_NONE };
static int irqval[PARPORT_PC_MAX_PORTS] = { [0 ... PARPORT_PC_MAX_PORTS-1] = PARPORT_IRQ_PROBEONLY };
static const char *irq[PARPORT_PC_MAX_PORTS] = { NULL, };
MODULE_PARM(io, "1-" __MODULE_STRING(PARPORT_PC_MAX_PORTS) "i");
MODULE_PARM(io_hi, "1-" __MODULE_STRING(PARPORT_PC_MAX_PORTS) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(PARPORT_PC_MAX_PORTS) "s");
MODULE_PARM(dma, "1-" __MODULE_STRING(PARPORT_PC_MAX_PORTS) "i");

int init_module(void)
{	
	/* Work out how many ports we have, then get parport_share to parse
	   the irq values. */
	unsigned int i;
	for (i = 0; i < PARPORT_PC_MAX_PORTS && io[i]; i++);
	parport_parse_irqs(i, irq, irqval);

	return (parport_pc_init(io, io_hi, irqval, dma)?0:1);
}

void cleanup_module(void)
{
	struct parport *p = parport_enumerate(), *tmp;
	while (p) {
		tmp = p->next;
		if (p->modes & PARPORT_MODE_PCSPP) { 
			if (!(p->flags & PARPORT_FLAG_COMA)) 
				parport_quiesce(p);
			parport_proc_unregister(p);
			kfree (p->private_data);
			parport_unregister_port(p);
		}
		p = tmp;
	}
}
#endif
