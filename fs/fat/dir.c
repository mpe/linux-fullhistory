/*
 *  linux/fs/fat/dir.c
 *
 *  directory handling functions for fat-based filesystems
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Hidden files 1995 by Albert Cahalan <albert@ccs.neu.edu> <adc@coe.neu.edu>
 *
 *  VFAT extensions by Gordon Chaffee, merged with msdos fs by Henrik Storner
 */

#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/ioctl.h>
#include <linux/dirent.h>

#include <asm/segment.h>

#include "msbuffer.h"
#include "tables.h"


#define PRINTK(X)

static int fat_dir_read(struct inode * inode,struct file * filp, char * buf,int count)
{
	return -EISDIR;
}

struct file_operations fat_dir_operations = {
	NULL,			/* lseek - default */
	fat_dir_read,		/* read */
	NULL,			/* write - bad */
	fat_readdir,		/* readdir */
	NULL,			/* select - default */
	fat_dir_ioctl,		/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* no special release code */
	file_fsync		/* fsync */
};

int fat_readdirx(
	struct inode *inode,
	struct file *filp,
	void *dirent,
	filldir_t filldir,
	int both)
{
	struct super_block *sb = inode->i_sb;
	int ino,i,i2,last;
	char c;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	unsigned long oldpos = filp->f_pos;
	int is_long;
	char longname[275];
	unsigned char long_len = 0; /* Make compiler warning go away */
	unsigned char alias_checksum = 0; /* Make compiler warning go away */


	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;
/* Fake . and .. for the root directory. */
	if (inode->i_ino == MSDOS_ROOT_INO) {
		while (oldpos < 2) {
			if (filldir(dirent, "..", oldpos+1, oldpos, MSDOS_ROOT_INO) < 0)
				return 0;
			oldpos++;
			filp->f_pos++;
		}
		if (oldpos == 2)
			filp->f_pos = 0;
	}
	if (filp->f_pos & (sizeof(struct msdos_dir_entry)-1))
		return -ENOENT;

 	bh = NULL;
	longname[0] = '\0';
	is_long = 0;
	ino = fat_get_entry(inode,&filp->f_pos,&bh,&de);
	while (ino > -1) {
		/* Should we warn about finding extended entries on fs
		 * mounted non-vfat ? Deleting or renaming files will cause
		 * corruption, as extended entries are not updated.
		 */
		if (!MSDOS_SB(sb)->vfat && 
		    !MSDOS_SB(sb)->umsdos &&  /* umsdos is safe for us */
		    !IS_RDONLY(inode) && 
		    (de->attr == ATTR_EXT) &&
		    !MSDOS_SB(sb)->quiet) {
			printk("MSDOS-fs warning: vfat directory entry found on fs mounted non-vfat (device %s)\n",
			       kdevname(sb->s_dev));
		}

		/* Check for long filename entry */
		if (MSDOS_SB(sb)->vfat && (de->name[0] == (__s8) DELETED_FLAG)) {
			is_long = 0;
			oldpos = filp->f_pos;
		} else if (MSDOS_SB(sb)->vfat && de->attr ==  ATTR_EXT) {
			int get_new_entry;
			struct msdos_dir_slot *ds;
			unsigned char page, pg_off, ascii;
			unsigned char *uni_page;
			unsigned char offset;
			unsigned char id;
			unsigned char slot;
			unsigned char slots = 0;
			int i;

			offset = 0;
			ds = (struct msdos_dir_slot *) de;
			id = ds->id;
			if (id & 0x40) {
				slots = id & ~0x40;
				is_long = 1;
				alias_checksum = ds->alias_checksum;
			}

			get_new_entry = 1;
			slot = slots;
			while (slot > 0) {
				PRINTK(("1. get_new_entry: %d\n", get_new_entry));
				if (ds->attr !=  ATTR_EXT) {
					is_long = 0;
					get_new_entry = 0;
					break;
				}
				if ((ds->id & ~0x40) != slot) {
					is_long = 0;
					break;
				}
				if (ds->alias_checksum != alias_checksum) {
					is_long = 0;
					break;
				}
				slot--;
				offset = slot * 13;
				PRINTK(("2. get_new_entry: %d\n", get_new_entry));
				for (i = 0; i < 10; i += 2) {
					pg_off = ds->name0_4[i];
					page = ds->name0_4[i+1];
					if (pg_off == 0 && page == 0) {
						goto found_end;
					}
					uni_page = fat_uni2asc_pg[page];
					ascii = uni_page[pg_off];
					longname[offset++] = ascii ? ascii : '?';
				}
				for (i = 0; i < 12; i += 2) {
					pg_off = ds->name5_10[i];
					page = ds->name5_10[i+1];
					if (pg_off == 0 && page == 0) {
						goto found_end;
					}
					uni_page = fat_uni2asc_pg[page];
					ascii = uni_page[pg_off];
					longname[offset++] = ascii ? ascii : '?';
				}
				for (i = 0; i < 4; i += 2) {
					pg_off = ds->name11_12[i];
					page = ds->name11_12[i+1];
					if (pg_off == 0 && page == 0) {
						goto found_end;
					}
					uni_page = fat_uni2asc_pg[page];
					ascii = uni_page[pg_off];
					longname[offset++] = ascii ? ascii : '?';
				}
				found_end:
				PRINTK(("3. get_new_entry: %d\n", get_new_entry));
				if (ds->id & 0x40) {
					longname[offset] = '\0';
					long_len = offset;
				}
				if (slot > 0) {
					ino = fat_get_entry(inode,&filp->f_pos,&bh,&de);
					PRINTK(("4. get_new_entry: %d\n", get_new_entry));
					if (ino == -1) {
						is_long = 0;
						get_new_entry = 0;
						break;
					}
					ds = (struct msdos_dir_slot *) de;
				}
				PRINTK(("5. get_new_entry: %d\n", get_new_entry));
			}
			PRINTK(("Long filename: %s, get_new_entry: %d\n", longname, get_new_entry));
		} else if (!IS_FREE(de->name) && !(de->attr & ATTR_VOLUME)) {
			char bufname[14];
			char *ptname = bufname;
			int dotoffset = 0;

			if (is_long) {
				unsigned char sum;

				for (sum = i = 0; i < 11; i++) {
					sum = (((sum&1)<<7)|((sum&0xfe)>>1)) + de->name[i];
				}

				if (sum != alias_checksum) {
					PRINTK(("Checksums don't match %d != %d\n", sum, alias_checksum));
					is_long = 0;
				}
			}

			if ((de->attr & ATTR_HIDDEN) && MSDOS_SB(sb)->dotsOK) {
				bufname[0] = '.';
				dotoffset = 1;
				ptname = bufname+1;
			}
			for (i = last = 0; i < 8; i++) {
				if (!(c = de->name[i])) break;
				if (c >= 'A' && c <= 'Z') c += 32;
				/* see namei.c, msdos_format_name */
				if (c == 0x05) c = 0xE5;
				if (c != ' ')
					last = i+1;
				ptname[i] = c;
			}
			i = last;
			ptname[i] = '.';
			i++;
			for (i2 = 0; i2 < 3; i2++) {
				if (!(c = de->ext[i2])) break;
				if (c >= 'A' && c <= 'Z') c += 32;
				if (c != ' ')
					last = i+1;
				ptname[i] = c;
				i++;
			}
			if ((i = last) != 0) {
				if (!strcmp(de->name,MSDOS_DOT))
					ino = inode->i_ino;
				else if (!strcmp(de->name,MSDOS_DOTDOT))
					ino = fat_parent_ino(inode,0);

				if (!is_long) {
					dcache_add(inode, bufname, i+dotoffset, ino);
					if (both) {
						bufname[i+dotoffset] = '\0';
					}
					if (filldir(dirent, bufname, i+dotoffset, oldpos, ino) < 0) {
						filp->f_pos = oldpos;
						break;
					}
				} else {
					dcache_add(inode, longname, long_len, ino);
					if (both) {
						memcpy(&longname[long_len+1], bufname, i+dotoffset);
						long_len += i+dotoffset;
					}
					if (filldir(dirent, longname, long_len, oldpos, ino) < 0) {
						filp->f_pos = oldpos;
						break;
					}
				}
				oldpos = filp->f_pos;
			}
			is_long = 0;
		} else {
			is_long = 0;
			oldpos = filp->f_pos;
		}
		ino = fat_get_entry(inode,&filp->f_pos,&bh,&de);	
	}
	if (bh) brelse(bh);
	return 0;
}

int fat_readdir(
	struct inode *inode,
	struct file *filp,
	void *dirent,
	filldir_t filldir)
{
    return fat_readdirx(inode, filp, dirent, filldir, 0);
}
static int vfat_ioctl_fill(
	void * buf,
	const char * name,
	int name_len,
	off_t offset,
	ino_t ino)
{
	struct dirent *d1 = (struct dirent *)buf;
	struct dirent *d2 = d1 + 1;
	int len, slen;
	int dotdir;

	if (get_user(&d1->d_reclen) != 0) {
		return -1;
	}

	if ((name_len == 1 && name[0] == '.') ||
	    (name_len == 2 && name[0] == '.' && name[1] == '.')) {
		dotdir = 1;
		len = name_len;
	} else {
		dotdir = 0;
		len = strlen(name);
	}
	if (len != name_len) {
		memcpy_tofs(d2->d_name, name, len);
		put_user(0, d2->d_name + len);
		put_user(len, &d2->d_reclen);
		put_user(ino, &d2->d_ino);
		put_user(offset, &d2->d_off);
		slen = name_len - len;
		memcpy_tofs(d1->d_name, name+len+1, slen);
		put_user(0, d1->d_name+slen);
		put_user(slen, &d1->d_reclen);
	} else {
		put_user(0, d2->d_name);
		put_user(0, &d2->d_reclen);
		memcpy_tofs(d1->d_name, name, len);
		put_user(0, d1->d_name+len);
		put_user(len, &d1->d_reclen);
	}
	PRINTK(("FAT d1=%p d2=%p len=%d, name_len=%d\n",
		d1, d2, len, name_len));

	return 0;
}

int fat_dir_ioctl(struct inode * inode, struct file * filp,
		  unsigned int cmd, unsigned long arg)
{
	/*
	 * We want to provide an interface for Samba to be able
	 * to get the short filename for a given long filename.
	 * Samba should use this ioctl instead of readdir() to
	 * get the information it needs.
	 */
	switch (cmd) {
	case VFAT_IOCTL_READDIR_BOTH: {
		struct dirent *d1 = (struct dirent *)arg;
		put_user(0, &d1->d_reclen);
		return fat_readdirx(inode,filp,(void *)arg,vfat_ioctl_fill,1);
	}
	default:
		return -EINVAL;
	}

	return 0;
}
