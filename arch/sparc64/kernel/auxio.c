/* auxio.c: Probing for the Sparc AUXIO register at boot time.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <asm/oplib.h>
#include <asm/io.h>
#include <asm/auxio.h>
#include <asm/sbus.h>

/* Probe and map in the Auxiliary I/O register */
unsigned char *auxio_register;

__initfunc(void auxio_probe(void))
{
        struct linux_sbus *bus;
        struct linux_sbus_device *sdev = 0;
	struct linux_prom_registers auxregs[1];

        for_each_sbus(bus) {
                for_each_sbusdev(sdev, bus) {
                        if(!strcmp(sdev->prom_name, "auxio")) {
				break;
                        }
                }
        }

	if (!sdev) {
		prom_printf("Cannot find auxio node, cannot continue...\n");
		prom_halt();
	}
	prom_getproperty(sdev->prom_node, "reg", (char *) auxregs, sizeof(auxregs));
	prom_apply_sbus_ranges(sdev->my_bus, auxregs, 0x1, sdev);
	/* Map the register both read and write */
	auxio_register = (unsigned char *) sparc_alloc_io(auxregs[0].phys_addr, 0,
							  auxregs[0].reg_size,
							  "auxiliaryIO",
							  auxregs[0].which_io, 0x0);
	TURN_ON_LED;
}
