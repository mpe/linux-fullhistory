/* Parallel-port routines for PC architecture
 * 
 * Authors: Phil Blundell <Philip.Blundell@pobox.com>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *	    Jose Renau <renau@acm.org>
 *          David Campbell <campbell@tirian.che.curtin.edu.au>
 *
 * based on work by Grant Guenther <grant@torque.net> and Phil Blundell.
 */

#include <linux/stddef.h>
#include <linux/tasks.h>

#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/module.h>
#include <linux/config.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>

#include <linux/parport.h>

#define ECONTROL 0x402
#define CONFIGB  0x401
#define CONFIGA  0x400
#define EPPREG   0x4
#define CONTROL  0x2
#define STATUS   0x1
#define DATA     0

#define PC_MAX_PORTS  8

static void pc_null_intr_func(int irq, void *dev_id, struct pt_regs *regs)
{
	/* NULL function - Does nothing */
	return;
}

#if 0
static void pc_write_epp(struct parport *p, unsigned int d)
{
	outb(d, p->base+EPPREG);
}
#endif

static unsigned int pc_read_epp(struct parport *p)
{
	return (unsigned int)inb(p->base+EPPREG);
}

static unsigned int pc_read_configb(struct parport *p)
{
	return (unsigned int)inb(p->base+CONFIGB);
}

static void pc_write_data(struct parport *p, unsigned int d)
{
	outb(d, p->base+DATA);
}

static unsigned int pc_read_data(struct parport *p)
{
	return (unsigned int)inb(p->base+DATA);
}

static void pc_write_control(struct parport *p, unsigned int d)
{
	outb(d, p->base+CONTROL);
}

static unsigned int pc_read_control(struct parport *p)
{
	return (unsigned int)inb(p->base+CONTROL);
}

static unsigned int pc_frob_control(struct parport *p, unsigned int mask,  unsigned int val)
{
	unsigned int old = (unsigned int)inb(p->base+CONTROL);
	outb(((old & ~mask) ^ val), p->base+CONTROL);
	return old;
}

static void pc_write_status(struct parport *p, unsigned int d)
{
	outb(d, p->base+STATUS);
}

static unsigned int pc_read_status(struct parport *p)
{
	return (unsigned int)inb(p->base+STATUS);
}

static void pc_write_econtrol(struct parport *p, unsigned int d)
{
	outb(d, p->base+ECONTROL);
}

static unsigned int pc_read_econtrol(struct parport *p)
{
	return (unsigned int)inb(p->base+ECONTROL);
}

static unsigned int pc_frob_econtrol(struct parport *p, unsigned int mask,  unsigned int val)
{
	unsigned int old = (unsigned int)inb(p->base+ECONTROL);
	outb(((old & ~mask) ^ val), p->base+ECONTROL);
	return old;
}

static void pc_change_mode(struct parport *p, int m)
{
	/* FIXME */
}

static void pc_write_fifo(struct parport *p, unsigned int v)
{
	/* FIXME */
}

static unsigned int pc_read_fifo(struct parport *p)
{
	return 0; /* FIXME */
}

static void pc_disable_irq(struct parport *p)
{
	/* FIXME */
}

static void pc_enable_irq(struct parport *p)
{
	/* FIXME */
}

static void pc_release_resources(struct parport *p)
{
	if (p->irq != PARPORT_IRQ_NONE)
		free_irq(p->irq, p);
	release_region(p->base, p->size);
	if (p->modes & PARPORT_MODE_PCECR)
		release_region(p->base+0x400, 3);
}

static int pc_claim_resources(struct parport *p)
{
	/* FIXME check that resources are free */
	if (p->irq != PARPORT_IRQ_NONE)
		request_irq(p->irq, pc_null_intr_func, 0, p->name, p);
	request_region(p->base, p->size, p->name);
	if (p->modes & PARPORT_MODE_PCECR)
		request_region(p->base+0x400, 3, p->name);
	return 0;
}

static void pc_save_state(struct parport *p, struct parport_state *s)
{
	s->u.pc.ctr = pc_read_control(p);
	s->u.pc.ecr = pc_read_econtrol(p);
}

static void pc_restore_state(struct parport *p, struct parport_state *s)
{
	pc_write_control(p, s->u.pc.ctr);
	pc_write_econtrol(p, s->u.pc.ecr);
}

static unsigned int pc_epp_read_block(struct parport *p, void *buf, unsigned  int length)
{
	return 0; /* FIXME */
}

static unsigned int pc_epp_write_block(struct parport *p, void *buf, unsigned  int length)
{
	return 0; /* FIXME */
}

static unsigned int pc_ecp_read_block(struct parport *p, void *buf, unsigned  int length, void (*fn)(struct parport *, void *, unsigned int), void *handle)
{
	return 0; /* FIXME */
}

static unsigned int pc_ecp_write_block(struct parport *p, void *buf, unsigned  int length, void (*fn)(struct parport *, void *, unsigned int), void *handle)
{
	return 0; /* FIXME */
}

static int pc_examine_irq(struct parport *p)
{
	return 0; /* FIXME */
}

static void pc_inc_use_count(void)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

static void pc_dec_use_count(void)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}

static struct parport_operations pc_ops = 
{
	pc_write_data,
	pc_read_data,

	pc_write_control,
	pc_read_control,
	pc_frob_control,

	pc_write_econtrol,
	pc_read_econtrol,
	pc_frob_econtrol,

	pc_write_status,
	pc_read_status,

	pc_write_fifo,
	pc_read_fifo,
	
	pc_change_mode,
	
	pc_release_resources,
	pc_claim_resources,
	
	pc_epp_write_block,
	pc_epp_read_block,

	pc_ecp_write_block,
	pc_ecp_read_block,
	
	pc_save_state,
	pc_restore_state,

	pc_enable_irq,
	pc_disable_irq,
	pc_examine_irq,

	pc_inc_use_count,
	pc_dec_use_count
};

/******************************************************
 *  DMA detection section:
 */

/*
 * Prepare DMA channels from 0-8 to transmit towards buffer
 */
static int parport_prepare_dma(char *buff, int size)
{
	int tmp = 0;
	int i,retv;
	
	for (i = 0; i < 8; i++) {
		retv = request_dma(i, "probe");
		if (retv)
			continue;
		tmp |= 1 << i;

		cli();
		disable_dma(i);
		clear_dma_ff(i);
		set_dma_addr(i, virt_to_bus(buff));
		set_dma_count(i, size);
		set_dma_mode(i, DMA_MODE_READ);
		sti();
	}

	return tmp;
}

/*
 * Activate all DMA channels passed in dma
 */
static int parport_enable_dma(int dma)
{
	int i;
	
	for (i = 0; i < 8; i++)
		if (dma & (1 << i)) {
		cli();
		enable_dma(i);
		sti();
	    }

	return dma;
}

static int parport_detect_dma_transfer(int dma, int size)
{
	int i,n,retv;
	int count=0;

	retv = PARPORT_DMA_NONE;
	for (i = 0; i < 8; i++)
		if (dma & (1 << i)) {
			disable_dma(i);
			clear_dma_ff(i);
			n = get_dma_residue(i);
			if (n != size) {
				retv = i;
				if (count > 0) {
					retv = PARPORT_DMA_NONE; /* Multiple DMA's */
					printk(KERN_ERR "parport: multiple DMA detected.  Huh?\n");
				}
				count++;
			}
			free_dma(i);
		}

	return retv;	
}

/* Only if supports ECP mode */
static int programmable_dma_support(struct parport *pb)
{
	int dma, oldstate = pc_read_econtrol(pb);

	pc_write_econtrol(pb, 0xe0); /* Configuration MODE */
	
	dma = pc_read_configb(pb) & 0x07;

	pc_write_econtrol(pb, oldstate);
	
	if (dma == 0 || dma == 4) /* Jumper selection */
		return PARPORT_DMA_NONE;
	else
		return dma;
}

/* Only called if port supports ECP mode.
 *
 * The only restriction on DMA channels is that it has to be
 * between 0 to 7 (inclusive). Used only in an ECP mode, DMAs are
 * considered a shared resource and hence they should be registered
 * when needed and then immediately unregistered.
 *
 * DMA autoprobes for ECP mode are known not to work for some
 * main board BIOS configs. I had to remove everything from the
 * port, set the mode to SPP, reboot to DOS, set the mode to ECP,
 * and reboot again, then I got IRQ probes and DMA probes to work.
 * [Is the BIOS doing a device detection?]
 *
 * A value of PARPORT_DMA_NONE is allowed indicating no DMA support.
 *
 * if( 0 < DMA < 4 )
 *    1Byte DMA transfer
 * else // 4 < DMA < 8
 *    2Byte DMA transfer
 *
 */
static int parport_dma_probe(struct parport *pb)
{
	int dma,retv;
	int dsr,dsr_read;
	char *buff;

	retv = programmable_dma_support(pb);
	if (retv != PARPORT_DMA_NONE)
		return retv;
	
	if (!(buff = kmalloc(2048, GFP_KERNEL | GFP_DMA))) {
	    printk(KERN_ERR "parport: memory squeeze\n");
	    return PARPORT_DMA_NONE;
	}
	
 	dsr = pb->ops->read_control(pb);
	dsr_read = (dsr & ~(0x20)) | 0x04;    /* Direction == read */

	pb->ops->write_econtrol(pb, 0xc0);	   /* ECP MODE */
 	pb->ops->write_control(pb, dsr_read );
	dma = parport_prepare_dma(buff, 1000);
	pb->ops->write_econtrol(pb, 0xd8);	   /* ECP FIFO + enable DMA */
	parport_enable_dma(dma);
	udelay(500);           /* Give some for DMA tranfer */
	retv = parport_detect_dma_transfer(dma, 1000);
	
	/*
	 * National Semiconductors only supports DMA tranfers
	 * in ECP MODE
	 */
	if (retv == PARPORT_DMA_NONE) {
		pb->ops->write_econtrol(pb, 0x60);	   /* ECP MODE */
		pb->ops->write_control(pb, dsr_read );
		dma=parport_prepare_dma(buff,1000);
		pb->ops->write_econtrol(pb, 0x68);	   /* ECP FIFO + enable DMA */
		parport_enable_dma(dma);
		udelay(500);           /* Give some for DMA tranfer */
		retv = parport_detect_dma_transfer(dma, 1000);
	}
	
	kfree(buff);
	
	return retv;
}

/******************************************************
 *  MODE detection section:
 */

/*
 * Clear TIMEOUT BIT in EPP MODE
 */
static int epp_clear_timeout(struct parport *pb)
{
	int r;

	if (!(pc_read_status(pb) & 0x01))
		return 1;

	/* To clear timeout some chips require double read */
	pc_read_status(pb);
	r = pc_read_status(pb);
	pc_write_status(pb, r | 0x01); /* Some reset by writing 1 */
	pc_write_status(pb, r & 0xfe); /* Others by writing 0 */
	r = pc_read_status(pb);

	return !(r & 0x01);
}


/*
 * Checks for port existence, all ports support SPP MODE
 */
static int parport_SPP_supported(struct parport *pb)
{
	/* Do a simple read-write test to make sure the port exists. */
	pc_write_control(pb, 0xc);
	pc_write_data(pb, 0xaa);
	if (pc_read_data(pb) != 0xaa) return 0;
	
	pc_write_data(pb, 0x55);
	if (pc_read_data(pb) != 0x55) return 0;

	return PARPORT_MODE_PCSPP;
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
static int parport_ECR_present(struct parport *pb)
{
	unsigned int r, octr = pc_read_control(pb), 
	  oecr = pc_read_econtrol(pb);

	r = pc_read_control(pb);	
	if ((pc_read_econtrol(pb) & 0x3) == (r & 0x3)) {
		pc_write_control(pb, r ^ 0x2 ); /* Toggle bit 1 */

		r = pc_read_control(pb);	
		if ((pc_read_econtrol(pb) & 0x2) == (r & 0x2)) {
			pc_write_control(pb, octr);
			return 0; /* Sure that no ECR register exists */
		}
	}
	
	if ((pc_read_econtrol(pb) & 0x3 ) != 0x1)
		return 0;

	pc_write_econtrol(pb, 0x34);
	if (pc_read_econtrol(pb) != 0x35)
		return 0;

	pc_write_econtrol(pb, oecr);
	pc_write_control(pb, octr);
	
	return PARPORT_MODE_PCECR;
}

static int parport_ECP_supported(struct parport *pb)
{
	int i, oecr = pc_read_econtrol(pb);
	
	/* If there is no ECR, we have no hope of supporting ECP. */
	if (!(pb->modes & PARPORT_MODE_PCECR))
		return 0;

	/*
	 * Using LGS chipset it uses ECR register, but
	 * it doesn't support ECP or FIFO MODE
	 */
	
	pc_write_econtrol(pb, 0xc0); /* TEST FIFO */
	for (i=0; i < 1024 && (pc_read_econtrol(pb) & 0x01); i++)
		pc_write_fifo(pb, 0xaa);

	pc_write_econtrol(pb, oecr);
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
static int parport_EPP_supported(struct parport *pb)
{
	/* If EPP timeout bit clear then EPP available */
	if (!epp_clear_timeout(pb))
		return 0;  /* No way to clear timeout */

	pc_write_control(pb, pc_read_control(pb) | 0x20);
	pc_write_control(pb, pc_read_control(pb) | 0x10);
	epp_clear_timeout(pb);
	
	pc_read_epp(pb);
	udelay(30);  /* Wait for possible EPP timeout */
	
	if (pc_read_status(pb) & 0x01) {
		epp_clear_timeout(pb);
		return PARPORT_MODE_PCEPP;
	}

	return 0;
}

static int parport_ECPEPP_supported(struct parport *pb)
{
	int mode, oecr = pc_read_econtrol(pb);

	if (!(pb->modes & PARPORT_MODE_PCECR))
		return 0;
	
	/* Search for SMC style EPP+ECP mode */
	pc_write_econtrol(pb, 0x80);
	
	mode = parport_EPP_supported(pb);

	pc_write_econtrol(pb, oecr);
	
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

static int parport_PS2_supported(struct parport *pb)
{
	int ok = 0, octr = pc_read_control(pb);
  
	epp_clear_timeout(pb);

	pc_write_control(pb, octr | 0x20);  /* try to tri-state the buffer */
	
	pc_write_data(pb, 0x55);
	if (pc_read_data(pb) != 0x55) ok++;

	pc_write_data(pb, 0xaa);
	if (pc_read_data(pb) != 0xaa) ok++;
	
	pc_write_control(pb, octr);          /* cancel input mode */

	return ok?PARPORT_MODE_PCPS2:0;
}

static int parport_ECPPS2_supported(struct parport *pb)
{
	int mode, oecr = pc_read_econtrol(pb);

	if (!(pb->modes & PARPORT_MODE_PCECR))
		return 0;
	
	pc_write_econtrol(pb, 0x20);
	
	mode = parport_PS2_supported(pb);

	pc_write_econtrol(pb, oecr);
	return mode?PARPORT_MODE_PCECPPS2:0;
}

/******************************************************
 *  IRQ detection section:
 *
 * This code is for detecting ECP interrupts (due to problems with the
 * monolithic interrupt probing routines).
 *
 * In short this is a voting system where the interrupt with the most
 * "votes" is the elected interrupt (it SHOULD work...)
 *
 * This is horribly x86-specific at the moment.  I'm not convinced it
 * belongs at all.
 */

static int intr_vote[16];

static void parport_vote_intr_func(int irq, void *dev_id, struct pt_regs *regs)
{
	intr_vote[irq]++;
	return;
}

static long open_intr_election(void)
{
	long tmp = 0;
	int i;

	/* We ignore the timer - irq 0 */
	for (i = 1; i < 16; i++) {
		intr_vote[i] = 0;
		if (request_irq(i, parport_vote_intr_func,
		       SA_INTERRUPT, "probe", intr_vote) == 0)
			tmp |= 1 << i;
	}
	return tmp;
}

static int close_intr_election(long tmp)
{
	int irq = PARPORT_IRQ_NONE;
	int i;

	/* We ignore the timer - irq 0 */
	for (i = 1; i < 16; i++)
		if (tmp & (1 << i)) {
			if (intr_vote[i]) {
				if (irq != PARPORT_IRQ_NONE)
					/* More than one interrupt */
					return PARPORT_IRQ_NONE;
				irq = i;
			}
			free_irq(i, intr_vote);
		}
	return irq;
}

/* Only if supports ECP mode */
static int programmable_irq_support(struct parport *pb)
{
	int irq, oecr = pc_read_econtrol(pb);

	pc_write_econtrol(pb,0xE0); /* Configuration MODE */
	
	irq = (pc_read_configb(pb) >> 3) & 0x07;

	switch(irq){
	case 2:
		irq = 9;
		break;
	case 7:
		irq = 5;
		break;
	case 0:
		irq = PARPORT_IRQ_NONE;
		break;
	default:
		irq += 7;
	}
	
	pc_write_econtrol(pb, oecr);
	return irq;
}

static int irq_probe_ECP(struct parport *pb)
{
	int irqs, i, oecr = pc_read_econtrol(pb);
		
	probe_irq_off(probe_irq_on());	/* Clear any interrupts */
	irqs = open_intr_election();
		
	pc_write_econtrol(pb, 0x00);	    /* Reset FIFO */
	pc_write_econtrol(pb, 0xd0);	    /* TEST FIFO + nErrIntrEn */

	/* If Full FIFO sure that WriteIntrThresold is generated */
	for (i=0; i < 1024 && !(pc_read_econtrol(pb) & 0x02) ; i++) 
		pc_write_fifo(pb, 0xaa);
		
	pb->irq = close_intr_election(irqs);
	pc_write_econtrol(pb, oecr);
	return pb->irq;
}

/*
 * This detection seems that only works in National Semiconductors
 * This doesn't work in SMC, LGS, and Winbond 
 */
static int irq_probe_EPP(struct parport *pb)
{
	int irqs, octr = pc_read_control(pb);

#ifndef ADVANCED_DETECT
	return PARPORT_IRQ_NONE;
#endif
	
	probe_irq_off(probe_irq_on());	/* Clear any interrupts */
	irqs = open_intr_election();

	if (pb->modes & PARPORT_MODE_PCECR)
		pc_write_econtrol(pb, pc_read_econtrol(pb) | 0x10);
	
	epp_clear_timeout(pb);
	pc_write_control(pb, pc_read_control(pb) | 0x20);
	pc_write_control(pb, pc_read_control(pb) | 0x10);
	epp_clear_timeout(pb);

	/*  Device isn't expecting an EPP read
	 * and generates an IRQ.
	 */
	pc_read_epp(pb);
	udelay(20);

	pb->irq = close_intr_election(irqs);
	pc_write_control(pb, octr);
	return pb->irq;
}

static int irq_probe_SPP(struct parport *pb)
{
	int irqs, octr = pc_read_control(pb);

#ifndef ADVANCED_DETECT
	return PARPORT_IRQ_NONE;
#endif

	probe_irq_off(probe_irq_on());	/* Clear any interrupts */
	irqs = probe_irq_on();

	if (pb->modes & PARPORT_MODE_PCECR)
		pc_write_econtrol(pb, 0x10);

	pc_write_data(pb,0x00);
	pc_write_control(pb,0x00);
	pc_write_control(pb,0x0c);
	udelay(5);
	pc_write_control(pb,0x0d);
	udelay(5);
	pc_write_control(pb,0x0c);
	udelay(25);
	pc_write_control(pb,0x08);
	udelay(25);
	pc_write_control(pb,0x0c);
	udelay(50);

	pb->irq = probe_irq_off(irqs);
	if (pb->irq <= 0)
		pb->irq = PARPORT_IRQ_NONE;	/* No interrupt detected */
	
	pc_write_control(pb, octr);
	return pb->irq;
}

/* We will attempt to share interrupt requests since other devices
 * such as sound cards and network cards seem to like using the
 * printer IRQs.
 *
 * When ECP is available we can autoprobe for IRQs.
 * NOTE: If we can autoprobe it, we can register the IRQ.
 */
static int parport_irq_probe(struct parport *pb)
{
	if (pb->modes & PARPORT_MODE_PCECR)
		pb->irq = programmable_irq_support(pb);

	if (pb->modes & PARPORT_MODE_PCECP)
		pb->irq = irq_probe_ECP(pb);
			
	if (pb->irq == PARPORT_IRQ_NONE && 
	    (pb->modes & PARPORT_MODE_PCECPEPP)) {
		int oecr = pc_read_econtrol(pb);
		pc_write_econtrol(pb, 0x80);
		pb->irq = irq_probe_EPP(pb);
		pc_write_econtrol(pb, oecr);
	}

	epp_clear_timeout(pb);

	if (pb->irq == PARPORT_IRQ_NONE && (pb->modes & PARPORT_MODE_PCEPP))
		pb->irq = irq_probe_EPP(pb);

	epp_clear_timeout(pb);

	if (pb->irq == PARPORT_IRQ_NONE)
		pb->irq = irq_probe_SPP(pb);

	return pb->irq;
}

static int probe_one_port(unsigned long int base, int irq, int dma)
{
	struct parport tmpport, *p;
	if (check_region(base, 3)) return 0;
	tmpport.base = base;
	tmpport.ops = &pc_ops;
	if (!(parport_SPP_supported(&tmpport))) return 0;
       	if (!(p = parport_register_port(base, irq, dma, &pc_ops))) return 0;
	p->modes = PARPORT_MODE_PCSPP | parport_PS2_supported(p);
	if (p->base != 0x3bc) {
		if (!check_region(base+0x400,3)) {
			p->modes |= parport_ECR_present(p);	
			p->modes |= parport_ECP_supported(p);
			p->modes |= parport_ECPPS2_supported(p);
		}
		if (!check_region(base+0x3, 5)) {
			p->modes |= parport_EPP_supported(p);
			p->modes |= parport_ECPEPP_supported(p);
		}
	}
	p->size = (p->modes & (PARPORT_MODE_PCEPP 
			       | PARPORT_MODE_PCECPEPP))?8:3;
	printk(KERN_INFO "%s: PC-style at 0x%x", p->name, p->base);
	if (p->irq == PARPORT_IRQ_AUTO) {
		p->irq = PARPORT_IRQ_NONE;
		parport_irq_probe(p);
	}
	if (p->irq != PARPORT_IRQ_NONE)
		printk(", irq %d", p->irq);
	if (p->dma == PARPORT_DMA_AUTO)		
		p->dma = (p->modes & PARPORT_MODE_PCECP)?
			parport_dma_probe(p):PARPORT_DMA_NONE;
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
	parport_proc_register(p);
	p->flags |= PARPORT_FLAG_COMA;

	/* Done probing.  Now put the port into a sensible start-up state. */
	pc_write_control(p, 0xc);
	pc_write_data(p, 0);
	return 1;
}

int parport_pc_init(int *io, int *irq, int *dma)
{
	int count = 0, i = 0;
	if (io && *io) {
		/* Only probe the ports we were given. */
		do {
			count += probe_one_port(*(io++), *(irq++), *(dma++));
		} while (*io && (++i < PC_MAX_PORTS));
	} else {
		/* Probe all the likely ports. */
		count += probe_one_port(0x3bc, PARPORT_IRQ_AUTO, PARPORT_DMA_AUTO);
		count += probe_one_port(0x378, PARPORT_IRQ_AUTO, PARPORT_DMA_AUTO);
		count += probe_one_port(0x278, PARPORT_IRQ_AUTO, PARPORT_DMA_AUTO);
	}
	return count;
}

#ifdef MODULE
static int io[PC_MAX_PORTS+1] = { [0 ... PC_MAX_PORTS] = 0 };
static int dma[PC_MAX_PORTS] = { [0 ... PC_MAX_PORTS-1] = PARPORT_DMA_AUTO };
static int irq[PC_MAX_PORTS] = { [0 ... PC_MAX_PORTS-1] = PARPORT_IRQ_AUTO };
MODULE_PARM(io, "1-" __MODULE_STRING(PC_MAX_PORTS) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(PC_MAX_PORTS) "i");
MODULE_PARM(dma, "1-" __MODULE_STRING(PC_MAX_PORTS) "i");

int init_module(void)
{	
	return (parport_pc_init(io, irq, dma)?0:1);
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
			parport_unregister_port(p);
		}
		p = tmp;
	}
}
#endif
