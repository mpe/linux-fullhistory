/*
 *  linux/fs/msdos/namei.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  Hidden files 1995 by Albert Cahalan <albert@ccs.neu.edu> <adc@coe.neu.edu>
 */

#include <linux/config.h>

#define __NO_VERSION__
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/msdos_fs.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>

#include <asm/uaccess.h>

#include "../fat/msbuffer.h"

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
	char msdos_name[MSDOS_NAME];
	int res;
	char dotsOK;
	char scantype;

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


static int msdos_hash(struct dentry *dentry, struct qstr *qstr)
{
	unsigned long hash;
	char msdos_name[MSDOS_NAME];
	int error;
	int i;
	struct fat_mount_options *options = 
		& (MSDOS_SB(dentry->d_inode->i_sb)->options);
	
	error = msdos_format_name(options->name_check,
				  qstr->name, qstr->len, msdos_name,1,
				  options->dotsOK);
	if(error)
		return error;
	hash = init_name_hash();
	for(i=0; i< MSDOS_NAME; i++)
		hash = partial_name_hash(msdos_name[i], hash);
	qstr->hash = end_name_hash(hash);
	return 0;
}


static int msdos_cmp(struct dentry *dentry,       
		     struct qstr *a, struct qstr *b)
{
	char a_msdos_name[MSDOS_NAME],b_msdos_name[MSDOS_NAME];
	int error;
	struct fat_mount_options *options = 
		& (MSDOS_SB(dentry->d_inode->i_sb)->options);

	error = msdos_format_name(options->name_check,
				  a->name, a->len, a_msdos_name,1,
				  options->dotsOK);
	if(error)
		return error;
	error = msdos_format_name(options->name_check,
				  b->name, b->len, b_msdos_name,1,
				  options->dotsOK);
	if(error)
		return error;

	return memcmp(a_msdos_name, b_msdos_name, MSDOS_NAME);
}


static struct dentry_operations msdos_dentry_operations = {
	0, 		/* d_revalidate */
	msdos_hash,
	msdos_cmp
};

struct super_block *msdos_read_super(struct super_block *sb,void *data, int silent)
{
	struct super_block *res;

	MOD_INC_USE_COUNT;

	sb->s_op = &msdos_sops;
	res =  fat_read_super(sb, data, silent);
	if (res == NULL)
		MOD_DEC_USE_COUNT;
	sb->s_root->d_op = &msdos_dentry_operations;
	return res;
}


/***** Get inode using directory and name */
int msdos_lookup(struct inode *dir,struct dentry *dentry)
{
	struct super_block *sb = dir->i_sb;
	int ino,res;
	struct msdos_dir_entry *de;
	struct buffer_head *bh;
	struct inode *next, *inode;
	
	PRINTK (("msdos_lookup\n"));

	dentry->d_op = &msdos_dentry_operations;

	if(!dir) {
		d_add(dentry, NULL);
		return 0;
	}

	if ((res = msdos_find(dir,dentry->d_name.name,dentry->d_name.len,&bh,&de,&ino)) < 0) {
		if(res == -ENOENT) {
			d_add(dentry, NULL);
			res = 0;
		}
		return res;
	}
	PRINTK (("msdos_lookup 4\n"));
	if (bh)
		fat_brelse(sb, bh);
	PRINTK (("msdos_lookup 4.5\n"));
	if (!(inode = iget(dir->i_sb,ino)))
		return -EACCES;
	PRINTK (("msdos_lookup 5\n"));
	if (!inode->i_sb ||
	    (inode->i_sb->s_magic != MSDOS_SUPER_MAGIC)) {
		/* crossed a mount point into a non-msdos fs */
		d_add(dentry, inode);
		return 0;
	}
	if (MSDOS_I(inode)->i_busy) { /* mkdir in progress */
		iput(inode);
		d_add(dentry, NULL);
		return 0;
	}
	PRINTK (("msdos_lookup 6\n"));
	while (MSDOS_I(inode)->i_old) {
		next = MSDOS_I(inode)->i_old;
		iput(inode);
		if (!(inode = iget(next->i_sb,next->i_ino))) {
			fat_fs_panic(dir->i_sb,"msdos_lookup: Can't happen");
			d_add(dentry, NULL);
			return -ENOENT;
		}
	}
	PRINTK (("msdos_lookup 7\n"));
	d_add(dentry, inode);
	PRINTK (("msdos_lookup 8\n"));
	return 0;
}


/***** Creates a directory entry (name is already formatted). */
static int msdos_create_entry(struct inode *dir, const char *name,
    int is_dir, int is_hid, struct inode **result)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	int res,ino;

	if(!dir)
		return -ENOENT;

	*result = NULL;
	if ((res = fat_scan(dir,NULL,&bh,&de,&ino,SCAN_ANY)) < 0) {
		if (res != -ENOENT) return res;
		if (dir->i_ino == MSDOS_ROOT_INO) return -ENOSPC;
		if ((res = fat_add_cluster(dir)) < 0) return res;
		if ((res = fat_scan(dir,NULL,&bh,&de,&ino,SCAN_ANY)) < 0) return res;
	}
	/*
	 * XXX all times should be set by caller upon successful completion.
	 */
	dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	mark_inode_dirty(dir);
	memcpy(de->name,name,MSDOS_NAME);
	memset(de->unused, 0, sizeof(de->unused));
	de->attr = is_dir ? ATTR_DIR : ATTR_ARCH;
	de->attr = is_hid ? (de->attr|ATTR_HIDDEN) : (de->attr&~ATTR_HIDDEN);
	de->start = 0;
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
	char msdos_name[MSDOS_NAME];
	int ino,res,is_hid;

	if (!dir) return -ENOENT;
	if ((res = msdos_format_name(MSDOS_SB(dir->i_sb)->options.name_check,
				     dentry->d_name.name,dentry->d_name.len,
				     msdos_name,0,
				     MSDOS_SB(dir->i_sb)->options.dotsOK)) < 0)
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
			if (!IS_FREE(de->name) && strncmp(de->name,MSDOS_DOT,
			    MSDOS_NAME) && strncmp(de->name,MSDOS_DOTDOT,
			    MSDOS_NAME)) {
				fat_brelse(sb, bh);
				return -ENOTEMPTY;
			}
		if (bh)
			fat_brelse(sb, bh);
	}
	return 0;
}

/***** Remove a directory */
int msdos_rmdir(struct inode *dir,struct dentry *dentry)
{
	struct super_block *sb = dir->i_sb;
	int res,ino;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct inode *inode;

	bh = NULL;
	inode = NULL;
	res = -EPERM;
	if ((res = msdos_find(dir,dentry->d_name.name,dentry->d_name.len,
			      &bh,&de,&ino)) < 0)
		goto rmdir_done;
	inode = dentry->d_inode;
	res = -ENOTDIR;
	if (!S_ISDIR(inode->i_mode)) goto rmdir_done;
	res = -EBUSY;
	if (dir->i_dev != inode->i_dev || dir == inode)
	  goto rmdir_done;
	res = msdos_empty(inode);
	if (res)
		goto rmdir_done;
	inode->i_nlink = 0;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_nlink--;
	mark_inode_dirty(inode);
	mark_inode_dirty(dir);
	de->name[0] = DELETED_FLAG;
	fat_mark_buffer_dirty(sb, bh, 1);
	d_delete(dentry);
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
	char msdos_name[MSDOS_NAME];
	int ino,res,is_hid;

	if ((res = msdos_format_name(MSDOS_SB(dir->i_sb)->options.name_check,
				     dentry->d_name.name,dentry->d_name.len,
				     msdos_name,0,
				     MSDOS_SB(dir->i_sb)->options.dotsOK)) < 0)
		return res;
	is_hid = (dentry->d_name.name[0]=='.') && (msdos_name[0]!='.');
	fat_lock_creation();
	if (fat_scan(dir,msdos_name,&bh,&de,&ino,SCAN_ANY) >= 0) {
		fat_unlock_creation();
		fat_brelse(sb, bh);
		return -EEXIST;
 	}
	if ((res = msdos_create_entry(dir,msdos_name,1,is_hid,
				      &inode)) < 0) {
		fat_unlock_creation();
		return res;
	}
	dir->i_nlink++;
	inode->i_nlink = 2; /* no need to mark them dirty */
	MSDOS_I(inode)->i_busy = 1; /* prevent lookups */
	if ((res = fat_add_cluster(inode)) < 0) goto mkdir_error;
	if ((res = msdos_create_entry(inode,MSDOS_DOT,1,0,&dot)) < 0)
		goto mkdir_error;
	dot->i_size = inode->i_size; /* doesn't grow in the 2nd create_entry */
	MSDOS_I(dot)->i_start = MSDOS_I(inode)->i_start;
	dot->i_nlink = inode->i_nlink;
	mark_inode_dirty(dot);
	iput(dot);
	if ((res = msdos_create_entry(inode,MSDOS_DOTDOT,1,0,&dot)) < 0)
		goto mkdir_error;
	fat_unlock_creation();
	dot->i_size = dir->i_size;
	MSDOS_I(dot)->i_start = MSDOS_I(dir)->i_start;
	dot->i_nlink = dir->i_nlink;
	mark_inode_dirty(dot);
	MSDOS_I(inode)->i_busy = 0;
	iput(dot);
	d_instantiate(dentry, inode);
	return 0;
mkdir_error:
	if (msdos_rmdir(dir,dentry) < 0)
		fat_fs_panic(dir->i_sb,"rmdir in mkdir failed");
	fat_unlock_creation();
	return res;
}

/***** Unlink a file */
static int msdos_unlinkx(
	struct inode *dir,
	struct dentry *dentry,
	int nospc)	/* Flag special file ? */
{
	struct super_block *sb = dir->i_sb;
	int res,ino;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct inode *inode;

	bh = NULL;
	inode = NULL;
	if ((res = msdos_find(dir,dentry->d_name.name,dentry->d_name.len,
			      &bh,&de,&ino)) < 0)
		goto unlink_done;
	inode = dentry->d_inode;
	if (!S_ISREG(inode->i_mode) && nospc){
		res = -EPERM;
		goto unlink_done;
	}
	if (IS_IMMUTABLE(inode)){
		res = -EPERM;
		goto unlink_done;
	}
	inode->i_nlink = 0;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	MSDOS_I(inode)->i_busy = 1;
	mark_inode_dirty(inode);
	mark_inode_dirty(dir);
	de->name[0] = DELETED_FLAG;
	fat_mark_buffer_dirty(sb, bh, 1);
	d_delete(dentry);	/* This also frees the inode */
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

/***** Rename within a directory */
static int rename_same_dir(struct inode *old_dir,char *old_name,
    struct dentry *old_dentry,
    struct inode *new_dir,char *new_name,struct dentry *new_dentry,
    struct buffer_head *old_bh,
    struct msdos_dir_entry *old_de,int old_ino,int is_hid)
{
	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *new_bh;
	struct msdos_dir_entry *new_de;
	struct inode *new_inode,*old_inode;
	int new_ino,exists,error;

	if (!strncmp(old_name,new_name,MSDOS_NAME)) goto set_hid;
	exists = fat_scan(new_dir,new_name,&new_bh,&new_de,&new_ino,SCAN_ANY) >= 0;
	if (*(unsigned char *) old_de->name == DELETED_FLAG) {
		if (exists)
			fat_brelse(sb, new_bh);
		return -ENOENT;
	}
	if (exists) {
		new_inode = new_dentry->d_inode;
		error = S_ISDIR(new_inode->i_mode)
			? (old_de->attr & ATTR_DIR)
				? msdos_empty(new_inode)
				: -EPERM
			: (old_de->attr & ATTR_DIR)
				? -EPERM
				: 0;
		if (!error && (old_de->attr & ATTR_SYS)) error = -EPERM;
		if (error) {
			fat_brelse(sb, new_bh);
			return error;
		}
		if (S_ISDIR(new_inode->i_mode)) {
			new_dir->i_nlink--;
			mark_inode_dirty(new_dir);
		}
		new_inode->i_nlink = 0;
		MSDOS_I(new_inode)->i_busy = 1;
		mark_inode_dirty(new_inode);
		new_de->name[0] = DELETED_FLAG;
		fat_mark_buffer_dirty(sb, new_bh, 1);
		fat_brelse(sb, new_bh);
	}
	memcpy(old_de->name,new_name,MSDOS_NAME);
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
	return 0;
}

/***** Rename across directories - a nonphysical move */
static int rename_diff_dir(struct inode *old_dir,char *old_name,
    struct dentry *old_dentry,
    struct inode *new_dir,char *new_name,struct dentry *new_dentry,
    struct buffer_head *old_bh,
    struct msdos_dir_entry *old_de,int old_ino,int is_hid)
{
	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *new_bh,*free_bh,*dotdot_bh;
	struct msdos_dir_entry *new_de,*free_de,*dotdot_de;
	struct inode *old_inode,*new_inode,*free_inode,*dotdot_inode;
	struct dentry *walk;
	int new_ino,free_ino,dotdot_ino;
	int error,exists;

	if (old_dir->i_dev != new_dir->i_dev) return -EINVAL;
	if (old_ino == new_dir->i_ino) return -EINVAL;
	walk = new_dentry;
	/* prevent moving directory below itself */
	for (;;) {
		if (walk == old_dentry) return -EINVAL;
		if (walk == walk->d_parent) break;
		walk = walk->d_parent;
	}
	/* find free spot */
	while ((error = fat_scan(new_dir,NULL,&free_bh,&free_de,&free_ino,
	    SCAN_ANY)) < 0) {
		if (error != -ENOENT) return error;
		error = fat_add_cluster(new_dir);
		if (error) return error;
	}
	exists = fat_scan(new_dir,new_name,&new_bh,&new_de,&new_ino,SCAN_ANY) >= 0;
	old_inode = old_dentry->d_inode;
	if (*(unsigned char *) old_de->name == DELETED_FLAG) {
		fat_brelse(sb, free_bh);
		if (exists)
			fat_brelse(sb, new_bh);
		return -ENOENT;
	}
	new_inode = NULL; /* to make GCC happy */
	if (exists) {  /* Trash the old file! */
		new_inode = new_dentry->d_inode;
		error = S_ISDIR(new_inode->i_mode)
			? (old_de->attr & ATTR_DIR)
				? msdos_empty(new_inode)
				: -EPERM
			: (old_de->attr & ATTR_DIR)
				? -EPERM
				: 0;
		if (!error && (old_de->attr & ATTR_SYS)) error = -EPERM;
		if (error) {
			fat_brelse(sb, new_bh);
			return error;
		}
		new_inode->i_nlink = 0;
		MSDOS_I(new_inode)->i_busy = 1;
		mark_inode_dirty(new_inode);
		new_de->name[0] = DELETED_FLAG;
		fat_mark_buffer_dirty(sb, new_bh, 1);
	}
	memcpy(free_de,old_de,sizeof(struct msdos_dir_entry));
	memcpy(free_de->name,new_name,MSDOS_NAME);
	free_de->attr = is_hid
		? (free_de->attr|ATTR_HIDDEN)
		: (free_de->attr&~ATTR_HIDDEN);
	if (!(free_inode = iget(new_dir->i_sb,free_ino))) {
		free_de->name[0] = DELETED_FLAG;
/*  Don't mark free_bh as dirty. Both states are supposed to be equivalent. */
		fat_brelse(sb, free_bh);
		if (exists)
			fat_brelse(sb, new_bh);
		return -EIO;
	}
	if (exists && S_ISDIR(new_inode->i_mode)) {
		new_dir->i_nlink--;
		mark_inode_dirty(new_dir);
	}
	msdos_read_inode(free_inode);
	MSDOS_I(old_inode)->i_busy = 1;
	MSDOS_I(old_inode)->i_linked = free_inode;
	MSDOS_I(free_inode)->i_oldlink = old_inode;
	fat_cache_inval_inode(old_inode);
	mark_inode_dirty(old_inode);
	old_de->name[0] = DELETED_FLAG;
	fat_mark_buffer_dirty(sb, old_bh, 1);
	fat_mark_buffer_dirty(sb, free_bh, 1);
	if (exists) {
		MSDOS_I(new_inode)->i_depend = free_inode;
		MSDOS_I(free_inode)->i_old = new_inode;
		/* Two references now exist to free_inode so increase count */
		free_inode->i_count++;
		/* free_inode is put after putting new_inode and old_inode */
		fat_brelse(sb, new_bh);
	}
	if (S_ISDIR(old_inode->i_mode)) {
		if ((error = fat_scan(old_inode,MSDOS_DOTDOT,&dotdot_bh,
		    &dotdot_de,&dotdot_ino,SCAN_ANY)) < 0) goto rename_done;
		if (!(dotdot_inode = iget(old_inode->i_sb,dotdot_ino))) {
			fat_brelse(sb, dotdot_bh);
			error = -EIO;
			goto rename_done;
		}
		dotdot_de->start = MSDOS_I(dotdot_inode)->i_start =
		    MSDOS_I(new_dir)->i_start;
		mark_inode_dirty(dotdot_inode);
		fat_mark_buffer_dirty(sb, dotdot_bh, 1);
		old_dir->i_nlink--;
		new_dir->i_nlink++;
		/* no need to mark them dirty */
		dotdot_inode->i_nlink = new_dir->i_nlink;
		iput(dotdot_inode);
		fat_brelse(sb, dotdot_bh);
	}
	/* Update the dcache */
	d_move(old_dentry, new_dentry);
	error = 0;
rename_done:
	fat_brelse(sb, free_bh);
	return error;
}

/***** Rename, a wrapper for rename_same_dir & rename_diff_dir */
int msdos_rename(struct inode *old_dir,struct dentry *old_dentry,
		 struct inode *new_dir,struct dentry *new_dentry)
{
	struct super_block *sb = old_dir->i_sb;
	char old_msdos_name[MSDOS_NAME],new_msdos_name[MSDOS_NAME];
	struct buffer_head *old_bh;
	struct msdos_dir_entry *old_de;
	int old_ino,error;
	int is_hid,old_hid; /* if new file and old file are hidden */

	if ((error = msdos_format_name(MSDOS_SB(old_dir->i_sb)->options.name_check,
				       old_dentry->d_name.name,
				       old_dentry->d_name.len,old_msdos_name,1,
				       MSDOS_SB(old_dir->i_sb)->options.dotsOK))
	    < 0) goto rename_done;
	if ((error = msdos_format_name(MSDOS_SB(new_dir->i_sb)->options.name_check,
				       new_dentry->d_name.name,
				       new_dentry->d_name.len,new_msdos_name,0,
				       MSDOS_SB(new_dir->i_sb)->options.dotsOK))
	    < 0) goto rename_done;
	is_hid = (new_dentry->d_name.name[0]=='.') && (new_msdos_name[0]!='.');
	old_hid = (old_dentry->d_name.name[0]=='.') && (old_msdos_name[0]!='.');
	if ((error = fat_scan(old_dir,old_msdos_name,&old_bh,&old_de,
	    &old_ino,old_hid?SCAN_HID:SCAN_NOTHID)) < 0) goto rename_done;
	fat_lock_creation();
	if (old_dir == new_dir)
		error = rename_same_dir(old_dir,old_msdos_name,old_dentry,
		    new_dir,new_msdos_name,new_dentry,
		    old_bh,old_de,old_ino,is_hid);
	else error = rename_diff_dir(old_dir,old_msdos_name,old_dentry,
		     new_dir,new_msdos_name,new_dentry,
		     old_bh,old_de,old_ino,is_hid);
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
	fat_bmap,		/* bmap */
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

