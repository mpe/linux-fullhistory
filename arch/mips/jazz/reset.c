/*
 *  linux/arch/mips/jazz/process.c
 *
 *  Reset a Jazz machine.
 */
#include <asm/io.h>
#include <asm/system.h>
#include <asm/reboot.h>

void jazz_machine_restart(char *command)
{
	printk("Implement jazz_machine_restart().\n");
	printk("Press reset to continue.\n");
	while(1);
}

void jazz_machine_halt(void)
{
}

void jazz_machine_power_off(void)
{
	/* Jazz machines don't have a software power switch */
}
