/*#include <linux/in.h>*/
#include <linux/autoconf.h>

void sys_iopl(void) { panic("sys_iopl"); }
void sys_vm86(void) { panic("sys_vm86"); }
void sys_modify_ldt(void) { panic("sys_modify_ldt"); }

void sys_ipc(void) {panic("sys_ipc"); }
void sys_newselect(void) {panic("sys_newselect"); }

#ifndef CONFIG_MODULES
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
#endif



