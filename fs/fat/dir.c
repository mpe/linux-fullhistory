/*
 *  linux/fs/fat/dir.c
 *
 *  directory handling functions for fat-based filesystems
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Hidden files 1995 by Albert Cahalan <albert@ccs.neu.edu> <adc@coe.neu.edu>
 *
 *  VFAT extensions by Gordon Chaffee <chaffee@plateau.cs.berkeley.edu>
 *  Merged with msdos fs by Henrik Storner <storner@osiris.ping.dk>
 */

#define ASC_LINUX_VERSION(V, P, S)	(((V) * 65536) + ((P) * 256) + (S))

#include <linux/version.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/nls.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/ioctl.h>
#include <linux/dirent.h>
#include <linux/mm.h>

#include <asm/uaccess.h>

#include "msbuffer.h"

#define PRINTK(X)

static ssize_t fat_dir_read(struct file * filp, char * buf,
			    size_t count, loff_t *ppos)
{
	return -EISDIR;
}

struct file_operations fat_dir_operations = {
	NULL,			/* lseek - default */
	fat_dir_read,		/* read */
	NULL,			/* write - bad */
	fat_readdir,		/* readdir */
	NULL,			/* select v2.0.x/poll v2.1.x - default */
	fat_dir_ioctl,		/* ioctl - default */
	NULL,			/* mmap */
	NULL,			/* no special open code */
	NULL,			/* flush */
	NULL,			/* no special release code */
	file_fsync		/* fsync */
};

/*
 * Convert Unicode 16 to UTF8, translated Unicode, or ASCII.
 * If uni_xlate is enabled and we
 * can't get a 1:1 conversion, use a colon as an escape character since
 * it is normally invalid on the vfat filesystem.  The following three
 * characters are a sort of uuencoded 16 bit Unicode value.  This lets
 * us do a full dump and restore of Unicode filenames.  We could get
 * into some trouble with long Unicode names, but ignore that right now.
 */
static int
uni16_to_x8(unsigned char *ascii, unsigned char *uni, int uni_xlate,
	    struct nls_table *nls)
{
	unsigned char *ip, *op;
	unsigned char ch, cl;
	unsigned char *uni_page;
	unsigned short val;

	ip = uni;
	op = ascii;

	while (*ip || ip[1]) {
		cl = *ip++;
		ch = *ip++;

		uni_page = nls->page_uni2charset[ch];
		if (uni_page && uni_page[cl]) {
			*op++ = uni_page[cl];
		} else {
			if (uni_xlate == 1) {
				*op++ = ':';
				val = (cl << 8) + ch;
				op[2] = fat_uni2esc[val & 0x3f];
				val >>= 6;
				op[1] = fat_uni2esc[val & 0x3f];
				val >>= 6;
				*op = fat_uni2esc[val & 0x3f];
				op += 3;
			} else {
				*op++ = '?';
			}
		}
	}
	*op = 0;
	return (op - ascii);
}

#if 0
static void dump_de(struct msdos_dir_entry *de)
{
	int i;
	unsigned char *p = (unsigned char *) de;
	printk("[");

	for (i = 0; i < 32; i++, p++) {
		printk("%02x ", *p);
	}
	printk("]\n");
}
#endif
int fat_readdirx(
	struct inode *inode,
	struct file *filp,
	void *dirent,
	fat_filldir_t fat_filldir,
	filldir_t filldir,
	int shortnames,
	int longnames,
	int both)
{
	struct super_block *sb = inode->i_sb;
	int ino,i,i2,last;
	char c;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	unsigned long oldpos = filp->f_pos;
	unsigned long spos;
	int is_long;
	char longname[275];
	unsigned char long_len = 0; /* Make compiler warning go away */
	unsigned char alias_checksum = 0; /* Make compiler warning go away */
	unsigned char long_slots = 0;
	int uni_xlate = MSDOS_SB(sb)->options.unicode_xlate;
	int utf8 = MSDOS_SB(sb)->options.utf8;
	unsigned char *unicode = NULL;
	struct nls_table *nls = MSDOS_SB(sb)->nls_io;

	if (!inode || !S_ISDIR(inode->i_mode))
		return -EBADF;
/* Fake . and .. for the root directory. */
	if (inode->i_ino == MSDOS_ROOT_INO) {
		while (oldpos < 2) {
			if (fat_filldir(filldir, dirent, "..", oldpos+1, 0, oldpos, oldpos, 0, MSDOS_ROOT_INO) < 0)
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
	longname[0] = longname[1] = 0;
	is_long = 0;
	ino = fat_get_entry(inode,&filp->f_pos,&bh,&de);
	while (ino > -1) {
#if 0
		dump_de(de);
#endif
		/* Check for long filename entry */
		if (MSDOS_SB(sb)->options.isvfat && (de->name[0] == (__s8) DELETED_FLAG)) {
			is_long = 0;
			oldpos = filp->f_pos;
		} else if (MSDOS_SB(sb)->options.isvfat && de->attr ==  ATTR_EXT) {
			int get_new_entry;
			struct msdos_dir_slot *ds;
			int offset;
			unsigned char id;
			unsigned char slot;
			unsigned char slots = 0;

			if (!unicode) {
				unicode = (unsigned char *)
					__get_free_page(GFP_KERNEL);
				if (!unicode)
					return -ENOMEM;
			}

			offset = 0;
			ds = (struct msdos_dir_slot *) de;
			id = ds->id;
			if (id & 0x40) {
				slots = id & ~0x40;
				long_slots = slots;
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
				offset = slot * 26;
				PRINTK(("2. get_new_entry: %d\n", get_new_entry));
				memcpy(&unicode[offset], ds->name0_4, 10);
				offset += 10;
				memcpy(&unicode[offset], ds->name5_10, 12);
				offset += 12;
				memcpy(&unicode[offset], ds->name11_12, 4);
				offset += 4;

				if (ds->id & 0x40) {
					unicode[offset] = 0;
					unicode[offset+1] = 0;
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
		} else if (!IS_FREE(de->name) && !(de->attr & ATTR_VOLUME)) {
			char bufname[14];
			char *ptname = bufname;
			int dotoffset = 0;
			int was_long = is_long;

			if (is_long) {
				unsigned char sum;
				for (sum = 0, i = 0; i < 11; i++) {
					sum = (((sum&1)<<7)|((sum&0xfe)>>1)) + de->name[i];
				}

				if (sum != alias_checksum) {
					PRINTK(("Checksums don't match %d != %d\n", sum, alias_checksum));
					is_long = 0;
					long_slots = 0;
				}
				if (utf8) {
					long_len = utf8_wcstombs(longname, (__u16 *) unicode, sizeof(longname));
				} else {
					long_len = uni16_to_x8(longname, unicode, uni_xlate, nls);
				}
			}

			if ((de->attr & ATTR_HIDDEN) && MSDOS_SB(sb)->options.dotsOK) {
				bufname[0] = '.';
				dotoffset = 1;
				ptname = bufname+1;
			}
			for (i = 0, last = 0; i < 8; i++) {
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

				if (shortnames || !is_long) {
					if (both)
						bufname[i+dotoffset] = '\0';
					spos = oldpos;
					if (was_long) {
						spos = filp->f_pos - sizeof(struct msdos_dir_entry);
					} else {
						long_slots = 0;
					}
					if (fat_filldir(filldir, dirent, bufname, i+dotoffset, 0, oldpos, spos, long_slots, ino) < 0) {
						filp->f_pos = oldpos;
						break;
					}
				}
				if (is_long && longnames) {
					if (both) {
						memcpy(&longname[long_len+1], bufname, i+dotoffset);
						long_len += i+dotoffset;
					}
					spos = filp->f_pos - sizeof(struct msdos_dir_entry);
					if (fat_filldir(filldir, dirent, longname, long_len, 1, oldpos, spos, long_slots, ino) < 0) {
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
	if (bh)
		fat_brelse(sb, bh);
	if (unicode) {
		free_page((unsigned long) unicode);
	}
	return 0;
}

static int fat_filldir(
	filldir_t filldir,
	void * buf,
	const char * name,
	int name_len,
	int is_long,
	off_t offset,
	off_t short_offset,
	int long_slots,
	ino_t ino)
{
	return filldir(buf, name, name_len, offset, ino);
}

int fat_readdir(
	struct file *filp,
	void *dirent,
	filldir_t filldir)
{
	struct inode *inode = filp->f_dentry->d_inode;
	return fat_readdirx(inode, filp, dirent, fat_filldir, filldir,
			    0, 1, 0);
}

static int vfat_ioctl_fill(
	filldir_t filldir,
	void * buf,
	const char * name,
	int name_len,
	int is_long,
	off_t offset,
	off_t short_offset,
	int long_slots,
	ino_t ino)
{
	struct dirent *d1 = (struct dirent *)buf;
	struct dirent *d2 = d1 + 1;
	int len, slen;
	int dotdir;

	get_user(len, &d1->d_reclen);
	if (len != 0) {
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
		copy_to_user(d2->d_name, name, len);
		put_user(0, d2->d_name + len);
		put_user(len, &d2->d_reclen);
		put_user(ino, &d2->d_ino);
		put_user(offset, &d2->d_off);
		slen = name_len - len;
		copy_to_user(d1->d_name, name+len+1, slen);
		put_user(0, d1->d_name+slen);
		put_user(slen, &d1->d_reclen);
	} else {
		put_user(0, d2->d_name);
		put_user(0, &d2->d_reclen);
		copy_to_user(d1->d_name, name, len);
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
	int err;
	/*
	 * We want to provide an interface for Samba to be able
	 * to get the short filename for a given long filename.
	 * Samba should use this ioctl instead of readdir() to
	 * get the information it needs.
	 */
	switch (cmd) {
	case VFAT_IOCTL_READDIR_BOTH: {
		struct dirent *d1 = (struct dirent *)arg;
		err = verify_area(VERIFY_WRITE, d1, sizeof(struct dirent[2]));
		if (err)
			return err;
		put_user(0, &d1->d_reclen);
		return fat_readdirx(inode,filp,(void *)arg,
				    vfat_ioctl_fill, NULL, 0, 1, 1);
	}
	case VFAT_IOCTL_READDIR_SHORT: {
		struct dirent *d1 = (struct dirent *)arg;
		put_user(0, &d1->d_reclen);
		err = verify_area(VERIFY_WRITE, d1, sizeof(struct dirent[2]));
		if (err)
			return err;
		return fat_readdirx(inode,filp,(void *)arg,
				    vfat_ioctl_fill, NULL, 1, 0, 1);
	}
	default:
		/* forward ioctl to CVF extension */
	       if (MSDOS_SB(inode->i_sb)->cvf_format &&
		   MSDOS_SB(inode->i_sb)->cvf_format->cvf_dir_ioctl)
		       return MSDOS_SB(inode->i_sb)->cvf_format
			       ->cvf_dir_ioctl(inode,filp,cmd,arg);
		return -EINVAL;
	}

	return 0;
}

/*
 * Overrides for Emacs so that we follow Linus's tabbing style.
 * Emacs will notice this stuff at the end of the file and automatically
 * adjust the settings for this buffer only.  This must remain at the end
 * of the file.
 * ---------------------------------------------------------------------------
 * Local variables:
 * c-indent-level: 8
 * c-brace-imaginary-offset: 0
 * c-brace-offset: -8
 * c-argdecl-indent: 8
 * c-label-offset: -8
 * c-continued-statement-offset: 8
 * c-continued-brace-offset: 0
 * End:
 */
