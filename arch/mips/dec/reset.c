/*
 *  linux/arch/mips/dec/process.c
 *
 *  Reset a DECstation.
 */
#include <linux/kernel.h>
#include <asm/reboot.h>

void dec_machine_restart(char *command)
{
	printk("Implement dec_machine_restart().\n");
	printk("Press reset to continue.\n");
	while(1);
}

void dec_machine_halt(void)
{
	printk("Implement dec_machine_halt().\n");
	printk("Press reset to continue.\n");
	while(1);
}

void dec_machine_power_off(void)
{
	printk("Implement dec_machine_power_off().\n");
	printk("Press reset to continue.\n");
	while(1);
}
