/*
 *	Machine Check Handler For PII/PIII
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <asm/processor.h> 
#include <asm/msr.h>

static int banks = 0;

void mcheck_fault(void)
{
	int recover=1;
	u32 alow, ahigh, high, low;
	u32 mcgstl, mcgsth;
	int i;
	
	rdmsr(0x17a, mcgstl, mcgsth);
	if(mcgstl&(1<<0))	/* Recoverable ? */
		recover=0;

	printk(KERN_EMERG "CPU %d: Machine Check Exception: %08x%08x", smp_processor_id(), mcgstl, mcgsth);
	
	for(i=0;i<banks;i++)
	{
		rdmsr(0x401+i*4,low, high);
		if(high&(1<<31))
		{
			if(high&(1<<29))
				recover|=1;
			if(high&(1<<25))
				recover|=2;
			printk(KERN_EMERG "Bank %d: %08x%08x", i, high, low);
			high&=~(1<<31);
			if(high&(1<<27))
			{
				rdmsr(0x402+i*4, alow, ahigh);
				printk("[%08x%08x]", alow, ahigh);
			}
			if(high&(1<<26))
			{
				rdmsr(0x402+i*4, alow, ahigh);
				printk(" at %08x%08x", 
					high, low);
			}
			wrmsr(0x401+i*4, low, high);
		}
	}
	
	if(recover&2)
		panic("CPU context corrupt");
	if(recover&1)
		panic("Unable to continue");
	printk(KERN_EMERG "Attempting to continue.\n");
	mcgstl&=~(1<<2);
	wrmsr(0x17a,mcgstl, mcgsth);
}


/*
 *	This has to be run for each processor
 */
 
void mcheck_init(void)
{
	u32 l, h;
	int i;
	struct cpuinfo_x86 *c;
	static int done=0;

	c=cpu_data+smp_processor_id();
	
	if(c->x86_vendor!=X86_VENDOR_INTEL)
		return;
	
	if(!(c->x86_capability&X86_FEATURE_MCE))
		return;
		
	if(!(c->x86_capability&X86_FEATURE_MCA))
		return;
		
	/* Ok machine check is available */
	
	if(done==0)
		printk(KERN_INFO "Intel machine check architecture supported.\n");
	rdmsr(0x179, l, h);
	if(l&(1<<8))
		wrmsr(0x17b, 0xffffffff, 0xffffffff);
	banks = l&0xff;
	for(i=1;i<banks;i++)
	{
		wrmsr(0x400+4*i, 0xffffffff, 0xffffffff); 
	}
	for(i=0;i<banks;i++)
	{
		wrmsr(0x401+4*i, 0x0, 0x0); 
	}
	__asm__ __volatile__ (
		"movl %%cr4, %%eax\n\t"
		"orl $0x40, %%eax\n\t"
		"movl %%eax, %%cr4\n\t" : : : "eax");
	printk(KERN_INFO "Intel machine check reporting enabled on CPU#%d.\n", smp_processor_id());
	done=1;
}
