/*
 *  linux/fs/affs/amigaffs.c
 *
 *  (C) 1996  Stefan Reinauer - Modified to compile as Module
 *
 *  (C) 1993  Ray Burr - Amiga FFS filesystem.
 *
 */


#include <linux/module.h>

#include <linux/fs.h>
#include <linux/affs_fs.h>
#include <linux/mm.h>

#include "amigaffs.h"

/*
 * Functions for accessing Amiga-FFS structures.
 *
 */

/* Get key entry number ENTRY_POS from the header block pointed to
   by DATA.  If ENTRY_POS is invalid, -1 is returned.  This is
   used to get entries from file and directory headers as well
   as extension and root blocks.  In the current FFS specs, these
   tables are defined to be the same size in all of these. */

int affs_get_key_entry (int bsize, void *data, int entry_pos)
{
	struct dir_front *dir_front = (struct dir_front *)data;
	int key, hash_table_size;

	hash_table_size = MIDBLOCK_LONGS (bsize);
	key = 0;
	if (entry_pos >= 0 && entry_pos < hash_table_size)
		key = swap_long (dir_front->hash_table[entry_pos]);

	return key;
}

/* Find the next used hash entry at or after *HASH_POS in a directory's hash
   table.  *HASH_POS is assigned that entry's number.  DIR_DATA points to
   the directory header block in memory.  If there are no more entries,
   0 is returned.  Otherwise, the key number in the next used hash slot
   is returned. */

int affs_find_next_hash_entry (int bsize, void *dir_data, int *hash_pos)
{
	struct dir_front *dir_front = (struct dir_front *)dir_data;
	int i, hash_table_size;

	hash_table_size = MIDBLOCK_LONGS (bsize);
	if (*hash_pos < 0 || *hash_pos >= hash_table_size)
		return -1;
	for (i = *hash_pos; i < hash_table_size; i++)
		if (dir_front->hash_table[i] != 0)
			break;
	if (i == hash_table_size)
		return 0;
	*hash_pos = i;
	return swap_long (dir_front->hash_table[i]);
}

/* Get the hash_chain (next file header key in hash chain) entry from a
   file header block in memory pointed to by FH_DATA. */

int affs_get_fh_hash_link (int bsize, void *fh_data)
{
	struct file_end *file_end;
	int key;

	file_end = GET_END_PTR (struct file_end, fh_data, bsize);
	key = swap_long (file_end->hash_chain);
	return key;
}

/* Set *NAME to point to the file name in a file header block in memory
   pointed to by FH_DATA.  The length of the name is returned. */

int affs_get_file_name (int bsize, void *fh_data, char **name)
{
	struct file_end *file_end;

	file_end = GET_END_PTR (struct file_end, fh_data, bsize);
	if (file_end->file_name[0] == 0
	    || file_end->file_name[0] > 30) {
		printk ("affs_get_file_name: OOPS! bad filename\n");
		printk ("  file_end->file_name[0] = %d\n",
			file_end->file_name[0]);
		*name = "***BAD_FILE***";
		return 14;
        }
	*name = (char *) &file_end->file_name[1];
        return file_end->file_name[0];
}

/* Get the key number of the first extension block for the file
   header pointed to by FH_DATA. */

int affs_get_extension (int bsize, void *fh_data)
{
	struct file_end *file_end;
	int key;

	file_end = GET_END_PTR (struct file_end, fh_data, bsize);
	key = swap_long (file_end->extension);
	return key;
}

/* Checksum a block, do various consistency checks and optionally return
   the blocks type number.  DATA points to the block.  If their pointers
   are non-null, *PTYPE and *STYPE are set to the primary and secondary
   block types respectively.  Returns non-zero if the block is not
   consistent. */

int affs_checksum_block (int bsize, void *data, int *ptype, int *stype)
{
	if (ptype)
		*ptype = swap_long (((long *) data)[0]);
	if (stype)
		*stype = swap_long (((long *) data)[bsize / 4 - 1]);
	return 0;
}

static struct file_system_type affs_fs_type = {
        affs_read_super, "affs", 1, NULL
};

int init_affs_fs(void)
{
        return register_filesystem(&affs_fs_type);
}

#ifdef MODULE
int init_module(void)
{
	int status;

	if ((status = init_affs_fs()) == 0)
	        register_symtab(0);
	return status;
}

void cleanup_module(void)
{
	unregister_filesystem(&affs_fs_type);
}

#endif

