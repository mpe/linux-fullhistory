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

static int vfat_hashi(struct dentry *parent, struct qstr *qstr);
static int vfat_hash(struct dentry *parent, struct qstr *qstr);
static int vfat_cmpi(struct dentry *dentry, struct qstr *a, struct qstr *b);
static int vfat_cmp(struct dentry *dentry, struct qstr *a, struct qstr *b);
static int vfat_revalidate(struct dentry *dentry, int);

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

static void vfat_put_super_callback(struct super_block *sb)
{
	MOD_DEC_USE_COUNT;
}

static int vfat_revalidate(struct dentry *dentry, int flags)
{
	PRINTK1(("vfat_revalidate: %s\n", dentry->d_name.name));
	if (dentry->d_time == dentry->d_parent->d_inode->i_version) {
		return 1;
	}
	return 0;
}

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

/* Checks the validity of a long MS-DOS filename */
/* Returns negative number on error, 0 for a normal
 * return, and 1 for . or .. */

static int vfat_valid_longname(const char *name, int len, int xlate)
{
	const char **reserved, *walk;
	unsigned char c;
	int i, baselen;

	if (IS_FREE(name)) return -EINVAL;

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

static int vfat_valid_shortname(const char *name,int len,int utf8)
{
	const char *walk;
	unsigned char c;
	int space;
	int baselen;

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
	int ino,res;

	res=fat_scan(dir,name,&bh,&de,&ino);
	fat_brelse(dir->i_sb, bh);
	if (res<0)
		return -ENOENT;
	return 0;
}

static int vfat_format_name(const char *name,int len,char *res,int utf8)
{
	char *walk;
	unsigned char c;
	int space;

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
						len, name_res, utf8) < 0)
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
		ps->id = slot;
		ps->attr = ATTR_EXT;
		ps->reserved = 0;
		ps->alias_checksum = cksum;
		ps->start = 0;
		offset = (slot - 1) * 26;
		ip = &uniname[offset];
		memcpy(ps->name0_4, ip, 10);
		memcpy(ps->name5_10, ip+10, 12);
		memcpy(ps->name11_12, ip+22, 4);
	}
	ds[0].id |= 0x40;

	de = (struct msdos_dir_entry *) ps;
	PRINTK3(("vfat_fill_long_slots 9\n"));
	strncpy(de->name, msdos_name, MSDOS_NAME);
	(*slots)++;

	free_page(page);
	return 0;
}

/* We can't get "." or ".." here - VFS takes care of those cases */

static int vfat_build_slots(struct inode *dir,const char *name,int len,
     struct msdos_dir_slot *ds, int *slots)
{
	struct msdos_dir_entry *de;
	char msdos_name[MSDOS_NAME];
	int res, xlate, utf8;
	struct nls_table *nls;

	de = (struct msdos_dir_entry *) ds;
	xlate = MSDOS_SB(dir->i_sb)->options.unicode_xlate;
	utf8 = MSDOS_SB(dir->i_sb)->options.utf8;
	nls = MSDOS_SB(dir->i_sb)->nls_io;

	*slots = 1;
	res = vfat_valid_longname(name, len, xlate);
	if (res < 0)
		return res;
	if (vfat_valid_shortname(name, len, utf8) >= 0) {
		vfat_format_name(name, len, de->name, utf8);
		return 0;
	}
	res = vfat_create_shortname(dir, name, len, msdos_name, utf8);
	if (res < 0)
		return res;
	return vfat_fill_long_slots(ds, name, len, msdos_name, slots, xlate,
					utf8, nls);
}

static int vfat_add_entry(struct inode *dir,struct qstr* qname,
	int is_dir,struct vfat_slot_info *sinfo_out,
	struct buffer_head **bh, struct msdos_dir_entry **de)
{
	struct super_block *sb = dir->i_sb;
	struct msdos_dir_slot *ps;
	loff_t offset;
	struct msdos_dir_slot *ds;
	int slots, slot;
	int res;
	struct msdos_dir_entry *de1;
	struct buffer_head *bh1;
	int ino;
	int len;
	loff_t dummy;

	ds = (struct msdos_dir_slot *)
	    kmalloc(sizeof(struct msdos_dir_slot)*MSDOS_SLOTS, GFP_KERNEL);
	if (ds == NULL) return -ENOMEM;

	len = qname->len;
	while (len && qname->name[len-1] == '.')
		len--;
	res = fat_search_long(dir, qname->name, len,
			(MSDOS_SB(sb)->options.name_check != 's') ||
			!MSDOS_SB(sb)->options.posixfs,
			&dummy, &dummy);
	if (res > 0) /* found */
		res = -EEXIST;
	if (res)
		goto cleanup;

	res = vfat_build_slots(dir, qname->name, len, ds, &slots);
	if (res < 0) goto cleanup;

	offset = fat_add_entries(dir, slots, &bh1, &de1, &ino);
	if (offset < 0) {
		res = offset;
		goto cleanup;
	}
	fat_brelse(sb, bh1);

	/* Now create the new entry */
	*bh = NULL;
	for (slot = 0, ps = ds; slot < slots; slot++, ps++) {
		if (fat_get_entry(dir,&offset,bh,de, &sinfo_out->ino) < 0) {
			res = -EIO;
			goto cleanup;
		}
		memcpy(*de, ps, sizeof(struct msdos_dir_slot));
		fat_mark_buffer_dirty(sb, *bh, 1);
	}

	dir->i_ctime = dir->i_mtime = dir->i_atime = CURRENT_TIME;
	mark_inode_dirty(dir);

	fat_date_unix2dos(dir->i_mtime,&(*de)->time,&(*de)->date);
	(*de)->ctime_ms = 0;
	(*de)->ctime = (*de)->time;
	(*de)->adate = (*de)->cdate = (*de)->date;
	(*de)->start = 0;
	(*de)->starthi = 0;
	(*de)->size = 0;
	(*de)->attr = is_dir ? ATTR_DIR : ATTR_ARCH;
	(*de)->lcase = CASE_LOWER_BASE | CASE_LOWER_EXT;


	fat_mark_buffer_dirty(sb, *bh, 1);

	/* slots can't be less than 1 */
	sinfo_out->long_slots = slots - 1;
	sinfo_out->longname_offset = offset - sizeof(struct msdos_dir_slot) * slots;
	res = 0;

cleanup:
	kfree(ds);
	return res;
}

static int vfat_find(struct inode *dir,struct qstr* qname,
	struct vfat_slot_info *sinfo, struct buffer_head **last_bh,
	struct msdos_dir_entry **last_de)
{
	struct super_block *sb = dir->i_sb;
	loff_t offset;
	int res,len;

	len = qname->len;
	while (len && qname->name[len-1] == '.') 
		len--;
	res = fat_search_long(dir, qname->name, len,
			(MSDOS_SB(sb)->options.name_check != 's'),
			&offset,&sinfo->longname_offset);
	if (res>0) {
		sinfo->long_slots = res-1;
		if (fat_get_entry(dir,&offset,last_bh,last_de,&sinfo->ino)>=0)
			return 0;
		res = -EIO;
	} 
	return res ? res : -ENOENT;
}

/* Find a hashed dentry for inode; NULL if there are none */
static struct dentry *find_alias(struct inode *inode)
{
	struct list_head *head, *next, *tmp;
	struct dentry *alias;

	head = &inode->i_dentry;
	next = inode->i_dentry.next;
	while (next != head) {
		tmp = next;
		next = tmp->next;
		alias = list_entry(tmp, struct dentry, d_alias);
		if (!list_empty(&alias->d_hash))
			return dget(alias);
	}
	return NULL;
}

struct dentry *vfat_lookup(struct inode *dir,struct dentry *dentry)
{
	int res;
	struct vfat_slot_info sinfo;
	struct inode *inode;
	struct dentry *alias;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;
	int table;
	
	PRINTK2(("vfat_lookup: name=%s, len=%d\n", 
		 dentry->d_name.name, dentry->d_name.len));

	table = (MSDOS_SB(dir->i_sb)->options.name_check == 's') ? 2 : 0;
	dentry->d_op = &vfat_dentry_ops[table];

	inode = NULL;
	res = vfat_find(dir,&dentry->d_name,&sinfo,&bh,&de);
	if (res < 0) {
		table++;
		goto error;
	}
	inode = fat_build_inode(dir->i_sb, de, sinfo.ino, &res);
	fat_brelse(dir->i_sb, bh);
	if (res)
		return ERR_PTR(res);
	alias = find_alias(inode);
	if (alias) {
		if (d_invalidate(alias)==0)
			dput(alias);
		else {
			iput(inode);
			return alias;
		}
		
	}
error:
	dentry->d_op = &vfat_dentry_ops[table];
	dentry->d_time = dentry->d_parent->d_inode->i_version;
	d_add(dentry,inode);
	return NULL;
}

int vfat_create(struct inode *dir,struct dentry* dentry,int mode)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = NULL;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;
	struct vfat_slot_info sinfo;
	int res;

	res = vfat_add_entry(dir, &dentry->d_name, 0, &sinfo, &bh, &de);
	if (res < 0)
		return res;
	inode = fat_build_inode(sb, de, sinfo.ino, &res);
	fat_brelse(sb, bh);
	if (!inode)
		return res;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
	inode->i_version = ++event;
	dir->i_version = event;
	dentry->d_time = dentry->d_parent->d_inode->i_version;
	d_instantiate(dentry,inode);
	return 0;
}

static void vfat_remove_entry(struct inode *dir,struct vfat_slot_info *sinfo,
     struct buffer_head *bh, struct msdos_dir_entry *de)
{
	struct super_block *sb = dir->i_sb;
	loff_t offset;
	int i,ino;

	/* remove the shortname */
	dir->i_mtime = CURRENT_TIME;
	dir->i_atime = CURRENT_TIME;
	dir->i_version = ++event;
	mark_inode_dirty(dir);
	de->name[0] = DELETED_FLAG;
	fat_mark_buffer_dirty(sb, bh, 1);
	/* remove the longname */
	offset = sinfo->longname_offset; de = NULL;
	for (i = sinfo->long_slots; i > 0; --i) {
		if (fat_get_entry(dir, &offset, &bh, &de, &ino) < 0)
			continue;
		de->name[0] = DELETED_FLAG;
		de->attr = 0;
		fat_mark_buffer_dirty(sb, bh, 1);
	}
	if (bh) fat_brelse(sb, bh);
}

int vfat_rmdir(struct inode *dir,struct dentry* dentry)
{
	int res;
	struct vfat_slot_info sinfo;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;

	if (!list_empty(&dentry->d_hash))
		return -EBUSY;

	res = fat_dir_empty(dentry->d_inode);
	if (res)
		return res;

	res = vfat_find(dir,&dentry->d_name,&sinfo, &bh, &de);
	if (res<0)
		return res;
	dentry->d_inode->i_nlink = 0;
	dentry->d_inode->i_mtime = CURRENT_TIME;
	dentry->d_inode->i_atime = CURRENT_TIME;
	fat_detach(dentry->d_inode);
	mark_inode_dirty(dentry->d_inode);
	/* releases bh */
	vfat_remove_entry(dir,&sinfo,bh,de);
	dir->i_nlink--;
	return 0;
}

int vfat_unlink(struct inode *dir, struct dentry* dentry)
{
	int res;
	struct vfat_slot_info sinfo;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;

	PRINTK1(("vfat_unlink: %s\n", dentry->d_name.name));
	res = vfat_find(dir,&dentry->d_name,&sinfo,&bh,&de);
	if (res < 0)
		return res;
	dentry->d_inode->i_nlink = 0;
	dentry->d_inode->i_mtime = CURRENT_TIME;
	dentry->d_inode->i_atime = CURRENT_TIME;
	fat_detach(dentry->d_inode);
	mark_inode_dirty(dentry->d_inode);
	/* releases bh */
	vfat_remove_entry(dir,&sinfo,bh,de);
	d_delete(dentry);

	return res;
}


int vfat_mkdir(struct inode *dir,struct dentry* dentry,int mode)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = NULL;
	struct vfat_slot_info sinfo;
	struct buffer_head *bh = NULL;
	struct msdos_dir_entry *de;
	int res;

	res = vfat_add_entry(dir, &dentry->d_name, 1, &sinfo, &bh, &de);
	if (res < 0)
		return res;
	inode = fat_build_inode(sb, de, sinfo.ino, &res);
	if (!inode)
		goto out;
	inode->i_mtime = inode->i_atime = inode->i_ctime = CURRENT_TIME;
	mark_inode_dirty(inode);
	inode->i_version = ++event;
	dir->i_version = event;
	dir->i_nlink++;
	inode->i_nlink = 2; /* no need to mark them dirty */
	res = fat_new_dir(inode, dir, 1);
	if (res < 0)
		goto mkdir_failed;
	dentry->d_time = dentry->d_parent->d_inode->i_version;
	d_instantiate(dentry,inode);
out:
	fat_brelse(sb, bh);
	return res;

mkdir_failed:
	inode->i_nlink = 0;
	inode->i_mtime = CURRENT_TIME;
	inode->i_atime = CURRENT_TIME;
	fat_detach(inode);
	mark_inode_dirty(inode);
	/* releases bh */
	vfat_remove_entry(dir,&sinfo,bh,de);
	iput(inode);
	dir->i_nlink--;
	return res;
}
 
int vfat_rename(struct inode *old_dir,struct dentry *old_dentry,
		struct inode *new_dir,struct dentry *new_dentry)
{
	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *old_bh,*new_bh,*dotdot_bh;
	struct msdos_dir_entry *old_de,*new_de,*dotdot_de;
	int dotdot_ino;
	struct inode *old_inode, *new_inode;
	int res, is_dir;
	struct vfat_slot_info old_sinfo,sinfo;

	old_bh = new_bh = dotdot_bh = NULL;
	old_inode = old_dentry->d_inode;
	new_inode = new_dentry->d_inode;
	res = vfat_find(old_dir,&old_dentry->d_name,&old_sinfo,&old_bh,&old_de);
	PRINTK3(("vfat_rename 2\n"));
	if (res < 0) goto rename_done;

	is_dir = S_ISDIR(old_inode->i_mode);

	if (is_dir && (res = fat_scan(old_inode,MSDOS_DOTDOT,&dotdot_bh,
				&dotdot_de,&dotdot_ino)) < 0)
		goto rename_done;

	if (new_dentry->d_inode) {
		res = vfat_find(new_dir,&new_dentry->d_name,&sinfo,&new_bh,
				&new_de);
		if (res < 0 || MSDOS_I(new_inode)->i_location != sinfo.ino) {
			/* WTF??? Cry and fail. */
			printk(KERN_WARNING "vfat_rename: fs corrupted\n");
			goto rename_done;
		}

		if (is_dir) {
			res = fat_dir_empty(new_inode);
			if (res)
				goto rename_done;
		}
		fat_detach(new_inode);
	} else {
		res = vfat_add_entry(new_dir,&new_dentry->d_name,is_dir,&sinfo,
					&new_bh,&new_de);
		if (res < 0) goto rename_done;
	}

	new_dir->i_version = ++event;

	/* releases old_bh */
	vfat_remove_entry(old_dir,&old_sinfo,old_bh,old_de);
	old_bh=NULL;
	fat_detach(old_inode);
	fat_attach(old_inode, sinfo.ino);
	mark_inode_dirty(old_inode);

	old_dir->i_version = ++event;
	old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(old_dir);
	if (new_inode) {
		new_inode->i_nlink--;
		new_inode->i_ctime=CURRENT_TIME;
	}

	if (is_dir) {
		int start = MSDOS_I(new_dir)->i_logstart;
		dotdot_de->start = CT_LE_W(start);
		dotdot_de->starthi = CT_LE_W(start>>16);
		fat_mark_buffer_dirty(sb, dotdot_bh, 1);
		old_dir->i_nlink--;
		if (new_inode) {
			new_inode->i_nlink--;
		} else {
			new_dir->i_nlink++;
			mark_inode_dirty(new_dir);
		}
	}

rename_done:
	fat_brelse(sb, dotdot_bh);
	fat_brelse(sb, old_bh);
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
	NULL,			/* get_block */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL			/* revalidate */
};

struct super_block *vfat_read_super(struct super_block *sb,void *data,
				    int silent)
{
	struct super_block *res;
  
	MOD_INC_USE_COUNT;
	
	MSDOS_SB(sb)->options.isvfat = 1;

	res = fat_read_super(sb, data, silent, &vfat_dir_inode_operations);
	if (res == NULL) {
		sb->s_dev = 0;
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	if (!parse_options((char *) data, &(MSDOS_SB(sb)->options))) {
		MOD_DEC_USE_COUNT;
	} else {
		MSDOS_SB(sb)->put_super_callback=vfat_put_super_callback;
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
