/*
 *  linux/fs/isofs/dir.c
 *
 *  (C) 1992, 1993, 1994  Eric Youngdale Modified for ISO9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 *
 *  Steve Beynon		       : Missing last directory entries fixed
 *  (stephen@askone.demon.co.uk)      : 21st June 1996
 * 
 *  isofs directory handling functions
 */
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/iso_fs.h>
#include <linux/kernel.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/locks.h>
#include <linux/config.h>

#include <asm/uaccess.h>

static int isofs_readdir(struct file *, void *, filldir_t);

static struct file_operations isofs_dir_operations =
{
	NULL,			/* lseek - default */
	NULL,			/* read */
	NULL,			/* write - bad */
	isofs_readdir,		/* readdir */
	NULL,			/* poll - default */
	NULL,			/* ioctl - default */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	NULL			/* fsync */
};

/*
 * directories can handle most operations...
 */
struct inode_operations isofs_dir_inode_operations =
{
	&isofs_dir_operations,	/* default directory file-ops */
	NULL,			/* create */
	isofs_lookup,		/* lookup */
	NULL,			/* link */
	NULL,			/* unlink */
	NULL,			/* symlink */
	NULL,			/* mkdir */
	NULL,			/* rmdir */
	NULL,			/* mknod */
	NULL,			/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	isofs_bmap,		/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};

static int isofs_name_translate(char * old, int len, char * new)
{
	int i, c;
			
	for (i = 0; i < len; i++) {
		c = old[i];
		if (!c)
			break;
		if (c >= 'A' && c <= 'Z')
			c |= 0x20;	/* lower case */

		/* Drop trailing '.;1' (ISO9660:1988 7.5.1 requires period) */
		if (c == '.' && i == len - 3 && old[i + 1] == ';' && old[i + 2] == '1')
			break;

		/* Drop trailing ';1' */
		if (c == ';' && i == len - 2 && old[i + 1] == '1')
			break;

		/* Convert remaining ';' to '.' */
		if (c == ';')
			c = '.';

		new[i] = c;
	}
	return i;
}

/*
 * This should _really_ be cleaned up some day..
 */
static int do_isofs_readdir(struct inode *inode, struct file *filp,
		void *dirent, filldir_t filldir,
		char * tmpname, struct iso_directory_record * tmpde)
{
	unsigned long bufsize = ISOFS_BUFFER_SIZE(inode);
	unsigned char bufbits = ISOFS_BUFFER_BITS(inode);
	unsigned int block, offset;
	int inode_number;
	struct buffer_head *bh;
	int len;
	int map;
	int high_sierra = 0;
	char *p = NULL;		/* Quiet GCC */
	struct iso_directory_record *de;

 	if (filp->f_pos >= inode->i_size)
		return 0;
 
	offset = filp->f_pos & (bufsize - 1);
	block = isofs_bmap(inode, filp->f_pos >> bufbits);

	if (!block)
		return 0;

	if (!(bh = breada(inode->i_dev, block, bufsize, filp->f_pos, inode->i_size)))
		return 0;

	while (filp->f_pos < inode->i_size) {
		int de_len;
#ifdef DEBUG
		printk("Block, offset, f_pos: %x %x %x\n",
		       block, offset, filp->f_pos);
	        printk("inode->i_size = %x\n",inode->i_size);
#endif
		de = (struct iso_directory_record *) (bh->b_data + offset);
		inode_number = (block << bufbits) + (offset & (bufsize - 1));

		de_len = *(unsigned char *) de;
#ifdef DEBUG
		printk("de_len = %ld\n", de_len);
#endif
	    

		/* If the length byte is zero, we should move on to the next
		   CDROM sector.  If we are at the end of the directory, we
		   kick out of the while loop. */

		if ((de_len == 0) || (offset >= bufsize) ) {
			brelse(bh);
			if (de_len == 0) {
				filp->f_pos = ((filp->f_pos & ~(ISOFS_BLOCK_SIZE - 1))
					       + ISOFS_BLOCK_SIZE);
				offset = 0;
			} else {
				offset -= bufsize;
				filp->f_pos += offset;
			}

			if (filp->f_pos >= inode->i_size)
				return 0;

			block = isofs_bmap(inode, (filp->f_pos) >> bufbits);
			if (!block)
				return 0;
			bh = breada(inode->i_dev, block, bufsize, filp->f_pos, inode->i_size);
			if (!bh)
				return 0;
			continue;
		}

		offset +=  de_len;
		if (offset > bufsize) {
			/*
			 * This would only normally happen if we had
			 * a buggy cdrom image.  All directory
			 * entries should terminate with a null size
			 * or end exactly at the end of the sector.
			 */
		        printk("next_offset (%x) > bufsize (%lx)\n",
			       offset,bufsize);
			break;
		}

		/* Handle the case of the '.' directory */
		if (de->name_len[0] == 1 && de->name[0] == 0) {
			if (filldir(dirent, ".", 1, filp->f_pos, inode->i_ino) < 0)
				break;
			filp->f_pos += de_len;
			continue;
		}

		len = 0;

		/* Handle the case of the '..' directory */
		if (de->name_len[0] == 1 && de->name[0] == 1) {
			inode_number = filp->f_dentry->d_parent->d_inode->i_ino;
			if (filldir(dirent, "..", 2, filp->f_pos, inode_number) < 0)
				break;
			filp->f_pos += de_len;
			continue;
		}

		/* Handle everything else.  Do name translation if there
		   is no Rock Ridge NM field. */
		if (inode->i_sb->u.isofs_sb.s_unhide == 'n') {
			/* Do not report hidden or associated files */
			high_sierra = inode->i_sb->u.isofs_sb.s_high_sierra;
			if (de->flags[-high_sierra] & 5) {
				filp->f_pos += de_len;
				continue;
			}
		}

		map = 1;
		if (inode->i_sb->u.isofs_sb.s_rock) {
			len = get_rock_ridge_filename(de, tmpname, inode);
			if (len != 0) {
				p = tmpname;
				map = 0;
			}
		}
		if (map) {
#ifdef CONFIG_JOLIET
			if (inode->i_sb->u.isofs_sb.s_joliet_level) {
				len = get_joliet_filename(de, inode, tmpname);
				p = tmpname;
			} else
#endif
			{
				if (inode->i_sb->u.isofs_sb.s_mapping == 'n') {
					len = isofs_name_translate(de->name, de->name_len[0],
								   tmpname);
					p = tmpname;
				} else {
					p = de->name;
					len = de->name_len[0];
				}
			}
		}
		if (len > 0) {
			if (filldir(dirent, p, len, filp->f_pos, inode_number) < 0)
				break;
		}
		filp->f_pos += de_len;

		continue;
	}
	brelse(bh);
	return 0;
}

/*
 * Handle allocation of temporary space for name translation and
 * handling split directory entries.. The real work is done by
 * "do_isofs_readdir()".
 */
static int isofs_readdir(struct file *filp,
		void *dirent, filldir_t filldir)
{
	int result;
	char * tmpname;
	struct iso_directory_record * tmpde;
	struct inode *inode = filp->f_dentry->d_inode;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;

	tmpname = (char *) __get_free_page(GFP_KERNEL);
	if (!tmpname)
		return -ENOMEM;
	tmpde = (struct iso_directory_record *) (tmpname+1024);

	result = do_isofs_readdir(inode, filp, dirent, filldir, tmpname, tmpde);

	free_page((unsigned long) tmpname);
	return result;
}
