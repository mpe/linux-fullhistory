/*
 *  linux/fs/msdos/vfat.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Windows95/Windows NT compatible extended MSDOS filesystem
 *    by Gordon Chaffee Copyright (C) 1995
 */

#include <linux/module.h>

#include <linux/sched.h>
#include <linux/msdos_fs.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>

#include <asm/segment.h>

#include "../fat/msbuffer.h"
#include "../fat/tables.h"

#if 0
#define PRINTK(x) printk x
#else
#define PRINTK(x)
#endif


void vfat_read_inode(struct inode *inode);


void vfat_put_super(struct super_block *sb)
{
	fat_put_super(sb);
	MOD_DEC_USE_COUNT;
}


static struct super_operations vfat_sops = { 
	vfat_read_inode,
	fat_notify_change,
	fat_write_inode,
	fat_put_inode,
	vfat_put_super,
	NULL, /* added in 0.96c */
	fat_statfs,
	NULL
};

struct super_block *vfat_read_super(struct super_block *sb,void *data,
				    int silent)
{
	struct super_block *res;
  
	MOD_INC_USE_COUNT;
	
	sb->s_op = &vfat_sops;
	res = fat_read_super(sb, data, silent);
	if (res == NULL) {
	  MOD_DEC_USE_COUNT;
	} else {
	  MSDOS_SB(sb)->vfat = 1;
	  MSDOS_SB(sb)->dotsOK = 0;
	}

	return res;
}



#ifdef DEBUG

static void dump_fat(struct super_block *sb,int start)
{
	printk("[");
	while (start) {
		printk("%d ",start);
		start = fat_access(sb,start,-1);
		if (!start) {
			printk("ERROR");
			break;
		}
		if (start == -1) break;
	}
	printk("]\n");
}

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

/* MS-DOS "device special files" */

static const char *reserved_names[] = {
	"CON     ","PRN     ","NUL     ","AUX     ",
	"LPT1    ","LPT2    ","LPT3    ","LPT4    ",
	"COM1    ","COM2    ","COM3    ","COM4    ",
	NULL };


/* Characters that are undesirable in an MS-DOS file name */

#if 1  /* Steve Searle's characters */
static char bad_chars[] = "*?<>|\":/\\";
static char bad_if_strict[] = "+=,; []";
#else
static char bad_chars[] = "*?<>|\"";
static char bad_if_strict[] = "+=,; ";
#endif
static char replace_chars[] = "[];,+=";

static int vfat_find(struct inode *dir,const char *name,int len,
		      int find_long,int new_filename,int is_dir,
		      struct slot_info *sinfo_out);

/* Checks the validity of an long MS-DOS filename */
/* Returns negative number on error, 0 for a normal
 * return, and 1 for . or .. */

static int vfat_valid_longname(const char *name, int len, int dot_dirs)
{
	const char **reserved;
	unsigned char c;
	int i;

	if (IS_FREE(name)) return -EINVAL;
	if (name[0] == '.' && (len == 1 || (len == 2 && name[1] == '.'))) {
		if (!dot_dirs) return -EEXIST;
		return 1;
	}

	if (len && name[len-1] == ' ') return -EINVAL;
	if (len >= 256) return -EINVAL;
	for (i = 0; i < len; i++) {
		c = name[i];
		if (strchr(bad_chars,c)) return -EINVAL;
	}
	if (len == 3 || len == 4) {
		for (reserved = reserved_names; *reserved; reserved++)
			if (!strncmp(name,*reserved,8)) return -EINVAL;
	}
	return 0;
}

static int vfat_valid_shortname(char conv,const char *name,int len,
				 int dot_dirs)
{
	const char *walk, **reserved;
	unsigned char c;
	int space;

	if (IS_FREE(name)) return -EINVAL;
	if (name[0] == '.' && (len == 1 || (len == 2 && name[1] == '.'))) {
		if (!dot_dirs) return -EEXIST;
		return 1;
	}

	space = 1; /* disallow names starting with a dot */
	c = 0;
	for (walk = name; len && walk-name < 8;) {
	    	c = *walk++;
		len--;
		if (conv != 'r' && strchr(bad_chars,c)) return -EINVAL;
		if (conv == 'x' && strchr(replace_chars,c)) return -EINVAL;
		if (conv == 's' && strchr(bad_if_strict,c)) return -EINVAL;
  		if (c >= 'A' && c <= 'Z' && (conv == 's' || conv == 'x')) return -EINVAL;
		if (c < ' ' || c == ':' || c == '\\') return -EINVAL;
		if ((walk == name) && (c == 0xE5)) c = 0x05;
		if (c == '.') break;
		space = c == ' ';
	}
	if (space) return -EINVAL;
	if ((conv == 's' || conv == 'x') && len && c != '.') {
		c = *walk++;
		len--;
		if (c != '.') return -EINVAL;
	}
	while (c != '.' && len--) c = *walk++;
	if (c == '.') {
		if (len >= 4) return -EINVAL;
		while (len > 0 && walk-name < (MSDOS_NAME+1)) {
			c = *walk++;
			len--;
			if (conv != 'r' && strchr(bad_chars,c)) return -EINVAL;
			if (conv == 's' && strchr(bad_if_strict,c))
				return -EINVAL;
			if (conv == 'x' && strchr(replace_chars,c))
				return -EINVAL;
			if (c < ' ' || c == ':' || c == '\\' || c == '.')
				return -EINVAL;
			if (c >= 'A' && c <= 'Z' && (conv == 's' || conv == 'x')) return -EINVAL;
			space = c == ' ';
		}
		if (space) return -EINVAL;
		if ((conv == 's' || conv == 'x') && len) return -EINVAL;
	}
	for (reserved = reserved_names; *reserved; reserved++)
		if (!strncmp(name,*reserved,8)) return -EINVAL;

	return 0;
}

/* Takes a short filename and converts it to a formatted MS-DOS filename.
 * If the short filename is not a valid MS-DOS filename, an error is 
 * returned.  The formatted short filename is returned in 'res'.
 */

static int vfat_format_name(char conv,const char *name,int len,char *res,
  int dot_dirs)
{
	char *walk;
	const char **reserved;
	unsigned char c;
	int space;

	if (IS_FREE(name)) return -EINVAL;
	if (name[0] == '.' && (len == 1 || (len == 2 && name[1] == '.'))) {
		if (!dot_dirs) return -EEXIST;
		memset(res+1,' ',10);
		while (len--) *res++ = '.';
		return 0;
	}

	space = 1; /* disallow names starting with a dot */
	c = 0;
	for (walk = res; len && walk-res < 8; walk++) {
	    	c = *name++;
		len--;
		if (conv != 'r' && strchr(bad_chars,c)) return -EINVAL;
		if (conv == 's' && strchr(bad_if_strict,c)) return -EINVAL;
		if (conv == 'x' && strchr(replace_chars,c)) return -EINVAL;
		if (c >= 'A' && c <= 'Z' && (conv == 's' || conv == 'x')) return -EINVAL;
		if (c < ' ' || c == ':' || c == '\\') return -EINVAL;
		if (c == '.') break;
		space = c == ' ';
		*walk = c >= 'a' && c <= 'z' ? c-32 : c;
	}
	if (space) return -EINVAL;
	if ((conv == 's' || conv == 'x') && len && c != '.') {
		c = *name++;
		len--;
		if (c != '.') return -EINVAL;
	}
	while (c != '.' && len--) c = *name++;
	if (c == '.') {
		while (walk-res < 8) *walk++ = ' ';
		while (len > 0 && walk-res < MSDOS_NAME) {
			c = *name++;
			len--;
			if (conv != 'r' && strchr(bad_chars,c)) return -EINVAL;
			if (conv == 's' && strchr(bad_if_strict,c))
				return -EINVAL;
			if (conv == 'x' && strchr(replace_chars,c))
				return -EINVAL;
			if (c < ' ' || c == ':' || c == '\\' || c == '.')
				return -EINVAL;
			if (c >= 'A' && c <= 'Z' && (conv == 's' || conv == 'x')) return -EINVAL;
			space = c == ' ';
			*walk++ = c >= 'a' && c <= 'z' ? c-32 : c;
		}
		if (space) return -EINVAL;
		if ((conv == 's' || conv == 'x') && len) return -EINVAL;
	}
	while (walk-res < MSDOS_NAME) *walk++ = ' ';
	for (reserved = reserved_names; *reserved; reserved++)
		if (!strncmp(res,*reserved,8)) return -EINVAL;

	return 0;
}

static char skip_chars[] = ".:\"?<>| ";

/* Given a valid longname, create a unique shortname.  Make sure the
 * shortname does not exist
 */
static int vfat_create_shortname(struct inode *dir, const char *name,
     int len, char *name_res)
{
	const char *ip, *ext_start, *end;
	char *p;
	int valid;
	int sz, extlen, baselen, totlen;
	char msdos_name[13];
	char base[9], ext[4];
	int i;
	int res;
	int ino;
	int spaces;
	int count;
	char buf[8];
	struct slot_info sinfo;
	const char *name_start;

	PRINTK(("Entering vfat_create_shortname: name=%s, len=%d\n", name, len));
	sz = 0;			/* Make compiler happy */
	valid = 1;
	if (len && name[len-1]==' ') return -EINVAL;
	if (len <= 12) {
		/* Do a case insensitive search if the name would be a valid
		 * shortname if is were all capitalized */
		for (i = 0, p = msdos_name, ip = name; i < len; i++, p++, ip++)
		{
			if (*ip >= 'A' && *ip <= 'Z') {
				*p = *ip + 32;
			} else {
				*p = *ip;
			}
		}
		res = vfat_format_name('x', msdos_name, len, name_res, 1);
		if (res > -1) {
			PRINTK(("vfat_create_shortname 1\n"));
			res = vfat_find(dir, msdos_name, len, 0, 0, 0, &sinfo);
			ino = sinfo.ino;
			PRINTK(("vfat_create_shortname 2\n"));
			if (res > -1) return -EEXIST;
			return 0;
		}
	}

	PRINTK(("vfat_create_shortname 3\n"));
	/* Now, we need to create a shortname from the long name */
	ext_start = end = &name[len];
	while (--ext_start >= name) {
		if (*ext_start == '.') {
			if (ext_start == end - 1) {
				sz = len;
				ext_start = NULL;
			}
			break;
		}
	}
	if (ext_start == name - 1) {
		sz = len;
		ext_start = NULL;
	} else if (ext_start) {
		/*
		 * Names which start with a dot could be just
		 * an extension eg. "...test".  In this case Win95
		 * uses the extension as the name and sets no extension.
		 */
		name_start = &name[0];
		while (name_start < ext_start)
		{
			if (!strchr(skip_chars,*name_start)) break;
			name_start++;
		}
		if (name_start != ext_start) {
			sz = ext_start - name;
			ext_start++;
		} else {
			sz = len;
			ext_start=NULL;
		}
	}

	for (baselen = i = 0, p = base, ip = name; i < sz && baselen < 8; i++)
	{
		if (!strchr(skip_chars, *ip)) {
			if (*ip >= 'A' && *ip <= 'Z') {
				*p = *ip + 32;
			} else {
				*p = *ip;
			}
			p++; baselen++;
		}
		ip++;
	}
	if (baselen == 0) {
		return -EINVAL;
	}
		
	spaces = 8 - baselen;

	if (ext_start) {
		extlen = 0;
		for (p = ext, ip = ext_start; extlen < 3 && ip < end; ip++) {
			if (!strchr(skip_chars, *ip)) {
				if (*ip >= 'A' && *ip <= 'Z') {
					*p = *ip + 32;
				} else {
					*p = *ip;
				}
				if (strchr(replace_chars, *p)) *p='_';
				extlen++;
				p++;
			}
		}
	} else {
		extlen = 0;
	}
	ext[extlen] = '\0';

	count = 0;
	strcpy(msdos_name, base);
	msdos_name[baselen] = '.';
	strcpy(&msdos_name[baselen+1], ext);

	totlen = baselen + extlen + 1;
	res = 0;
	while (res > -1) {
		/* Create the next shortname to try */
		count++;
		if (count == 10000000) return -EEXIST;
		sprintf(buf, "%d", count);
		sz = strlen(buf);
		if (sz + 1 > spaces) {
			baselen = baselen - (sz + 1 - spaces);
			spaces = sz + 1;
		}

		strncpy(msdos_name, base, baselen);
		msdos_name[baselen] = '~';
		strcpy(&msdos_name[baselen+1], buf);
		msdos_name[baselen+sz+1] = '.';
		strcpy(&msdos_name[baselen+sz+2], ext);

		totlen = baselen + sz + 2 + extlen;
		res = vfat_find(dir, msdos_name, totlen, 0, 0, 0, &sinfo);
	}
	res = vfat_format_name('x', msdos_name, totlen, name_res, 1);
	return res;
}

static loff_t vfat_find_free_slots(struct inode *dir,int slots)
{
	struct super_block *sb = dir->i_sb;
	loff_t offset, curr;
	struct msdos_dir_entry *de;
	struct buffer_head *bh;
	struct inode *inode;
	int ino;
	int row;
	int done;
	int res;
	int added;

	PRINTK(("vfat_find_free_slots: find %d free slots\n", slots));
	offset = curr = 0;
	bh = NULL;
	row = 0;
	ino = fat_get_entry(dir,&curr,&bh,&de);

	for (added = 0; added < 2; added++) {
		while (ino > -1) {
			done = IS_FREE(de->name);
			if (done) {
				inode = iget(sb,ino);
				if (inode) {
					/* Directory slots of busy deleted files aren't available yet. */
					done = !MSDOS_I(inode)->i_busy;
					/* PRINTK(("inode %d still busy\n", ino)); */
				}
				iput(inode);
			}
			if (done) {
				row++;
				if (row == slots) {
					brelse(bh);
					/* printk("----- Free offset at %d\n", offset); */
					return offset;
				}
			} else {
				row = 0;
				offset = curr;
			}
			ino = fat_get_entry(dir,&curr,&bh,&de);
		}

		if (dir->i_ino == MSDOS_ROOT_INO) return -ENOSPC;
		if ((res = fat_add_cluster(dir)) < 0) return res;
		ino = fat_get_entry(dir,&curr,&bh,&de);
	}
	/* Should never get here, but if it does */
	printk("vfat_find_free_slots: Unable to find any\n");
	return -ENOSPC;
}
		
static int vfat_build_slots(struct inode *dir,const char *name,int len,
     int find_long, int new_filename,
     struct msdos_dir_slot *ds, struct msdos_dir_slot *ds_mask,
     int *slots, int *is_long)
{
	struct msdos_dir_slot *ps, *ps_mask;
	struct msdos_dir_entry *de, *de_mask;
	char msdos_name[MSDOS_NAME];
	int res;
	int slot;
	int i;
	const unsigned char *ip;
	loff_t offset;
	unsigned char alias_checksum;

	PRINTK(("Entering vfat_build_slots: name=%s, len=%d\n", name, len));
	de = (struct msdos_dir_entry *) ds;
	de_mask = (struct msdos_dir_entry *) ds_mask;

	*slots = 1;
	*is_long = 0;
	memset(ds_mask, 0, sizeof(struct msdos_dir_slot) * MSDOS_SLOTS);
	if (len == 1 && name[0] == '.') {
		strncpy(de->name, MSDOS_DOT, MSDOS_NAME);
		memset(de_mask, 0xff, MSDOS_NAME);
	} else if (len == 2 && name[0] == '.' && name[1] == '.') {
		strncpy(de->name, MSDOS_DOT, MSDOS_NAME);
		memset(de_mask, 0xff, MSDOS_NAME);
	} else {
		PRINTK(("vfat_build_slots 4\n"));
		res = vfat_valid_shortname('x', name, len, 1);
		if (res > -1) {
			PRINTK(("vfat_build_slots 5a\n"));
			res = vfat_format_name('x', name, len, de->name, 1);
			PRINTK(("vfat_build_slots 5b\n"));
			memset(de_mask->name, 0xff, MSDOS_NAME);
			PRINTK(("vfat_build_slots 5c\n"));
		} else {
			PRINTK(("vfat_build_slots 5A: %s (len=%d) is an invalid shortname\n", name, len));
			if (new_filename) {
				unsigned char sum;

				PRINTK(("vfat_build_slots 5Z\n"));
				res = vfat_create_shortname(dir, name, len, msdos_name);
				PRINTK(("vfat_build_slots 5Y\n"));
				if (res < 0) {
					return res;
				}

				for (sum = i = 0; i < 11; i++) {
					sum = (((sum&1)<<7)|((sum&0xfe)>>1)) + msdos_name[i];
				}
				PRINTK(("vfat_build_slots 5X: msdos_name=%s\n", msdos_name));
				alias_checksum = sum;
			} else {
				alias_checksum = 0;
			}

			if (!find_long) return -EINVAL;
			res = vfat_valid_longname(name, len, 1);
			if (res < 0) return res;

			*is_long = 1;
			*slots = (len + 12) / 13;
			PRINTK(("vfat_build_slots 6: slots=%d\n",*slots));

			for (ps = ds, slot = *slots, ps_mask = ds_mask;
			     slot > 0; slot--, ps++, ps_mask++)
			{
				int end, j;

				PRINTK(("vfat_build_slots 6a\n"));
				ps->id = slot; ps_mask->id = 0xff;
				ps->attr = ATTR_EXT; ps_mask->attr = 0xff;
				ps->reserved = 0; ps_mask->reserved = 0xff;
				ps->alias_checksum = alias_checksum;
				ps_mask->alias_checksum = 0;
				ps->start[0] = 0; ps_mask->start[0] = 0xff;
				ps->start[1] = 0; ps_mask->start[1] = 0xff;
				PRINTK(("vfat_build_slots 6b: name=%s\n",name));
				offset = (slot - 1) * 13;
				ip = &name[offset];
				j = offset;
				end = 0;
				for (i = 0; i < 10; i += 2) {
					if (!end && j == len) {
						end = 1;
						ps->name0_4[i] = 0;
						ps_mask->name0_4[i] = 0xff;
						ps->name0_4[i+1] = 0;
						ps_mask->name0_4[i] = 0xff;
						continue;
					} else if (end) {
						ps->name0_4[i] = 0xff;
						ps_mask->name0_4[i] = 0xff;
						ps->name0_4[i+1] = 0xff;
						ps_mask->name0_4[i+1] = 0xff;
						continue;
					}
					ps->name0_4[i] = fat_a2uni[*ip].uni1;
					ps->name0_4[i+1] = fat_a2uni[*ip].uni2;
					if ((*ip >= 'a' && *ip <= 'z') ||
					    (*ip >= 'A' && *ip <= 'Z')) {
						ps_mask->name0_4[i] = 0xdf;
					} else {
						ps_mask->name0_4[i] = 0xff;
					}
					ps_mask->name0_4[i+1] = 0xff;
					j++; ip++;
				}
				PRINTK(("vfat_build_slots 6c\n"));
				for (i = 0; i < 12; i += 2) {
					if (!end && j == len) {
						end = 1;
						ps->name5_10[i] = 0;
						ps->name5_10[i+1] = 0;
						continue;
					} else if (end) {
						ps->name5_10[i] = 0xff;
						ps_mask->name5_10[i] = 0xff;
						ps->name5_10[i+1] = 0xff;
						ps_mask->name5_10[i+1] = 0xff;
						continue;
					}
					ps->name5_10[i] = fat_a2uni[*ip].uni1;
					ps->name5_10[i+1] = fat_a2uni[*ip].uni2;
					if ((*ip >= 'a' && *ip <= 'z') ||
					    (*ip >= 'A' && *ip <= 'Z')) {
						ps_mask->name5_10[i] = 0xdf;
					} else {
						ps_mask->name5_10[i] = 0xff;
					}
					ps_mask->name5_10[i+1] = 0xff;
					j++; ip++;
				}
				PRINTK(("vfat_build_slots 6d\n"));
				for (i = 0; i < 4; i += 2) {
					if (!end && j == len) {
						end = 1;
						ps->name11_12[i] = 0;
						ps->name11_12[i+1] = 0;
						continue;
					} else if (end) {
						ps->name11_12[i] = 0xff;
						ps_mask->name11_12[i] = 0xff;
						ps->name11_12[i+1] = 0xff;
						ps_mask->name11_12[i+1] = 0xff;
						continue;
					}
					ps->name11_12[i] = fat_a2uni[*ip].uni1;
					ps->name11_12[i+1] = fat_a2uni[*ip].uni2;
					if ((*ip >= 'a' && *ip <= 'z') ||
					    (*ip >= 'A' && *ip <= 'Z')) {
						ps_mask->name11_12[i] = 0xdf;
					} else {
						ps_mask->name11_12[i] = 0xff;
					}
					ps_mask->name11_12[i+1] = 0xff;
					j++; ip++;
				}
			}
			PRINTK(("vfat_build_slots 6e\n"));
			ds[0].id |= 0x40;

			if (new_filename) {
				de = (struct msdos_dir_entry *) ps;
				de_mask = (struct msdos_dir_entry *) ps_mask;

				PRINTK(("vfat_build_slots 10\n"));
				strncpy(de->name, msdos_name, MSDOS_NAME);
				memset(de_mask->name, 0xff, MSDOS_NAME);
			}
		}
	}
	return 0;
}

/* Given a shortname offset, see if there is an associated longname.  Returns
 * the number of slots in the longname if one is found, else 0 */
static int vfat_get_longname(struct inode *dir,loff_t short_offset,
     unsigned char checksum, loff_t *pos_out)
{
	struct super_block *sb = dir->i_sb;
	struct msdos_dir_slot *ps;
	struct msdos_dir_entry *de;
	struct buffer_head *bh;
	loff_t offset, temp;
	int id, res, slots;

	/* printk("Short offset: %d\n", short_offset); */
	if (short_offset == 0) {
		return 0;
	}

	slots = 0;
	id = 1;
	bh = NULL;
	offset = short_offset - sizeof(struct msdos_dir_slot);
	while (offset > 0) {
		temp = offset;
		res = fat_get_entry(dir,&temp,&bh,&de);
		if (res < 0) goto finish;
		ps = (struct msdos_dir_slot *) de;
		if (ps->alias_checksum != checksum) goto finish;
		if ((ps->id &~ 0x40) != id) goto finish;
		if (IS_FREE(de->name)) goto finish;
		if (ps->id & 0x40) {
			*pos_out = offset;
			slots = ps->id &~ 0x40;
			/* printk("Found a longname for the shortname: long_offset=%ld\n", offset); */
			goto finish;
		}
		offset -= sizeof(struct msdos_dir_slot);
		id++;
	}
 finish:
	if (bh) brelse(bh);
	return slots;
}

static int vfat_find(struct inode *dir,const char *name,int len,
    int find_long, int new_filename,int is_dir,struct slot_info *sinfo_out)
{
	struct super_block *sb = dir->i_sb;
	const char *ip;
	char *op, *op_mask;
	int res;
	struct msdos_dir_slot ds[MSDOS_SLOTS], ds_mask[MSDOS_SLOTS];
	struct msdos_dir_slot *ps;
	struct msdos_dir_entry *de, *de_mask;
	struct buffer_head *bh;
	int i, slot, slots;
	loff_t offset, start;
	int match;
	int is_long;
	unsigned char alias_checksum = 0;

	PRINTK(("vfat_find 1: name=%s, len=%d\n",name,len));

	res = vfat_build_slots(dir, name, len, find_long, new_filename,
				ds, ds_mask, &slots, &is_long);
	if (res < 0) return res;

	de = (struct msdos_dir_entry *) ds;
	de_mask = (struct msdos_dir_entry *) ds_mask;

	PRINTK(("vfat_find 7\n"));
	offset = start = 0;
	bh = NULL;
	sinfo_out->ino = fat_get_entry(dir,&offset,&bh,&de);
	while (sinfo_out->ino > -1 && slots > 0) {
		match = 1;

		ps = (struct msdos_dir_slot *) de;
		alias_checksum = ps->alias_checksum;

		for (slot = 0; slot < slots; slot++) {
			ip = (char *) de;
			ps = (struct msdos_dir_slot *) de;
			if (is_long && ps->alias_checksum != alias_checksum) {
				printk("Checksums don't match 1\n");
				match = 0;
				start = offset;
				break;
			}

			for (i = 0, ip = (char *) de, op = (char *) &ds[slot], op_mask = (char *) &ds_mask[slot];
			     i < sizeof(struct msdos_dir_entry);
			     i++, ip++, op++, op_mask++)
			{
#if 0
				if (is_long && de->attr == ATTR_EXT)
					printk("%02x?%02x ",
					       (unsigned char) *ip,
					       (unsigned char) *op);
#endif
				if ((*ip & *op_mask) != (*op & *op_mask)) {
					start = offset;
					match = 0;
					break;
				}
			}
#if 0
			if (is_long && de->attr == ATTR_EXT) printk("\n");
#endif
			if ((!is_long && !match) ||
			    (de->attr == ATTR_VOLUME) ||
			    (is_long && (match || slot == 0)))
			{
				sinfo_out->ino = fat_get_entry(dir,&offset,&bh,&de);
				/* if (ino >=0 && de->attr == ATTR_EXT) dump_de(de); */
			}
			if (!match) {
				break;
			}
			if (sinfo_out->ino == -1) {
				match = 0;
				goto breakout;
			}
		}
		if (match) {
			unsigned char sum;

			for (sum = i = 0; i < 11; i++) {
				sum = (((sum&1)<<7)|((sum&0xfe)>>1)) + de->name[i];
			}

			if (is_long) {
				if (sum != alias_checksum) {
					PRINTK(("Checksums don't match %d != %d\n", sum, alias_checksum));
					match = 0;
				}
			} else {
				int long_slots;
				long_slots = vfat_get_longname(dir, offset - sizeof(struct msdos_dir_entry), sum, &start);
				if (long_slots > 0) {
					slots = long_slots;
					is_long = 1;
				}
			}

				
			if (match) {
				PRINTK(("name: %s, alias: %c%c%c%c%c%c%c%c%c%c%c\n",
					name,
					de->name[0], de->name[1], de->name[2],
					de->name[3], de->name[4], de->name[5],
					de->name[6], de->name[7], de->name[8],
					de->name[9], de->name[10]));
				PRINTK(("vfat_find 10\n"));
				res = CF_LE_W(de->start);

				sinfo_out->shortname_offset = offset - sizeof(struct msdos_dir_slot);
				sinfo_out->longname_offset = start;
				sinfo_out->is_long = is_long;
				if (is_long) {
					sinfo_out->long_slots = slots;
					slots++;
				} else {
					sinfo_out->long_slots = 0;
				}

				sinfo_out->total_slots = slots;
				if (new_filename) {
					if (bh) brelse(bh);
					return -EEXIST;
				}
				if (bh) brelse(bh);
				return res;
			}
		}
	}
 breakout:
	PRINTK(("breakout\n"));

	if (bh) brelse(bh);
	if (new_filename) {
		PRINTK(("vfat_find: create file 1\n"));
		if (is_long) slots++;
		offset = vfat_find_free_slots(dir, slots);
		if (offset < 0) {
			return offset;
		}

		PRINTK(("vfat_find: create file 2\n"));
		/* Now create the new entry */
		bh = NULL;
		for (slot = 0, ps = ds; slot < slots; slot++, ps++) {
			PRINTK(("vfat_find: create file 3, slot=%d\n",slot));
			sinfo_out->ino = fat_get_entry(dir,&offset,&bh,&de);
			if (sinfo_out->ino < 0) {
				PRINTK(("vfat_find: problem\n"));
				return sinfo_out->ino;
			}
			memcpy(de, ps, sizeof(struct msdos_dir_slot));
			mark_buffer_dirty(bh, 1);
		}

		PRINTK(("vfat_find: create file 4\n"));
		dir->i_ctime = dir->i_mtime = dir->i_atime = CURRENT_TIME;
		dir->i_dirt = 1;

		PRINTK(("vfat_find: create file 5\n"));

		memset(de->unused, 0, sizeof(de->unused));
		fat_date_unix2dos(dir->i_mtime,&de->time,&de->date);
		de->ctime_ms = 0;
		de->ctime = de->time;
		de->adate = de->cdate = de->date;
		de->start = 0;
		de->size = 0;
		de->attr = is_dir ? ATTR_DIR : ATTR_ARCH;
		de->lcase = CASE_LOWER_BASE | CASE_LOWER_EXT;


		mark_buffer_dirty(bh, 1);
		brelse(bh);

		sinfo_out->is_long = (slots > 1) ? 1 : 0;
		if (sinfo_out->is_long) {
			sinfo_out->long_slots = slots - 1;
		} else {
			sinfo_out->long_slots = 0;
		}
		sinfo_out->total_slots = slots;
		sinfo_out->shortname_offset = offset - sizeof(struct msdos_dir_slot);
		sinfo_out->longname_offset = offset - sizeof(struct msdos_dir_slot) * slots;
		return 0;
	}

	return -ENOENT;
}

int vfat_lookup(struct inode *dir,const char *name,int len,
    struct inode **result)
{
	int res, ino;
	struct inode *next;
	struct slot_info sinfo;
	
	PRINTK (("vfat_lookup: name=%s, len=%d\n", name, len));

	*result = NULL;
	if (!dir) return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOENT;
	}
	PRINTK (("vfat_lookup 2\n"));
	if (len == 1 && name[0] == '.') {
		*result = dir;
		return 0;
	}
	if (len == 2 && name[0] == '.' && name[1] == '.') {
		ino = fat_parent_ino(dir,0);
		iput(dir);
		if (ino < 0) return ino;
		if (!(*result = iget(dir->i_sb,ino))) return -EACCES;
		return 0;
	}
	PRINTK (("vfat_lookup 3\n"));
	if ((res = vfat_find(dir,name,len,1,0,0,&sinfo)) < 0) {
		iput(dir);
		return res;
	}
	PRINTK (("vfat_lookup 4.5\n"));
	if (!(*result = iget(dir->i_sb,sinfo.ino))) {
		iput(dir);
		return -EACCES;
	}
	PRINTK (("vfat_lookup 5\n"));
	if (!(*result)->i_sb ||
	    ((*result)->i_sb->s_magic != MSDOS_SUPER_MAGIC)) {
		/* crossed a mount point into a non-msdos fs */
		iput(dir);
		return 0;
	}
	if (MSDOS_I(*result)->i_busy) { /* mkdir in progress */
		iput(*result);
		iput(dir);
		return -ENOENT;
	}
	PRINTK (("vfat_lookup 6\n"));
	while (MSDOS_I(*result)->i_old) {
		next = MSDOS_I(*result)->i_old;
		iput(*result);
		if (!(*result = iget(next->i_sb,next->i_ino))) {
			fat_fs_panic(dir->i_sb,"vfat_lookup: Can't happen");
			iput(dir);
			return -ENOENT;
		}
	}
	iput(dir);
	return 0;
}


static int vfat_create_entry(struct inode *dir,const char *name,int len,
    int is_dir, struct inode **result)
{
	struct super_block *sb = dir->i_sb;
	int res,ino;
	loff_t offset;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct slot_info sinfo;

	PRINTK(("vfat_create_entry 1\n"));
	res = vfat_find(dir, name, len, 1, 1, is_dir, &sinfo);
	if (res < 0) {
		return res;
	}

	offset = sinfo.shortname_offset;

	PRINTK(("vfat_create_entry 2\n"));
	bh = NULL;
	ino = fat_get_entry(dir, &offset, &bh, &de);
	if (ino < 0) {
		PRINTK(("vfat_mkdir problem\n"));
		if (bh) brelse(bh);
		return ino;
	}
	PRINTK(("vfat_create_entry 3\n"));

	if ((*result = iget(dir->i_sb,ino)) != NULL)
		vfat_read_inode(*result);
	brelse(bh);
	if (!*result) return -EIO;
	(*result)->i_mtime = (*result)->i_atime = (*result)->i_ctime =
	    CURRENT_TIME;
	(*result)->i_dirt = 1;

	return 0;
}

int vfat_create(struct inode *dir,const char *name,int len,int mode,
	struct inode **result)
{
	int res;

	if (!dir) return -ENOENT;

	fat_lock_creation();
	if ((res = vfat_create_entry(dir,name,len,0,result)) < 0) {
		printk("vfat_create: unable to get new entry\n");
		fat_unlock_creation();
		iput(dir);
		return res;
	}

	fat_unlock_creation();
	iput(dir);
	return res;
}

static int vfat_create_a_dotdir(struct inode *dir,struct inode *parent,
     struct buffer_head *bh,
     struct msdos_dir_entry *de,int ino,const char *name, int isdot)
{
	struct super_block *sb = dir->i_sb;
	struct inode *dot;

	PRINTK(("vfat_create_a_dotdir 1\n"));

	/*
	 * XXX all times should be set by caller upon successful completion.
	 */
	dir->i_atime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_dirt = 1;
	memcpy(de->name,name,MSDOS_NAME);
	memset(de->unused, 0, sizeof(de->unused));
	de->lcase = 0;
	de->attr = ATTR_DIR;
	de->start = 0;
	fat_date_unix2dos(dir->i_mtime,&de->time,&de->date);
	de->ctime_ms = 0;
	de->ctime = de->time;
	de->adate = de->cdate = de->date;
	de->size = 0;
	mark_buffer_dirty(bh, 1);
	if ((dot = iget(dir->i_sb,ino)) != NULL)
		vfat_read_inode(dot);
	if (!dot) return -EIO;
	dot->i_mtime = dot->i_atime = CURRENT_TIME;
	dot->i_dirt = 1;
	if (isdot) {
		dot->i_size = dir->i_size;
		MSDOS_I(dot)->i_start = MSDOS_I(dir)->i_start;
		dot->i_nlink = dir->i_nlink;
	} else {
		dot->i_size = parent->i_size;
		MSDOS_I(dot)->i_start = MSDOS_I(parent)->i_start;
		dot->i_nlink = parent->i_nlink;
	}

	iput(dot);

	PRINTK(("vfat_create_a_dotdir 2\n"));
	return 0;
}

static int vfat_create_dotdirs(struct inode *dir, struct inode *parent)
{
	struct super_block *sb = dir->i_sb;
	int res;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	loff_t offset;

	PRINTK(("vfat_create_dotdirs 1\n"));
	if ((res = fat_add_cluster(dir)) < 0) return res;

	PRINTK(("vfat_create_dotdirs 2\n"));
	offset = 0;
	bh = NULL;
	if ((res = fat_get_entry(dir,&offset,&bh,&de)) < 0) return res;
	
	PRINTK(("vfat_create_dotdirs 3\n"));
	res = vfat_create_a_dotdir(dir, parent, bh, de, res, MSDOS_DOT, 1);
	PRINTK(("vfat_create_dotdirs 4\n"));
	if (res < 0) {
		brelse(bh);
		return res;
	}
	PRINTK(("vfat_create_dotdirs 5\n"));

	if ((res = fat_get_entry(dir,&offset,&bh,&de)) < 0) {
		brelse(bh);
		return res;
	}
	PRINTK(("vfat_create_dotdirs 6\n"));

	res = vfat_create_a_dotdir(dir, parent, bh, de, res, MSDOS_DOTDOT, 0);
	PRINTK(("vfat_create_dotdirs 7\n"));
	brelse(bh);

	return res;
}

/***** See if directory is empty */
static int vfat_empty(struct inode *dir)
{
	struct super_block *sb = dir->i_sb;
	loff_t pos;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;

	if (dir->i_count > 1)
		return -EBUSY;
	if (MSDOS_I(dir)->i_start) { /* may be zero in mkdir */
		pos = 0;
		bh = NULL;
		while (fat_get_entry(dir,&pos,&bh,&de) > -1)
			/* Skip extended filename entries */
			if (de->attr == ATTR_EXT) continue;

			if (!IS_FREE(de->name) && strncmp(de->name,MSDOS_DOT,
			    MSDOS_NAME) && strncmp(de->name,MSDOS_DOTDOT,
			    MSDOS_NAME)) {
				brelse(bh);
				return -ENOTEMPTY;
			}
		if (bh)
			brelse(bh);
	}
	return 0;
}

static int vfat_rmdir_free_ino(struct inode *dir,struct buffer_head *bh,
     struct msdos_dir_entry *de,int ino)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	int res;

	if (ino < 0) return -EINVAL;
	if (!(inode = iget(dir->i_sb,ino))) return -ENOENT;
	if (!S_ISDIR(inode->i_mode)) {
		iput(inode);
		return -ENOTDIR;
	}
	if (dir->i_dev != inode->i_dev || dir == inode) {
		iput(inode);
		return -EBUSY;
	}
	res = vfat_empty(inode);
	if (res) {
		iput(inode);
		return res;
	}
	inode->i_nlink = 0;
	inode->i_mtime = dir->i_mtime = CURRENT_TIME;
	inode->i_atime = dir->i_atime = CURRENT_TIME;
	dir->i_nlink--;
	inode->i_dirt = dir->i_dirt = 1;
	de->name[0] = DELETED_FLAG;
	mark_buffer_dirty(bh, 1);
	iput(inode);

	return 0;
}

static int vfat_unlink_free_ino(struct inode *dir,struct buffer_head *bh,
     struct msdos_dir_entry *de,int ino,int nospc)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode;
	if (!(inode = iget(dir->i_sb,ino))) return -ENOENT;
	if (!S_ISREG(inode->i_mode) && nospc) {
		iput(inode);
		return -EPERM;
	}
	if (IS_IMMUTABLE(inode)){
		iput(inode);
		return -EPERM;
	}
	inode->i_nlink = 0;
	inode->i_mtime = dir->i_mtime = CURRENT_TIME;
	inode->i_atime = dir->i_atime = CURRENT_TIME;
	MSDOS_I(inode)->i_busy = 1;
	inode->i_dirt = dir->i_dirt = 1;
	de->name[0] = DELETED_FLAG;
	mark_buffer_dirty(bh, 1);

	iput(inode);
	return 0;
}

static int vfat_remove_entry(struct inode *dir,struct slot_info *sinfo,
     struct buffer_head **bh,struct msdos_dir_entry **de,
     int is_dir,int nospc)
{
	struct super_block *sb = dir->i_sb;
	loff_t offset;
	int res, i;

	/* remove the shortname */
	offset = sinfo->shortname_offset;
	res = fat_get_entry(dir, &offset, bh, de);
	if (res < 0) return res;
	if (is_dir) {
		res = vfat_rmdir_free_ino(dir,*bh,*de,res);
	} else {
		res = vfat_unlink_free_ino(dir,*bh,*de,res,nospc);
	}
	if (res < 0) return res;
		
	/* remove the longname */
	offset = sinfo->longname_offset;
	for (i = sinfo->long_slots; i > 0; --i) {
		res = fat_get_entry(dir, &offset, bh, de);
		if (res < 0) {
			printk("vfat_remove_entry: problem 1\n");
			continue;
		}
		(*de)->name[0] = DELETED_FLAG;
		(*de)->attr = 0;
		mark_buffer_dirty(*bh, 1);
	}
	return 0;
}


static int vfat_rmdirx(struct inode *dir,const char *name,int len)
{
	struct super_block *sb = dir->i_sb;
	int res;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct slot_info sinfo;

	bh = NULL;
	res = -EPERM;
	if (name[0] == '.' && (len == 1 || (len == 2 && name[1] == '.')))
		goto rmdir_done;
	res = vfat_find(dir,name,len,1,0,0,&sinfo);

	if (res >= 0 && sinfo.total_slots > 0) {
		res = vfat_remove_entry(dir,&sinfo,&bh,&de,1,0);
		if (res > 0) {
			res = 0;
		}
	} else {
		printk("Problem in vfat_rmdirx\n");
	}

rmdir_done:
	brelse(bh);
	return res;
}

/***** Remove a directory */
int vfat_rmdir(struct inode *dir,const char *name,int len)
{
	int res;

	res = vfat_rmdirx(dir, name, len);
	iput(dir);
	return res;
}

static int vfat_unlinkx(
	struct inode *dir,
	const char *name,
	int len,
	int nospc)	/* Flag special file ? */
{
	struct super_block *sb = dir->i_sb;
	int res;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct slot_info sinfo;

	bh = NULL;
	if ((res = vfat_find(dir,name,len,1,0,0,&sinfo)) < 0)
		goto unlink_done;

	if (res >= 0 && sinfo.total_slots > 0) {
		res = vfat_remove_entry(dir,&sinfo,&bh,&de,0,nospc);
		if (res > 0) {
			res = 0;
		}
	} else {
		printk("Problem in vfat_unlinkx: res=%d, total_slots=%d\n",res, sinfo.total_slots);
	}

unlink_done:
	brelse(bh);
	return res;
}


int vfat_mkdir(struct inode *dir,const char *name,int len,int mode)
{
	struct inode *inode;
	int res;

	fat_lock_creation();
	if ((res = vfat_create_entry(dir,name,len,1,&inode)) < 0) {
		fat_unlock_creation();
		iput(dir);
		return res;
	}

	dir->i_nlink++;
	inode->i_nlink = 2; /* no need to mark them dirty */
	MSDOS_I(inode)->i_busy = 1; /* prevent lookups */

	res = vfat_create_dotdirs(inode, dir);
	fat_unlock_creation();
	MSDOS_I(inode)->i_busy = 0;
	iput(inode);
	iput(dir);
	if (res < 0) {
		if (vfat_rmdir(dir,name,len) < 0)
			fat_fs_panic(dir->i_sb,"rmdir in mkdir failed");
	}
	return res;
}

/***** Unlink, as called for msdosfs */
int vfat_unlink(struct inode *dir,const char *name,int len)
{
	int res;

	res = vfat_unlinkx (dir,name,len,1);
	iput(dir);
	return res;
}


int vfat_rename(struct inode *old_dir,const char *old_name,int old_len,
	struct inode *new_dir,const char *new_name,int new_len)
{
	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *old_bh,*new_bh,*dotdot_bh;
	struct msdos_dir_entry *old_de,*new_de,*dotdot_de;
	loff_t old_offset,new_offset,old_longname_offset;
	int old_slots,old_ino,new_ino,dotdot_ino,ino;
	struct inode *old_inode, *new_inode, *dotdot_inode, *walk;
	int res, is_dir, i;
	int locked = 0;
	struct slot_info sinfo;

	PRINTK(("vfat_rename 1\n"));
	if (old_dir == new_dir && old_len == new_len &&
	    strncmp(old_name, new_name, old_len) == 0)
		return 0;

	old_bh = new_bh = NULL;
	old_inode = new_inode = NULL;
	res = vfat_find(old_dir,old_name,old_len,1,0,0,&sinfo);
	PRINTK(("vfat_rename 2\n"));
	if (res < 0) goto rename_done;

	old_slots = sinfo.total_slots;
	old_longname_offset = sinfo.longname_offset;
	old_offset = sinfo.shortname_offset;
	old_ino = sinfo.ino;
	res = fat_get_entry(old_dir, &old_offset, &old_bh, &old_de);
	PRINTK(("vfat_rename 3\n"));
	if (res < 0) goto rename_done;

	if (!(old_inode = iget(old_dir->i_sb,old_ino))) goto rename_done;
	is_dir = S_ISDIR(old_inode->i_mode);
	if (is_dir) {
		if ((old_dir->i_dev != new_dir->i_dev) ||
		    (old_ino == new_dir->i_ino)) {
			res = -EINVAL;
			goto rename_done;
		}
		if (!(walk = iget(new_dir->i_sb,new_dir->i_ino))) return -EIO;
		/* prevent moving directory below itself */
		while (walk->i_ino != MSDOS_ROOT_INO) {
			ino = fat_parent_ino(walk,1);
			iput(walk);
			if (ino < 0) return ino;
			if (ino == old_ino) return -EINVAL;
			if (!(walk = iget(new_dir->i_sb,ino))) return -EIO;
		}
		iput(walk);
	}

	res = vfat_find(new_dir,new_name,new_len,1,0,is_dir,&sinfo);

	PRINTK(("vfat_rename 4\n"));
	if (res > -1) {
		int new_is_dir;

		PRINTK(("vfat_rename 5\n"));
		/* Filename currently exists.  Need to delete it */
		new_offset = sinfo.shortname_offset;
		res = fat_get_entry(new_dir, &new_offset, &new_bh, &new_de);
		PRINTK(("vfat_rename 6\n"));
		if (res < 0) goto rename_done;

		if (!(new_inode = iget(new_dir->i_sb,res)))
			goto rename_done;
		new_is_dir = S_ISDIR(new_inode->i_mode);
		iput(new_inode);
		if (new_is_dir) {
			PRINTK(("vfat_rename 7\n"));
			res = vfat_rmdirx(new_dir,new_name,new_len);
			PRINTK(("vfat_rename 8\n"));
			if (res < 0) goto rename_done;
		} else {
			PRINTK(("vfat_rename 9\n"));
			res = vfat_unlinkx(new_dir,new_name,new_len,1);
			PRINTK(("vfat_rename 10\n"));
			if (res < 0) goto rename_done;
		}
	}

	PRINTK(("vfat_rename 11\n"));
	fat_lock_creation(); locked = 1;
	res = vfat_find(new_dir,new_name,new_len,1,1,is_dir,&sinfo);

	PRINTK(("vfat_rename 12\n"));
	if (res < 0) goto rename_done;

	new_offset = sinfo.shortname_offset;
	new_ino = sinfo.ino;
	res = fat_get_entry(new_dir, &new_offset, &new_bh, &new_de);
	PRINTK(("vfat_rename 13\n"));
	if (res < 0) goto rename_done;

	new_de->attr = old_de->attr;
	new_de->time = old_de->time;
	new_de->date = old_de->date;
	new_de->ctime_ms = old_de->ctime_ms;
	new_de->cdate = old_de->cdate;
	new_de->adate = old_de->adate;
	new_de->start = old_de->start;
	new_de->size = old_de->size;

	if (!(new_inode = iget(new_dir->i_sb,new_ino))) goto rename_done;
	PRINTK(("vfat_rename 14\n"));

	/* At this point, we have the inodes of the old file and the
	 * new file.  We need to transfer all information from the old
	 * inode to the new inode and then delete the slots of the old
	 * entry
	 */

	vfat_read_inode(new_inode);
	MSDOS_I(old_inode)->i_busy = 1;
	fat_cache_inval_inode(old_inode);
	PRINTK(("vfat_rename 15: old_slots=%d\n",old_slots));
	old_inode->i_dirt = 1;

	/* remove the old entry */
	for (i = old_slots; i > 0; --i) {
		res = fat_get_entry(old_dir, &old_longname_offset, &old_bh, &old_de);
		if (res < 0) {
			printk("vfat_unlinkx: problem 1\n");
			continue;
		}
		old_de->name[0] = DELETED_FLAG;
		old_de->attr = 0;
		mark_buffer_dirty(old_bh, 1);
	}
	PRINTK(("vfat_rename 15b\n"));

	mark_buffer_dirty(new_bh, 1);
	iput(new_inode);
	/* XXX: There is some code in the original MSDOS rename that
	 * is not duplicated here and it might cause a problem in
	 * certain circumstances.
	 */
	
	if (S_ISDIR(old_inode->i_mode)) {
		if ((res = fat_scan(old_inode,MSDOS_DOTDOT,&dotdot_bh,
		    &dotdot_de,&dotdot_ino,SCAN_ANY)) < 0) goto rename_done;
		if (!(dotdot_inode = iget(old_inode->i_sb,dotdot_ino))) {
			brelse(dotdot_bh);
			res = -EIO;
			goto rename_done;
		}
		dotdot_de->start = MSDOS_I(dotdot_inode)->i_start =
		    MSDOS_I(new_dir)->i_start;
		dotdot_inode->i_dirt = 1;
		mark_buffer_dirty(dotdot_bh, 1);
		old_dir->i_nlink--;
		new_dir->i_nlink++;
		/* no need to mark them dirty */
		dotdot_inode->i_nlink = new_dir->i_nlink;
		iput(dotdot_inode);
		brelse(dotdot_bh);
	}

	if (res > 0) res = 0;

rename_done:
	if (locked) fat_unlock_creation();
	if (old_bh) brelse(old_bh);
	if (new_bh) brelse(new_bh);
	if (old_inode) iput(old_inode);
	iput(old_dir);
	iput(new_dir);
	return res;
}



/* Public inode operations for the VFAT fs */
struct inode_operations vfat_dir_inode_operations = {
	&fat_dir_operations,	/* default directory file-ops */
	vfat_create,		/* create */
	vfat_lookup,		/* lookup */
	NULL,			/* link */
	vfat_unlink,		/* unlink */
	NULL,			/* symlink */
	vfat_mkdir,		/* mkdir */
	vfat_rmdir,		/* rmdir */
	NULL,			/* mknod */
	vfat_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	fat_bmap,		/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};


void vfat_read_inode(struct inode *inode)
{
	fat_read_inode(inode, &vfat_dir_inode_operations);
}




static struct file_system_type vfat_fs_type = {
	vfat_read_super, "vfat", 1, NULL
};

int init_vfat_fs(void)
{
	int status;

	if ((status = register_filesystem(&vfat_fs_type)) == 0)
		status = register_symtab(0);
	return status;
}

#ifdef MODULE
int init_module(void)
{
	return init_vfat_fs();
}


void cleanup_module(void)
{
	unregister_filesystem(&vfat_fs_type);
}

#endif /* ifdef MODULE */



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
