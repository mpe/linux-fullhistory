/* $Id: parport_init.c,v 1.1.2.4 1997/04/01 18:19:10 phil Exp $
 * Parallel-port initialisation code.
 * 
 * Authors: David Campbell <campbell@tirian.che.curtin.edu.au>
 *          Tim Waugh <tmw20@cam.ac.uk>
 *	    Jose Renau <renau@acm.org>
 *
 * based on work by Grant Guenther <grant@torque.net>
 *              and Philip Blundell <Philip.Blundell@pobox.com>
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/tasks.h>
#include <asm/io.h>
#include <asm/dma.h>

#include <linux/parport.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/malloc.h>

#include "parport_ll_io.h"

static int io[PARPORT_MAX] = { 0, };
static int irq[PARPORT_MAX] = { -1, };
static int dma[PARPORT_MAX] = { -1, };

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

static int parport_detect_dma_transfer(int dma,int size)
{
	int i,n,retv;
	int count=0;

	retv = -1;
	for (i = 0; i < 8; i++)
		if (dma & (1 << i)) {
			disable_dma(i);
			clear_dma_ff(i);
			n = get_dma_residue(i);
			if (n != size) {
				retv = i;
				if (count > 0) {
					retv = -1;	/* Multiple DMA's */
					printk("Multiple DMA detected.\n");
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
	int dma;

	w_ecr(pb,0xE0); /* Configuration MODE */
	
	dma = r_cnfgB(pb) & 0x07;

	w_ecr(pb,pb->ecr);
	
	if( dma == 0 || dma == 4 ) /* Jumper selection */
		return -1;
	else
		return dma;
}

/* Only called if port support ECP mode.
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
 * A value of -1 is allowed indicating no DMA support.
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
	if( retv != -1 )
		return retv;
	
	buff = kmalloc(16, GFP_KERNEL | GFP_DMA);
	if( !buff ){
	    printk("parport: memory squezze\n");
	    return -1;
	}
	
 	dsr = r_ctr(pb);
	dsr_read = (dsr & ~(0x20)) | 0x04;    /* Direction == read */

	w_ecr(pb, 0xc0);	   /* ECP MODE */
 	w_ctr(pb, dsr_read );
	dma=parport_prepare_dma(buff,8);
	w_ecr(pb, 0xd8);	   /* ECP FIFO + enable DMA */
	parport_enable_dma(dma);
	udelay(30);           /* Give some for DMA tranfer */
	retv = parport_detect_dma_transfer(dma,8);
	
	/*
	 * National Semiconductors only supports DMA tranfers
	 * in ECP MODE
	 */
	if( retv == -1 ){
		w_ecr(pb, 0x60);	   /* ECP MODE */
		w_ctr(pb, dsr_read );
		dma=parport_prepare_dma(buff,8);
		w_ecr(pb, 0x68);	   /* ECP FIFO + enable DMA */
		parport_enable_dma(dma);
		udelay(30);           /* Give some for DMA tranfer */
		retv = parport_detect_dma_transfer(dma,8);
	}
	
	kfree(buff);
	
	w_ctr(pb, pb->ctr);
	w_ecr(pb, pb->ecr);

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

    if( !(r_str(pb) & 0x01) )
		return 1;

	/* To clear timeout some chip requiere double read */
	r_str(pb);
	r = r_str(pb);
	w_str(pb, r | 0x01); /* Some reset by writing 1 */
	w_str(pb, r & 0xfe); /* Others by writing 0 */
	r = r_str(pb);

	return !(r & 0x01);
}


/*
 * Checks for por existence, all ports support SPP MODE
 */
static int parport_SPP_supported(struct parport *pb)
{
	int r,rr;
	
	/* Do a simple read-write test to make sure the port exists. */
	w_dtr(pb, 0xaa);
	r = r_dtr(pb);
	
	w_dtr(pb, 0x55);
	rr = r_dtr(pb);
	
	if (r != 0xaa || rr != 0x55) {
		return 0;
	}
	
	return PARPORT_MODE_SPP;
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
	int r;

	if( pb->base == 0x3BC )
		return 0;
	
	r= r_ctr(pb);	
	if( (r_ecr(pb) & 0x03) == (r & 0x03) ){
		w_ctr(pb, r ^ 0x03 ); /* Toggle bits 0-1 */

		r= r_ctr(pb);	
		if( (r_ecr(pb) & 0x03) == (r & 0x03) )
			return 0; /* Sure that no ECR register exists */
	}
	
	if( (r_ecr(pb) & 0x03 ) != 0x01 )
		return 0;

	w_ecr(pb,0x34);
	if( r_ecr(pb) != 0x35 )
		return 0;

	w_ecr(pb,pb->ecr);
	w_ctr(pb,pb->ctr);
	
	return PARPORT_MODE_ECR;
}

static int parport_ECP_supported(struct parport *pb)
{
	int i;
	
	if( !(pb->modes & PARPORT_MODE_ECR) )
		return 0;
	/*
	 * Usign LGS chipset it uses ECR register, but
	 * it doesn't support ECP or FIFO MODE
	 */
	
	w_ecr(pb,0xc0); /* TEST FIFO */
	for( i=0 ; i < 1024 && (r_ecr(pb) & 0x01) ; i++ )
		w_fifo(pb, 0xaa);

	w_ecr(pb,pb->ecr);

	if( i >= 1024 )
		return 0;
	
	return PARPORT_MODE_ECP;
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
	if( pb->base == 0x3BC )
		return 0;

	/* If EPP timeout bit clear then EPP available */
	if( !epp_clear_timeout(pb) )
		return 0;  /* No way to clear timeout */

	w_ctr(pb, r_ctr(pb) | 0x20);
	w_ctr(pb, r_ctr(pb) | 0x10);
	epp_clear_timeout(pb);
	
	r_epp(pb);
	udelay(30);  /* Wait for possible EPP timeout */
	
	if( r_str(pb) & 0x01 ){
		epp_clear_timeout(pb);
		return PARPORT_MODE_EPP;
	}

	return 0;
}

static int parport_ECPEPP_supported(struct parport *pb)
{
	int mode;

	if( !(pb->modes & PARPORT_MODE_ECR) )
		return 0;
	
	/* Search for SMC style EPP+ECP mode */
	w_ecr(pb, 0x80);
	
	mode = parport_EPP_supported(pb);

	w_ecr(pb,pb->ecr);
	
	if( mode )
		return PARPORT_MODE_ECPEPP;
	
	return 0;
}

/* Detect LP_PS2 support
 * Bit 5 (0x20) sets the PS/2 data direction, setting this high
 * allows us to read data from the data lines, old style SPP ports
 * will return 0xff.  This may not be reliable if there is a
 * peripheral attached to the port. 
 */
static int parport_PS2_supported(struct parport *pb)
{
	int r,rr;

	epp_clear_timeout(pb);

	w_ctr(pb, pb->ctr | 0x20);	/* Tri-state the buffer */
	
	w_dtr(pb, 0xAA);
	r = r_dtr(pb);

	w_dtr(pb, 0x55);
	rr = r_dtr(pb);
	
	w_ctr(pb, pb->ctr);	/* Reset CTR register */

	if (r != 0xAA || rr != 0x55 )
		return PARPORT_MODE_PS2;
	
	return 0;
}

static int parport_ECPPS2_supported(struct parport *pb)
{
	int mode;

	if( !(pb->modes & PARPORT_MODE_ECR) )
		return 0;
	
	w_ecr(pb, 0x20);
	
	mode = parport_PS2_supported(pb);

	w_ecr(pb,pb->ecr);
	
	if (mode)
		return PARPORT_MODE_ECPPS2;
	
	return 0;
}

/******************************************************
 *  IRQ detection section:
 */
/*
 * This code is for detecting ECP interrupts (due to problems with the
 * monolithic interrupt probing routines).
 *
 * In short this is a voting system where the interrupt with the most
 * "votes" is the elected interrupt (it SHOULD work...)
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
	long max_vote = 0;
	int irq = -1;
	int i;

	/* We ignore the timer - irq 0 */
	for (i = 1; i < 16; i++)
		if (tmp & (1 << i)) {
			if (intr_vote[i] > max_vote) {
				if (max_vote)
					return -1;
				max_vote = intr_vote[i];
				irq = i;
			}
			free_irq(i, intr_vote);
		}
	return irq;
}

/* Only if supports ECP mode */
static int programmable_irq_support(struct parport *pb)
{
	int irq;

	w_ecr(pb,0xE0); /* Configuration MODE */
	
	irq = (r_cnfgB(pb) >> 3) & 0x07;

	switch(irq){
	  case 2:
		  irq = 9;
		  break;
	  case 7:
		  irq = 5;
		  break;
	  case 0:
		  irq = -1;
		  break;
	  default:
		  irq += 7;
	}
		  
	w_ecr(pb,pb->ecr);
	
	return irq;
}

static int irq_probe_ECP(struct parport *pb)
{
	int irqs,i;
		
	probe_irq_off(probe_irq_on());	/* Clear any interrupts */
	irqs = open_intr_election();
		
	w_ecr(pb, 0x00);	    /* Reset FIFO */
	w_ctr(pb, pb->ctr );    /* Force direction = 0 */
	w_ecr(pb, 0xd0);	    /* TEST FIFO + nErrIntrEn */

	/* If Full FIFO sure that WriteIntrThresold is generated */
	for( i=0 ; i < 1024 && !(r_ecr(pb) & 0x02) ; i++ ){
		w_fifo(pb, 0xaa);
	}
		
	pb->irq = close_intr_election(irqs);
	if (pb->irq == 0)
		pb->irq = -1;	/* No interrupt detected */
	
	w_ecr(pb, pb->ecr);

	return pb->irq;
}

/*
 * It's called only if supports EPP on National Semiconductors
 * This doesn't work in SMC, LGS, and Winbond 
 */
static int irq_probe_EPP(struct parport *pb)
{
	int irqs;

#ifndef ADVANCED_DETECT
	return -1;
#endif
	
	probe_irq_off(probe_irq_on());	/* Clear any interrupts */
	irqs = open_intr_election();

	if( pb->modes & PARPORT_MODE_ECR )
		w_ecr(pb, r_ecr(pb) | 0x10 );
	
	epp_clear_timeout(pb);
	w_ctr(pb, r_ctr(pb) | 0x20);
	w_ctr(pb, r_ctr(pb) | 0x10);
	epp_clear_timeout(pb);

	/*  Device isn't expecting an EPP read
	 * and generates an IRQ.
	 */
	r_epp(pb);
	udelay(20);

	pb->irq = close_intr_election(irqs);
	if (pb->irq == 0)
		pb->irq = -1;	/* No interrupt detected */
	
	w_ctr(pb,pb->ctr);
	
	return pb->irq;
}

static int irq_probe_SPP(struct parport *pb)
{
	int irqs;

#ifndef ADVANCED_DETECT
	return -1;
#endif

	probe_irq_off(probe_irq_on());	/* Clear any interrupts */
	irqs = probe_irq_on();

	if( pb->modes & PARPORT_MODE_ECR )
		w_ecr(pb, 0x10 );

	w_dtr(pb,0x00);
	w_ctr(pb,0x00);
	w_ctr(pb,0x0c);
	udelay(5);
	w_ctr(pb,0x0d);
	udelay(5);
	w_ctr(pb,0x0c);
	udelay(25);
	w_ctr(pb,0x08);
	udelay(25);
	w_ctr(pb,0x0c);
	udelay(50);

	pb->irq = probe_irq_off(irqs);
	if (pb->irq <= 0)
		pb->irq = -1;	/* No interrupt detected */
	
	w_ctr(pb,pb->ctr);
	
	return pb->irq;
}

/* We will attempt to share interrupt requests since other devices
 * such as sound cards and network cards seem to like using the
 * printer IRQs.
 *
 * When LP_ECP is available we can autoprobe for IRQs.
 * NOTE: If we can autoprobe it, we can register the IRQ.
 */
static int parport_irq_probe(struct parport *pb)
{
	if( pb->modes & PARPORT_MODE_ECR )
		pb->irq = programmable_irq_support(pb);

	if( pb->modes & PARPORT_MODE_ECP )
		pb->irq = irq_probe_ECP(pb);
			
	if( pb->irq == -1 && (pb->modes & PARPORT_MODE_ECPEPP)){
		w_ecr(pb,0x80);
		pb->irq = irq_probe_EPP(pb);
		w_ecr(pb,pb->ecr);
	}

	epp_clear_timeout(pb);

	if( pb->irq == -1 && (pb->modes & PARPORT_MODE_EPP))
		pb->irq = irq_probe_EPP(pb);

	epp_clear_timeout(pb);

	if( pb->irq == -1 )
		pb->irq = irq_probe_SPP(pb);

	return pb->irq;
}


int initialize_parport(struct parport *pb, unsigned long base, int irq, int dma, int count)
{
	/* Check some parameters */
	if (dma < -2) {
		printk("parport: Invalid DMA[%d] at base 0x%lx\n",dma,base);
		return 0;
	}

	if (irq < -2) {
		printk("parport: Invalid IRQ[%d] at base 0x%lx\n",irq,base);
		return 0;
	}
	
	/* Init our structure */
 	memset(pb, 0, sizeof(struct parport));
	pb->base = base;
	pb->irq = irq;
	pb->dma = dma;
	pb->modes = 0;
 	pb->next = NULL;
	pb->devices = pb->cad = pb->lurker = NULL;
	pb->flags = 0;

	/* Before we start, set the control registers to something sensible. */
	pb->ecr = 0xc;
	pb->ctr = 0xc;

	pb->name = kmalloc(15, GFP_KERNEL);
	if (!pb->name) {
		printk("parport: memory squeeze\n");
		return 0;
	}
	sprintf(pb->name, "parport%d", count);

	if (!parport_SPP_supported(pb)) {
		epp_clear_timeout(pb);
		if (!parport_SPP_supported(pb)) {
			kfree(pb->name);
			return 0;
		}
	}

	pb->modes |= PARPORT_MODE_SPP; 	/* All ports support SPP mode. */
	pb->modes |= parport_ECR_present(pb);	
	pb->modes |= parport_ECP_supported(pb);
	pb->modes |= parport_PS2_supported(pb);
	pb->modes |= parport_ECPPS2_supported(pb);
	pb->modes |= parport_EPP_supported(pb);
	pb->modes |= parport_ECPEPP_supported(pb);

	/* Now register regions */
	if ((pb->modes & (PARPORT_MODE_EPP | PARPORT_MODE_ECPEPP)) && 
	    (check_region(pb->base, 8))) {
		printk(KERN_INFO "%s: EPP disabled due to port conflict at %x.\n", pb->name, pb->base + 3);
		pb->modes &= ~(PARPORT_MODE_EPP | PARPORT_MODE_ECPEPP);
	}
	pb->size = (pb->modes & (PARPORT_MODE_EPP | PARPORT_MODE_ECPEPP)) ? 8 : 3;

	request_region(pb->base, pb->size, pb->name);
	if (pb->modes & PARPORT_MODE_ECR)
		request_region(pb->base+0x400, 3, pb->name);

	/* DMA check */
	if (pb->modes & PARPORT_MODE_ECP) {
		if (pb->dma == -1)
			pb->dma = parport_dma_probe(pb);
		else if (pb->dma == -2)
			pb->dma = -1;
	}

	/* IRQ check */
	if (pb->irq == -1)
		pb->irq = parport_irq_probe(pb);
	else if (pb->irq == -2)
		pb->irq = -1;

	return 1;
}

#ifndef MODULE
static int parport_setup_ptr = 0;

void parport_setup(char *str, int *ints)
{
	if (ints[0] == 0 || ints[1] == 0) {
		/* Disable parport if "parport=" or "parport=0" in cmdline */
		io[0] = -2; 
		return;
	}
	if (parport_setup_ptr < PARPORT_MAX) {
		io[parport_setup_ptr] = ints[1];
		if (ints[0]>1) {
			irq[parport_setup_ptr] = ints[2];
			if (ints[0]>2) dma[parport_setup_ptr] = ints[3];
		}
		parport_setup_ptr++;
	} else {
		printk(KERN_ERR "parport=0x%x", ints[1]);
		if (ints[0]>1) {
			printk(",%d", ints[2]);
			if (ints[0]>2) printk(",%d", ints[3]);
		}
		printk(" ignored, too many ports.\n");
	}
}
#endif

#ifdef CONFIG_PNP_PARPORT_AUTOPROBE
extern void parport_probe_one(struct parport *port);
#endif

#ifdef MODULE
MODULE_PARM(io, "1-" __MODULE_STRING(PARPORT_MAX) "i");
MODULE_PARM(irq, "1-" __MODULE_STRING(PARPORT_MAX) "i");
MODULE_PARM(dma, "1-" __MODULE_STRING(PARPORT_MAX) "i");

int init_module(void)
#else
int pnp_parport_init(void)
#endif				/* MODULE */
{
	struct parport *pb;

	printk(KERN_INFO "Parallel port sharing: %s\n",
	       "$Revision: 1.1.2.4 $");

	if (io[0] == -2) return 1; 

	/* Register /proc/parport */
	parport_proc_register(NULL);

	/* Run probes to ensure parport does exist */
#define PORT(a,b,c) \
		if ((pb = parport_register_port((a), (b), (c))))  \
		        parport_destroy(pb); 
	if (io[0]) {
		/* If the user specified any ports, use them */
		int i;
		for (i = 0; io[i] && i < PARPORT_MAX; i++) {
			PORT(io[i], irq[i], dma[i]);
		}
	} else {
		/* Go for the standard ports. */
		PORT(0x378, -1, -1);
		PORT(0x278, -1, -1);
		PORT(0x3bc, -1, -1);
#undef PORT
	}

#ifdef CONFIG_PNP_PARPORT_AUTOPROBE
	for (pb = parport_enumerate(); pb; pb = pb->next)
		parport_probe_one(pb);
#endif

	return 0;
}

#ifdef MODULE
void cleanup_module(void)
{
	struct parport *port, *next;
   
	for (port = parport_enumerate(); port; port = next) {
		next = port->next;
		parport_destroy(port);
		parport_proc_unregister(port);
		kfree(port->name);
		kfree(port);
	}
	
	parport_proc_unregister(NULL);
}
#endif

/* Exported symbols for modules. */

EXPORT_SYMBOL(parport_claim);
EXPORT_SYMBOL(parport_release);
EXPORT_SYMBOL(parport_register_port);
EXPORT_SYMBOL(parport_destroy);
EXPORT_SYMBOL(parport_register_device);
EXPORT_SYMBOL(parport_unregister_device);
EXPORT_SYMBOL(parport_enumerate);
EXPORT_SYMBOL(parport_ieee1284_nibble_mode_ok);

#ifdef CONFIG_PNP_PARPORT_AUTOPROBE
EXPORT_SYMBOL(parport_probe);
EXPORT_SYMBOL(parport_probe_one);
#endif

void inc_parport_count(void)
{
#ifdef MODULE
	MOD_INC_USE_COUNT;
#endif
}

void dec_parport_count(void)
{
#ifdef MODULE
	MOD_DEC_USE_COUNT;
#endif
}
