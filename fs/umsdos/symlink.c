/*
 *  linux/fs/umsdos/file.c
 *
 *  Written 1992 by Jacques Gelinas
 *      inspired from linux/fs/msdos/file.c Werner Almesberger
 *
 *  Extended MS-DOS regular file handling primitives
 *
 *  Wow. It looks like we could support them on FAT with little (if any)
 *  problems. Oh, well...
 */

#include <linux/fs.h>
#include <linux/msdos_fs.h>

struct inode_operations umsdos_symlink_inode_operations =
{
	readlink:	page_readlink,
	follow_link:	page_follow_link,
	get_block:	fat_get_block,
	readpage:	block_read_full_page
};
