#include <linux/in.h>

void sys_iopl(void) { _panic("sys_iopl"); }
void sys_vm86(void) { _panic("sys_vm86"); }
void sys_modify_ldt(void) { _panic("sys_modify_ldt"); }

void sys_ipc(void) {_panic("sys_ipc"); }
void sys_newselect(void) {_panic("sys_newselect"); }

halt()
{
	printk("\n...Halt!\n");
	abort();
}

_panic(char *msg)
{
	printk("Panic: %s\n", msg);
	printk("Panic: %s\n", msg);
	abort();
}

_warn(char *msg)
{
	printk("*** Warning: %s UNIMPLEMENTED!\n", msg);
}


void
saved_command_line(void)
{
	panic("saved_command_line");
}

void
KSTK_EIP(void)
{
	panic("KSTK_EIP");
}

void
KSTK_ESP(void)
{
	panic("KSTK_ESP");
}

void
scsi_register_module(void)
{
	panic("scsi_register_module");
}

void
scsi_unregister_module(void)
{
	panic("scsi_unregister_module");
}


