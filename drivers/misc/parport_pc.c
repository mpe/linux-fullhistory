/* Low-level parallel-port routines for PC-style hardware.
 * 
 * Authors: Phil Blundell <Philip.Blundell@pobox.com>
 *          Tim Waugh <tim@cyberelk.demon.co.uk>
 *	    Jose Renau <renau@acm.org>
 *          David Campbell <campbell@tirian.che.curtin.edu.au>
 *
 * based on work by Grant Guenther <grant@torque.net> and Phil Blundell.
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
 *	base+3		EPP command
 *	base+4		EPP
 *	base+0x400	ECP config A
 *	base+0x401	ECP config B
 *	base+0x402	ECP control
 *
 * All registers are 8 bits wide and read/write.  If your hardware differs
 * only in register addresses (eg because your registers are on 32-bit
 * word boundaries) then you can alter the constants in parport_pc.h to
 * accomodate this.
 */

#include <linux/stddef.h>
#include <linux/tasks.h>

#include <asm/ptrace.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>

#include <linux/parport.h>
#include <linux/parport_pc.h>

/* Maximum number of ports to support.  It is useless to set this greater
   than PARPORT_MAX (in <linux/parport.h>).  */
#define PARPORT_PC_MAX_PORTS  8

static void parport_pc_null_intr_func(int irq, void *dev_id, struct pt_regs *regs)
{
	/* Null function - does nothing */
}

void parport_pc_write_epp(struct parport *p, unsigned char d)
{
	outb(d, p->base+EPPREG);
}

unsigned char parport_pc_read_epp(struct parport *p)
{
	return inb(p->base+EPPREG);
}

unsigned char parport_pc_read_configb(struct parport *p)
{
	return inb(p->base+CONFIGB);
}

void parport_pc_write_data(struct parport *p, unsigned char d)
{
	outb(d, p->base+DATA);
}

unsigned char parport_pc_read_data(struct parport *p)
{
	return inb(p->base+DATA);
}

void parport_pc_write_control(struct parport *p, unsigned char d)
{
	outb(d, p->base+CONTROL);
}

unsigned char parport_pc_read_control(struct parport *p)
{
	return inb(p->base+CONTROL);
}

unsigned char parport_pc_frob_control(struct parport *p, unsigned char mask,  unsigned char val)
{
	unsigned char old = inb(p->base+CONTROL);
	outb(((old & ~mask) ^ val), p->base+CONTROL);
	return old;
}

void parport_pc_write_status(struct parport *p, unsigned char d)
{
	outb(d, p->base+STATUS);
}

unsigned char parport_pc_read_status(struct parport *p)
{
	return inb(p->base+STATUS);
}

void parport_pc_write_econtrol(struct parport *p, unsigned char d)
{
	outb(d, p->base+ECONTROL);
}

unsigned char parport_pc_read_econtrol(struct parport *p)
{
	return inb(p->base+ECONTROL);
}

unsigned char parport_pc_frob_econtrol(struct parport *p, unsigned char mask,  unsigned char val)
{
	unsigned char old = inb(p->base+ECONTROL);
	outb(((old & ~mask) ^ val), p->base+ECONTROL);
	return old;
}

void parport_pc_change_mode(struct parport *p, int m)
{
	/* FIXME */
}

void parport_pc_write_fifo(struct parport *p, unsigned char v)
{
	outb (v, p->base+CONFIGA);
}

unsigned char parport_pc_read_fifo(struct parport *p)
{
	return inb (p->base+CONFIGA);
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
		free_irq(p->irq, NULL);
	release_region(p->base, p->size);
	if (p->modes & PARPORT_MODE_PCECR)
		release_region(p->base+0x400, 3);
}

int parport_pc_claim_resources(struct parport *p)
{
	/* FIXME check that resources are free */
	if (p->irq != PARPORT_IRQ_NONE)
		request_irq(p->irq, parport_pc_null_intr_func, 0, p->name, NULL);
	request_region(p->base, p->size, p->name);
	if (p->modes & PARPORT_MODE_PCECR)
		request_region(p->base+0x400, 3, p->name);
	return 0;
}

void parport_pc_save_state(struct parport *p, struct parport_state *s)
{
	s->u.pc.ctr = parport_pc_read_control(p);
	s->u.pc.ecr = parport_pc_read_econtrol(p);
}

void parport_pc_restore_state(struct parport *p, struct parport_state *s)
{
	parport_pc_write_control(p, s->u.pc.ctr);
	parport_pc_write_econtrol(p, s->u.pc.ecr);
}

size_t parport_pc_epp_read_block(struct parport *p, void *buf, size_t length)
{
	size_t got = 0;
	for (; got < length; got++) {
		*((char*)buf)++ = inb (p->base+EPPREG);
		if (inb (p->base+STATUS) & 0x01)
			break;
	}
	return got;
}

size_t parport_pc_epp_write_block(struct parport *p, void *buf, size_t length)
{
	size_t written = 0;
	for (; written < length; written++) {
		outb (*((char*)buf)++, p->base+EPPREG);
		if (inb (p->base+STATUS) & 0x01)
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

int parport_pc_examine_irq(struct parport *p)
{
	return 0; /* FIXME */
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
	
	parport_pc_epp_write_block,
	parport_pc_epp_read_block,

	parport_pc_ecp_write_block,
	parport_pc_ecp_read_block,
	
	parport_pc_save_state,
	parport_pc_restore_state,

	parport_pc_enable_irq,
	parport_pc_disable_irq,
	parport_pc_examine_irq,

	parport_pc_inc_use_count,
	parport_pc_dec_use_count
};

/* --- DMA detection -------------------------------------- */

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
	unsigned char dma, oldstate = parport_pc_read_econtrol(pb);

	parport_pc_write_econtrol(pb, 0xe0); /* Configuration MODE */
	
	dma = parport_pc_read_configb(pb) & 0x07;

	parport_pc_write_econtrol(pb, oldstate);
	
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
	unsigned char dsr,dsr_read;
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

/* --- Mode detection ------------------------------------- */

/*
 * Clear TIMEOUT BIT in EPP MODE
 */
static int epp_clear_timeout(struct parport *pb)
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
static int parport_SPP_supported(struct parport *pb)
{
	/* Do a simple read-write test to make sure the port exists. */
	parport_pc_write_control(pb, 0xc);
	parport_pc_write_data(pb, 0xaa);
	if (parport_pc_read_data(pb) != 0xaa) return 0;
	
	parport_pc_write_data(pb, 0x55);
	if (parport_pc_read_data(pb) != 0x55) return 0;

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
	unsigned char r, octr = parport_pc_read_control(pb);
	unsigned char oecr = parport_pc_read_econtrol(pb);
	unsigned char tmp;

	r = parport_pc_read_control(pb);	
	if ((parport_pc_read_econtrol(pb) & 0x3) == (r & 0x3)) {
		parport_pc_write_control(pb, r ^ 0x2 ); /* Toggle bit 1 */

		r = parport_pc_read_control(pb);	
		if ((parport_pc_read_econtrol(pb) & 0x2) == (r & 0x2)) {
			parport_pc_write_control(pb, octr);
			return 0; /* Sure that no ECR register exists */
		}
	}
	
	if ((parport_pc_read_econtrol(pb) & 0x3 ) != 0x1)
		return 0;

	parport_pc_write_econtrol(pb, 0x34);
	tmp = parport_pc_read_econtrol(pb);

	parport_pc_write_econtrol(pb, oecr);
	parport_pc_write_control(pb, octr);
	
	if (tmp != 0x35)
		return 0;

	return PARPORT_MODE_PCECR;
}

static int parport_ECP_supported(struct parport *pb)
{
	int i;
	unsigned char oecr = parport_pc_read_econtrol(pb);
	
	/* If there is no ECR, we have no hope of supporting ECP. */
	if (!(pb->modes & PARPORT_MODE_PCECR))
		return 0;

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
static int parport_EPP_supported(struct parport *pb)
{
	/* If EPP timeout bit clear then EPP available */
	if (!epp_clear_timeout(pb))
		return 0;  /* No way to clear timeout */

	parport_pc_write_control(pb, parport_pc_read_control(pb) | 0x20);
	parport_pc_write_control(pb, parport_pc_read_control(pb) | 0x10);
	epp_clear_timeout(pb);
	
	parport_pc_read_epp(pb);
	udelay(30);  /* Wait for possible EPP timeout */
	
	if (parport_pc_read_status(pb) & 0x01) {
		epp_clear_timeout(pb);
		return PARPORT_MODE_PCEPP;
	}

	return 0;
}

static int parport_ECPEPP_supported(struct parport *pb)
{
	int mode;
	unsigned char oecr = parport_pc_read_econtrol(pb);

	if (!(pb->modes & PARPORT_MODE_PCECR))
		return 0;
	
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

static int parport_PS2_supported(struct parport *pb)
{
	int ok = 0;
	unsigned char octr = parport_pc_read_control(pb);
  
	epp_clear_timeout(pb);

	parport_pc_write_control(pb, octr | 0x20);  /* try to tri-state the buffer */
	
	parport_pc_write_data(pb, 0x55);
	if (parport_pc_read_data(pb) != 0x55) ok++;

	parport_pc_write_data(pb, 0xaa);
	if (parport_pc_read_data(pb) != 0xaa) ok++;
	
	parport_pc_write_control(pb, octr);          /* cancel input mode */

	return ok?PARPORT_MODE_PCPS2:0;
}

static int parport_ECPPS2_supported(struct parport *pb)
{
	int mode;
	unsigned char oecr = parport_pc_read_econtrol(pb);

	if (!(pb->modes & PARPORT_MODE_PCECR))
		return 0;
	
	parport_pc_write_econtrol(pb, 0x20);
	
	mode = parport_PS2_supported(pb);

	parport_pc_write_econtrol(pb, oecr);
	return mode?PARPORT_MODE_PCECPPS2:0;
}

/* --- IRQ detection -------------------------------------- */

/* This code is for detecting ECP interrupts (due to problems with the
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

static int irq_probe_ECP(struct parport *pb)
{
	int irqs, i;
	unsigned char oecr = parport_pc_read_econtrol(pb);
		
	probe_irq_off(probe_irq_on());	/* Clear any interrupts */
	irqs = open_intr_election();
		
	parport_pc_write_econtrol(pb, 0x00);	    /* Reset FIFO */
	parport_pc_write_econtrol(pb, 0xd0);	    /* TEST FIFO + nErrIntrEn */

	/* If Full FIFO sure that WriteIntrThresold is generated */
	for (i=0; i < 1024 && !(parport_pc_read_econtrol(pb) & 0x02) ; i++) 
		parport_pc_write_fifo(pb, 0xaa);
		
	pb->irq = close_intr_election(irqs);
	parport_pc_write_econtrol(pb, oecr);
	return pb->irq;
}

/*
 * This detection seems that only works in National Semiconductors
 * This doesn't work in SMC, LGS, and Winbond 
 */
static int irq_probe_EPP(struct parport *pb)
{
	int irqs;
	unsigned char octr = parport_pc_read_control(pb);
	unsigned char oecr = parport_pc_read_econtrol(pb);

#ifndef ADVANCED_DETECT
	return PARPORT_IRQ_NONE;
#endif
	
	probe_irq_off(probe_irq_on());	/* Clear any interrupts */
	irqs = open_intr_election();

	if (pb->modes & PARPORT_MODE_PCECR)
		parport_pc_write_econtrol(pb, parport_pc_read_econtrol(pb) | 0x10);
	
	epp_clear_timeout(pb);
	parport_pc_write_control(pb, parport_pc_read_control(pb) | 0x20);
	parport_pc_write_control(pb, parport_pc_read_control(pb) | 0x10);
	epp_clear_timeout(pb);

	/*  Device isn't expecting an EPP read
	 * and generates an IRQ.
	 */
	parport_pc_read_epp(pb);
	udelay(20);

	pb->irq = close_intr_election(irqs);
	parport_pc_write_econtrol(pb, oecr);
	parport_pc_write_control(pb, octr);
	return pb->irq;
}

static int irq_probe_SPP(struct parport *pb)
{
	int irqs;
	unsigned char octr = parport_pc_read_control(pb);
	unsigned char oecr = parport_pc_read_econtrol(pb);

#ifndef ADVANCED_DETECT
	return PARPORT_IRQ_NONE;
#endif

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
static int parport_irq_probe(struct parport *pb)
{
	if (pb->modes & PARPORT_MODE_PCECR)
		pb->irq = programmable_irq_support(pb);

	if (pb->modes & PARPORT_MODE_PCECP)
		pb->irq = irq_probe_ECP(pb);
			
	if (pb->irq == PARPORT_IRQ_NONE && 
	    (pb->modes & PARPORT_MODE_PCECPEPP)) {
		unsigned char oecr = parport_pc_read_econtrol(pb);
		parport_pc_write_econtrol(pb, 0x80);
		pb->irq = irq_probe_EPP(pb);
		parport_pc_write_econtrol(pb, oecr);
	}

	epp_clear_timeout(pb);

	if (pb->irq == PARPORT_IRQ_NONE && (pb->modes & PARPORT_MODE_PCEPP))
		pb->irq = irq_probe_EPP(pb);

	epp_clear_timeout(pb);

	if (pb->irq == PARPORT_IRQ_NONE)
		pb->irq = irq_probe_SPP(pb);

	return pb->irq;
}

/* --- Initialisation code -------------------------------- */

static int probe_one_port(unsigned long int base, int irq, int dma)
{
	struct parport tmpport, *p;
	if (check_region(base, 3)) return 0;
	tmpport.base = base;
	tmpport.ops = &parport_pc_ops;
	if (!(parport_SPP_supported(&tmpport))) return 0;
       	if (!(p = parport_register_port(base, irq, dma, &parport_pc_ops))) return 0;
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
	printk(KERN_INFO "%s: PC-style at 0x%lx", p->name, p->base);
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
	parport_pc_write_control(p, 0xc);
	parport_pc_write_data(p, 0);

	if (parport_probe_hook)
		(*parport_probe_hook)(p);

	return 1;
}

int parport_pc_init(int *io, int *irq, int *dma)
{
	int count = 0, i = 0;
	if (io && *io) {
		/* Only probe the ports we were given. */
		do {
			count += probe_one_port(*(io++), *(irq++), *(dma++));
		} while (*io && (++i < PARPORT_PC_MAX_PORTS));
	} else {
		/* Probe all the likely ports. */
		count += probe_one_port(0x3bc, PARPORT_IRQ_AUTO, PARPORT_DMA_AUTO);
		count += probe_one_port(0x378, PARPORT_IRQ_AUTO, PARPORT_DMA_AUTO);
		count += probe_one_port(0x278, PARPORT_IRQ_AUTO, PARPORT_DMA_AUTO);
	}

	/* Give any attached devices a chance to gather their thoughts */
	current->state = TASK_INTERRUPTIBLE;
	current->timeout = jiffies + 75;
	schedule ();

	return count;
}

#ifdef MODULE
static int io[PARPORT_PC_MAX_PORTS+1] = { [0 ... PARPORT_PC_MAX_PORTS] = 0 };
static int dma[PARPORT_PC_MAX_PORTS] = { [0 ... PARPORT_PC_MAX_PORTS-1] = PARPORT_DMA_AUTO };
static int irq[PARPORT_PC_MAX_PORTS] = { [0 ... PARPORT_PC_MAX_PORTS-1] = PARPORT_IRQ_AUTO };
MODULE_PARM(io, "1-" __MODULE_STRING(PARPORT_PC_MAX_PORTS) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(PARPORT_PC_MAX_PORTS) "i");
MODULE_PARM(dma, "1-" __MODULE_STRING(PARPORT_PC_MAX_PORTS) "i");

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
