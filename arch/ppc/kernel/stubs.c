/*#include <linux/in.h>*/
#include <linux/autoconf.h>
#include <linux/smp.h>
#include <linux/smp_lock.h>

void sys_iopl(void)
{
	lock_kernel();
	panic("sys_iopl");
	unlock_kernel();
}
void sys_vm86(void)
{
	lock_kernel();
	panic("sys_vm86");
	unlock_kernel();
}
void sys_modify_ldt(void)
{
	lock_kernel();
	panic("sys_modify_ldt");
	unlock_kernel();
}

void sys_ipc(void)
{
	lock_kernel();
	panic("sys_ipc");
	unlock_kernel();
}

void sys_newselect(void)
{
	lock_kernel();
	panic("sys_newselect");
	unlock_kernel();
}

#ifndef CONFIG_MODULES
void
scsi_register_module(void)
{
	lock_kernel();
	panic("scsi_register_module");
	unlock_kernel();
}

void
scsi_unregister_module(void)
{
	lock_kernel();
	panic("scsi_unregister_module");
	unlock_kernel();
}
#endif



