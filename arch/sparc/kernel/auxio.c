/* auxio.c: Probing for the Sparc AUXIO register at boot time.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/config.h>
#include <asm/oplib.h>
#include <asm/io.h>
#include <asm/auxio.h>

/* Probe and map in the Auxiliary I/O register */
unsigned char *auxio_register;

__initfunc(void auxio_probe(void))
{
	int node, auxio_nd;
	struct linux_prom_registers auxregs[1];

	switch (sparc_cpu_model) {
	case sun4d:
	case sun4:
		auxio_register = 0;
		return;
	default:
		break;
	}
	node = prom_getchild(prom_root_node);
	auxio_nd = prom_searchsiblings(node, "auxiliary-io");
	if(!auxio_nd) {
		node = prom_searchsiblings(node, "obio");
		node = prom_getchild(node);
		auxio_nd = prom_searchsiblings(node, "auxio");
		if(!auxio_nd) {
#ifdef CONFIG_PCI
			/* There may be auxio on Ebus */
			auxio_register = 0;
			return;
#else
			if(prom_searchsiblings(node, "leds")) {
				/* VME chassis sun4m machine, no auxio exists. */
				auxio_register = 0;
				return;
			}
			prom_printf("Cannot find auxio node, cannot continue...\n");
			prom_halt();
#endif
		}
	}
	prom_getproperty(auxio_nd, "reg", (char *) auxregs, sizeof(auxregs));
	prom_apply_obio_ranges(auxregs, 0x1);
	/* Map the register both read and write */
	auxio_register = (unsigned char *) sparc_alloc_io(auxregs[0].phys_addr, 0,
							  auxregs[0].reg_size,
							  "auxiliaryIO",
							  auxregs[0].which_io, 0x0);
	/* Fix the address on sun4m and sun4c. */
	if((((unsigned long) auxregs[0].phys_addr) & 3) == 3 ||
	   sparc_cpu_model == sun4c)
		auxio_register = (unsigned char *) ((int)auxio_register | 3);

	TURN_ON_LED;
}


/* sun4m power control register (AUXIO2) */

volatile unsigned char * auxio_power_register = NULL;

__initfunc(void auxio_power_probe(void))
{
	struct linux_prom_registers regs;
	int node;

	/* Attempt to find the sun4m power control node. */
	node = prom_getchild(prom_root_node);
	node = prom_searchsiblings(node, "obio");
	node = prom_getchild(node);
	node = prom_searchsiblings(node, "power");
	if (node == 0 || node == -1)
		return;

	/* Map the power control register. */
	prom_getproperty(node, "reg", (char *)&regs, sizeof(regs));
	prom_apply_obio_ranges(&regs, 1);
	auxio_power_register = (volatile unsigned char *)
		sparc_alloc_io(regs.phys_addr, 0, regs.reg_size,
			       "power off control", regs.which_io, 0);

	/* Display a quick message on the console. */
	if (auxio_power_register)
		printk(KERN_INFO "Power off control detected.\n");
}
