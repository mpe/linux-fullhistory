/*
 * linux/fs/fat/fatfs_syms.c
 *
 * Exported kernel symbols for the low-level FAT-based fs support.
 *
 */
#include <linux/module.h>

#include <linux/mm.h>
#include <linux/msdos_fs.h>

#include "msbuffer.h"
#include "tables.h"

extern struct file_operations fat_dir_operations;

static struct symbol_table fat_syms = {
#include <linux/symtab_begin.h>
	X(fat_a2alias),
	X(fat_a2uni),
	X(fat_add_cluster),
	X(fat_bmap),
	X(fat_brelse),
	X(fat_cache_inval_inode),
	X(fat_code2uni),
	X(fat_date_unix2dos),
	X(fat_dir_operations),
	X(fat_file_read),
	X(fat_file_write),
	X(fat_fs_panic),
	X(fat_get_entry),
	X(fat_lock_creation),
	X(fat_mark_buffer_dirty),
	X(fat_mmap),
	X(fat_notify_change),
	X(fat_parent_ino),
	X(fat_put_inode),
	X(fat_put_super),
	X(fat_read_inode),
	X(fat_read_super),
	X(fat_readdirx),
	X(fat_readdir),
	X(fat_scan),
	X(fat_smap),
	X(fat_statfs),
	X(fat_truncate),
	X(fat_uni2asc_pg),
	X(fat_uni2code),
	X(fat_unlock_creation),
	X(fat_write_inode),
#include <linux/symtab_end.h>
};                                           

int init_fat_fs(void)
{
	return register_symtab(&fat_syms);
}

