/*
 *	fs/libfs.c
 *	Library for filesystems writers.
 */

#include <linux/pagemap.h>
#include <linux/smp_lock.h>

int simple_statfs(struct super_block *sb, struct statfs *buf)
{
	buf->f_type = sb->s_magic;
	buf->f_bsize = PAGE_CACHE_SIZE;
	buf->f_namelen = NAME_MAX;
	return 0;
}

/*
 * Lookup the data. This is trivial - if the dentry didn't already
 * exist, we know it is negative.
 */

struct dentry *simple_lookup(struct inode *dir, struct dentry *dentry)
{
	d_add(dentry, NULL);
	return NULL;
}

int simple_sync_file(struct file * file, struct dentry *dentry, int datasync)
{
	return 0;
}
 
int dcache_dir_open(struct inode *inode, struct file *file)
{
	static struct qstr cursor_name = {.len = 1, .name = "."};

	file->private_data = d_alloc(file->f_dentry, &cursor_name);

	return file->private_data ? 0 : -ENOMEM;
}

int dcache_dir_close(struct inode *inode, struct file *file)
{
	dput(file->private_data);
	return 0;
}

loff_t dcache_dir_lseek(struct file *file, loff_t offset, int origin)
{
	down(&file->f_dentry->d_inode->i_sem);
	switch (origin) {
		case 1:
			offset += file->f_pos;
		case 0:
			if (offset >= 0)
				break;
		default:
			up(&file->f_dentry->d_inode->i_sem);
			return -EINVAL;
	}
	if (offset != file->f_pos) {
		file->f_pos = offset;
		if (file->f_pos >= 2) {
			struct list_head *p;
			struct dentry *cursor = file->private_data;
			loff_t n = file->f_pos - 2;

			spin_lock(&dcache_lock);
			p = file->f_dentry->d_subdirs.next;
			while (n && p != &file->f_dentry->d_subdirs) {
				struct dentry *next;
				next = list_entry(p, struct dentry, d_child);
				if (!list_empty(&next->d_hash) && next->d_inode)
					n--;
				p = p->next;
			}
			list_del(&cursor->d_child);
			list_add_tail(&cursor->d_child, p);
			spin_unlock(&dcache_lock);
		}
	}
	up(&file->f_dentry->d_inode->i_sem);
	return offset;
}

/* Relationship between i_mode and the DT_xxx types */
static inline unsigned char dt_type(struct inode *inode)
{
	return (inode->i_mode >> 12) & 15;
}

/*
 * Directory is locked and all positive dentries in it are safe, since
 * for ramfs-type trees they can't go away without unlink() or rmdir(),
 * both impossible due to the lock on directory.
 */

int dcache_readdir(struct file * filp, void * dirent, filldir_t filldir)
{
	struct dentry *dentry = filp->f_dentry;
	struct dentry *cursor = filp->private_data;
	struct list_head *p, *q = &cursor->d_child;
	ino_t ino;
	int i = filp->f_pos;

	switch (i) {
		case 0:
			ino = dentry->d_inode->i_ino;
			if (filldir(dirent, ".", 1, i, ino, DT_DIR) < 0)
				break;
			filp->f_pos++;
			i++;
			/* fallthrough */
		case 1:
			ino = parent_ino(dentry);
			if (filldir(dirent, "..", 2, i, ino, DT_DIR) < 0)
				break;
			filp->f_pos++;
			i++;
			/* fallthrough */
		default:
			spin_lock(&dcache_lock);
			if (filp->f_pos == 2) {
				list_del(q);
				list_add(q, &dentry->d_subdirs);
			}
			for (p=q->next; p != &dentry->d_subdirs; p=p->next) {
				struct dentry *next;
				next = list_entry(p, struct dentry, d_child);
				if (list_empty(&next->d_hash) || !next->d_inode)
					continue;

				spin_unlock(&dcache_lock);
				if (filldir(dirent, next->d_name.name, next->d_name.len, filp->f_pos, next->d_inode->i_ino, dt_type(next->d_inode)) < 0)
					return 0;
				spin_lock(&dcache_lock);
				/* next is still alive */
				list_del(q);
				list_add(q, p);
				p = q;
				filp->f_pos++;
			}
			spin_unlock(&dcache_lock);
	}
	return 0;
}

ssize_t generic_read_dir(struct file *filp, char *buf, size_t siz, loff_t *ppos)
{
	return -EISDIR;
}

struct file_operations simple_dir_operations = {
	.open		= dcache_dir_open,
	.release	= dcache_dir_close,
	.llseek		= dcache_dir_lseek,
	.read		= generic_read_dir,
	.readdir	= dcache_readdir,
};

struct inode_operations simple_dir_inode_operations = {
	.lookup		= simple_lookup,
};

/*
 * Common helper for pseudo-filesystems (sockfs, pipefs, bdev - stuff that
 * will never be mountable)
 */
struct super_block *
get_sb_pseudo(struct file_system_type *fs_type, char *name,
	struct super_operations *ops, unsigned long magic)
{
	struct super_block *s = sget(fs_type, NULL, set_anon_super, NULL);
	static struct super_operations default_ops = {.statfs = simple_statfs};
	struct dentry *dentry;
	struct inode *root;
	struct qstr d_name = {.name = name, .len = strlen(name)};

	if (IS_ERR(s))
		return s;

	s->s_flags = MS_NOUSER;
	s->s_maxbytes = ~0ULL;
	s->s_blocksize = 1024;
	s->s_blocksize_bits = 10;
	s->s_magic = magic;
	s->s_op = ops ? ops : &default_ops;
	root = new_inode(s);
	if (!root)
		goto Enomem;
	root->i_mode = S_IFDIR | S_IRUSR | S_IWUSR;
	root->i_uid = root->i_gid = 0;
	root->i_atime = root->i_mtime = root->i_ctime = CURRENT_TIME;
	dentry = d_alloc(NULL, &d_name);
	if (!dentry) {
		iput(root);
		goto Enomem;
	}
	dentry->d_sb = s;
	dentry->d_parent = dentry;
	d_instantiate(dentry, root);
	s->s_root = dentry;
	s->s_flags |= MS_ACTIVE;
	return s;

Enomem:
	up_write(&s->s_umount);
	deactivate_super(s);
	return ERR_PTR(-ENOMEM);
}

int simple_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;

	inode->i_nlink++;
	atomic_inc(&inode->i_count);
	dget(dentry);
	d_instantiate(dentry, inode);
	return 0;
}

static inline int simple_positive(struct dentry *dentry)
{
	return dentry->d_inode && !d_unhashed(dentry);
}

int simple_empty(struct dentry *dentry)
{
	struct dentry *child;
	int ret = 0;

	spin_lock(&dcache_lock);
	list_for_each_entry(child, &dentry->d_subdirs, d_child)
		if (simple_positive(child))
			goto out;
	ret = 1;
out:
	spin_unlock(&dcache_lock);
	return ret;
}

int simple_unlink(struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = dentry->d_inode;

	inode->i_nlink--;
	dput(dentry);
	return 0;
}

int simple_rmdir(struct inode *dir, struct dentry *dentry)
{
	if (!simple_empty(dentry))
		return -ENOTEMPTY;

	dentry->d_inode->i_nlink--;
	simple_unlink(dir, dentry);
	dir->i_nlink--;
	return 0;
}

int simple_rename(struct inode *old_dir, struct dentry *old_dentry, struct inode *new_dir, struct dentry *new_dentry)
{
	struct inode *inode;

	if (!simple_empty(new_dentry))
		return -ENOTEMPTY;

	inode = new_dentry->d_inode;
	if (inode) {
		inode->i_nlink--;
		dput(new_dentry);
	}
	if (S_ISDIR(old_dentry->d_inode->i_mode)) {
		old_dir->i_nlink--;
		new_dir->i_nlink++;
	}
	return 0;
}
