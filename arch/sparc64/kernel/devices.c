/* devices.c: Initial scan of the prom device tree for important
 *            Sparc device nodes which we need to find.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/kernel.h>
#include <linux/tasks.h>
#include <linux/init.h>

#include <asm/page.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/smp.h>

struct prom_cpuinfo linux_cpus[NR_CPUS] __initdata = { { 0 } };
unsigned prom_cpu_nodes[NR_CPUS];
int linux_num_cpus = 0;

extern void cpu_probe(void);
extern unsigned long central_probe(unsigned long);

__initfunc(unsigned long
device_scan(unsigned long mem_start))
{
	char node_str[128];
	int nd, prom_node_cpu, thismid;
	int cpu_nds[NR_CPUS];  /* One node for each cpu */
	int cpu_ctr = 0;

	prom_getstring(prom_root_node, "device_type", node_str, sizeof(node_str));

	if(strcmp(node_str, "cpu") == 0) {
		cpu_nds[0] = prom_root_node;
		linux_cpus[0].prom_node = prom_root_node;
		linux_cpus[0].mid = 0;
		cpu_ctr++;
	} else {
		int scan;
		scan = prom_getchild(prom_root_node);
		prom_printf("root child is %08x\n", (unsigned) scan);
		nd = 0;
		while((scan = prom_getsibling(scan)) != 0) {
			prom_getstring(scan, "device_type", node_str, sizeof(node_str));
			if(strcmp(node_str, "cpu") == 0) {
				cpu_nds[cpu_ctr] = scan;
				linux_cpus[cpu_ctr].prom_node = scan;
				prom_getproperty(scan, "upa-portid",
						 (char *) &thismid, sizeof(thismid));
				linux_cpus[cpu_ctr].mid = thismid;
#ifdef __SMP__				
				prom_printf("Found CPU %d (node=%08x,mid=%d)\n",
					    cpu_ctr, (unsigned) scan,
					    thismid);
				printk("Found CPU %d (node=%08x,mid=%d)\n",
				       cpu_ctr, (unsigned) scan, thismid);
#endif				       
				cpu_ctr++;
			}
		};
		if(cpu_ctr == 0) {
			prom_printf("No CPU nodes found, cannot continue.\n");
			prom_halt();
		}
#ifdef __SMP__		
		printk("Found %d CPU prom device tree node(s).\n", cpu_ctr);
#endif		
	};
	prom_node_cpu = cpu_nds[0];

	linux_num_cpus = cpu_ctr;
	
	prom_cpu_nodes[0] = prom_node_cpu;

	cpu_probe();
	return central_probe(mem_start);
}
