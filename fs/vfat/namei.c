/*
 *  linux/fs/vfat/namei.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Windows95/Windows NT compatible extended MSDOS filesystem
 *    by Gordon Chaffee Copyright (C) 1995.  Send bug reports for the
 *    VFAT filesystem to <chaffee@cs.berkeley.edu>.  Specify
 *    what file operation caused you trouble and if you can duplicate
 *    the problem, send a script that demonstrates it.
 */

#define __NO_VERSION__
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/msdos_fs.h>
#include <linux/nls.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/ctype.h>
#include <linux/stat.h>
#include <linux/mm.h>
#include <linux/malloc.h>

#include "../fat/msbuffer.h"

#define DEBUG_LEVEL 0
#if (DEBUG_LEVEL >= 1)
#  define PRINTK1(x) printk x
#else
#  define PRINTK1(x)
#endif
#if (DEBUG_LEVEL >= 2)
#  define PRINTK2(x) printk x
#else
#  define PRINTK2(x)
#endif
#if (DEBUG_LEVEL >= 3)
#  define PRINTK3(x) printk x
#else
#  define PRINTK3(x)
#endif

#ifndef DEBUG
# define CHECK_STACK
#else
# define CHECK_STACK check_stack(__FILE__, __LINE__)
#endif

struct vfat_find_info {
	const char *name;
	int len;
	int new_filename;
	int found;
	int is_long;
	off_t offset;
	off_t short_offset;
	int long_slots;
	ino_t ino;
	int posix;
	int anycase;
};

void vfat_read_inode(struct inode *inode);
static int vfat_valid_shortname(const char *,int, int, int);
static int vfat_format_name(const char *, int, char *, int, int);
static int vfat_valid_longname(const char *, int, int, int);
static int vfat_hashi(struct dentry *parent, struct qstr *qstr);
static int vfat_hash(struct dentry *parent, struct qstr *qstr);
static int vfat_cmpi(struct dentry *dentry, struct qstr *a, struct qstr *b);
static int vfat_cmp(struct dentry *dentry, struct qstr *a, struct qstr *b);
static int vfat_revalidate(struct dentry *dentry);

static struct dentry_operations vfat_dentry_ops[4] = {
	{
		NULL,			/* d_revalidate */
		vfat_hashi,
		vfat_cmpi,
		NULL			/* d_delete */
	},
	{
		vfat_revalidate,
		vfat_hashi,
		vfat_cmpi,
		NULL			/* d_delete */
	},
	{
		NULL, 			/* d_revalidate */
		vfat_hash,
		vfat_cmp,
		NULL			/* d_delete */
	},
	{
		vfat_revalidate,
		vfat_hash,
		vfat_cmp,
		NULL			/* d_delete */
	}
};

void vfat_put_super(struct super_block *sb)
{
	fat_put_super(sb);
	MOD_DEC_USE_COUNT;
}

static int vfat_revalidate(struct dentry *dentry)
{
	PRINTK1(("vfat_revalidate: %s\n", dentry->d_name.name));
	if (dentry->d_time == dentry->d_parent->d_inode->i_version) {
		return 1;
	}
	return 0;
}

static struct super_operations vfat_sops = { 
	vfat_read_inode,
	fat_write_inode,
	fat_put_inode,
	fat_delete_inode,
	fat_notify_change,
	vfat_put_super,
	NULL, /* write_super */
	fat_statfs,
	NULL  /* remount */
};

static int simple_getbool(char *s, int *setval)
{
	if (s) {
		if (!strcmp(s,"1") || !strcmp(s,"yes") || !strcmp(s,"true")) {
			*setval = 1;
		} else if (!strcmp(s,"0") || !strcmp(s,"no") || !strcmp(s,"false")) {
			*setval = 0;
		} else {
			return 0;
		}
	} else {
		*setval = 1;
	}
	return 1;
}

static int parse_options(char *options,	struct fat_mount_options *opts)
{
	char *this_char,*value,save,*savep;
	int ret, val;

	opts->unicode_xlate = opts->posixfs = 0;
	opts->numtail = 1;
	opts->utf8 = 0;

	if (!options) return 1;
	save = 0;
	savep = NULL;
	ret = 1;
	for (this_char = strtok(options,","); this_char; this_char = strtok(NULL,",")) {
		if ((value = strchr(this_char,'=')) != NULL) {
			save = *value;
			savep = value;
			*value++ = 0;
		}
		if (!strcmp(this_char,"utf8")) {
			ret = simple_getbool(value, &val);
			if (ret) opts->utf8 = val;
		} else if (!strcmp(this_char,"uni_xlate")) {
			ret = simple_getbool(value, &val);
			if (ret) opts->unicode_xlate = val;
		} else if (!strcmp(this_char,"posix")) {
			ret = simple_getbool(value, &val);
			if (ret) opts->posixfs = val;
		} else if (!strcmp(this_char,"nonumtail")) {
			ret = simple_getbool(value, &val);
			if (ret) {
				opts->numtail = !val;
			}
		}
		if (this_char != options)
			*(this_char-1) = ',';
		if (value) {
			*savep = save;
		}
		if (ret == 0) {
			return 0;
		}
	}
	if (opts->unicode_xlate) {
		opts->utf8 = 0;
	}
	return 1;
}

/*
 * Compute the hash for the vfat name corresponding to the dentry.
 * Note: if the name is invalid, we leave the hash code unchanged so
 * that the existing dentry can be used. The vfat fs routines will
 * return ENOENT or EINVAL as appropriate.
 */
static int vfat_hash(struct dentry *dentry, struct qstr *qstr)
{
	const char *name;
	int len;

	len = qstr->len;
	name = qstr->name;
	while (len && name[len-1] == '.')
		len--;

	qstr->hash = full_name_hash(name, len);

	return 0;
}

/*
 * Compute the hash for the vfat name corresponding to the dentry.
 * Note: if the name is invalid, we leave the hash code unchanged so
 * that the existing dentry can be used. The vfat fs routines will
 * return ENOENT or EINVAL as appropriate.
 */
static int vfat_hashi(struct dentry *dentry, struct qstr *qstr)
{
	const char *name;
	int len;
	char c;
	unsigned long hash;

	len = qstr->len;
	name = qstr->name;
	while (len && name[len-1] == '.')
		len--;

	hash = init_name_hash();
	while (len--) {
		c = tolower(*name++);
		hash = partial_name_hash(tolower(c), hash);
	}
	qstr->hash = end_name_hash(hash);

	return 0;
}

/*
 * Case insensitive compare of two vfat names.
 */
static int vfat_cmpi(struct dentry *dentry, struct qstr *a, struct qstr *b)
{
	int alen, blen;

	/* A filename cannot end in '.' or we treat it like it has none */
	alen = a->len;
	blen = b->len;
	while (alen && a->name[alen-1] == '.')
		alen--;
	while (blen && b->name[blen-1] == '.')
		blen--;
	if (alen == blen) {
		if (strnicmp(a->name, b->name, alen) == 0)
			return 0;
	}
	return 1;
}

/*
 * Case sensitive compare of two vfat names.
 */
static int vfat_cmp(struct dentry *dentry, struct qstr *a, struct qstr *b)
{
	int alen, blen;

	/* A filename cannot end in '.' or we treat it like it has none */
	alen = a->len;
	blen = b->len;
	while (alen && a->name[alen-1] == '.')
		alen--;
	while (blen && b->name[blen-1] == '.')
		blen--;
	if (alen == blen) {
		if (strncmp(a->name, b->name, alen) == 0)
			return 0;
	}
	return 1;
}

struct super_block *vfat_read_super(struct super_block *sb,void *data,
				    int silent)
{
	struct super_block *res;
  
	MOD_INC_USE_COUNT;
	
	MSDOS_SB(sb)->options.isvfat = 1;

	sb->s_op = &vfat_sops;
	res = fat_read_super(sb, data, silent);
	if (res == NULL) {
		sb->s_dev = 0;
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	if (!parse_options((char *) data, &(MSDOS_SB(sb)->options))) {
		MOD_DEC_USE_COUNT;
	} else {
		MSDOS_SB(sb)->options.dotsOK = 0;
		if (MSDOS_SB(sb)->options.posixfs) {
			MSDOS_SB(sb)->options.name_check = 's';
		}
		if (MSDOS_SB(sb)->options.name_check != 's') {
			sb->s_root->d_op = &vfat_dentry_ops[0];
		} else {
			sb->s_root->d_op = &vfat_dentry_ops[2];
		}
	}

	return res;
}

#ifdef DEBUG

static void
check_stack(const char *fname, int lineno)
{
	int stack_level;
	char *pg_dir;

	stack_level = (long)(&pg_dir)-current->kernel_stack_page;
	if (stack_level < 0)
	        printk("*-*-*-* vfat kstack overflow in %s line %d: SL=%d\n",
		       fname, lineno, stack_level);
	else if (stack_level < 500)
	        printk("*-*-*-* vfat kstack low in %s line %d: SL=%d\n",
		       fname, lineno, stack_level);
#if 0
	else
		printk("------- vfat kstack ok in %s line %d: SL=%d\n",
		       fname, lineno, stack_level);
#endif
#if 0
	if (*(unsigned long *) current->kernel_stack_page != STACK_MAGIC) {
		printk("******* vfat stack corruption detected in %s at line %d\n",
		       fname, lineno);
	}
#endif
}

static int debug = 0;
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

static const char *reserved3_names[] = {
	"con     ", "prn     ", "nul     ", "aux     ", NULL
};

static const char *reserved4_names[] = {
	"com1    ", "com2    ", "com3    ", "com4    ", "com5    ",
	"com6    ", "com7    ", "com8    ", "com9    ",
	"lpt1    ", "lpt2    ", "lpt3    ", "lpt4    ", "lpt5    ",
	"lpt6    ", "lpt7    ", "lpt8    ", "lpt9    ",
	NULL };


/* Characters that are undesirable in an MS-DOS file name */

static char bad_chars[] = "*?<>|\":/\\";
static char replace_chars[] = "[];,+=";

static int vfat_find(struct inode *dir,struct qstr* name,
		      int new_filename,int is_dir,
		      struct vfat_slot_info *sinfo_out);

/* Checks the validity of a long MS-DOS filename */
/* Returns negative number on error, 0 for a normal
 * return, and 1 for . or .. */

static int vfat_valid_longname(const char *name, int len, int dot_dirs,
			       int xlate)
{
	const char **reserved, *walk;
	unsigned char c;
	int i, baselen;

	if (IS_FREE(name)) return -EINVAL;
	if (name[0] == '.' && (len == 1 || (len == 2 && name[1] == '.'))) {
		if (!dot_dirs) return -EEXIST;
		return 1;
	}

	if (len && name[len-1] == ' ') return -EINVAL;
	if (len >= 256) return -EINVAL;
	for (i = 0; i < len; i++) {
		c = name[i];
		if (xlate && c == ':') continue;
		if (strchr(bad_chars,c)) {
			return -EINVAL;
		}
	}
 	if (len < 3) return 0;

	for (walk = name; *walk != 0 && *walk != '.'; walk++);
	baselen = walk - name;

	if (baselen == 3) {
		for (reserved = reserved3_names; *reserved; reserved++) {
			if (!strnicmp(name,*reserved,baselen))
				return -EINVAL;
		}
	} else if (baselen == 4) {
		for (reserved = reserved4_names; *reserved; reserved++) {
			if (!strnicmp(name,*reserved,baselen))
				return -EINVAL;
		}
	}
	return 0;
}

static int vfat_valid_shortname(const char *name,int len,
				int dot_dirs, int utf8)
{
	const char *walk;
	unsigned char c;
	int space;
	int baselen;

	if (name[0] == '.' && (len == 1 || (len == 2 && name[1] == '.'))) {
		if (!dot_dirs) return -EEXIST;
		return 1;
	}

	space = 1; /* disallow names starting with a dot */
	c = 0;
	for (walk = name; len && walk-name < 8;) {
	    	c = *walk++;
		len--;
		if (utf8 && (c & 0x80)) return -EINVAL;
		if (strchr(replace_chars,c)) return -EINVAL;
  		if (c >= 'A' && c <= 'Z') return -EINVAL;
		if (c < ' '|| c==':') return -EINVAL;
		if (c == '.') break;
		space = c == ' ';
	}
	if (space) return -EINVAL;
	if (len && c != '.') {
		c = *walk++;
		len--;
		if (c != '.') return -EINVAL;
	}
	baselen = walk - name;
	if (c == '.') {
		baselen--;
		if (len >= 4) return -EINVAL;
		while (len > 0) {
			c = *walk++;
			len--;
			if (utf8 && (c & 0x80)) return -EINVAL;
			if (strchr(replace_chars,c))
				return -EINVAL;
			if (c < ' ' || c == '.'|| c==':')
				return -EINVAL;
			if (c >= 'A' && c <= 'Z') return -EINVAL;
			space = c == ' ';
		}
		if (space) return -EINVAL;
	}

	return 0;
}

static int vfat_find_form(struct inode *dir,char *name)
{
	struct msdos_dir_entry *de;
	struct buffer_head *bh = NULL;
	loff_t pos = 0;

	while(fat_get_entry(dir, &pos, &bh, &de) >= 0) {
		if (de->attr == ATTR_EXT)
			continue;
		if (memcmp(de->name,name,MSDOS_NAME))
			continue;
		brelse(bh);
		return 0;
	}
	brelse(bh);
	return -ENOENT;
}

static int vfat_format_name(const char *name,int len,char *res,
			    int dot_dirs,int utf8)
{
	char *walk;
	unsigned char c;
	int space;

	if (name[0] == '.' && (len == 1 || (len == 2 && name[1] == '.'))) {
		if (!dot_dirs) return -EEXIST;
		memset(res+1,' ',10);
		while (len--) *res++ = '.';
		return 0;
	}

	space = 1; /* disallow names starting with a dot */
	for (walk = res; len-- && (c=*name++)!='.' ; walk++) {
		if (walk-res == 8) return -EINVAL;
		if (utf8 && (c & 0x80)) return -EINVAL;
		if (strchr(replace_chars,c)) return -EINVAL;
		if (c >= 'A' && c <= 'Z') return -EINVAL;
		if (c < ' '|| c==':') return -EINVAL;
		space = c == ' ';
		*walk = c >= 'a' && c <= 'z' ? c-32 : c;
	}
	if (space) return -EINVAL;
	if (len >= 0) {
		while (walk-res < 8) *walk++ = ' ';
		while (len > 0 && walk-res < MSDOS_NAME) {
			c = *name++;
			len--;
			if (utf8 && (c & 0x80)) return -EINVAL;
			if (strchr(replace_chars,c))
				return -EINVAL;
			if (c < ' ' || c == '.'|| c==':')
				return -EINVAL;
			if (c >= 'A' && c <= 'Z') return -EINVAL;
			space = c == ' ';
			*walk++ = c >= 'a' && c <= 'z' ? c-32 : c;
		}
		if (space) return -EINVAL;
		if (len) return -EINVAL;
	}
	while (walk-res < MSDOS_NAME) *walk++ = ' ';

	return 0;
}

static char skip_chars[] = ".:\"?<>| ";

/* Given a valid longname, create a unique shortname.  Make sure the
 * shortname does not exist
 */
static int vfat_create_shortname(struct inode *dir, const char *name,
     int len, char *name_res, int utf8)
{
	const char *ip, *ext_start, *end;
	char *p;
	int sz, extlen, baselen;
	char msdos_name[13];
	char base[9], ext[4];
	int i;
	char buf[8];
	const char *name_start;

	PRINTK2(("Entering vfat_create_shortname: name=%s, len=%d\n", name, len));
	sz = 0;			/* Make compiler happy */
	if (len && name[len-1]==' ') return -EINVAL;
	if (len <= 12) {
		/* Do a case insensitive search if the name would be a valid
		 * shortname if is were all capitalized.  However, do not
		 * allow spaces in short names because Win95 scandisk does
		 * not like that */
		for (i = 0, p = msdos_name, ip = name; ; i++, p++, ip++) {
			if (i == len) {
				if (vfat_format_name(msdos_name,
						len, name_res, 1, utf8) < 0)
					break;
				PRINTK3(("vfat_create_shortname 1\n"));
				if (vfat_find_form(dir, name_res) < 0)
					return 0;
				return -EEXIST;
			}

			if (*ip == ' ')
				break;
			if (*ip >= 'A' && *ip <= 'Z') {
				*p = *ip + 32;
			} else {
				*p = *ip;
			}
		}
	}

	PRINTK3(("vfat_create_shortname 3\n"));
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
		if (utf8 && (*ip & 0x80)) {
			*p++ = '_';
			baselen++;
		} else if (!strchr(skip_chars, *ip)) {
			if (*ip >= 'a' && *ip <= 'z') {
				*p = *ip - 32;
			} else {
				*p = *ip;
			}
			if (strchr(replace_chars, *p)) *p='_';
			p++; baselen++;
		}
		ip++;
	}
	if (baselen == 0) {
		return -EINVAL;
	}
		
	extlen = 0;
	if (ext_start) {
		for (p = ext, ip = ext_start; extlen < 3 && ip < end; ip++) {
			if (utf8 && (*ip & 0x80)) {
				*p++ = '_';
				extlen++;
			} else if (!strchr(skip_chars, *ip)) {
				if (*ip >= 'a' && *ip <= 'z') {
					*p = *ip - 32;
				} else {
					*p = *ip;
				}
				if (strchr(replace_chars, *p)) *p='_';
				extlen++;
				p++;
			}
		}
	}
	ext[extlen] = '\0';
	base[baselen] = '\0';

	/* Yes, it can happen. ".\xe5" would do it. */
	if (IS_FREE(base))
		base[0]='_';

	/* OK, at this point we know that base is not longer than 8 symbols,
	 * ext is not longer than 3, base is nonempty, both don't contain
	 * any bad symbols (lowercase transformed to uppercase).
	 */

	memset(name_res, ' ', MSDOS_NAME);
	memcpy(name_res,base,baselen);
	memcpy(name_res+8,ext,extlen);
	if (MSDOS_SB(dir->i_sb)->options.numtail == 0)
		if (vfat_find_form(dir, name_res) < 0)
			return 0;

	/*
	 * Try to find a unique extension.  This used to
	 * iterate through all possibilities sequentially,
	 * but that gave extremely bad performance.  Windows
	 * only tries a few cases before using random
	 * values for part of the base.
	 */

	if (baselen>6)
		baselen = 6;
	name_res[baselen] = '~';
	for (i = 1; i < 10; i++) {
		name_res[baselen+1] = i + '0';
		if (vfat_find_form(dir, name_res) < 0)
			return 0;
	}

	i = jiffies & 0xffff;
	sz = (jiffies >> 16) & 0x7;
	if (baselen>2)
		baselen = 2;
	name_res[baselen+4] = '~';
	name_res[baselen+5] = '1' + sz;
	while (1) {
		sprintf(buf, "%04X", i);
		memcpy(&name_res[baselen], buf, 4);
		if (vfat_find_form(dir, name_res) < 0)
			break;
		i -= 11;
	}
	return 0;
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

	PRINTK2(("vfat_find_free_slots: find %d free slots\n", slots));
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
					/* PRINTK3(("inode %d still busy\n", ino)); */
					iput(inode);
				}
			}
			if (done) {
				row++;
				if (row == slots) {
					fat_brelse(sb, bh);
					/* printk("----- Free offset at %d\n", offset); */
					return offset;
				}
			} else {
				row = 0;
				offset = curr;
			}
			ino = fat_get_entry(dir,&curr,&bh,&de);
		}

		if ((dir->i_ino == MSDOS_ROOT_INO) &&
		    (MSDOS_SB(sb)->fat_bits != 32)) 
			return -ENOSPC;
		if ((res = fat_add_cluster(dir)) < 0) return res;
		ino = fat_get_entry(dir,&curr,&bh,&de);
	}
	return -ENOSPC;
}

/* Translate a string, including coded sequences into Unicode */
static int
xlate_to_uni(const char *name, int len, char *outname, int *outlen,
	     int escape, int utf8, struct nls_table *nls)
{
	int i;
	const unsigned char *ip;
	char *op;
	int fill;
	unsigned char c1, c2, c3;

	if (utf8) {
		*outlen = utf8_mbstowcs((__u16 *) outname, name, PAGE_SIZE);
		if (name[len-1] == '.')
			*outlen-=2;
		op = &outname[*outlen * sizeof(__u16)];
	} else {
		if (name[len-1] == '.') 
			len--;
		op = outname;
		if (nls) {
			for (i = 0, ip = name, op = outname, *outlen = 0;
			     i < len && *outlen <= 260; i++, *outlen += 1)
			{
				if (escape && (*ip == ':')) {
					if (i > len - 4) return -EINVAL;
					c1 = fat_esc2uni[ip[1]];
					c2 = fat_esc2uni[ip[2]];
					c3 = fat_esc2uni[ip[3]];
					if (c1 == 255 || c2 == 255 || c3 == 255)
						return -EINVAL;
					*op++ = (c1 << 4) + (c2 >> 2);
					*op++ = ((c2 & 0x3) << 6) + c3;
					ip += 4;
				} else {
					*op++ = nls->charset2uni[*ip].uni1;
					*op++ = nls->charset2uni[*ip].uni2;
					ip++;
				}
			}
		} else {
			for (i = 0, ip = name, op = outname, *outlen = 0;
			     i < len && *outlen <= 260; i++, *outlen += 1)
			{
				*op++ = *ip++;
				*op++ = 0;
			}
		}
	}
	if (*outlen > 260)
		return -ENAMETOOLONG;

	if (*outlen % 13) {
		*op++ = 0;
		*op++ = 0;
		*outlen += 1;
		if (*outlen % 13) {
			fill = 13 - (*outlen % 13);
			for (i = 0; i < fill; i++) {
				*op++ = 0xff;
				*op++ = 0xff;
			}
			*outlen += fill;
		}
	}

	return 0;
}

static int
vfat_fill_long_slots(struct msdos_dir_slot *ds, const char *name, int len,
		     char *msdos_name, int *slots,
		     int uni_xlate, int utf8, struct nls_table *nls)
{
	struct msdos_dir_slot *ps;
	struct msdos_dir_entry *de;
	int res;
	int slot;
	unsigned char cksum;
	char *uniname;
	const char *ip;
	unsigned long page;
	int unilen;
	int i;
	loff_t offset;

	if (name[len-1] == '.') len--;
	if(!(page = __get_free_page(GFP_KERNEL)))
		return -ENOMEM;
	uniname = (char *) page;
	res = xlate_to_uni(name, len, uniname, &unilen, uni_xlate, utf8, nls);
	if (res < 0) {
		free_page(page);
		return res;
	}

	*slots = unilen / 13;
	for (cksum = i = 0; i < 11; i++) {
		cksum = (((cksum&1)<<7)|((cksum&0xfe)>>1)) + msdos_name[i];
	}
	PRINTK3(("vfat_fill_long_slots 3: slots=%d\n",*slots));

	for (ps = ds, slot = *slots; slot > 0; slot--, ps++) {
		int end, j;

		PRINTK3(("vfat_fill_long_slots 4\n"));
		ps->id = slot;
		ps->attr = ATTR_EXT;
		ps->reserved = 0;
		ps->alias_checksum = cksum;
		ps->start = 0;
		PRINTK3(("vfat_fill_long_slots 5: uniname=%s\n",uniname));
		offset = (slot - 1) * 26;
		ip = &uniname[offset];
		j = offset;
		end = 0;
		for (i = 0; i < 10; i += 2) {
			ps->name0_4[i] = *ip++;
			ps->name0_4[i+1] = *ip++;
		}
		PRINTK3(("vfat_fill_long_slots 6\n"));
		for (i = 0; i < 12; i += 2) {
			ps->name5_10[i] = *ip++;
			ps->name5_10[i+1] = *ip++;
		}
		PRINTK3(("vfat_fill_long_slots 7\n"));
		for (i = 0; i < 4; i += 2) {
			ps->name11_12[i] = *ip++;
			ps->name11_12[i+1] = *ip++;
		}
	}
	PRINTK3(("vfat_fill_long_slots 8\n"));
	ds[0].id |= 0x40;

	de = (struct msdos_dir_entry *) ps;
	PRINTK3(("vfat_fill_long_slots 9\n"));
	strncpy(de->name, msdos_name, MSDOS_NAME);

	free_page(page);
	return 0;
}
		
static int vfat_build_slots(struct inode *dir,const char *name,int len,
     struct msdos_dir_slot *ds, int *slots, int *is_long)
{
	struct msdos_dir_entry *de;
	char msdos_name[MSDOS_NAME];
	int res, xlate, utf8;
	struct nls_table *nls;

	PRINTK2(("Entering vfat_build_slots: name=%s, len=%d\n", name, len));
	de = (struct msdos_dir_entry *) ds;
	xlate = MSDOS_SB(dir->i_sb)->options.unicode_xlate;
	utf8 = MSDOS_SB(dir->i_sb)->options.utf8;
	nls = MSDOS_SB(dir->i_sb)->nls_io;

	*slots = 1;
	*is_long = 0;
	if (len == 1 && name[0] == '.') {
		strncpy(de->name, MSDOS_DOT, MSDOS_NAME);
	} else if (len == 2 && name[0] == '.' && name[1] == '.') {
		strncpy(de->name, MSDOS_DOT, MSDOS_NAME);
	} else {
		PRINTK3(("vfat_build_slots 4\n"));
		res = vfat_valid_longname(name, len, 1, xlate);
		if (res < 0) {
			return res;
		}
		res = vfat_valid_shortname(name, len, 1, utf8);
		if (res > -1) {
			PRINTK3(("vfat_build_slots 5a\n"));
			res = vfat_format_name(name, len, de->name, 1, utf8);
			PRINTK3(("vfat_build_slots 5b\n"));
		} else {
			res = vfat_create_shortname(dir, name, len, msdos_name, utf8);
			if (res < 0) {
				return res;
			}

			*is_long = 1;

			return vfat_fill_long_slots(ds, name, len, msdos_name,
						    slots, xlate, utf8, nls);
		}
	}
	return 0;
}

static int vfat_readdir_cb(
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
	struct vfat_find_info *vf = (struct vfat_find_info *) buf;
	const char *s1, *s2;
	int i;

#ifdef DEBUG
	if (debug) printk("cb: vf.name=%s, len=%d, name=%s, name_len=%d\n",
			  vf->name, vf->len, name, name_len);
#endif

	if (vf->len != name_len) {
		return 0;
	}

	s1 = name; s2 = vf->name;
	for (i = 0; i < name_len; i++) {
		if (vf->anycase || (vf->new_filename && !vf->posix)) {
			if (tolower(*s1) != tolower(*s2))
				return 0;
		} else {
			if (*s1 != *s2)
				return 0;
		}
		s1++; s2++;
	}
	vf->found = 1;
	vf->is_long = is_long;
	vf->offset = (offset == 2) ? 0 : offset;
	vf->short_offset = (short_offset == 2) ? 0 : short_offset;
	vf->long_slots = long_slots;
	vf->ino = ino;
	return -1;
}

static int vfat_find(struct inode *dir,struct qstr* qname,
    int new_filename,int is_dir,struct vfat_slot_info *sinfo_out)
{
	struct super_block *sb = dir->i_sb;
	struct vfat_find_info vf;
	struct file fil;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct msdos_dir_slot *ps;
	loff_t offset;
	struct msdos_dir_slot *ds;
	int is_long;
	int slots, slot;
	int res;

	PRINTK2(("Entering vfat_find\n"));

	ds = (struct msdos_dir_slot *)
	    kmalloc(sizeof(struct msdos_dir_slot)*MSDOS_SLOTS, GFP_KERNEL);
	if (ds == NULL) return -ENOMEM;

	fil.f_pos = 0;
	vf.name = qname->name;
	vf.len = qname->len;
	while (vf.len && vf.name[vf.len-1] == '.') {
		vf.len--;
	}
	vf.new_filename = new_filename;
	vf.found = 0;
	vf.posix = MSDOS_SB(sb)->options.posixfs;
	vf.anycase = (MSDOS_SB(sb)->options.name_check != 's');
	res = fat_readdirx(dir,&fil,(void *)&vf,vfat_readdir_cb,NULL,1,1,0);
	PRINTK3(("vfat_find: Debug 1\n"));
	if (res < 0) goto cleanup;
	if (vf.found) {
		if (new_filename) {
			res = -EEXIST;
			goto cleanup;
		}
		sinfo_out->longname_offset = vf.offset;
		sinfo_out->shortname_offset = vf.short_offset;
		sinfo_out->is_long = vf.is_long;
		sinfo_out->long_slots = vf.long_slots;
		sinfo_out->total_slots = vf.long_slots + 1;
		sinfo_out->ino = vf.ino;

		PRINTK3(("vfat_find: Debug 2\n"));
		res = 0;
		goto cleanup;
	}

	PRINTK3(("vfat_find: Debug 3\n"));
	if (!new_filename) {
		res = -ENOENT;
		goto cleanup;
	}
	
	res = vfat_build_slots(dir, qname->name, vf.len, ds, 
			       &slots, &is_long);
	/* Here we either have is_long and slots>=0 or slots==1 */
	if (res < 0) goto cleanup;

	de = (struct msdos_dir_entry *) ds;

	bh = NULL;

	PRINTK3(("vfat_find: create file 1\n"));
	if (is_long) slots++;
	offset = vfat_find_free_slots(dir, slots);
	if (offset < 0) {
		res = offset;
		goto cleanup;
	}

	PRINTK3(("vfat_find: create file 2\n"));
	/* Now create the new entry */
	bh = NULL;
	for (slot = 0, ps = ds; slot < slots; slot++, ps++) {
		PRINTK3(("vfat_find: create file 3, slot=%d\n",slot));
		sinfo_out->ino = fat_get_entry(dir,&offset,&bh,&de);
		if (sinfo_out->ino < 0) {
			PRINTK3(("vfat_find: problem\n"));
			res = sinfo_out->ino;
			goto cleanup;
		}
		memcpy(de, ps, sizeof(struct msdos_dir_slot));
		fat_mark_buffer_dirty(sb, bh, 1);
	}

	PRINTK3(("vfat_find: create file 4\n"));
	dir->i_ctime = dir->i_mtime = dir->i_atime = CURRENT_TIME;
	mark_inode_dirty(dir);

	PRINTK3(("vfat_find: create file 5\n"));

	fat_date_unix2dos(dir->i_mtime,&de->time,&de->date);
	de->ctime_ms = 0;
	de->ctime = de->time;
	de->adate = de->cdate = de->date;
	de->start = 0;
	de->starthi = 0;
	de->size = 0;
	de->attr = is_dir ? ATTR_DIR : ATTR_ARCH;
	de->lcase = CASE_LOWER_BASE | CASE_LOWER_EXT;


	fat_mark_buffer_dirty(sb, bh, 1);
	fat_brelse(sb, bh);

	/* slots can't be less than 1 */
	sinfo_out->is_long = (slots > 1);
	sinfo_out->long_slots = slots - 1;
	sinfo_out->total_slots = slots;
	sinfo_out->shortname_offset = offset - sizeof(struct msdos_dir_slot);
	sinfo_out->longname_offset = offset - sizeof(struct msdos_dir_slot) * slots;
	res = 0;

cleanup:
	kfree(ds);
	return res;
}

int vfat_lookup(struct inode *dir,struct dentry *dentry)
{
	int res;
	struct vfat_slot_info sinfo;
	struct inode *result;
	int table;
	
	PRINTK2(("vfat_lookup: name=%s, len=%d\n", 
		 dentry->d_name.name, dentry->d_name.len));

	table = (MSDOS_SB(dir->i_sb)->options.name_check == 's') ? 2 : 0;
	dentry->d_op = &vfat_dentry_ops[table];

	result = NULL;
	if ((res = vfat_find(dir,&dentry->d_name,0,0,&sinfo)) < 0) {
		result = NULL;
		table++;
		goto error;
	}
	PRINTK3(("vfat_lookup 4.5\n"));
	if (!(result = iget(dir->i_sb,sinfo.ino)))
		return -EACCES;
	PRINTK3(("vfat_lookup 5\n"));
	if (MSDOS_I(result)->i_busy) { /* mkdir in progress */
		iput(result);
		result = NULL;
		table++;
		goto error;
	}
	PRINTK3(("vfat_lookup 6\n"));
error:
	dentry->d_op = &vfat_dentry_ops[table];
	dentry->d_time = dentry->d_parent->d_inode->i_version;
	d_add(dentry,result);
	return 0;
}


static int vfat_create_entry(struct inode *dir,struct qstr* qname,
    int is_dir, struct inode **result)
{
	struct super_block *sb = dir->i_sb;
	int res,ino;
	loff_t offset;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct vfat_slot_info sinfo;

	*result=0;
	PRINTK1(("vfat_create_entry: Entering\n"));
	res = vfat_find(dir, qname, 1, is_dir, &sinfo);
	if (res < 0) {
		return res;
	}

	offset = sinfo.shortname_offset;

	PRINTK3(("vfat_create_entry 2\n"));
	bh = NULL;
	ino = fat_get_entry(dir, &offset, &bh, &de);
	if (ino < 0) {
		PRINTK3(("vfat_mkdir problem\n"));
		if (bh)
			fat_brelse(sb, bh);
		return ino;
	}
	PRINTK3(("vfat_create_entry 3\n"));

	if ((*result = iget(dir->i_sb,ino)) != NULL)
		vfat_read_inode(*result);
	fat_brelse(sb, bh);
	if (!*result)
		return -EIO;
	(*result)->i_mtime = (*result)->i_atime = (*result)->i_ctime =
	    CURRENT_TIME;
	mark_inode_dirty(*result);
	(*result)->i_version = ++event;
	dir->i_version = event;

	return 0;
}

int vfat_create(struct inode *dir,struct dentry* dentry,int mode)
{
	int res;
	struct inode *result;

	result=NULL;
	fat_lock_creation();
	res = vfat_create_entry(dir,&dentry->d_name,0,&result);
	fat_unlock_creation();
	if (res < 0) {
		PRINTK3(("vfat_create: unable to get new entry\n"));
	} else {
		dentry->d_time = dentry->d_parent->d_inode->i_version;
		d_instantiate(dentry,result);
	}
	return res;
}

static int vfat_create_a_dotdir(struct inode *dir,struct inode *parent,
     struct buffer_head *bh,
     struct msdos_dir_entry *de,int ino,const char *name, int isdot)
{
	struct super_block *sb = dir->i_sb;
	struct inode *dot;

	PRINTK2(("vfat_create_a_dotdir: Entering\n"));

	/*
	 * XXX all times should be set by caller upon successful completion.
	 */
	dir->i_atime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dir);
	memcpy(de->name,name,MSDOS_NAME);
	de->lcase = 0;
	de->attr = ATTR_DIR;
	de->start = 0;
	de->starthi = 0;
	fat_date_unix2dos(dir->i_mtime,&de->time,&de->date);
	de->ctime_ms = 0;
	de->ctime = de->time;
	de->adate = de->cdate = de->date;
	de->size = 0;
	fat_mark_buffer_dirty(sb, bh, 1);
	dot = iget(dir->i_sb,ino);
	if (!dot)
		return -EIO;
	vfat_read_inode(dot);
	dot->i_mtime = dot->i_atime = CURRENT_TIME;
	mark_inode_dirty(dot);
	if (isdot) {
		dot->i_size = dir->i_size;
		MSDOS_I(dot)->i_start = MSDOS_I(dir)->i_start;
		MSDOS_I(dot)->i_logstart = MSDOS_I(dir)->i_logstart;
		dot->i_nlink = dir->i_nlink;
	} else {
		dot->i_size = parent->i_size;
		MSDOS_I(dot)->i_start = MSDOS_I(parent)->i_start;
		MSDOS_I(dot)->i_logstart = MSDOS_I(parent)->i_logstart;
		dot->i_nlink = parent->i_nlink;
	}

	iput(dot);

	PRINTK3(("vfat_create_a_dotdir 2\n"));
	return 0;
}

static int vfat_create_dotdirs(struct inode *dir, struct inode *parent)
{
	struct super_block *sb = dir->i_sb;
	int res;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	loff_t offset;

	PRINTK2(("vfat_create_dotdirs: Entering\n"));
	if ((res = fat_add_cluster(dir)) < 0) return res;

	PRINTK3(("vfat_create_dotdirs 2\n"));
	offset = 0;
	bh = NULL;
	if ((res = fat_get_entry(dir,&offset,&bh,&de)) < 0) return res;
	
	PRINTK3(("vfat_create_dotdirs 3\n"));
	res = vfat_create_a_dotdir(dir, parent, bh, de, res, MSDOS_DOT, 1);
	PRINTK3(("vfat_create_dotdirs 4\n"));
	if (res < 0) {
		fat_brelse(sb, bh);
		return res;
	}
	PRINTK3(("vfat_create_dotdirs 5\n"));

	if ((res = fat_get_entry(dir,&offset,&bh,&de)) < 0) {
		fat_brelse(sb, bh);
		return res;
	}
	PRINTK3(("vfat_create_dotdirs 6\n"));

	res = vfat_create_a_dotdir(dir, parent, bh, de, res, MSDOS_DOTDOT, 0);
	PRINTK3(("vfat_create_dotdirs 7\n"));
	fat_brelse(sb, bh);

	return res;
}

/***** See if directory is empty */
static int vfat_empty(struct inode *dir)
{
	struct super_block *sb = dir->i_sb;
	loff_t pos;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;

	if (MSDOS_I(dir)->i_start) { /* may be zero in mkdir */
		pos = 0;
		bh = NULL;
		while (fat_get_entry(dir,&pos,&bh,&de) > -1) {
			/* Skip extended filename entries */
			if (de->attr == ATTR_EXT) continue;

			if (!IS_FREE(de->name) && strncmp(de->name,MSDOS_DOT,
			    MSDOS_NAME) && strncmp(de->name,MSDOS_DOTDOT,
			    MSDOS_NAME)) {
				fat_brelse(sb, bh);
				return -ENOTEMPTY;
			}
		}
		if (bh)
			fat_brelse(sb, bh);
	}
	return 0;
}

static void vfat_free_ino(struct inode *dir,struct buffer_head *bh,
     struct msdos_dir_entry *de,struct inode* victim)
{
	struct super_block *sb = dir->i_sb;
	victim->i_nlink = 0;
	victim->i_mtime = dir->i_mtime = CURRENT_TIME;
	victim->i_atime = dir->i_atime = CURRENT_TIME;
	dir->i_version = ++event;
	MSDOS_I(victim)->i_busy = 1;
	mark_inode_dirty(dir);
	mark_inode_dirty(victim);
	de->name[0] = DELETED_FLAG;
	fat_mark_buffer_dirty(sb, bh, 1);
}

static int vfat_remove_entry(struct inode *dir,struct vfat_slot_info *sinfo,
     struct inode* victim)
{
	struct super_block *sb = dir->i_sb;
	loff_t offset;
	struct buffer_head *bh=NULL;
	struct msdos_dir_entry *de;
	int res, i;

	/* remove the shortname */
	offset = sinfo->shortname_offset;
	res = fat_get_entry(dir, &offset, &bh, &de);
	if (res < 0) return res;
	vfat_free_ino(dir,bh,de,victim);
	/* remove the longname */
	offset = sinfo->longname_offset;
	for (i = sinfo->long_slots; i > 0; --i) {
		if (fat_get_entry(dir, &offset, &bh, &de) < 0)
			continue;
		de->name[0] = DELETED_FLAG;
		de->attr = 0;
		fat_mark_buffer_dirty(sb, bh, 1);
	}
	if (bh) fat_brelse(sb, bh);
	return 0;
}

/* Drop all aliases */
static void drop_aliases(struct dentry *dentry)
{
	struct list_head *head, *next, *tmp;
	struct dentry *alias;

	PRINTK1(("drop_replace_inodes: dentry=%p, inode=%p\n", dentry, inode));
	head = &dentry->d_inode->i_dentry;
	if (dentry->d_inode) {
		next = dentry->d_inode->i_dentry.next;
		while (next != head) {
			tmp = next;
			next = tmp->next;
			alias = list_entry(tmp, struct dentry, d_alias);
			if (alias == dentry)
				continue;

			d_drop(alias);
		}
	}
}

static int vfat_rmdirx(struct inode *dir,struct dentry* dentry)
{
	int res;
	struct vfat_slot_info sinfo;

	PRINTK1(("vfat_rmdirx: dentry=%p\n", dentry));
	res = vfat_find(dir,&dentry->d_name,0,0,&sinfo);

	if (res >= 0 && sinfo.total_slots > 0) {
		if (!list_empty(&dentry->d_hash))
			return -EBUSY;
		/* Take care of aliases */
		if (dentry->d_inode->i_count > 1) {
			shrink_dcache_parent(dentry->d_parent);
			if (dentry->d_inode->i_count > 1)
				return -EBUSY;
		}
		res = vfat_empty(dentry->d_inode);
		if (res)
			return res;
		
		res = vfat_remove_entry(dir,&sinfo,dentry->d_inode);
		if (res >= 0) {
			dir->i_nlink--;
			res = 0;
		}
	}
	return res;
}

/***** Remove a directory */
int vfat_rmdir(struct inode *dir,struct dentry* dentry)
{
	int res;
	PRINTK1(("vfat_rmdir: dentry=%p, inode=%p\n", dentry, dentry->d_inode));

	res = -EBUSY;
	if (list_empty(&dentry->d_hash)) {
		res = vfat_rmdirx(dir, dentry);
		/* If that went OK all aliases are already dropped */
	}
	return res;
}

static int vfat_unlinkx(
	struct inode *dir,
	struct dentry* dentry,
	int nospc)	/* Flag special file ? */
{
	int res;
	struct vfat_slot_info sinfo;

	PRINTK1(("vfat_unlinkx: dentry=%p, inode=%p\n", dentry, dentry->d_inode));
	res = vfat_find(dir,&dentry->d_name,0,0,&sinfo);

	if (res >= 0 && sinfo.total_slots > 0) {
		if (!S_ISREG(dentry->d_inode->i_mode) && nospc) {
			return -EPERM;
		}
		res = vfat_remove_entry(dir,&sinfo,dentry->d_inode);
		if (res > 0) {
			res = 0;
		}
	}

	return res;
}


int vfat_mkdir(struct inode *dir,struct dentry* dentry,int mode)
{
	struct inode *inode;
	struct vfat_slot_info sinfo;
	int res;

	PRINTK1(("vfat_mkdir: dentry=%p, inode=%p\n", dentry, dentry->d_inode));
	fat_lock_creation();
	if ((res = vfat_create_entry(dir,&dentry->d_name,1,&inode)) < 0) {
		fat_unlock_creation();
		return res;
	}

	dir->i_nlink++;
	inode->i_nlink = 2; /* no need to mark them dirty */

	res = vfat_create_dotdirs(inode, dir);
	if (res < 0)
		goto mkdir_failed;
	fat_unlock_creation();
	dentry->d_time = dentry->d_parent->d_inode->i_version;
	d_instantiate(dentry,inode);
	return res;

mkdir_failed:
	fat_unlock_creation();
	if (vfat_find(dir,&dentry->d_name,0,0,&sinfo) < 0)
		goto mkdir_panic;
	if (vfat_remove_entry(dir, &sinfo, inode) < 0)
		goto mkdir_panic;
	iput(inode);
	dir->i_nlink--;
	return res;

mkdir_panic:
	dir->i_version = ++event;
	fat_fs_panic(dir->i_sb,"rmdir in mkdir failed");
	return res;
}

/***** Unlink, as called for msdosfs */
int vfat_unlink(struct inode *dir,struct dentry* dentry)
{
	int res;

	PRINTK1(("vfat_unlink: dentry=%p, inode=%p\n", dentry, dentry->d_inode));
	res = vfat_unlinkx (dir,dentry,1);
	if (res >= 0) {
		drop_aliases(dentry);
		d_delete(dentry);
	}
	return res;
}

/***** Unlink, as called for uvfatfs */
int vfat_unlink_uvfat(struct inode *dir,struct dentry *dentry)
{
	int res;

	res = vfat_unlinkx (dir,dentry,0);
	iput(dir);
	return res;
}
 
int vfat_rename(struct inode *old_dir,struct dentry *old_dentry,
		struct inode *new_dir,struct dentry *new_dentry)
{
	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *old_bh,*new_bh,*dotdot_bh;
	struct msdos_dir_entry *old_de,*dotdot_de;
	loff_t old_offset,new_offset,old_longname_offset;
	int old_slots,old_ino,new_ino,dotdot_ino;
	struct inode *old_inode, *new_inode, *dotdot_inode;
	struct dentry *walk;
	int res, is_dir, i;
	int locked = 0;
	struct vfat_slot_info sinfo;

	PRINTK1(("vfat_rename: Entering: old_dentry=%p, old_inode=%p, old ino=%ld, new_dentry=%p, new_inode=%p, new ino=%ld\n",
		 old_dentry, old_dentry->d_inode, old_dentry->d_inode->i_ino,
		 new_dentry, new_dentry->d_inode,
		 new_dentry->d_inode ? new_dentry->d_inode->i_ino : 0));
	/*
	 * POSIX is braindead (surprise, surprise). It requires that rename()
	 * should return 0 and do nothing if the target has the same inode as
	 * the source. Somebody, get a time machine, return to '89 and tell
	 * RMS & Co *not* to do that idiocy, FAST!
	 */
	if (old_dentry->d_inode == new_dentry->d_inode)
		return 0;

	old_bh = new_bh = NULL;
	old_inode = new_inode = NULL;
	res = vfat_find(old_dir,&old_dentry->d_name,0,0,&sinfo);
	PRINTK3(("vfat_rename 2\n"));
	if (res < 0) goto rename_done;

	old_slots = sinfo.total_slots;
	old_longname_offset = sinfo.longname_offset;
	old_offset = sinfo.shortname_offset;
	old_ino = sinfo.ino;
	res = fat_get_entry(old_dir, &old_offset, &old_bh, &old_de);
	PRINTK3(("vfat_rename 3\n"));
	if (res < 0) goto rename_done;

	res = -ENOENT;
	old_inode = old_dentry->d_inode;
	is_dir = S_ISDIR(old_inode->i_mode);

	/*
	 * Race: we can be hit by another rename after this check.
	 * For the time being use fat_lock_creation(), but it's
	 * ugly. FIXME.
	 */

	fat_lock_creation(); locked = 1;

	if (is_dir) {
		/* We can't use d_subdir() here. Arrgh. */
		for (walk=new_dentry;walk!=walk->d_parent;walk=walk->d_parent) {
			if (walk->d_inode != old_dentry->d_inode)
				continue;
			res = -EINVAL;
			goto rename_done;
		}
	}

	if (new_dentry->d_inode) {
		/*
		 * OK, we have to remove the target. We should do it so
		 * that nobody might go and find it negative. Actually we
		 * should give warranties wrt preserving target over the
		 * possible crash, but that's another story. We can't
		 * get here with the target unhashed, so the directory entry
		 * must exist.
		 */

		new_inode = new_dentry->d_inode;
		res = vfat_find(new_dir,&new_dentry->d_name,0,is_dir,&sinfo);
		if (res < 0 || new_inode->i_ino != sinfo.ino) {
			/* WTF??? Cry and fail. */
			printk(KERN_WARNING "vfat_rename: fs corrupted\n");
			goto rename_done;
		}

		if (is_dir) {
			/*
			 * Target is a directory. No other owners will
			 * be tolerated.
			 */
			res = -EBUSY;
			if (d_invalidate(new_dentry) < 0)
				goto rename_done;
			/*
			 * OK, let's try to get rid of other dentries.
			 * No need to do it if i_count is 1.
			 */
			if (new_inode->i_count>1) {
				shrink_dcache_parent(new_dentry->d_parent);
				if (new_inode->i_count>1)
					goto rename_done;
			}
			res = vfat_empty(new_inode);
			if (res)
				goto rename_done;
		} else {
			drop_aliases(new_dentry);
		}
		res = vfat_remove_entry(new_dir,&sinfo,new_inode);
		if (res)
			goto rename_done;
	}

	/* Serious lossage here. FAT uses braindead inode numbers scheme,
	 * so we can't simply cannibalize the entry. It means that we have
	 * no warranties that crash here will not make target disappear
	 * after reboot. Lose, lose. Nothing to do with that until we'll
	 * separate the functions of i_ino: it serves both as a search key
	 * in icache and as a part of stat output. It would kill all the
	 * 'busy' stuff on the spot. Later.
	 */

	if (is_dir)
		new_dir->i_nlink--;

	res = vfat_find(new_dir,&new_dentry->d_name,1,is_dir,&sinfo);

	if (res < 0) goto rename_done;

	new_offset = sinfo.shortname_offset;
	new_ino = sinfo.ino;

	/* XXX: take care of other owners */

	remove_inode_hash(old_inode);
	fat_cache_inval_inode(old_inode);
	old_inode->i_ino = new_ino;
	old_inode->i_version = ++event;
	insert_inode_hash(old_inode);
	mark_inode_dirty(old_inode);

	old_dir->i_version = ++event;
	new_dir->i_version = ++event;

	/* remove the old entry */
	for (i = old_slots; i > 0; --i) {
		res = fat_get_entry(old_dir, &old_longname_offset, &old_bh, &old_de);
		if (res < 0) {
			printk("vfat_rename: problem 1\n");
			continue;
		}
		old_de->name[0] = DELETED_FLAG;
		old_de->attr = 0;
		fat_mark_buffer_dirty(sb, old_bh, 1);
	}

	if (is_dir) {
		if ((res = fat_scan(old_inode,MSDOS_DOTDOT,&dotdot_bh,
		    &dotdot_de,&dotdot_ino,SCAN_ANY)) < 0) goto rename_done;
		if (!(dotdot_inode = iget(old_inode->i_sb,dotdot_ino))) {
			fat_brelse(sb, dotdot_bh);
			res = -EIO;
			goto rename_done;
		}
		MSDOS_I(dotdot_inode)->i_start = MSDOS_I(new_dir)->i_start;
		MSDOS_I(dotdot_inode)->i_logstart = MSDOS_I(new_dir)->i_logstart;
		dotdot_de->start = CT_LE_W(MSDOS_I(new_dir)->i_logstart);
		dotdot_de->starthi = CT_LE_W((MSDOS_I(new_dir)->i_logstart) >> 16);
		mark_inode_dirty(dotdot_inode);
		fat_mark_buffer_dirty(sb, dotdot_bh, 1);
		old_dir->i_nlink--;
		new_dir->i_nlink++;
		/* no need to mark them dirty */
		dotdot_inode->i_nlink = new_dir->i_nlink;
		iput(dotdot_inode);
		fat_brelse(sb, dotdot_bh);
	}

	if (res >= 0) {
		if (new_inode && is_dir)
			d_rehash(new_dentry);
		d_move(old_dentry, new_dentry);
		res = 0;
	}

rename_done:
	if (locked)
		fat_unlock_creation();
	if (old_bh)
		fat_brelse(sb, old_bh);
	if (new_bh)
		fat_brelse(sb, new_bh);
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
	NULL,			/* followlink */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};


void vfat_read_inode(struct inode *inode)
{
	fat_read_inode(inode, &vfat_dir_inode_operations);
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
