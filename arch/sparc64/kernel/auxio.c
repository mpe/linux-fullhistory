/* auxio.c: Probing for the Sparc AUXIO register at boot time.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/ioport.h>

#include <asm/oplib.h>
#include <asm/io.h>
#include <asm/auxio.h>
#include <asm/sbus.h>
#include <asm/ebus.h>
#include <asm/fhc.h>

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
#ifdef CONFIG_PCI
		struct linux_ebus *ebus;
		struct linux_ebus_device *edev = 0;
		unsigned long led_auxio;

		for_each_ebus(ebus) {
			for_each_ebusdev(edev, ebus) {
				if (!strcmp(edev->prom_name, "auxio"))
					goto ebus_done;
			}
		}
	ebus_done:

		if (edev) {
			if (check_region(edev->base_address[0],
					 sizeof(unsigned int))) {
				prom_printf("%s: Can't get region %lx, %d\n",
					    __FUNCTION__, edev->base_address[0],
					    sizeof(unsigned int));
				prom_halt();
			}
			request_region(edev->base_address[0],
				       sizeof(unsigned int), "LED auxio");

			led_auxio = edev->base_address[0];
			outl(0x01, led_auxio);
			return;
		}
#endif
		if(central_bus) {
			auxio_register = NULL;
			return;
		}
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
