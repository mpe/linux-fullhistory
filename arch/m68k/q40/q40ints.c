/*
 * arch/m68k/q40/q40ints.c
 *
 * Copyright (C) 1999 Richard Zidlicky
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * losely based on bvme6000ints.c
 *
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>

#include <asm/q40_master.h>
#include <asm/q40ints.h>

/* 
 * Q40 IRQs are defined as follows: 
 *            3,4,5,6,7,10,11,14,15 : ISA dev IRQs
 *            16-31: reserved
 *            32   : keyboard int
 *            33   : frame int (50 Hz periodic timer)
 *            34   : sample int (10/20 KHz periodic timer)
 *          
*/

extern int ints_inited;


void q40_irq2_handler (int, void *, struct pt_regs *fp);


extern void (*q40_sys_default_handler[]) (int, void *, struct pt_regs *);  /* added just for debugging */

static void q40_defhand (int irq, void *dev_id, struct pt_regs *fp);
static void sys_default_handler(int lev, void *dev_id, struct pt_regs *regs);

/*
 * This should ideally be 4 elements only, for speed.
 */

#define DEVNAME_SIZE 24

static struct {
	void		(*handler)(int, void *, struct pt_regs *);
	unsigned long	flags;
	void		*dev_id;
        char	        devname[DEVNAME_SIZE];
	unsigned	count;
} irq_tab[Q40_IRQ_MAX+1];

/*
 * void q40_init_IRQ (void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function is called during kernel startup to initialize
 * the q40 IRQ handling routines.
 */

void q40_init_IRQ (void)
{
	int i;

	for (i = 0; i <= Q40_IRQ_MAX; i++) {
		irq_tab[i].handler = q40_defhand;
		irq_tab[i].flags = IRQ_FLG_STD;
		irq_tab[i].dev_id = NULL;
		irq_tab[i].devname[0] = 0;
		irq_tab[i].count = 0;
	}

	/* setup handler for ISA ints */
	sys_request_irq(IRQ2,q40_irq2_handler, IRQ_FLG_LOCK, "q40 ISA and master chip", NULL);

	/* now enable some ints.. */

#if 0  /* has been abandoned */
	master_outb(1,SER_ENABLE_REG);
#endif
	master_outb(1,EXT_ENABLE_REG);

	/* would be spurious ints by now, q40kbd_init_hw() does that */
	master_outb(0,KEY_IRQ_ENABLE_REG);
}

int q40_request_irq(unsigned int irq,
		void (*handler)(int, void *, struct pt_regs *),
                unsigned long flags, const char *devname, void *dev_id)
{
  /*printk("q40_request_irq %d, %s\n",irq,devname);*/

	if (irq > Q40_IRQ_MAX || (irq>15 && irq<32)) {
		printk("%s: Incorrect IRQ %d from %s\n", __FUNCTION__, irq, devname);
		return -ENXIO;
	}

	/* test for ISA ints not implemented by HW */
	if (irq<15) 
	  {
	    switch (irq){
	    case 1: case 2: case 8: case 9:
	    case 12: case 13:
	      printk("%s: ISA IRQ %d from %s not implemented by HW\n", __FUNCTION__, irq, devname);
	      return -ENXIO;
	    default:
	    }
	  }

	if (irq<Q40_IRQ_TIMER)
	  {
	    if (irq==11) {
	      printk("warning IRQ 10 and 11 not distinguishable\n");
	      irq=10;
	    }
	    if (!(irq_tab[irq].flags & IRQ_FLG_STD)) 
	      {
		if (irq_tab[irq].flags & IRQ_FLG_LOCK) 
		  {
		    printk("%s: IRQ %d from %s is not replaceable\n",
			   __FUNCTION__, irq, irq_tab[irq].devname);
		    return -EBUSY;
		  }
		if (flags & IRQ_FLG_REPLACE) 
		  {
		    printk("%s: %s can't replace IRQ %d from %s\n",
			   __FUNCTION__, devname, irq, irq_tab[irq].devname);
		    return -EBUSY;
		  }
	      }
	    /*printk("IRQ %d set to handler %p\n",irq,handler);*/
	    irq_tab[irq].handler = handler;
	    irq_tab[irq].flags   = flags;
	    irq_tab[irq].dev_id  = dev_id;
	    strncpy(irq_tab[irq].devname,devname,DEVNAME_SIZE);
	    return 0;
	  }
	else {
	  /* Q40_IRQ_TIMER :somewhat special actions required here ..*/
	  sys_request_irq(4,handler,flags,devname,dev_id);
	  sys_request_irq(6,handler,flags,devname,dev_id);
	  return 0;
	}
}

void q40_free_irq(unsigned int irq, void *dev_id)
{
	if (irq > Q40_IRQ_MAX || (irq>15 && irq<32)) {
		printk("%s: Incorrect IRQ %d, dev_id %x \n", __FUNCTION__, irq, (unsigned)dev_id);
		return;
	}

	/* test for ISA ints not implemented by HW */
	if (irq<15) {
	  switch (irq){
	  case 1: case 2: case 8: case 9:
	  case 12: case 13:
	        printk("%s: ISA IRQ %d from %x illegal\n", __FUNCTION__, irq, (unsigned)dev_id);
		return;
	  default:
		  }
	}
	
	if (irq<Q40_IRQ_TIMER){
	  if (irq==11) irq=10;
	  if (irq_tab[irq].dev_id != dev_id)
	    printk("%s: Removing probably wrong IRQ %d from %s\n",
		   __FUNCTION__, irq, irq_tab[irq].devname);
	  
	  irq_tab[irq].handler = q40_defhand;
	  irq_tab[irq].flags   = IRQ_FLG_STD;
	  irq_tab[irq].dev_id  = NULL;
	  /* irq_tab[irq].devname = NULL; */
	} else { /* == Q40_IRQ_TIMER */
	  sys_free_irq(4,dev_id);
	  sys_free_irq(6,dev_id);
	}
}

#if 1
void q40_process_int (int level, struct pt_regs *fp)
{
  printk("unexpected interrupt %x\n",level);
}
#endif

/* 
 * tables to translate bits into IRQ numbers 
 * it is a good idea to order the entries by priority
 * 
*/

struct IRQ_TABLE{ unsigned mask; int irq ;};

static struct IRQ_TABLE iirqs[]={
  {IRQ_FRAME_MASK,Q40_IRQ_FRAME},
  {IRQ_KEYB_MASK,Q40_IRQ_KEYBOARD},
  {0,0}};
static struct IRQ_TABLE eirqs[]={
  {IRQ3_MASK,3},                   /* ser 1 */
  {IRQ4_MASK,4},                   /* ser 2 */
  {IRQ14_MASK,14},                 /* IDE 1 */
  {IRQ15_MASK,15},                 /* IDE 2 */
  {IRQ6_MASK,6},                   /* floppy */
  {IRQ7_MASK,7},                   /* par */

  {IRQ5_MASK,5},
  {IRQ10_MASK,10},




  {0,0}};

/* complaiun only this many times about spurious ints : */
static int ccleirq=60;    /* ISA dev IRQ's*/
static int cclirq=60;     /* internal */

/* FIX: add IRQ_INPROGRESS,mask,unmask,probing.... */

void q40_irq2_handler (int vec, void *devname, struct pt_regs *fp)
{
  /* got level 2 interrupt, dispatch to ISA or keyboard IRQs */

        unsigned mir=master_inb(IIRQ_REG);
	unsigned mer;
	int irq,i;

	/*
	 *  more than 1 bit might be set, must handle atmost 1 int source,
	 *  - handle only those with explicitly set handler
	 */

	if ((mir&IRQ_SER_MASK) || (mir&IRQ_EXT_MASK)) 
	  {
	    
	    /* some ISA dev caused the int */
	    
	    mer=master_inb(EIRQ_REG);
	    
	    for (i=0; eirqs[i].mask; i++)
	      {
		if (mer&(eirqs[i].mask)) 
		  {
		    irq=eirqs[i].irq;
		    irq_tab[irq].count++;
		    if (irq_tab[irq].handler == q40_defhand )
		      continue; /* ignore uninited INTs :-( */
		    
		    irq_tab[irq].handler(irq,irq_tab[irq].dev_id,fp);
		    return;
		  }
	      }
	    if (ccleirq>0) 
	      printk("ISA interrupt from unknown source? EIRQ_REG = %x\n",mer),ccleirq--;
	  } 
	else 
	  {
	    /* internal */

	    for (i=0; iirqs[i].mask; i++)
	      {
		if (mir&(iirqs[i].mask)) 
		  {
		    irq=iirqs[i].irq;
		    irq_tab[irq].count++;
		    if (irq_tab[irq].handler == q40_defhand )
		      continue; /* ignore uninited INTs :-( */
		    
		    irq_tab[irq].handler(irq,irq_tab[irq].dev_id,fp);
		    return;
		  }
	      }
	    if (cclirq>0)
	      printk("internal level 2 interrupt from unknown source ? IIRQ_REG=%x\n",mir),cclirq--;
	  }
}

int q40_get_irq_list (char *buf)
{
	int i, len = 0;

	for (i = 0; i <= Q40_IRQ_MAX; i++) {
		if (irq_tab[i].count)
			len += sprintf (buf+len, "Vec 0x%02x: %8d  %s%s\n",
			    i, irq_tab[i].count,
			    irq_tab[i].devname[0] ? irq_tab[i].devname : "?",
			    irq_tab[i].handler == q40_defhand ? 
					" (now unassigned)" : "");
	}
	return len;
}


static void q40_defhand (int irq, void *dev_id, struct pt_regs *fp)
{
#if 0
	printk ("Unknown q40 interrupt 0x%02x\n", irq);
#endif
}
static void sys_default_handler(int lev, void *dev_id, struct pt_regs *regs)
{
#if 0
        if (ints_inited)
#endif
	  printk ("Uninitialised interrupt level %d\n", lev);
#if 0
	else
	  printk ("Interrupt before interrupt initialisation\n");
#endif
}

 void (*q40_sys_default_handler[SYS_IRQS]) (int, void *, struct pt_regs *) = {
   sys_default_handler,sys_default_handler,sys_default_handler,sys_default_handler,
   sys_default_handler,sys_default_handler,sys_default_handler,sys_default_handler
 };

void q40_enable_irq (unsigned int irq)
{
}


void q40_disable_irq (unsigned int irq)
{
}

unsigned long q40_probe_irq_on (void)
{
  printk("sorry, irq probing not yet implemented - reconfigure the driver to avoid this\n");
  return 0;
}
int q40_probe_irq_off (unsigned long irqs)
{
  return -1;
}
