/* mp.c:  SMP cpu idling and dispatch on the Sparc.
 *
 * Copyright (C) 1995 David S. Miller
 */

#include <asm/mp.h>
#include <asm/mbus.h>

struct sparc_percpu *percpu_table;

void
sparc_cpu_init(void)
{
	/* We now have our per-cpu mappings ok, and we should
	 * be good to go.
	 */

	/* Do cache crap here. */

	/* CPU initted, idle the puppy. */

	return;
}

extern thiscpus_mid;

void
sparc_cpu_idle(void)
{
	int cpuid;

/*	cpuid = get_cpuid(); */
	cpuid = (thiscpus_mid&(~8));
/*	printk("SMP: cpu%d has entered idle loop", cpuid); */

	/* Say that we exist and set up. */
	percpu_table[cpuid].cpuid = cpuid;
	percpu_table[cpuid].cpu_is_alive = 0x1;
	percpu_table[cpuid].cpu_is_idling = 0x1;

	/* Let other cpus catch up. */
	while(linux_smp_still_initting) ;
	printk("cpu%d done spinning\n", get_cpuid());
	for(;;) ;  /* Do something useful here... */

	return;
}	
