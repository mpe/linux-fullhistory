/*
 *  linux/fs/affs/namei.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */

#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/fcntl.h>
#include <linux/locks.h>
#include <linux/amigaffs.h>
#include <asm/segment.h>

#include <linux/errno.h>

/* Simple toupper() for DOS\1 */

static inline unsigned int
affs_toupper(unsigned int ch)
{
	return ch >= 'a' && ch <= 'z' ? ch -= ('a' - 'A') : ch;
}

/* International toupper() for DOS\3 */

static inline unsigned int
affs_intl_toupper(unsigned int ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 0xE0
		&& ch <= 0xFE && ch != 0xF7) ?
		ch - ('a' - 'A') : ch;
}

/*
 * NOTE! unlike strncmp, affs_match returns 1 for success, 0 for failure.
 */

static int
affs_match(const char *name, int len, const char *compare, int dlen, int intl)
{
	if (!compare)
		return 0;

	if (len > 30)
		len = 30;
	if (dlen > 30)
		dlen = 30;

	/* "" means "." ---> so paths like "/usr/lib//libc.a" work */
	if (!len && dlen == 1 && compare[0] == '.')
		return 1;
	if (dlen != len)
		return 0;
	if (intl) {
		while (dlen--) {
			if (affs_intl_toupper(*name & 0xFF) != affs_intl_toupper(*compare & 0xFF))
				return 0;
			name++;
			compare++;
		}
	} else {
		while (dlen--) {
			if (affs_toupper(*name & 0xFF) != affs_toupper(*compare & 0xFF))
				return 0;
			name++;
			compare++;
		}
	}
	return 1;
}

int
affs_hash_name(const char *name, int len, int intl, int hashsize)
{
	unsigned int i, x;

	if (len > 30)
		len = 30;

	x = len;
	for (i = 0; i < len; i++)
		if (intl)
			x = (x * 13 + affs_intl_toupper(name[i] & 0xFF)) & 0x7ff;
		else
			x = (x * 13 + affs_toupper(name[i] & 0xFF)) & 0x7ff;

	return x % hashsize;
}

static struct buffer_head *
affs_find_entry(struct inode *dir, const char *name, int namelen,
		unsigned long *ino)
{
	struct buffer_head *bh;
	int	 intl;
	int	 key;

	pr_debug("AFFS: find_entry(%.*s)=\n",namelen,name);

	intl = AFFS_I2FSTYPE(dir);
	bh   = affs_bread(dir->i_dev,dir->i_ino,AFFS_I2BSIZE(dir));
	if (!bh)
		return NULL;

	if (affs_match(name,namelen,".",1,intl)) {
		*ino = dir->i_ino;
		return bh;
	}
	if (affs_match(name,namelen,"..",2,intl)) {
		*ino = affs_parent_ino(dir);
		return bh;
	}

	key = AFFS_GET_HASHENTRY(bh->b_data,affs_hash_name(name,namelen,intl,AFFS_I2HSIZE(dir)));

	for (;;) {
		char *cname;
		int cnamelen;

		affs_brelse(bh);
		if (key == 0) {
			bh = NULL;
			break;
		}
		bh = affs_bread(dir->i_dev,key,AFFS_I2BSIZE(dir));
		if (!bh)
			break;
		cnamelen = affs_get_file_name(AFFS_I2BSIZE(dir),bh->b_data,&cname);
		if (affs_match(name,namelen,cname,cnamelen,intl))
			break;
		key = htonl(FILE_END(bh->b_data,dir)->hash_chain);
	}
	*ino = key;
	return bh;
}

int
affs_lookup(struct inode *dir, const char *name, int len, struct inode **result)
{
	int res;
	unsigned long ino;
	struct buffer_head *bh;

	pr_debug("AFFS: lookup(%.*s)\n",len,name);

	*result = NULL;
	if (!dir)
		return -ENOENT;

	res = -ENOENT;
	if (S_ISDIR(dir->i_mode)) {
		if ((bh = affs_find_entry(dir,name,len,&ino))) {
			if (FILE_END(bh->b_data,dir)->original)
				ino = htonl(FILE_END(bh->b_data,dir)->original);
			affs_brelse(bh);
			if ((*result = iget(dir->i_sb,ino))) 
				res = 0;
			else
				res = -EACCES;
		}
	}
	iput(dir);
	return res;
}

int
affs_unlink(struct inode *dir, const char *name, int len)
{
	int			 retval;
	struct buffer_head	*bh;
	unsigned long		 ino;
	struct inode		*inode;

	pr_debug("AFFS: unlink(dir=%ld,\"%.*s\")\n",dir->i_ino,len,name);

	bh      = NULL;
	inode   = NULL;
	retval  = -ENOENT;
	if (!(bh = affs_find_entry(dir,name,len,&ino))) {
		goto unlink_done;
	}
	if (!(inode = iget(dir->i_sb,ino))) {
		goto unlink_done;
	}
	if (S_ISDIR(inode->i_mode)) {
		retval = -EPERM;
		goto unlink_done;
	}

	if ((retval = affs_fix_hash_pred(dir,affs_hash_name(name,len,AFFS_I2FSTYPE(dir),
					 AFFS_I2HSIZE(dir)) + 6,ino,
					 FILE_END(bh->b_data,dir)->hash_chain)))
		goto unlink_done;

	if ((retval = affs_fixup(bh,inode)))
		goto unlink_done;

	inode->i_nlink=0;
	inode->i_dirt=1;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_version = ++event;
	dir->i_dirt=1;
unlink_done:
	affs_brelse(bh);
	iput(inode);
	iput(dir);
	return retval;
}

int
affs_create(struct inode *dir, const char *name, int len, int mode, struct inode **result)
{
	struct inode	*inode;
	int		 error;
	
	pr_debug("AFFS: create(%lu,\"%.*s\",0%o)\n",dir->i_ino,len,name,mode);


	*result = NULL;

	if (!dir || !dir->i_sb) {
		iput(dir);
		return -EINVAL;
	}
	inode = affs_new_inode(dir);
	if (!inode) {
		iput (dir);
		return -ENOSPC;
	}
	inode->i_mode = mode;
	if (dir->i_sb->u.affs_sb.s_flags & SF_OFS)
		inode->i_op = &affs_file_inode_operations_ofs;
	else
		inode->i_op = &affs_file_inode_operations;

	error = affs_add_entry(dir,NULL,inode,name,len,ST_FILE);
	if (error) {
		iput(dir);
		inode->i_nlink = 0;
		inode->i_dirt  = 1;
		iput(inode);
		return -ENOSPC;
	}
	inode->u.affs_i.i_protect = mode_to_prot(inode->i_mode);

	iput(dir);
	*result = inode;

	return 0;
}

int
affs_mkdir(struct inode *dir, const char *name, int len, int mode)
{
	struct inode		*inode;
	struct buffer_head	*bh;
	unsigned long		 i;
	int			 error;
	
	pr_debug("AFFS: mkdir(%lu,\"%.*s\",0%o)\n",dir->i_ino,len,name,mode);

	if (!dir || !dir->i_sb) {
		iput(dir);
		return -EINVAL;
	}
	bh = affs_find_entry(dir,name,len,&i);
	if (bh) {
		affs_brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	inode = affs_new_inode(dir);
	if (!inode) {
		iput (dir);
		return -ENOSPC;
	}
	inode->i_op = &affs_dir_inode_operations;
	error       = affs_add_entry(dir,NULL,inode,name,len,ST_USERDIR);
	if (error) {
		iput(dir);
		inode->i_nlink = 0;
		inode->i_dirt  = 1;
		iput(inode);
		return error;
	}
	inode->i_mode = S_IFDIR | (mode & 0777 & ~current->fs->umask);
	inode->u.affs_i.i_protect = mode_to_prot(inode->i_mode);

	iput(dir);
	iput(inode);

	return 0;
}

static int
empty_dir(struct buffer_head *bh, int hashsize)
{
	while (--hashsize >= 0) {
		if (((struct dir_front *)bh->b_data)->hashtable[hashsize])
			return 0;
	}
	return 1;
}

int
affs_rmdir(struct inode *dir, const char *name, int len)
{
	int			 retval;
	unsigned long		 ino;
	struct inode		*inode;
	struct buffer_head	*bh;

	pr_debug("AFFS: rmdir(dir=%lu,\"%.*s\")\n",dir->i_ino,len,name);

	inode  = NULL;
	retval = -ENOENT;
	if (!(bh = affs_find_entry(dir,name,len,&ino))) {
		goto rmdir_done;
	}
	if (!(inode = iget(dir->i_sb,ino))) {
		goto rmdir_done;
	}
	retval = -EPERM;
        if (!fsuser() && current->fsuid != inode->i_uid &&
            current->fsuid != dir->i_uid)
		goto rmdir_done;
	if (inode->i_dev != dir->i_dev)
		goto rmdir_done;
	if (inode == dir)	/* we may not delete ".", but "../dir" is ok */
		goto rmdir_done;
	if (!S_ISDIR(inode->i_mode)) {
		retval = -ENOTDIR;
		goto rmdir_done;
	}
	if (!empty_dir(bh,AFFS_I2HSIZE(inode))) {
		retval = -ENOTEMPTY;
		goto rmdir_done;
	}
	if (inode->i_count > 1) {
		retval = -EBUSY;
		goto rmdir_done;
	}
	if ((retval = affs_fix_hash_pred(dir,affs_hash_name(name,len,AFFS_I2FSTYPE(dir),
					 AFFS_I2HSIZE(dir)) + 6,ino,
					 FILE_END(bh->b_data,dir)->hash_chain)))
		goto rmdir_done;

	if ((retval = affs_fixup(bh,inode)))
		goto rmdir_done;

	inode->i_nlink=0;
	inode->i_dirt=1;
	inode->i_ctime = dir->i_ctime = dir->i_mtime = CURRENT_TIME;
	dir->i_version = ++event;
	dir->i_dirt=1;
rmdir_done:
	iput(dir);
	iput(inode);
	affs_brelse(bh);
	return retval;
}

int
affs_symlink(struct inode *dir, const char *name, int len, const char *symname)
{
	struct buffer_head	*bh;
	struct inode		*inode;
	char			*p;
	unsigned long		 tmp;
	int			 i, maxlen;
	char			 c, lc;

	pr_debug("AFFS: symlink(%lu,\"%.*s\" -> \"%s\")\n",dir->i_ino,len,name,symname);
	
	maxlen = 4 * AFFS_I2HSIZE(dir) - 1;
	inode  = affs_new_inode(dir);
	if (!inode) {
		iput(dir);
		return -ENOSPC;
	}
	inode->i_op   = &affs_symlink_inode_operations;
	inode->i_mode = S_IFLNK | 0777;
	inode->u.affs_i.i_protect = mode_to_prot(inode->i_mode);
	bh = affs_bread(inode->i_dev,inode->i_ino,AFFS_I2BSIZE(inode));
	if (!bh) {
		iput(dir);
		inode->i_nlink = 0;
		inode->i_dirt  = 1;
		iput(inode);
		return -EIO;
	}
	i  = 0;
	p  = ((struct slink_front *)bh->b_data)->symname;
	lc = '/';
	if (*symname == '/') {
		while (*symname == '/')
			symname++;
		while (inode->i_sb->u.affs_sb.s_volume[i])	/* Cannot overflow */
			*p++ = inode->i_sb->u.affs_sb.s_volume[i++];
	}
	while (i < maxlen && (c = *symname++)) {
		if (c == '.' && lc == '/' && *symname == '.' && symname[1] == '/') {
			*p++ = '/';
			i++;
			symname += 2;
			lc = '/';
		} else if (c == '.' && lc == '/' && *symname == '/') {
			symname++;
			lc = '/';
		} else {
			*p++ = c;
			lc   = c;
			i++;
		}
		if (lc == '/')
			while (*symname == '/')
				symname++;
	}
	*p = 0;
	mark_buffer_dirty(bh,1);
	affs_brelse(bh);
	inode->i_dirt = 1;
	bh = affs_find_entry(dir,name,len,&tmp);
	if (bh) {
		inode->i_nlink = 0;
		iput(inode);
		affs_brelse(bh);
		iput(dir);
		return -EEXIST;
	}
	i = affs_add_entry(dir,NULL,inode,name,len,ST_SOFTLINK);
	if (i) {
		inode->i_nlink = 0;
		inode->i_dirt  = 1;
		iput(inode);
		affs_brelse(bh);
		iput(dir);
		return i;
	}
	iput(dir);
	iput(inode);
	
	return 0;
}

int
affs_link(struct inode *oldinode, struct inode *dir, const char *name, int len)
{
	struct inode		*inode;
	struct buffer_head	*bh;
	unsigned long		 i;
	int			 error;
	
	pr_debug("AFFS: link(%lu,%lu,\"%.*s\")\n",oldinode->i_ino,dir->i_ino,len,name);

	bh = affs_find_entry(dir,name,len,&i);
	if (bh) {
		affs_brelse(bh);
		iput(oldinode);
		iput(dir);
		return -EEXIST;
	}
	if (oldinode->u.affs_i.i_hlink) {
		i = oldinode->u.affs_i.i_original;
		iput(oldinode);
		oldinode = iget(dir->i_sb,i);
		if (!oldinode) {
			printk("AFFS: link(): original does not exist.\n");
			iput(dir);
			return -ENOENT;
		}
	}
	inode = affs_new_inode(dir);
	if (!inode) {
		iput(oldinode);
		iput(dir);
		return -ENOSPC;
	}
	inode->i_op                = oldinode->i_op;
	inode->i_mode              = oldinode->i_mode;
	inode->i_uid               = oldinode->i_uid;
	inode->i_gid               = oldinode->i_gid;
	inode->u.affs_i.i_protect  = mode_to_prot(inode->i_mode);
	inode->u.affs_i.i_original = oldinode->i_ino;
	inode->u.affs_i.i_hlink    = 1;

	if (S_ISDIR(oldinode->i_mode))
		error = affs_add_entry(dir,oldinode,inode,name,len,ST_LINKDIR);
	else
		error = affs_add_entry(dir,oldinode,inode,name,len,ST_LINKFILE);
	if (error) {
		inode->i_nlink = 0;
		inode->i_dirt  = 1;
	}
	iput(dir);
	iput(inode);
	iput(oldinode);

	return error;
}

static int
subdir(struct inode * new_inode, struct inode * old_inode)
{
    int ino;
    int result;

    new_inode->i_count++;
    result = 0;
    for (;;) {
        if (new_inode == old_inode) {
	    result = 1;
	    break;
	}
	if (new_inode->i_dev != old_inode->i_dev)
	    break;
	ino = new_inode->i_ino;
	if (affs_lookup(new_inode,"..",2,&new_inode))
	    break;
	if (new_inode->i_ino == ino)
	    break;
    }
    iput(new_inode);
    return result;
}

/* I'm afraid this might not be race proof. Maybe next time. */

int
affs_rename(struct inode *old_dir, const char *old_name, int old_len,
	    struct inode *new_dir, const char *new_name, int new_len,
	    int must_be_dir)
{
	struct inode		*old_inode;
	struct inode		*new_inode;
	struct buffer_head	*old_bh;
	struct buffer_head	*new_bh;
	unsigned long		 old_ino;
	unsigned long		 new_ino;
	int			 retval;

	pr_debug("AFFS: rename(old=%lu,\"%*s\" to new=%lu,\"%*s\")\n",old_dir->i_ino,old_len,old_name,
		 new_dir->i_ino,new_len,new_name);
	
	if (new_len > 30)
		new_len = 30;
	goto start_up;
retry:
	affs_brelse(old_bh);
	affs_brelse(new_bh);
	iput(new_inode);
	iput(old_inode);
	current->counter = 0;
	schedule();
start_up:
	old_inode = new_inode = NULL;
	old_bh    = new_bh = NULL;
	retval    = -ENOENT;

	old_bh = affs_find_entry(old_dir,old_name,old_len,&old_ino);
	if (!old_bh)
		goto end_rename;
	old_inode = __iget(old_dir->i_sb,old_ino,0);
	if (!old_inode)
		goto end_rename;
	if (must_be_dir && !S_ISDIR(old_inode->i_mode))
		goto end_rename;
	new_bh = affs_find_entry(new_dir,new_name,new_len,&new_ino);
	if (new_bh) {
		new_inode = __iget(new_dir->i_sb,new_ino,0);
		if (!new_inode) {		/* What does this mean? */
			affs_brelse(new_bh);
			new_bh = NULL;
		}
	}
	if (new_inode == old_inode) {		/* Won't happen */
		retval = 0;
		goto end_rename;
	}
	if (new_inode && S_ISDIR(new_inode->i_mode)) {
		retval = -EISDIR;
		if (!S_ISDIR(old_inode->i_mode))
			goto end_rename;
		retval = -EINVAL;
		if (subdir(new_dir,old_inode))
			goto end_rename;
		retval = -ENOTEMPTY;
		if (!empty_dir(new_bh,AFFS_I2HSIZE(new_inode)))
			goto end_rename;
		retval = -EBUSY;
		if (new_inode->i_count > 1)
			goto end_rename;
	}
	if (S_ISDIR(old_inode->i_mode)) {
		retval = -ENOTDIR;
		if (new_inode && !S_ISDIR(new_inode->i_mode))
			goto end_rename;
		retval = -EINVAL;
		if (subdir(new_dir,old_inode))
			goto end_rename;
		if (affs_parent_ino(old_inode) != old_dir->i_ino)
			goto end_rename;
	}
	/* Unlink destination if existent */
	if (new_inode) {
		if ((retval = affs_fix_hash_pred(new_dir,affs_hash_name(new_name,new_len,
		                                 AFFS_I2FSTYPE(new_dir),AFFS_I2HSIZE(new_dir)) + 6,
						 new_ino,
						 FILE_END(new_bh->b_data,new_dir)->hash_chain)))
			goto retry;
		if ((retval = affs_fixup(new_bh,new_inode)))
			goto retry;
		mark_buffer_dirty(new_bh,1);
		new_dir->i_version = ++event;
		new_dir->i_dirt    = 1;
		new_inode->i_nlink = 0;
		new_inode->i_dirt  = 1;
	}
	retval = affs_fix_hash_pred(old_dir,affs_hash_name(old_name,old_len,AFFS_I2FSTYPE(old_dir),
				    AFFS_I2HSIZE(old_dir)) + 6,old_ino,
				    FILE_END(old_bh->b_data,old_dir)->hash_chain);
	if (retval)
		goto retry;

	retval = affs_add_entry(new_dir,NULL,old_inode,new_name,new_len,
				htonl(FILE_END(old_bh->b_data,old_dir)->secondary_type));

	new_dir->i_ctime   = new_dir->i_mtime = old_dir->i_ctime = old_dir->i_mtime = CURRENT_TIME;
	new_dir->i_version = ++event;
	old_dir->i_version = ++event;
	new_dir->i_dirt    = 1;
	old_dir->i_dirt    = 1;
	mark_buffer_dirty(old_bh,1);
	
end_rename:
	affs_brelse(old_bh);
	affs_brelse(new_bh);
	iput(new_inode);
	iput(old_inode);
	iput(old_dir);
	iput(new_dir);

	return retval;
}

int
affs_fixup(struct buffer_head *bh, struct inode *inode)
{
	int			 key, link_key;
	int			 type;
	struct buffer_head	*nbh;
	struct inode		*ofinode;

	type = htonl(FILE_END(bh->b_data,inode)->secondary_type);
	if (type == ST_LINKFILE || type == ST_LINKDIR) {
		key = htonl(LINK_END(bh->b_data,inode)->original);
		LINK_END(bh->b_data,inode)->original = 0;
		if (!key) {
			printk("AFFS: fixup(): hard link without original: ino=%lu\n",inode->i_ino);
			return -ENOENT;
		}
		if (!(ofinode = iget(inode->i_sb,key)))
			return -ENOENT;
		type = affs_fix_link_pred(ofinode,inode->i_ino,
					  FILE_END(bh->b_data,inode)->link_chain);
		iput(ofinode);
		return type;
	} else if (type == ST_FILE || type == ST_USERDIR) {
		if ((key = htonl(FILE_END(bh->b_data,inode)->link_chain))) {
			/* Get first link, turn it to a file */
			if (!(ofinode = iget(inode->i_sb,key))) {
				printk("AFFS: fixup(): cannot read inode %u\n",key);
				return -ENOENT;
			}
			if (!ofinode->u.affs_i.i_hlink) {
				printk("AFFS: fixup(): first link to %lu (%u) is not a link?\n",
					inode->i_ino,key);
				iput(ofinode);
				return -ENOENT;
			}
			if (!(nbh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode)))) {
				printk("AFFS: fixup(): cannot read block %u\n",key);
				iput(ofinode);
				return -ENOENT;
			}
			lock_super(inode->i_sb);
			memcpy(nbh->b_data + 8,bh->b_data + 8,AFFS_I2BSIZE(inode) - 208);
			FILE_END(nbh->b_data,inode)->byte_size = FILE_END(bh->b_data,inode)->
									  byte_size;
			FILE_END(nbh->b_data,inode)->extension = FILE_END(bh->b_data,inode)->
									  extension;
			FILE_END(nbh->b_data,inode)->secondary_type = FILE_END(bh->b_data,inode)->
									  secondary_type;
			FILE_END(nbh->b_data,inode)->original = 0;

			ofinode->u.affs_i.i_original = 0;
			ofinode->u.affs_i.i_hlink    = 0;
			ofinode->i_size              = inode->i_size;
			ofinode->i_uid               = inode->i_uid;
			ofinode->i_gid               = inode->i_gid;
			ofinode->i_dirt              = 1;
			link_key                     = ofinode->i_ino;

			/* Let all remaining links point to the new file */
			while (1) {
				affs_fix_checksum(AFFS_I2BSIZE(inode),nbh->b_data,5);
				mark_buffer_dirty(nbh,1);
				key = htonl(FILE_END(nbh->b_data,inode)->link_chain);
				affs_brelse(nbh);
				iput(ofinode);
				if (!key || !(nbh = affs_bread(inode->i_dev,key,AFFS_I2BSIZE(inode))))
					break;
				if ((ofinode = iget(inode->i_sb,key))) {
					if (!ofinode->u.affs_i.i_hlink)
						printk("AFFS: fixup() inode %u in link chain is "
						       "not a link\n",key);
					ofinode->u.affs_i.i_original = link_key;
					ofinode->i_dirt              = 1;
					FILE_END(nbh->b_data,inode)->original = htonl(link_key);
				} else
					printk("AFFS: fixup(): cannot get inode %u\n",key);
			}
			/* Turn old inode to a link */
			inode->u.affs_i.i_hlink = 1;
			unlock_super(inode->i_sb);
		}
		return 0;
	} else if (type == ST_SOFTLINK) {
		return 0;
	} else {
		printk("AFFS: fixup(): secondary type=%d\n",type);
		return -EBADF;
	}
}
