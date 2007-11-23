#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>
#include <linux/init.h>

extern struct proc_dir_entry *proc_sys_root;

#ifdef CONFIG_SYSCTL
EXPORT_SYMBOL(proc_sys_root);
#endif
EXPORT_SYMBOL(proc_symlink);
EXPORT_SYMBOL(proc_mknod);
EXPORT_SYMBOL(proc_mkdir);
EXPORT_SYMBOL(create_proc_entry);
EXPORT_SYMBOL(remove_proc_entry);
EXPORT_SYMBOL(proc_root);
EXPORT_SYMBOL(proc_root_fs);
EXPORT_SYMBOL(proc_net);
EXPORT_SYMBOL(proc_bus);
EXPORT_SYMBOL(proc_root_driver);

static DECLARE_FSTYPE(proc_fs_type, "proc", proc_read_super, 0);

int __init init_proc_fs(void)
{
	return register_filesystem(&proc_fs_type) == 0;
}
