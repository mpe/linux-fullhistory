  /*
   * Copyright 1996 The Australian National University.
   * Copyright 1996 Fujitsu Laboratories Limited
   * 
   * This software may be distributed under the terms of the Gnu
   * Public License version 2 or later
  */
/* routines to control the AP1000 timer chip */   

#include <linux/time.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <asm/ap1000/apservice.h>
#include <asm/ap1000/apreg.h>
#include <asm/irq.h>

#define INIT_TIM1	(781250/HZ)
#define INIT_TIM0	(781250/(10*HZ))

static unsigned long last_freerun;

unsigned ap_freerun(void)
{
	return *((volatile unsigned long *)(MC_FREERUN + 4));
}

void ap_clear_clock_irq(void)
{
	MC_OUT(MC_INTR, AP_CLR_INTR_REQ << MC_INTR_ITIM1_SH);
	last_freerun = *((unsigned long *)(MC_FREERUN + 4));
	tnet_check_completion();
#if 1
	if ((((unsigned)jiffies) % (HZ/4)) == 0) {
		msc_timer();
		ap_xor_led(1);
		bif_timer();
		ap_dbg_flush();
#if 0
		bif_led_status();
#endif
	}
#endif
}


void ap_gettimeofday(struct timeval *xt)
{
	unsigned long d;
	unsigned v;
	unsigned long new_freerun;

	/* this is in 80ns units - we only use the low 32 bits 
	   as 5mins is plenty for this stuff */
	d = new_freerun = *((unsigned long *)(MC_FREERUN + 4)); 

	if (d < last_freerun) {
		/* wraparound */
		d += ((~0) - last_freerun);
	} else {
		d -= last_freerun;
	}

	/* convert to microseconds */
	v = ((d&0xffffff)*10)/125;
	
	/* only want microseconds/HZ */
	v = v%(1000000/HZ);
	
	xt->tv_usec += v;

	last_freerun = new_freerun;
}

static void profile_interrupt(int irq, void *dev_id, struct pt_regs * regs)
{
  if (prof_buffer && current->pid) {
    extern int _stext;
    unsigned long ip = instruction_pointer(regs);
    ip -= (unsigned long) &_stext;
    ip >>= prof_shift;
    if (ip < prof_len)
      prof_buffer[ip]++;
  }  
  MC_OUT(MC_INTR,AP_CLR_INTR_REQ << MC_INTR_ITIM0_SH);
}

void ap_profile_init(void)
{
  if (prof_shift) {
    printk("Initialising profiling with prof_shift=%d\n",(int)prof_shift);
    MC_OUT(MC_INTR,AP_CLR_INTR_REQ << MC_INTR_ITIM0_SH);
    MC_OUT(MC_INTR,AP_CLR_INTR_MASK << MC_INTR_ITIM0_SH);
  }
}

void ap_init_timers(void)
{
	extern void timer_interrupt(int irq, void *dev_id, struct pt_regs * regs);
	unsigned flags;

	printk("Initialising ap1000 timer\n");

	save_flags(flags); cli();
	
	request_irq(APTIM1_IRQ,
		    timer_interrupt,
		    (SA_INTERRUPT | SA_STATIC_ALLOC),
		    "timer", NULL);

	request_irq(APTIM0_IRQ,
		    profile_interrupt,
		    (SA_INTERRUPT | SA_STATIC_ALLOC),
		    "profile", NULL);
	
	ap_clear_clock_irq();
	
	MC_OUT(MC_ITIMER0,INIT_TIM0);
	MC_OUT(MC_ITIMER1,INIT_TIM1);
	MC_OUT(MC_INTR,AP_CLR_INTR_REQ << MC_INTR_ITIM1_SH);
	MC_OUT(MC_INTR,AP_CLR_INTR_MASK << MC_INTR_ITIM1_SH);
	MC_OUT(MC_INTR,AP_SET_INTR_MASK << MC_INTR_ITIM0_SH);
	restore_flags(flags); 
}
