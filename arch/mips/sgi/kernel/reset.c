/*
 *  linux/arch/mips/sgi/kernel/process.c
 *
 *  Reset a SGI.
 */
#include <asm/io.h>
#include <asm/system.h>
#include <asm/reboot.h>

/* XXX How to pass the reboot command to the firmware??? */
void sgi_machine_restart(char *command)
{
        for(;;)
                prom_imode();
}

void sgi_machine_halt(void)
{
	/* XXX */
}

void sgi_machine_power_off(void)
{
	/* XXX */
}
