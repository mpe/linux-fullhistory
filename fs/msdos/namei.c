/*
 *  linux/fs/msdos/namei.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *  Hidden files 1995 by Albert Cahalan <albert@ccs.neu.edu> <adc@coe.neu.edu>
 */

#define __NO_VERSION__
#include <linux/module.h>

#include <linux/sched.h>
#include <linux/msdos_fs.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>

#include <asm/segment.h>

#include "../fat/msbuffer.h"

#define PRINTK(x)


/* MS-DOS "device special files" */

static const char *reserved_names[] = {
    "CON     ","PRN     ","NUL     ","AUX     ",
    "LPT1    ","LPT2    ","LPT3    ","LPT4    ",
    "COM1    ","COM2    ","COM3    ","COM4    ",
    NULL };


/* Characters that are undesirable in an MS-DOS file name */
  
static char bad_chars[] = "*?<>|\"";
static char bad_if_strict[] = "+=,; ";


void msdos_put_super(struct super_block *sb)
{
	fat_put_super(sb);
	MOD_DEC_USE_COUNT;
}

struct super_operations msdos_sops = { 
	msdos_read_inode,
	fat_notify_change,
	fat_write_inode,
	fat_put_inode,
	msdos_put_super,
	NULL, /* added in 0.96c */
	fat_statfs,
	NULL
};

struct super_block *msdos_read_super(struct super_block *sb,void *data, int silent)
{
	struct super_block *res;

	MOD_INC_USE_COUNT;

	sb->s_op = &msdos_sops;
	res =  fat_read_super(sb, data, silent);
	if (res == NULL)
		MOD_DEC_USE_COUNT;

	return res;
}





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
	space = 1; /* disallow names that _really_ start with a dot */
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
			if (c < ' ' || c == ':' || c == '\\' || c == '.')
				return -EINVAL;
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

/***** Get inode using directory and name */
int msdos_lookup(struct inode *dir,const char *name,int len,
    struct inode **result)
{
	struct super_block *sb = dir->i_sb;
	int ino,res;
	struct msdos_dir_entry *de;
	struct buffer_head *bh;
	struct inode *next;
	
	PRINTK (("msdos_lookup\n"));

	*result = NULL;
	if (!dir) return -ENOENT;
	if (!S_ISDIR(dir->i_mode)) {
		iput(dir);
		return -ENOENT;
	}
	PRINTK (("msdos_lookup 2\n"));
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
#if 0
	if (dcache_lookup(dir, name, len, (unsigned long *) &ino)) {
		iput(dir);
		if (!(*result = iget(dir->i_sb, ino)))
			return -EACCES;
		return 0;
	}
#endif
	PRINTK (("msdos_lookup 3\n"));
	if ((res = msdos_find(dir,name,len,&bh,&de,&ino)) < 0) {
		iput(dir);
		return res;
	}
	PRINTK (("msdos_lookup 4\n"));
	if (bh)
		fat_brelse(sb, bh);
	PRINTK (("msdos_lookup 4.5\n"));
	if (!(*result = iget(dir->i_sb,ino))) {
		iput(dir);
		return -EACCES;
	}
	PRINTK (("msdos_lookup 5\n"));
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
	PRINTK (("msdos_lookup 6\n"));
	while (MSDOS_I(*result)->i_old) {
		next = MSDOS_I(*result)->i_old;
		iput(*result);
		if (!(*result = iget(next->i_sb,next->i_ino))) {
			fat_fs_panic(dir->i_sb,"msdos_lookup: Can't happen");
			iput(dir);
			return -ENOENT;
		}
	}
	PRINTK (("msdos_lookup 7\n"));
	iput(dir);
	PRINTK (("msdos_lookup 8\n"));
	return 0;
}


/***** Creates a directory entry (name is already formatted). */
static int msdos_create_entry(struct inode *dir, const char *name,int len,
    int is_dir, int is_hid, struct inode **result)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	int res,ino;

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
	dir->i_dirt = 1;
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
	(*result)->i_dirt = 1;
	dcache_add(dir, name, len, ino);
	return 0;
}

/***** Create a file or directory */
int msdos_create(struct inode *dir,const char *name,int len,int mode,
	struct inode **result)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	char msdos_name[MSDOS_NAME];
	int ino,res,is_hid;

	if (!dir) return -ENOENT;
	if ((res = msdos_format_name(MSDOS_SB(dir->i_sb)->options.name_check,
				     name,len,msdos_name,0,
				     MSDOS_SB(dir->i_sb)->options.dotsOK)) < 0) {
		iput(dir);
		return res;
	}
	is_hid = (name[0]=='.') && (msdos_name[0]!='.');
	fat_lock_creation();
	/* Scan for existing file twice, so that creating a file fails
	 * with -EINVAL if the other (dotfile/nondotfile) exists.
	 * Else SCAN_ANY would do. Maybe use EACCES, EBUSY, ENOSPC, ENFILE?
	 */
	if (fat_scan(dir,msdos_name,&bh,&de,&ino,SCAN_HID) >= 0) {
		fat_unlock_creation();
		fat_brelse(sb, bh);
		iput(dir);
		return is_hid ? -EEXIST : -EINVAL;
 	}
	if (fat_scan(dir,msdos_name,&bh,&de,&ino,SCAN_NOTHID) >= 0) {
		fat_unlock_creation();
		fat_brelse(sb, bh);
		iput(dir);
		return is_hid ? -EINVAL : -EEXIST;
 	}
	res = msdos_create_entry(dir,msdos_name,len,S_ISDIR(mode),is_hid,
				 result);
	fat_unlock_creation();
	iput(dir);
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
int msdos_rmdir(struct inode *dir,const char *name,int len)
{
	struct super_block *sb = dir->i_sb;
	int res,ino;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct inode *inode;

	bh = NULL;
	inode = NULL;
	res = -EPERM;
	if (name[0] == '.' && (len == 1 || (len == 2 && name[1] == '.')))
		goto rmdir_done;
	if ((res = msdos_find(dir,name,len,&bh,&de,&ino)) < 0) goto rmdir_done;
	res = -ENOENT;
	if (!(inode = iget(dir->i_sb,ino))) goto rmdir_done;
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
	inode->i_dirt = dir->i_dirt = 1;
	de->name[0] = DELETED_FLAG;
	fat_mark_buffer_dirty(sb, bh, 1);
	res = 0;
rmdir_done:
	fat_brelse(sb, bh);
	iput(dir);
	iput(inode);
	return res;
}

/***** Make a directory */
int msdos_mkdir(struct inode *dir,const char *name,int len,int mode)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct inode *inode,*dot;
	char msdos_name[MSDOS_NAME];
	int ino,res,is_hid;

	if ((res = msdos_format_name(MSDOS_SB(dir->i_sb)->options.name_check,
				     name,len,msdos_name,0,
				     MSDOS_SB(dir->i_sb)->options.dotsOK)) < 0) {
		iput(dir);
		return res;
	}
	is_hid = (name[0]=='.') && (msdos_name[0]!='.');
	fat_lock_creation();
	if (fat_scan(dir,msdos_name,&bh,&de,&ino,SCAN_ANY) >= 0) {
		fat_unlock_creation();
		fat_brelse(sb, bh);
		iput(dir);
		return -EEXIST;
 	}
	if ((res = msdos_create_entry(dir,msdos_name,len,1,is_hid,
				      &inode)) < 0) {
		fat_unlock_creation();
		iput(dir);
		return res;
	}
	dir->i_nlink++;
	inode->i_nlink = 2; /* no need to mark them dirty */
	MSDOS_I(inode)->i_busy = 1; /* prevent lookups */
	if ((res = fat_add_cluster(inode)) < 0) goto mkdir_error;
	if ((res = msdos_create_entry(inode,MSDOS_DOT,1,1,0,&dot)) < 0)
		goto mkdir_error;
	dot->i_size = inode->i_size; /* doesn't grow in the 2nd create_entry */
	MSDOS_I(dot)->i_start = MSDOS_I(inode)->i_start;
	dot->i_nlink = inode->i_nlink;
	dot->i_dirt = 1;
	iput(dot);
	if ((res = msdos_create_entry(inode,MSDOS_DOTDOT,2,1,0,&dot)) < 0)
		goto mkdir_error;
	fat_unlock_creation();
	dot->i_size = dir->i_size;
	MSDOS_I(dot)->i_start = MSDOS_I(dir)->i_start;
	dot->i_nlink = dir->i_nlink;
	dot->i_dirt = 1;
	MSDOS_I(inode)->i_busy = 0;
	iput(dot);
	iput(inode);
	iput(dir);
	return 0;
mkdir_error:
	iput(inode);
	if (msdos_rmdir(dir,name,len) < 0)
		fat_fs_panic(dir->i_sb,"rmdir in mkdir failed");
	fat_unlock_creation();
	return res;
}

/***** Unlink a file */
static int msdos_unlinkx(
	struct inode *dir,
	const char *name,
	int len,
	int nospc)	/* Flag special file ? */
{
	struct super_block *sb = dir->i_sb;
	int res,ino;
	struct buffer_head *bh;
	struct msdos_dir_entry *de;
	struct inode *inode;

	bh = NULL;
	inode = NULL;
	if ((res = msdos_find(dir,name,len,&bh,&de,&ino)) < 0)
		goto unlink_done;
	if (!(inode = iget(dir->i_sb,ino))) {
		res = -ENOENT;
		goto unlink_done;
	}
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
	inode->i_dirt = dir->i_dirt = 1;
	de->name[0] = DELETED_FLAG;
	fat_mark_buffer_dirty(sb, bh, 1);
unlink_done:
	fat_brelse(sb, bh);
	iput(inode);
	iput(dir);
	return res;
}

/***** Unlink, as called for msdosfs */
int msdos_unlink(struct inode *dir,const char *name,int len)
{
	return msdos_unlinkx (dir,name,len,1);
}

/***** Unlink, as called for umsdosfs */
int msdos_unlink_umsdos(struct inode *dir,const char *name,int len)
{
	return msdos_unlinkx (dir,name,len,0);
}

/***** Rename within a directory */
static int rename_same_dir(struct inode *old_dir,char *old_name,int old_len,
    struct inode *new_dir,char *new_name,int new_len,
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
		if (!(new_inode = iget(new_dir->i_sb,new_ino))) {
			fat_brelse(sb, new_bh);
			return -EIO;
		}
		error = S_ISDIR(new_inode->i_mode)
			? (old_de->attr & ATTR_DIR)
				? msdos_empty(new_inode)
				: -EPERM
			: (old_de->attr & ATTR_DIR)
				? -EPERM
				: 0;
		if (!error && (old_de->attr & ATTR_SYS)) error = -EPERM;
		if (error) {
			iput(new_inode);
			fat_brelse(sb, new_bh);
			return error;
		}
		if (S_ISDIR(new_inode->i_mode)) {
			new_dir->i_nlink--;
			new_dir->i_dirt = 1;
		}
		new_inode->i_nlink = 0;
		MSDOS_I(new_inode)->i_busy = 1;
		new_inode->i_dirt = 1;
		new_de->name[0] = DELETED_FLAG;
		fat_mark_buffer_dirty(sb, new_bh, 1);
		dcache_add(new_dir, new_name, new_len, new_ino);
		iput(new_inode);
		fat_brelse(sb, new_bh);
	}
	memcpy(old_de->name,new_name,MSDOS_NAME);
set_hid:
	old_de->attr = is_hid
		? (old_de->attr | ATTR_HIDDEN)
		: (old_de->attr &~ ATTR_HIDDEN);
	fat_mark_buffer_dirty(sb, old_bh, 1);
	/* update binary info for conversion, i_attrs */
	if ((old_inode = iget(old_dir->i_sb,old_ino)) != NULL) {
		MSDOS_I(old_inode)->i_attrs = is_hid
			? (MSDOS_I(old_inode)->i_attrs |  ATTR_HIDDEN)
			: (MSDOS_I(old_inode)->i_attrs &~ ATTR_HIDDEN);
		iput(old_inode);
	}
	return 0;
}

/***** Rename across directories - a nonphysical move */
static int rename_diff_dir(struct inode *old_dir,char *old_name,int old_len,
    struct inode *new_dir,char *new_name,int new_len,
    struct buffer_head *old_bh,
    struct msdos_dir_entry *old_de,int old_ino,int is_hid)
{
	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *new_bh,*free_bh,*dotdot_bh;
	struct msdos_dir_entry *new_de,*free_de,*dotdot_de;
	struct inode *old_inode,*new_inode,*free_inode,*dotdot_inode,*walk;
	int new_ino,free_ino,dotdot_ino;
	int error,exists,ino;

	if (old_dir->i_dev != new_dir->i_dev) return -EINVAL;
	if (old_ino == new_dir->i_ino) return -EINVAL;
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
	/* find free spot */
	while ((error = fat_scan(new_dir,NULL,&free_bh,&free_de,&free_ino,
	    SCAN_ANY)) < 0) {
		if (error != -ENOENT) return error;
		error = fat_add_cluster(new_dir);
		if (error) return error;
	}
	exists = fat_scan(new_dir,new_name,&new_bh,&new_de,&new_ino,SCAN_ANY) >= 0;
	if (!(old_inode = iget(old_dir->i_sb,old_ino))) {
		fat_brelse(sb, free_bh);
		if (exists)
			fat_brelse(sb, new_bh);
		return -EIO;
	}
	if (*(unsigned char *) old_de->name == DELETED_FLAG) {
		iput(old_inode);
		fat_brelse(sb, free_bh);
		if (exists)
			fat_brelse(sb, new_bh);
		return -ENOENT;
	}
	new_inode = NULL; /* to make GCC happy */
	if (exists) {  /* Trash the old file! */
		if (!(new_inode = iget(new_dir->i_sb,new_ino))) {
			iput(old_inode);
			fat_brelse(sb, new_bh);
			return -EIO;
		}
		error = S_ISDIR(new_inode->i_mode)
			? (old_de->attr & ATTR_DIR)
				? msdos_empty(new_inode)
				: -EPERM
			: (old_de->attr & ATTR_DIR)
				? -EPERM
				: 0;
		if (!error && (old_de->attr & ATTR_SYS)) error = -EPERM;
		if (error) {
			iput(new_inode);
			iput(old_inode);
			fat_brelse(sb, new_bh);
			return error;
		}
		new_inode->i_nlink = 0;
		MSDOS_I(new_inode)->i_busy = 1;
		new_inode->i_dirt = 1;
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
		if (exists) {
			iput(new_inode);
			fat_brelse(sb, new_bh);
		}
		return -EIO;
	}
	if (exists && S_ISDIR(new_inode->i_mode)) {
		new_dir->i_nlink--;
		new_dir->i_dirt = 1;
	}
	msdos_read_inode(free_inode);
	MSDOS_I(old_inode)->i_busy = 1;
	MSDOS_I(old_inode)->i_linked = free_inode;
	MSDOS_I(free_inode)->i_oldlink = old_inode;
	fat_cache_inval_inode(old_inode);
	old_inode->i_dirt = 1;
	old_de->name[0] = DELETED_FLAG;
	fat_mark_buffer_dirty(sb, old_bh, 1);
	fat_mark_buffer_dirty(sb, free_bh, 1);
	if (exists) {
		MSDOS_I(new_inode)->i_depend = free_inode;
		MSDOS_I(free_inode)->i_old = new_inode;
		/* Two references now exist to free_inode so increase count */
		free_inode->i_count++;
		/* free_inode is put after putting new_inode and old_inode */
		iput(new_inode);
		dcache_add(new_dir, new_name, new_len, new_ino);
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
		dotdot_inode->i_dirt = 1;
		fat_mark_buffer_dirty(sb, dotdot_bh, 1);
		old_dir->i_nlink--;
		new_dir->i_nlink++;
		/* no need to mark them dirty */
		dotdot_inode->i_nlink = new_dir->i_nlink;
		iput(dotdot_inode);
		fat_brelse(sb, dotdot_bh);
	}
	error = 0;
rename_done:
	fat_brelse(sb, free_bh);
	iput(old_inode);
	return error;
}

/***** Rename, a wrapper for rename_same_dir & rename_diff_dir */
int msdos_rename(struct inode *old_dir,const char *old_name,int old_len,
	struct inode *new_dir,const char *new_name,int new_len,
	int must_be_dir)
{
	struct super_block *sb = old_dir->i_sb;
	char old_msdos_name[MSDOS_NAME],new_msdos_name[MSDOS_NAME];
	struct buffer_head *old_bh;
	struct msdos_dir_entry *old_de;
	int old_ino,error;
	int is_hid,old_hid; /* if new file and old file are hidden */

	if ((error = msdos_format_name(MSDOS_SB(old_dir->i_sb)->options.name_check,
				       old_name,old_len,old_msdos_name,1,
				       MSDOS_SB(old_dir->i_sb)->options.dotsOK))
	    < 0) goto rename_done;
	if ((error = msdos_format_name(MSDOS_SB(new_dir->i_sb)->options.name_check,
				       new_name,new_len,new_msdos_name,0,
				       MSDOS_SB(new_dir->i_sb)->options.dotsOK))
	    < 0) goto rename_done;
	is_hid = (new_name[0]=='.') && (new_msdos_name[0]!='.');
	old_hid = (old_name[0]=='.') && (old_msdos_name[0]!='.');
	if ((error = fat_scan(old_dir,old_msdos_name,&old_bh,&old_de,
	    &old_ino,old_hid?SCAN_HID:SCAN_NOTHID)) < 0) goto rename_done;
	fat_lock_creation();
	if (old_dir == new_dir)
		error = rename_same_dir(old_dir,old_msdos_name,old_len,new_dir,
		    new_msdos_name,new_len,old_bh,old_de,old_ino,is_hid);
	else error = rename_diff_dir(old_dir,old_msdos_name,old_len,new_dir,
		    new_msdos_name,new_len,old_bh,old_de,old_ino,is_hid);
	fat_unlock_creation();
	fat_brelse(sb, old_bh);
rename_done:
	iput(old_dir);
	iput(new_dir);
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
	NULL			/* permission */
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

