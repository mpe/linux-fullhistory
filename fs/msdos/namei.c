/*
 *  linux/fs/msdos/namei.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  Hidden files 1995 by Albert Cahalan <albert@ccs.neu.edu> <adc@coe.neu.edu>
 */


#define __NO_VERSION__
#include <linux/config.h>
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/string.h>

#include <asm/uaccess.h>

#include "../fat/msbuffer.h"

#define MSDOS_DEBUG 0
#define PRINTK(x)

/* MS-DOS "device special files" */

static const char *reserved_names[] = {
#ifndef CONFIG_ATARI /* GEMDOS is less stupid */
    "CON     ","PRN     ","NUL     ","AUX     ",
    "LPT1    ","LPT2    ","LPT3    ","LPT4    ",
    "COM1    ","COM2    ","COM3    ","COM4    ",
#endif
    NULL };


/* Characters that are undesirable in an MS-DOS file name */
  
static char bad_chars[] = "*?<>|\"";
#ifdef CONFIG_ATARI
/* GEMDOS is less restrictive */
static char bad_if_strict[] = " ";
#else
static char bad_if_strict[] = "+=,; ";
#endif

void msdos_put_super(struct super_block *sb)
{
	fat_put_super(sb);
	MOD_DEC_USE_COUNT;
}

struct super_operations msdos_sops = { 
	msdos_read_inode,
	fat_write_inode,
	fat_put_inode,
	fat_delete_inode,
	fat_notify_change,
	msdos_put_super,
	NULL, /* added in 0.96c */
	fat_statfs,
	NULL
};

/***** Formats an MS-DOS file name. Rejects invalid names. */
static int msdos_format_name(char conv,const char *name,int len,
	char *res,int dot_dirs,char dotsOK)
	/* conv is relaxed/normal/strict, name is proposed name,
	 * len is the length of the proposed name, res is the result name,
	 * dot_dirs is . and .. are OK, dotsOK is if hidden files get dots.
	 */
{
	char *walk;
	const char **reserved;
	unsigned char c;
	int space;

	if (name[0] == '.' && (len == 1 || (len == 2 && name[1] == '.'))) {
		if (!dot_dirs) return -EEXIST;
		memset(res+1,' ',10);
		while (len--) *res++ = '.';
		return 0;
	}
	if (name[0] == '.') {  /* dotfile because . and .. already done */
		if (!dotsOK) return -EINVAL;
		/* Get rid of dot - test for it elsewhere */
		name++; len--;
	}
#ifndef CONFIG_ATARI
	space = 1; /* disallow names that _really_ start with a dot */
#else
	space = 0; /* GEMDOS does not care */
#endif
	c = 0;
	for (walk = res; len && walk-res < 8; walk++) {
	    	c = *name++;
		len--;
		if (conv != 'r' && strchr(bad_chars,c)) return -EINVAL;
		if (conv == 's' && strchr(bad_if_strict,c)) return -EINVAL;
  		if (c >= 'A' && c <= 'Z' && conv == 's') return -EINVAL;
		if (c < ' ' || c == ':' || c == '\\') return -EINVAL;
/*  0xE5 is legal as a first character, but we must substitute 0x05     */
/*  because 0xE5 marks deleted files.  Yes, DOS really does this.       */
/*  It seems that Microsoft hacked DOS to support non-US characters     */
/*  after the 0xE5 character was already in use to mark deleted files.  */
		if((res==walk) && (c==0xE5)) c=0x05;
		if (c == '.') break;
		space = (c == ' ');
		*walk = (c >= 'a' && c <= 'z') ? c-32 : c;
	}
	if (space) return -EINVAL;
	if (conv == 's' && len && c != '.') {
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
			if (c < ' ' || c == ':' || c == '\\')
				return -EINVAL;
			if (c == '.') {
				if (conv == 's')
					return -EINVAL;
				break;
			}
			if (c >= 'A' && c <= 'Z' && conv == 's') return -EINVAL;
			space = c == ' ';
			*walk++ = c >= 'a' && c <= 'z' ? c-32 : c;
		}
		if (space) return -EINVAL;
		if (conv == 's' && len) return -EINVAL;
	}
	while (walk-res < MSDOS_NAME) *walk++ = ' ';
	for (reserved = reserved_names; *reserved; reserved++)
		if (!strncmp(res,*reserved,8)) return -EINVAL;
	return 0;
}


/***** Locates a directory entry.  Uses unformatted name. */
static int msdos_find(struct inode *dir,const char *name,int len,
    struct buffer_head **bh,struct msdos_dir_entry **de,int *ino)
{
	int res;
	char dotsOK;
	char scantype;
	char msdos_name[MSDOS_NAME];

	dotsOK = MSDOS_SB(dir->i_sb)->options.dotsOK;
	res = msdos_format_name(MSDOS_SB(dir->i_sb)->options.name_check,
				name,len, msdos_name,1,dotsOK);
	if (res < 0)
		return -ENOENT;
	if((name[0]=='.') && dotsOK){
	    switch(len){
		case  0: panic("Empty name in msdos_find!");
		case  1: scantype = SCAN_ANY;				break;
		case  2: scantype = ((name[1]=='.')?SCAN_ANY:SCAN_HID); break;
		default: scantype = SCAN_HID;
	    }
	} else {
	    scantype = (dotsOK ? SCAN_NOTHID : SCAN_ANY);
	}
	return fat_scan(dir,msdos_name,bh,de,ino,scantype);
}

/*
 * Compute the hash for the msdos name corresponding to the dentry.
 * Note: if the name is invalid, we leave the hash code unchanged so
 * that the existing dentry can be used. The msdos fs routines will
 * return ENOENT or EINVAL as appropriate.
 */
static int msdos_hash(struct dentry *dentry, struct qstr *qstr)
{
	struct fat_mount_options *options = & (MSDOS_SB(dentry->d_sb)->options);
	int error;
	char msdos_name[MSDOS_NAME];
	
	error = msdos_format_name(options->name_check, qstr->name, qstr->len,
					msdos_name, 1, options->dotsOK);
	if (!error)
		qstr->hash = full_name_hash(msdos_name, MSDOS_NAME);
	return 0;
}

/*
 * Compare two msdos names. If either of the names are invalid,
 * we fall back to doing the standard name comparison.
 */
static int msdos_cmp(struct dentry *dentry, struct qstr *a, struct qstr *b)
{
	struct fat_mount_options *options = & (MSDOS_SB(dentry->d_sb)->options);
	int error;
	char a_msdos_name[MSDOS_NAME], b_msdos_name[MSDOS_NAME];

	error = msdos_format_name(options->name_check, a->name, a->len,
					a_msdos_name, 1, options->dotsOK);
	if (error)
		goto old_compare;
	error = msdos_format_name(options->name_check, b->name, b->len,
					b_msdos_name, 1, options->dotsOK);
	if (error)
		goto old_compare;
	error = memcmp(a_msdos_name, b_msdos_name, MSDOS_NAME);
out:
	return error;

old_compare:
	error = 1;
	if (a->len == b->len)
		error = memcmp(a->name, b->name, a->len);
	goto out;
}


static struct dentry_operations msdos_dentry_operations = {
	NULL, 		/* d_revalidate */
	msdos_hash,
	msdos_cmp,
	NULL,		/* d_delete */
	NULL,
	NULL
};

struct super_block *msdos_read_super(struct super_block *sb,void *data, int silent)
{
	struct super_block *res;

	MOD_INC_USE_COUNT;

	MSDOS_SB(sb)->options.isvfat = 0;
	sb->s_op = &msdos_sops;
	res = fat_read_super(sb, data, silent);
	if (res == NULL)
		goto out_fail;
	sb->s_root->d_op = &msdos_dentry_operations;
	return res;

out_fail:
	sb->s_dev = 0;
	MOD_DEC_USE_COUNT;
	return NULL;
}


/***** Get inode using directory and name */
int msdos_lookup(struct inode *dir,struct dentry *dentry)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = NULL;
	struct msdos_dir_entry *de;
	struct buffer_head *bh;
	int ino,res;
	
	PRINTK (("msdos_lookup\n"));

	dentry->d_op = &msdos_dentry_operations;

	res = msdos_find(dir, dentry->d_name.name, dentry->d_name.len, &bh,
			&de, &ino);

	if (res == -ENOENT)
		goto add;
	if (res < 0)
		goto out;
	if (bh)
		fat_brelse(sb, bh);

	/* try to get the inode */
	res = -EACCES;
	inode = iget(sb, ino);
	if (!inode)
		goto out;
	if (!inode->i_sb ||
	   (inode->i_sb->s_magic != MSDOS_SUPER_MAGIC)) {
		printk(KERN_WARNING "msdos_lookup: foreign inode??\n");
	}
	/* mkdir in progress? */
	if (MSDOS_I(inode)->i_busy) {
		printk(KERN_WARNING "msdos_lookup: %s/%s busy\n",
			dentry->d_parent->d_name.name, dentry->d_name.name);
		iput(inode);
		goto out;
	}
add:
	d_add(dentry, inode);
	res = 0;
out:
	return res;
}


/***** Creates a directory entry (name is already formatted). */
static int msdos_create_entry(struct inode *dir, const char *name,
    int is_dir, int is_hid, struct inode **result)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	int res,ino;

	*result = NULL;
	if ((res = fat_scan(dir,NULL,&bh,&de,&ino,SCAN_ANY)) < 0) {
		if (res != -ENOENT) return res;
		if ((dir->i_ino == MSDOS_ROOT_INO) &&
		    (MSDOS_SB(sb)->fat_bits != 32))
			return -ENOSPC;
		if ((res = fat_add_cluster(dir)) < 0) return res;
		if ((res = fat_scan(dir,NULL,&bh,&de,&ino,SCAN_ANY)) < 0) return res;
	}
	/*
	 * XXX all times should be set by caller upon successful completion.
	 */
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dir);
	memcpy(de->name,name,MSDOS_NAME);
	de->attr = is_dir ? ATTR_DIR : ATTR_ARCH;
	de->attr = is_hid ? (de->attr|ATTR_HIDDEN) : (de->attr&~ATTR_HIDDEN);
	de->start = 0;
	de->starthi = 0;
	fat_date_unix2dos(dir->i_mtime,&de->time,&de->date);
	de->size = 0;
	fat_mark_buffer_dirty(sb, bh, 1);
	if ((*result = iget(dir->i_sb,ino)) != NULL)
		msdos_read_inode(*result);
	fat_brelse(sb, bh);
	if (!*result) return -EIO;
	(*result)->i_mtime = (*result)->i_atime = (*result)->i_ctime =
	    CURRENT_TIME;
	mark_inode_dirty(*result);
	return 0;
}

/***** Create a file or directory */
int msdos_create(struct inode *dir,struct dentry *dentry,int mode)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct inode *inode;
	int ino,res,is_hid;
	char msdos_name[MSDOS_NAME];

	res = msdos_format_name(MSDOS_SB(sb)->options.name_check,
				dentry->d_name.name,dentry->d_name.len,
				msdos_name,0,
				MSDOS_SB(sb)->options.dotsOK);
	if (res < 0)
		return res;
	is_hid = (dentry->d_name.name[0]=='.') && (msdos_name[0]!='.');
	fat_lock_creation();
	/* Scan for existing file twice, so that creating a file fails
	 * with -EINVAL if the other (dotfile/nondotfile) exists.
	 * Else SCAN_ANY would do. Maybe use EACCES, EBUSY, ENOSPC, ENFILE?
	 */
	if (fat_scan(dir,msdos_name,&bh,&de,&ino,SCAN_HID) >= 0) {
		fat_unlock_creation();
		fat_brelse(sb, bh);
		return is_hid ? -EEXIST : -EINVAL;
 	}
	if (fat_scan(dir,msdos_name,&bh,&de,&ino,SCAN_NOTHID) >= 0) {
		fat_unlock_creation();
		fat_brelse(sb, bh);
		return is_hid ? -EINVAL : -EEXIST;
 	}
	res = msdos_create_entry(dir,msdos_name,S_ISDIR(mode),is_hid,
				 &inode);
	fat_unlock_creation();
	if (!res)
		d_instantiate(dentry, inode);
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

#endif

/***** See if directory is empty */
static int msdos_empty(struct inode *dir)
{
	loff_t pos;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	int result = 0;

	if (MSDOS_I(dir)->i_start) { /* may be zero in mkdir */
		pos = 0;
		bh = NULL;
		while (fat_get_entry(dir,&pos,&bh,&de) > -1) {
			/* Ignore vfat longname entries */
			if (de->attr == ATTR_EXT)
				continue;
			if (!IS_FREE(de->name) && 
			    strncmp(de->name,MSDOS_DOT   , MSDOS_NAME) &&
			    strncmp(de->name,MSDOS_DOTDOT, MSDOS_NAME)) {
				result = -ENOTEMPTY;
				break;
			}
		}
		if (bh)
			fat_brelse(dir->i_sb, bh);
	}
	return result;
}

/***** Remove a directory */
int msdos_rmdir(struct inode *dir, struct dentry *dentry)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = dentry->d_inode;
	int res,ino;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;

	bh = NULL;
	res = msdos_find(dir, dentry->d_name.name, dentry->d_name.len,
				&bh, &de, &ino);
	if (res < 0)
		goto rmdir_done;
	/*
	 * Check whether the directory is not in use, then check
	 * whether it is empty.
	 */
	res = -EBUSY;
	if (!list_empty(&dentry->d_hash)) {
#ifdef MSDOS_DEBUG
printk("msdos_rmdir: %s/%s busy, d_count=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, dentry->d_count);
#endif
		goto rmdir_done;
	}
	res = msdos_empty(inode);
	if (res)
		goto rmdir_done;

	inode->i_nlink = 0;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_nlink--;
	mark_inode_dirty(inode);
	mark_inode_dirty(dir);
	/*
	 * Do the d_delete before any blocking operations.
	 * We must make a negative dentry, as the FAT code
	 * apparently relies on the inode being iput().
	 */
	d_delete(dentry);
	de->name[0] = DELETED_FLAG;
	fat_mark_buffer_dirty(sb, bh, 1);
	res = 0;

rmdir_done:
	fat_brelse(sb, bh);
	return res;
}

/***** Make a directory */
int msdos_mkdir(struct inode *dir,struct dentry *dentry,int mode)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct inode *inode,*dot;
	int ino,res,is_hid;
	char msdos_name[MSDOS_NAME];

	res = msdos_format_name(MSDOS_SB(sb)->options.name_check,
				dentry->d_name.name,dentry->d_name.len,
				msdos_name,0,
				MSDOS_SB(sb)->options.dotsOK);
	if (res < 0)
		return res;
	is_hid = (dentry->d_name.name[0]=='.') && (msdos_name[0]!='.');
	fat_lock_creation();
	if (fat_scan(dir,msdos_name,&bh,&de,&ino,SCAN_ANY) >= 0)
		goto out_exist;

	res = msdos_create_entry(dir,msdos_name,1,is_hid, &inode);
	if (res < 0)
		goto out_unlock;

	dir->i_nlink++;
	inode->i_nlink = 2; /* no need to mark them dirty */

#ifdef whatfor
	/*
	 * He's dead, Jim. We don't d_instantiate anymore. Should do it
	 * from the very beginning, actually.
	 */
	MSDOS_I(inode)->i_busy = 1; /* prevent lookups */
#endif

	if ((res = fat_add_cluster(inode)) < 0)
		goto mkdir_error;
	if ((res = msdos_create_entry(inode,MSDOS_DOT,1,0,&dot)) < 0)
		goto mkdir_error;
	dot->i_size = inode->i_size; /* doesn't grow in the 2nd create_entry */
	MSDOS_I(dot)->i_start = MSDOS_I(inode)->i_start;
	MSDOS_I(dot)->i_logstart = MSDOS_I(inode)->i_logstart;
	dot->i_nlink = inode->i_nlink;
	mark_inode_dirty(dot);
	iput(dot);

	if ((res = msdos_create_entry(inode,MSDOS_DOTDOT,1,0,&dot)) < 0)
		goto mkdir_error;
	dot->i_size = dir->i_size;
	MSDOS_I(dot)->i_start = MSDOS_I(dir)->i_start;
	MSDOS_I(dot)->i_logstart = MSDOS_I(dir)->i_logstart;
	dot->i_nlink = dir->i_nlink;
	mark_inode_dirty(dot);
#ifdef whatfor
	MSDOS_I(inode)->i_busy = 0;
#endif
	iput(dot);
	d_instantiate(dentry, inode);
	res = 0;

out_unlock:
	fat_unlock_creation();
	return res;

mkdir_error:
	printk("msdos_mkdir: error=%d, attempting cleanup\n", res);
	bh = NULL;
	fat_scan(dir,msdos_name,&bh,&de,&ino,SCAN_ANY);
	inode->i_nlink = 0;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_nlink--;
	mark_inode_dirty(inode);
	mark_inode_dirty(dir);
	iput(inode);
	de->name[0] = DELETED_FLAG;
	fat_mark_buffer_dirty(sb, bh, 1);
	fat_brelse(sb, bh);
	goto out_unlock;

out_exist:
	fat_brelse(sb, bh);
	res = -EEXIST;
	goto out_unlock;
}

/***** Unlink a file */
static int msdos_unlinkx( struct inode *dir, struct dentry *dentry, int nospc)
{
	struct super_block *sb = dir->i_sb;
	struct inode *inode = dentry->d_inode;
	int res,ino;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;

	bh = NULL;
	res = msdos_find(dir, dentry->d_name.name, dentry->d_name.len,
			&bh, &de, &ino);
	if (res < 0)
		goto unlink_done;
	res = -EPERM;
	if (!S_ISREG(inode->i_mode) && nospc)
		goto unlink_done;
	/* N.B. check for busy files? */

	inode->i_nlink = 0;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	MSDOS_I(inode)->i_busy = 1;
	mark_inode_dirty(inode);
	mark_inode_dirty(dir);
	d_delete(dentry);	/* This also frees the inode */
	de->name[0] = DELETED_FLAG;
	fat_mark_buffer_dirty(sb, bh, 1);
	res = 0;
unlink_done:
	fat_brelse(sb, bh);
	return res;
}

/***** Unlink, as called for msdosfs */
int msdos_unlink(struct inode *dir,struct dentry *dentry)
{
	return msdos_unlinkx (dir,dentry,1);
}

/***** Unlink, as called for umsdosfs */
int msdos_unlink_umsdos(struct inode *dir,struct dentry *dentry)
{
	return msdos_unlinkx (dir,dentry,0);
}

#define MSDOS_CHECK_BUSY 1

/***** Rename within a directory */
static int msdos_rename_same(struct inode *old_dir,char *old_name,
    struct dentry *old_dentry,
    struct inode *new_dir,char *new_name,struct dentry *new_dentry,
    struct buffer_head *old_bh,
    struct msdos_dir_entry *old_de, int old_ino, int is_hid)
{
	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *new_bh;
	struct msdos_dir_entry *new_de;
	struct inode *new_inode,*old_inode;
	int new_ino, exists, error;

	if (!strncmp(old_name, new_name, MSDOS_NAME))
		goto set_hid;
	error = -ENOENT;
	if (*(unsigned char *) old_de->name == DELETED_FLAG)
		goto out;

	exists = fat_scan(new_dir,new_name,&new_bh,&new_de,&new_ino,SCAN_ANY) >= 0;
	if (exists) {
		error = -EIO;
		new_inode = new_dentry->d_inode;
		/* Make sure it really exists ... */
		if (!new_inode) {
			printk(KERN_ERR
				"msdos_rename_same: %s/%s inode NULL, ino=%d\n",
				new_dentry->d_parent->d_name.name,
				new_dentry->d_name.name, new_ino);
			d_drop(new_dentry);
			goto out_error;
		}
		error = S_ISDIR(new_inode->i_mode)
			? (old_de->attr & ATTR_DIR)
				? msdos_empty(new_inode)
				: -EPERM
			: (old_de->attr & ATTR_DIR)
				? -EPERM
				: 0;
		if (error)
			goto out_error;
		error = -EPERM;
		if ((old_de->attr & ATTR_SYS))
			goto out_error;

		if (S_ISDIR(new_inode->i_mode)) {
			/* make sure it's empty */
			error = msdos_empty(new_inode);
			if (error)
				goto out_error;
#ifdef MSDOS_CHECK_BUSY
			/* check for a busy dentry */
			error = -EBUSY;
			shrink_dcache_parent(new_dentry);
			if (new_dentry->d_count > 1) {
printk("msdos_rename_same: %s/%s busy, count=%d\n",
new_dentry->d_parent->d_name.name, new_dentry->d_name.name,
new_dentry->d_count);
				goto out_error;
			}
#endif
			new_dir->i_nlink--;
			mark_inode_dirty(new_dir);
		}
		new_inode->i_nlink = 0;
		MSDOS_I(new_inode)->i_busy = 1;
		mark_inode_dirty(new_inode);
		/*
		 * Make it negative if it's not busy;
		 * otherwise let d_move() drop it.
		 */
		if (new_dentry->d_count == 1)
			d_delete(new_dentry);

		new_de->name[0] = DELETED_FLAG;
		fat_mark_buffer_dirty(sb, new_bh, 1);
		fat_brelse(sb, new_bh);
	}
	memcpy(old_de->name, new_name, MSDOS_NAME);
	/* Update the dcache */
	d_move(old_dentry, new_dentry);
set_hid:
	old_de->attr = is_hid
		? (old_de->attr | ATTR_HIDDEN)
		: (old_de->attr &~ ATTR_HIDDEN);
	fat_mark_buffer_dirty(sb, old_bh, 1);
	/* update binary info for conversion, i_attrs */
	old_inode = old_dentry->d_inode;
	MSDOS_I(old_inode)->i_attrs = is_hid
		? (MSDOS_I(old_inode)->i_attrs |  ATTR_HIDDEN)
		: (MSDOS_I(old_inode)->i_attrs &~ ATTR_HIDDEN);
	error = 0;
out:
	return error;

out_error:
	fat_brelse(sb, new_bh);
	goto out;
}

/***** Rename across directories - a nonphysical move */
static int msdos_rename_diff(struct inode *old_dir, char *old_name,
    struct dentry *old_dentry,
    struct inode *new_dir,char *new_name, struct dentry *new_dentry,
    struct buffer_head *old_bh,
    struct msdos_dir_entry *old_de, int old_ino, int is_hid)
{
	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *new_bh,*free_bh,*dotdot_bh;
	struct msdos_dir_entry *new_de,*free_de,*dotdot_de;
	struct inode *old_inode,*new_inode,*free_inode,*dotdot_inode;
	int new_ino,free_ino,dotdot_ino;
	int error, exists;

	error = -EINVAL;
	if (old_ino == new_dir->i_ino)
		goto out;
	/* prevent moving directory below itself */
	if (is_subdir(new_dentry, old_dentry))
		goto out;

	error = -ENOENT;
	if (*(unsigned char *) old_de->name == DELETED_FLAG)
		goto out;

	/* find free spot */
	while ((error = fat_scan(new_dir, NULL, &free_bh, &free_de, &free_ino,
					SCAN_ANY)) < 0) {
		if (error != -ENOENT)
			goto out;
		error = fat_add_cluster(new_dir);
		if (error)
			goto out;
	}

	exists = fat_scan(new_dir,new_name,&new_bh,&new_de,&new_ino,SCAN_ANY) >= 0;
	if (exists) {  /* Trash the old file! */
		error = -EIO;
		new_inode = new_dentry->d_inode;
		/* Make sure it really exists ... */
		if (!new_inode) {
			printk(KERN_ERR
				"msdos_rename_diff: %s/%s inode NULL, ino=%d\n",
				new_dentry->d_parent->d_name.name,
				new_dentry->d_name.name, new_ino);
			d_drop(new_dentry);
			goto out_new;
		}
		error = S_ISDIR(new_inode->i_mode)
			? (old_de->attr & ATTR_DIR)
				? msdos_empty(new_inode)
				: -EPERM
			: (old_de->attr & ATTR_DIR)
				? -EPERM
				: 0;
		if (error)
			goto out_new;
 		error = -EPERM;
		if ((old_de->attr & ATTR_SYS))
			goto out_new;

#ifdef MSDOS_CHECK_BUSY
		/* check for a busy dentry */
		error = -EBUSY;
		if (new_dentry->d_count > 1) {
			shrink_dcache_parent(new_dentry);
			if (new_dentry->d_count > 1) {
printk("msdos_rename_diff: target %s/%s busy, count=%d\n",
new_dentry->d_parent->d_name.name, new_dentry->d_name.name,
new_dentry->d_count);
				goto out_new;
			}
		}
#endif
		if (S_ISDIR(new_inode->i_mode)) {
			/* make sure it's empty */
			error = msdos_empty(new_inode);
			if (error)
				goto out_new;
			new_dir->i_nlink--;
			mark_inode_dirty(new_dir);
		}
		new_inode->i_nlink = 0;
		MSDOS_I(new_inode)->i_busy = 1;
		mark_inode_dirty(new_inode);
		/*
		 * Make it negative if it's not busy;
		 * otherwise let d_move() drop it.
		 */
		if (new_dentry->d_count == 1)
			d_delete(new_dentry);
		new_de->name[0] = DELETED_FLAG;
		fat_mark_buffer_dirty(sb, new_bh, 1);
		fat_brelse(sb, new_bh);
	}

	old_inode = old_dentry->d_inode;
	/* Get the dotdot inode if we'll need it ... */
	dotdot_bh = NULL;
	dotdot_inode = NULL;
	if (S_ISDIR(old_inode->i_mode)) {
		error = fat_scan(old_inode, MSDOS_DOTDOT, &dotdot_bh,
				&dotdot_de, &dotdot_ino, SCAN_ANY);
		if (error < 0) {
			printk(KERN_WARNING
				"MSDOS: %s/%s, get dotdot failed, ret=%d\n",
				old_dentry->d_parent->d_name.name,
				old_dentry->d_name.name, error);
			goto rename_done;
		}
		error = -EIO;
		dotdot_inode = iget(sb, dotdot_ino);
		if (!dotdot_inode)
			goto out_dotdot;
	}

	/* get an inode for the new name */
	memcpy(free_de, old_de, sizeof(struct msdos_dir_entry));
	memcpy(free_de->name, new_name, MSDOS_NAME);
	free_de->attr = is_hid
		? (free_de->attr|ATTR_HIDDEN)
		: (free_de->attr&~ATTR_HIDDEN);

	error = -EIO;
	free_inode = iget(sb, free_ino);
	if (!free_inode)
		goto out_iput;
	/* make sure it's not busy! */
	if (MSDOS_I(free_inode)->i_busy)
		printk(KERN_ERR "msdos_rename_diff: new inode %ld busy!\n",
			(ino_t) free_ino);
	if (!list_empty(&free_inode->i_dentry))
		printk("msdos_rename_diff: free inode has aliases??\n");
	msdos_read_inode(free_inode);

	/*
	 * Make sure the old dentry isn't busy,
	 * as we need to change inodes ...
	 */
	error = -EBUSY;
	if (old_dentry->d_count > 1) {
		shrink_dcache_parent(old_dentry);
		if (old_dentry->d_count > 1) {
printk("msdos_rename_diff: source %s/%s busy, count=%d\n",
old_dentry->d_parent->d_name.name, old_dentry->d_name.name,
old_dentry->d_count);
			goto out_iput;
		}
	}

	/* keep the inode for a bit ... */
	old_inode->i_count++;
	d_delete(old_dentry);

	free_inode->i_mode   = old_inode->i_mode;
	free_inode->i_nlink  = old_inode->i_nlink;
	free_inode->i_size   = old_inode->i_size;
	free_inode->i_blocks = old_inode->i_blocks;
	free_inode->i_mtime  = old_inode->i_mtime;
	free_inode->i_atime  = old_inode->i_atime;
	free_inode->i_ctime  = old_inode->i_ctime;
	MSDOS_I(free_inode)->i_ctime_ms = MSDOS_I(old_inode)->i_ctime_ms;

	MSDOS_I(free_inode)->i_start = MSDOS_I(old_inode)->i_start;
	MSDOS_I(free_inode)->i_logstart = MSDOS_I(old_inode)->i_logstart;
	MSDOS_I(free_inode)->i_attrs = MSDOS_I(old_inode)->i_attrs;

	/* release the old inode's resources */
	MSDOS_I(old_inode)->i_start = 0;
	MSDOS_I(old_inode)->i_logstart = 0;
	old_inode->i_nlink = 0;

	/*
	 * Install the new inode ...
	 */
	d_instantiate(old_dentry, free_inode);

	fat_mark_buffer_dirty(sb, free_bh, 1);
	fat_cache_inval_inode(old_inode);
	mark_inode_dirty(old_inode);
	old_de->name[0] = DELETED_FLAG;
	fat_mark_buffer_dirty(sb, old_bh, 1);
	iput(old_inode);

	/* a directory? */
	if (dotdot_bh) {
		MSDOS_I(dotdot_inode)->i_start = MSDOS_I(new_dir)->i_start;
		MSDOS_I(dotdot_inode)->i_logstart = MSDOS_I(new_dir)->i_logstart;
		dotdot_de->start = CT_LE_W(MSDOS_I(new_dir)->i_logstart);
		dotdot_de->starthi = CT_LE_W((MSDOS_I(new_dir)->i_logstart) >> 16);
		old_dir->i_nlink--;
		new_dir->i_nlink++;
		/* no need to mark them dirty */
		dotdot_inode->i_nlink = new_dir->i_nlink;
		mark_inode_dirty(dotdot_inode);
		iput(dotdot_inode);
		fat_mark_buffer_dirty(sb, dotdot_bh, 1);
		fat_brelse(sb, dotdot_bh);
	}

	/* Update the dcache */
	d_move(old_dentry, new_dentry);
	error = 0;

rename_done:
	fat_brelse(sb, free_bh);
out:
	return error;

out_iput:
	free_de->name[0] = DELETED_FLAG;
	/*
	 * Don't mark free_bh as dirty. Both states 
	 * are supposed to be equivalent.
	 */
	iput(free_inode); /* may be NULL */
	iput(dotdot_inode);
out_dotdot:
	fat_brelse(sb, dotdot_bh);
	goto rename_done;
out_new:
	fat_brelse(sb, new_bh);
	goto rename_done;
}

/***** Rename, a wrapper for rename_same_dir & rename_diff_dir */
int msdos_rename(struct inode *old_dir,struct dentry *old_dentry,
		 struct inode *new_dir,struct dentry *new_dentry)
{
	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *old_bh;
	struct msdos_dir_entry *old_de;
	int old_ino, error;
	int is_hid,old_hid; /* if new file and old file are hidden */
	char old_msdos_name[MSDOS_NAME], new_msdos_name[MSDOS_NAME];

	error = -EINVAL;
	if (sb != new_dir->i_sb)
		goto rename_done;
	error = msdos_format_name(MSDOS_SB(sb)->options.name_check,
				old_dentry->d_name.name, old_dentry->d_name.len,
				old_msdos_name, 1,MSDOS_SB(sb)->options.dotsOK);
	if (error < 0)
		goto rename_done;
	error = msdos_format_name(MSDOS_SB(sb)->options.name_check,
				new_dentry->d_name.name, new_dentry->d_name.len,
				new_msdos_name, 0,MSDOS_SB(sb)->options.dotsOK);
	if (error < 0)
		goto rename_done;

	is_hid  = (new_dentry->d_name.name[0]=='.') && (new_msdos_name[0]!='.');
	old_hid = (old_dentry->d_name.name[0]=='.') && (old_msdos_name[0]!='.');
	error = fat_scan(old_dir, old_msdos_name, &old_bh, &old_de,
			&old_ino, old_hid?SCAN_HID:SCAN_NOTHID);
	if (error < 0)
		goto rename_done;

	fat_lock_creation();
	if (old_dir == new_dir)
		error = msdos_rename_same(old_dir, old_msdos_name, old_dentry,
					new_dir, new_msdos_name, new_dentry,
		  			old_bh, old_de, (ino_t)old_ino, is_hid);
	else
		error = msdos_rename_diff(old_dir, old_msdos_name, old_dentry,
					new_dir, new_msdos_name, new_dentry,
					old_bh, old_de, (ino_t)old_ino, is_hid);
	fat_unlock_creation();
	fat_brelse(sb, old_bh);

rename_done:
	return error;
}


/* The public inode operations for the msdos fs */
struct inode_operations msdos_dir_inode_operations = {
	&fat_dir_operations,	/* default directory file-ops */
	msdos_create,		/* create */
	msdos_lookup,		/* lookup */
	NULL,			/* link */
	msdos_unlink,		/* unlink */
	NULL,			/* symlink */
	msdos_mkdir,		/* mkdir */
	msdos_rmdir,		/* rmdir */
	NULL,			/* mknod */
	msdos_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* readpage */
	NULL,			/* writepage */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL,			/* permission */
	NULL,                   /* smap */
	NULL,                   /* updatepage */
	NULL,                   /* revalidate */
};


void msdos_read_inode(struct inode *inode)
{
	fat_read_inode(inode, &msdos_dir_inode_operations);
}



#ifdef MODULE
int init_module(void)
{
	return init_msdos_fs();
}


void cleanup_module(void)
{
	unregister_filesystem(&msdos_fs_type);
}

#endif

