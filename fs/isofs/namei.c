/*
 *  linux/fs/isofs/namei.c
 *
 *  (C) 1992  Eric Youngdale Modified for ISO 9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */

#include <linux/sched.h>
#include <linux/iso_fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/malloc.h>
#include <linux/errno.h>
#include <linux/config.h>	/* Joliet? */

#include <asm/uaccess.h>

/*
 * ok, we cannot use strncmp, as the name is not in our data space.
 * Thus we'll have to use isofs_match. No big problem. Match also makes
 * some sanity tests.
 */
static int
isofs_cmp(struct dentry * dentry, const char * compare, int dlen)
{
	struct qstr qstr;

	if (!compare)
		return 1;

	/* check special "." and ".." files */
	if (dlen == 1) {
		/* "." */
		if (compare[0] == 0) {
			if (!dentry->d_name.len)
				return 0;
			compare = ".";
		} else if (compare[0] == 1) {
			compare = "..";
			dlen = 2;
		}
	}

	qstr.name = compare;
	qstr.len = dlen;
	return dentry->d_op->d_compare(dentry, &dentry->d_name, &qstr);
}

/*
 *	isofs_find_entry()
 *
 * finds an entry in the specified directory with the wanted name. It
 * returns the cache buffer in which the entry was found, and the entry
 * itself (as an inode number). It does NOT read the inode of the
 * entry - you'll have to do that yourself if you want to.
 */
static struct buffer_head *
isofs_find_entry(struct inode *dir, struct dentry *dentry, unsigned long *ino)
{
	unsigned long bufsize = ISOFS_BUFFER_SIZE(dir);
	unsigned char bufbits = ISOFS_BUFFER_BITS(dir);
	unsigned int block, i, f_pos, offset, 
		inode_number = 0; /* shut gcc up */
	struct buffer_head * bh , * retval = NULL;
	unsigned int old_offset;
	int dlen, match;
	char * dpnt;
	unsigned char *page = NULL;
	struct iso_directory_record * de = NULL; /* shut gcc up */
	char de_not_in_buf = 0;	  /* true if de is in kmalloc'd memory */
	char c;

	*ino = 0;
	if (!dir) return NULL;
	
	if (!(block = dir->u.isofs_i.i_first_extent)) return NULL;
  
	f_pos = 0;

	offset = f_pos & (bufsize - 1);
	block = isofs_bmap(dir,f_pos >> bufbits);

	if (!block || !(bh = bread(dir->i_dev,block,bufsize))) return NULL;

	while (f_pos < dir->i_size) {

		/* if de is in kmalloc'd memory, do not point to the
                   next de, instead we will move to the next sector */
		if(!de_not_in_buf) {
			de = (struct iso_directory_record *) 
				(bh->b_data + offset);
		}
		inode_number = (block << bufbits) + (offset & (bufsize - 1));

		/* If byte is zero, or we had to fetch this de past
		   the end of the buffer, this is the end of file, or
		   time to move to the next sector. Usually 2048 byte
		   boundaries. */
		
		if (*((unsigned char *) de) == 0 || de_not_in_buf) {
			if(de_not_in_buf) {
				/* james@bpgc.com: Since we slopped
                                   past the end of the last buffer, we
                                   must start some way into the new
                                   one */
				de_not_in_buf = 0;
				kfree(de);
				f_pos += offset;
			}
			else { 
				offset = 0;
				f_pos = ((f_pos & ~(ISOFS_BLOCK_SIZE - 1))
					 + ISOFS_BLOCK_SIZE);
			}
			brelse(bh);
			bh = NULL;

			if (f_pos >= dir->i_size) 
				break;

			block = isofs_bmap(dir,f_pos>>bufbits);
			if (!block || !(bh = bread(dir->i_dev,block,bufsize)))
				break;

			continue; /* Will kick out if past end of directory */
		}

		old_offset = offset;
		offset += *((unsigned char *) de);
		f_pos += *((unsigned char *) de);

		/* james@bpgc.com: new code to handle case where the
		   directory entry spans two blocks.  Usually 1024
		   byte boundaries */
		if (offset >= bufsize) {
			struct buffer_head *bh_next;

			/* james@bpgc.com: read the next block, and
                           copy the split de into a newly kmalloc'd
                           buffer */
			block = isofs_bmap(dir,f_pos>>bufbits);
			if (!block || 
			    !(bh_next = bread(dir->i_dev,block,bufsize)))
				break;
			
			de = (struct iso_directory_record *)
				kmalloc(offset - old_offset, GFP_KERNEL);
			memcpy((char *)de, bh->b_data + old_offset, 
			       bufsize - old_offset);
			memcpy((char *)de + bufsize - old_offset,
			       bh_next->b_data, offset - bufsize);
			brelse(bh_next);
			de_not_in_buf = 1;
			offset -= bufsize;
		}
		dlen = de->name_len[0];
		dpnt = de->name;

		if (dir->i_sb->u.isofs_sb.s_rock ||
		    dir->i_sb->u.isofs_sb.s_joliet_level || 
		    dir->i_sb->u.isofs_sb.s_mapping == 'n' ||
		    dir->i_sb->u.isofs_sb.s_mapping == 'a') {
			if (! page) {
				page = (unsigned char *)
					__get_free_page(GFP_KERNEL);
				if (!page) break;
			}
		}
		if (dir->i_sb->u.isofs_sb.s_rock &&
		    ((i = get_rock_ridge_filename(de, page, dir)))) {
			dlen = i;
			dpnt = page;
#ifdef CONFIG_JOLIET
		} else if (dir->i_sb->u.isofs_sb.s_joliet_level) {
			dlen = get_joliet_filename(de, dir, page);
			dpnt = page;
#endif
		} else if (dir->i_sb->u.isofs_sb.s_mapping == 'a') {
			dlen = get_acorn_filename(de, page, dir);
			dpnt = page;
		} else if (dir->i_sb->u.isofs_sb.s_mapping == 'n') {
			for (i = 0; i < dlen; i++) {
				c = dpnt[i];
				/* lower case */
				if (c >= 'A' && c <= 'Z') c |= 0x20;
				if (c == ';' && i == dlen-2 && dpnt[i+1] == '1') {
					dlen -= 2;
					break;
				}
				if (c == ';') c = '.';
				page[i] = c;
			}
			/* This allows us to match with and without
			 * a trailing period. */
			if(page[dlen-1] == '.' && dentry->d_name.len == dlen-1)
				dlen--;
			dpnt = page;
		}
		/*
		 * Skip hidden or associated files unless unhide is set 
		 */
		match = 0;
		if ((!(de->flags[-dir->i_sb->u.isofs_sb.s_high_sierra] & 5)
		    || dir->i_sb->u.isofs_sb.s_unhide == 'y') && dlen)
		{
			match = (isofs_cmp(dentry,dpnt,dlen) == 0);
		}
		if (match) {
			if(inode_number == -1) {
				/* Should only happen for the '..' entry */
				inode_number = 
					isofs_lookup_grandparent(dir,
					   find_rock_ridge_relocation(de,dir));
			}
			*ino = inode_number;
			retval = bh;
			bh = NULL;
			break;
		}
	}
	if (page) free_page((unsigned long) page);
	if (bh) brelse(bh);
	if(de_not_in_buf) 
		kfree(de);
	return retval;
}

int isofs_lookup(struct inode * dir, struct dentry * dentry)
{
	unsigned long ino;
	struct buffer_head * bh;
	struct inode *inode;

#ifdef DEBUG
	printk("lookup: %x %s\n",dir->i_ino, dentry->d_name.name);
#endif
	if (!dir)
		return -ENOENT;

	if (!S_ISDIR(dir->i_mode))
		return -ENOENT;

	dentry->d_op = dir->i_sb->s_root->d_op;

	bh = isofs_find_entry(dir, dentry, &ino);

	inode = NULL;
	if (bh) {
		brelse(bh);

		inode = iget(dir->i_sb,ino);
		if (!inode)
			return -EACCES;
	}
	d_add(dentry, inode);
	return 0;
}
