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

static struct symbol_table procfs_syms = {
/* Should this be surrounded with "#ifdef CONFIG_MODULES" ? */
#include <linux/symtab_begin.h>
	X(proc_register),
	X(proc_register_dynamic),
	X(proc_unregister),
	X(proc_root),
	X(in_group_p),
	X(generate_cluster),
	X(proc_net_inode_operations),
	X(proc_net),

	/*
	 * This is required so that if we load scsi later, that the
	 * scsi code can attach to /proc/scsi in the correct manner.
	 */
	X(proc_scsi),
	X(proc_scsi_inode_operations),
	X(dispatch_scsi_info_ptr),
#include <linux/symtab_end.h>
};

static struct file_system_type proc_fs_type = {
	proc_read_super, "proc", 0, NULL
};

int init_proc_fs(void)
{
	int status;

        if ((status = register_filesystem(&proc_fs_type)) == 0)
		status = register_symtab(&procfs_syms);
	return status;
}

