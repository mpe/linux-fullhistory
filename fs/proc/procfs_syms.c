#include <linux/config.h>
#include <linux/module.h>
#include <linux/fs.h>
#include <linux/proc_fs.h>

static struct symbol_table procfs_syms = {
/* Should this be surrounded with "#ifdef CONFIG_MODULES" ? */
#include <linux/symtab_begin.h>
	X(proc_register),
	X(proc_unregister),
	X(in_group_p),
	X(generate_cluster),
	X(proc_net_inode_operations),
	X(proc_net),
#ifdef CONFIG_SCSI /* Ugh... */
	X(proc_scsi),
	X(proc_scsi_inode_operations),
	X(dispatch_scsi_info_ptr),
#endif
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

