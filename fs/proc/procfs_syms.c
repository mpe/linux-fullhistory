#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>

/*
 * This is all required so that if we load all of scsi as a module,
 * that the scsi code will be able to talk to the /proc/scsi handling
 * in the procfs.
 */
extern int (* dispatch_scsi_info_ptr) (int ino, char *buffer, char **start,
				       off_t offset, int length, int inout);
extern struct inode_operations proc_scsi_inode_operations;

EXPORT_SYMBOL(proc_register);
EXPORT_SYMBOL(proc_unregister);
EXPORT_SYMBOL(create_proc_entry);
EXPORT_SYMBOL(remove_proc_entry);
EXPORT_SYMBOL(proc_root);
EXPORT_SYMBOL(proc_get_inode);
EXPORT_SYMBOL(in_group_p);
EXPORT_SYMBOL(proc_dir_inode_operations);
EXPORT_SYMBOL(proc_net_inode_operations);
EXPORT_SYMBOL(proc_net);

/*
 * This is required so that if we load scsi later, that the
 * scsi code can attach to /proc/scsi in the correct manner.
 */
EXPORT_SYMBOL(proc_scsi);
EXPORT_SYMBOL(proc_scsi_inode_operations);
EXPORT_SYMBOL(dispatch_scsi_info_ptr);

#if defined(CONFIG_SUN_OPENPROMFS_MODULE)
EXPORT_SYMBOL(proc_openprom_register);
EXPORT_SYMBOL(proc_openprom_deregister);
#endif

static struct file_system_type proc_fs_type = {
	"proc", 
	FS_NO_DCACHE,
	proc_read_super, 
	NULL
};

int init_proc_fs(void)
{
	return register_filesystem(&proc_fs_type) == 0;
}
