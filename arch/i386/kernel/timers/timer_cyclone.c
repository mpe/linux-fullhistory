/*	Cyclone-timer: 
 *		This code implements timer_ops for the cyclone counter found
 *		on IBM x440, x360, and other Summit based systems.
 *
 *	Copyright (C) 2002 IBM, John Stultz (johnstul@us.ibm.com)
 */


#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <linux/errno.h>

#include <asm/timer.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/fixmap.h>

extern spinlock_t i8253_lock;

/* Number of usecs that the last interrupt was delayed */
static int delay_at_last_interrupt;

#define CYCLONE_CBAR_ADDR 0xFEB00CD0
#define CYCLONE_PMCC_OFFSET 0x51A0
#define CYCLONE_MPMC_OFFSET 0x51D0
#define CYCLONE_MPCS_OFFSET 0x51A8
#define CYCLONE_TIMER_FREQ 100000000

int use_cyclone = 0;

static u32* volatile cyclone_timer;	/* Cyclone MPMC0 register */
static u32 last_cyclone_timer;

static void mark_offset_cyclone(void)
{
	int count;
	spin_lock(&i8253_lock);
	/* quickly read the cyclone timer */
	if(cyclone_timer)
		last_cyclone_timer = cyclone_timer[0];

	/* calculate delay_at_last_interrupt */
	outb_p(0x00, 0x43);     /* latch the count ASAP */

	count = inb_p(0x40);    /* read the latched count */
	count |= inb(0x40) << 8;
	spin_unlock(&i8253_lock);

	count = ((LATCH-1) - count) * TICK_SIZE;
	delay_at_last_interrupt = (count + LATCH/2) / LATCH;
}

static unsigned long get_offset_cyclone(void)
{
	u32 offset;

	if(!cyclone_timer)
		return delay_at_last_interrupt;

	/* Read the cyclone timer */
	offset = cyclone_timer[0];

	/* .. relative to previous jiffy */
	offset = offset - last_cyclone_timer;

	/* convert cyclone ticks to microseconds */	
	/* XXX slow, can we speed this up? */
	offset = offset/(CYCLONE_TIMER_FREQ/1000000);

	/* our adjusted time offset in microseconds */
	return delay_at_last_interrupt + offset;
}

static int init_cyclone(void)
{
	u32* reg;	
	u32 base;		/* saved cyclone base address */
	u32 pageaddr;	/* page that contains cyclone_timer register */
	u32 offset;		/* offset from pageaddr to cyclone_timer register */
	int i;
	
	/*make sure we're on a summit box*/
	/*XXX need to use proper summit hooks! such as xapic -john*/
	if(!use_cyclone) return -ENODEV; 
	
	printk(KERN_INFO "Summit chipset: Starting Cyclone Counter.\n");

	/* find base address */
	pageaddr = (CYCLONE_CBAR_ADDR)&PAGE_MASK;
	offset = (CYCLONE_CBAR_ADDR)&(~PAGE_MASK);
	set_fixmap_nocache(FIX_CYCLONE_TIMER, pageaddr);
	reg = (u32*)(fix_to_virt(FIX_CYCLONE_TIMER) + offset);
	if(!reg){
		printk(KERN_ERR "Summit chipset: Could not find valid CBAR register.\n");
		return -ENODEV;
	}
	base = *reg;	
	if(!base){
		printk(KERN_ERR "Summit chipset: Could not find valid CBAR value.\n");
		return -ENODEV;
	}
	
	/* setup PMCC */
	pageaddr = (base + CYCLONE_PMCC_OFFSET)&PAGE_MASK;
	offset = (base + CYCLONE_PMCC_OFFSET)&(~PAGE_MASK);
	set_fixmap_nocache(FIX_CYCLONE_TIMER, pageaddr);
	reg = (u32*)(fix_to_virt(FIX_CYCLONE_TIMER) + offset);
	if(!reg){
		printk(KERN_ERR "Summit chipset: Could not find valid PMCC register.\n");
		return -ENODEV;
	}
	reg[0] = 0x00000001;

	/* setup MPCS */
	pageaddr = (base + CYCLONE_MPCS_OFFSET)&PAGE_MASK;
	offset = (base + CYCLONE_MPCS_OFFSET)&(~PAGE_MASK);
	set_fixmap_nocache(FIX_CYCLONE_TIMER, pageaddr);
	reg = (u32*)(fix_to_virt(FIX_CYCLONE_TIMER) + offset);
	if(!reg){
		printk(KERN_ERR "Summit chipset: Could not find valid MPCS register.\n");
		return -ENODEV;
	}
	reg[0] = 0x00000001;

	/* map in cyclone_timer */
	pageaddr = (base + CYCLONE_MPMC_OFFSET)&PAGE_MASK;
	offset = (base + CYCLONE_MPMC_OFFSET)&(~PAGE_MASK);
	set_fixmap_nocache(FIX_CYCLONE_TIMER, pageaddr);
	cyclone_timer = (u32*)(fix_to_virt(FIX_CYCLONE_TIMER) + offset);
	if(!cyclone_timer){
		printk(KERN_ERR "Summit chipset: Could not find valid MPMC register.\n");
		return -ENODEV;
	}

	/*quick test to make sure its ticking*/
	for(i=0; i<3; i++){
		u32 old = cyclone_timer[0];
		int stall = 100;
		while(stall--) barrier();
		if(cyclone_timer[0] == old){
			printk(KERN_ERR "Summit chipset: Counter not counting! DISABLED\n");
			cyclone_timer = 0;
			return -ENODEV;
		}
	}

	/* Everything looks good! */
	return 0;
}


static void delay_cyclone(unsigned long loops)
{
	unsigned long bclock, now;
	if(!cyclone_timer)
		return;
	bclock = cyclone_timer[0];
	do {
		rep_nop();
		now = cyclone_timer[0];
	} while ((now-bclock) < loops);
}
/************************************************************/

/* cyclone timer_opts struct */
struct timer_opts timer_cyclone = {
	.init = init_cyclone, 
	.mark_offset = mark_offset_cyclone, 
	.get_offset = get_offset_cyclone,
	.delay = delay_cyclone,
};
