/* 
 * Do early PCI probing for bug detection when the main PCI subsystem is 
 * not up yet.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <asm/pci-direct.h>
#include <asm/acpi.h>

static int __init check_bridge(int vendor, int device) 
{
	/* According to Nvidia all timer overrides are bogus. Just ignore
	   them all. */
	if (vendor == PCI_VENDOR_ID_NVIDIA) { 
		acpi_skip_timer_override = 1; 		
	}
	return 0;
}
   
void __init check_acpi_pci(void) 
{ 
	int num,slot,func; 

	/* Assume the machine supports type 1. If not it will 
	   always read ffffffff and should not have any side effect. */

	/* Poor man's PCI discovery */
	for (num = 0; num < 32; num++) { 
		for (slot = 0; slot < 32; slot++) { 
			for (func = 0; func < 8; func++) { 
				u32 class;
				u32 vendor;
				class = read_pci_config(num,slot,func,
							PCI_CLASS_REVISION);
				if (class == 0xffffffff)
					break; 

				if ((class >> 16) != PCI_CLASS_BRIDGE_PCI)
					continue; 
				
				vendor = read_pci_config(num, slot, func, 
							 PCI_VENDOR_ID);
				
				if (check_bridge(vendor&0xffff, vendor >> 16))
					return; 
			} 
			
		}
	}
}
