#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>

extern struct proc_dir_entry *proc_sys_root;

#ifdef CONFIG_SYSCTL
EXPORT_SYMBOL(proc_sys_root);
#endif
EXPORT_SYMBOL(proc_register);
EXPORT_SYMBOL(proc_unregister);
EXPORT_SYMBOL(create_proc_entry);
EXPORT_SYMBOL(remove_proc_entry);
EXPORT_SYMBOL(proc_root);
EXPORT_SYMBOL(proc_root_fs);
EXPORT_SYMBOL(proc_get_inode);
EXPORT_SYMBOL(proc_dir_inode_operations);
EXPORT_SYMBOL(proc_net);
EXPORT_SYMBOL(proc_bus);
EXPORT_SYMBOL(proc_lookup);
EXPORT_SYMBOL(proc_readdir);

#if defined(CONFIG_SUN_OPENPROMFS_MODULE)
EXPORT_SYMBOL(proc_openprom_register);
EXPORT_SYMBOL(proc_openprom_deregister);
#endif

static struct file_system_type proc_fs_type = {
	"proc", 
	0 /* FS_NO_DCACHE doesn't work correctly */,
	proc_read_super, 
	NULL
};

int init_proc_fs(void)
{
	return register_filesystem(&proc_fs_type) == 0;
}
