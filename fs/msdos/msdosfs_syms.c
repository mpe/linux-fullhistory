/*
 * linux/fs/msdos/msdosfs_syms.c
 *
 * Exported kernel symbols for the MS-DOS filesystem.
 * These symbols are used by umsdos.
 */

#include <linux/module.h>

#include <linux/mm.h>
#include <linux/msdos_fs.h>

static struct symbol_table msdos_syms = {
#include <linux/symtab_begin.h>
	/*
	 * Support for umsdos fs
	 *
	 * These symbols are _always_ exported, in case someone
	 * wants to install the umsdos module later.
	 */
  	X(msdos_create),
  	X(msdos_lookup),
  	X(msdos_mkdir),
  	X(msdos_read_inode),
  	X(msdos_rename),
  	X(msdos_rmdir),
  	X(msdos_unlink),
  	X(msdos_unlink_umsdos),
  	X(msdos_read_super),
  	X(msdos_put_super), 
#include <linux/symtab_end.h>
};                                           

struct file_system_type msdos_fs_type = {
	msdos_read_super, "msdos", 1, NULL
};


int init_msdos_fs(void)
{
	int status;

	if ((status = register_filesystem(&msdos_fs_type)) == 0)
		status = register_symtab(&msdos_syms);
	return status;
}

