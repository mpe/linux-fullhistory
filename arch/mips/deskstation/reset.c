/*
 * linux/arch/mips/deskstation/process.c
 *
 * Reset a Deskstation.
 */
#include <asm/io.h>
#include <asm/reboot.h>
#include <asm/system.h>

void deskstation_machine_restart(void)
{
	printk("Implement deskstation_machine_restart().\n");
	printk("Press reset to continue.\n");
	while(1);
}

void deskstation_machine_halt(void)
{
	printk("Implement deskstation_machine_halt().\n");
	printk("Press reset to continue.\n");
	while(1);
}

void deskstation_machine_power_off(void)
{
	printk("Implement dec_machine_power_off().\n");
	printk("Press reset to continue.\n");
	while(1);
}
