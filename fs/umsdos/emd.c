/*
 *  linux/fs/umsdos/emd.c
 *
 *  Written 1993 by Jacques Gelinas
 *
 *  Extended MS-DOS directory handling functions
 */

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/msdos_fs.h>
#include <linux/umsdos_fs.h>
#include <linux/dcache.h>

#include <asm/uaccess.h>

#include <asm/delay.h>


/*
 *    Read a file into kernel space memory
 *      returns how many bytes read (from fat_file_read)
 */

ssize_t umsdos_file_read_kmem (	struct file *filp,
				char *buf,
				size_t count)
{
	ssize_t ret;
	mm_segment_t old_fs = get_fs ();

	set_fs (KERNEL_DS);
	MSDOS_I (filp->f_dentry->d_inode)->i_binary = 2;
	ret = fat_file_read (filp, buf, count, &filp->f_pos);
	set_fs (old_fs);
	return ret;
}


/*
 *    Write to file from kernel space. 
 *      Does the real job, assumes all structures are initialized!
 */


ssize_t umsdos_file_write_kmem_real (struct file * filp,
				     const char *buf,
				     size_t count)
{
	mm_segment_t old_fs = get_fs ();
	ssize_t ret;

	/* note: i_binary=2 is for CVF-FAT. We put it here, instead of
	 * umsdos_file_write_kmem, since it is also wise not to compress
	 * symlinks (in the unlikely event that they are > 512 bytes and
	 * can be compressed.
	 * FIXME: should we set it when reading symlinks too?
	 */

	MSDOS_I (filp->f_dentry->d_inode)->i_binary = 2;

	set_fs (KERNEL_DS);
	ret = fat_file_write (filp, buf, count, &filp->f_pos);
	set_fs (old_fs);
	if (ret < 0) {
		printk(KERN_WARNING "umsdos_file_write: ret=%d\n", ret);
		goto out;
	}
#ifdef UMSDOS_PARANOIA
if (ret != count)
printk(KERN_WARNING "umsdos_file_write: count=%u, ret=%u\n", count, ret);
#endif
out:
	return ret;
}


/*
 *    Write to a file from kernel space.
 */

ssize_t umsdos_file_write_kmem (struct file *filp,
				const char *buf,
				size_t count)
{
	ssize_t ret;

	ret = umsdos_file_write_kmem_real (filp, buf, count);
	return ret;
}



/*
 * Write a block of bytes into one EMD file.
 * The block of data is NOT in user space.
 * 
 * Return 0 if OK, a negative error code if not.
 *
 * Note: buffer is in kernel memory, not in user space.
 */

ssize_t umsdos_emd_dir_write (	struct file *filp,
				char *buf,	
				size_t count)
{
	int written;

#ifdef __BIG_ENDIAN
	struct umsdos_dirent *d = (struct umsdos_dirent *) buf;

	d->nlink = cpu_to_le16 (d->nlink);
	d->uid = cpu_to_le16 (d->uid);
	d->gid = cpu_to_le16 (d->gid);
	d->atime = cpu_to_le32 (d->atime);
	d->mtime = cpu_to_le32 (d->mtime);
	d->ctime = cpu_to_le32 (d->ctime);
	d->rdev = cpu_to_le16 (d->rdev);
	d->mode = cpu_to_le16 (d->mode);
#endif

	filp->f_flags = 0;
Printk (("umsdos_emd_dir_write /mn/: calling write_kmem with %p, %p, %d, %Ld\n",
filp, buf, count, filp->f_pos));
	written = umsdos_file_write_kmem (filp, buf, count);

#ifdef __BIG_ENDIAN
	d->nlink = le16_to_cpu (d->nlink);
	d->uid = le16_to_cpu (d->uid);
	d->gid = le16_to_cpu (d->gid);
	d->atime = le32_to_cpu (d->atime);
	d->mtime = le32_to_cpu (d->mtime);
	d->ctime = le32_to_cpu (d->ctime);
	d->rdev = le16_to_cpu (d->rdev);
	d->mode = le16_to_cpu (d->mode);
#endif

#ifdef UMSDOS_PARANOIA
if (written != count)
printk(KERN_ERR "umsdos_emd_dir_write: ERROR: written (%d) != count (%d)\n",
written, count);
#endif

	return (written != count) ? -EIO : 0;
}



/*
 *      Read a block of bytes from one EMD file.
 *      The block of data is NOT in user space.
 *      Return 0 if OK, -EIO if any error.
 */
/* buffer in kernel memory, not in user space */

ssize_t umsdos_emd_dir_read (struct file *filp, char *buf, size_t count)
{
	ssize_t sizeread, ret = 0;

#ifdef __BIG_ENDIAN
	struct umsdos_dirent *d = (struct umsdos_dirent *) buf;

#endif

	filp->f_flags = 0;
	sizeread = umsdos_file_read_kmem (filp, buf, count);
	if (sizeread != count) {
		printk (KERN_WARNING 
			"UMSDOS: EMD problem, pos=%Ld, count=%d, read=%d\n",
			filp->f_pos, count, sizeread);
		ret = -EIO;
	}
#ifdef __BIG_ENDIAN
	d->nlink = le16_to_cpu (d->nlink);
	d->uid = le16_to_cpu (d->uid);
	d->gid = le16_to_cpu (d->gid);
	d->atime = le32_to_cpu (d->atime);
	d->mtime = le32_to_cpu (d->mtime);
	d->ctime = le32_to_cpu (d->ctime);
	d->rdev = le16_to_cpu (d->rdev);
	d->mode = le16_to_cpu (d->mode);
#endif
	return ret;
}


/*
 * Lookup the EMD dentry for a directory.
 *
 * Note: the caller must hold a lock on the parent directory.
 */
struct dentry *umsdos_get_emd_dentry(struct dentry *parent)
{
	struct dentry *demd;

	demd = umsdos_lookup_dentry(parent, UMSDOS_EMD_FILE, 
					UMSDOS_EMD_NAMELEN, 1);
	return demd;
}

/*
 * Check whether a directory has an EMD file.
 *
 * Note: the caller must hold a lock on the parent directory.
 */
int umsdos_have_emd(struct dentry *dir)
{
	struct dentry *demd = umsdos_get_emd_dentry (dir);
	int found = 0;

	if (!IS_ERR(demd)) {
		if (demd->d_inode)
			found = 1;
		dput(demd);
	}
	return found;
}

/*
 * Create the EMD file for a directory if it doesn't
 * already exist. Returns 0 or an error code.
 *
 * Note: the caller must hold a lock on the parent directory.
 */
int umsdos_make_emd(struct dentry *parent)
{
	struct dentry *demd = umsdos_get_emd_dentry(parent);
	int err = PTR_ERR(demd);

	if (IS_ERR(demd)) {
		printk("umsdos_make_emd: can't get dentry in %s, err=%d\n",
			parent->d_name.name, err);
		goto out;
	}

	/* already created? */
	err = 0;
	if (demd->d_inode)
		goto out_set;

Printk(("umsdos_make_emd: creating EMD %s/%s\n",
parent->d_name.name, demd->d_name.name));

	err = msdos_create(parent->d_inode, demd, S_IFREG | 0777);
	if (err) {
		printk (KERN_WARNING
			"umsdos_make_emd: create %s/%s failed, err=%d\n",
			parent->d_name.name, demd->d_name.name, err);
		goto out_dput;
	}
out_set:
	parent->d_inode->u.umsdos_i.i_emd_dir = demd->d_inode->i_ino;

out_dput:
	dput(demd);
out:
	return err;
}


/*
 * Read an entry from the EMD file.
 * Support variable length record.
 * Return -EIO if error, 0 if OK.
 *
 * does not change {d,i}_count
 */

int umsdos_emd_dir_readentry (struct file *filp, struct umsdos_dirent *entry)
{
	int ret;

	Printk ((KERN_DEBUG "umsdos_emd_dir_readentry /mn/: entering.\n"));

	ret = umsdos_emd_dir_read (filp, (char *) entry, UMSDOS_REC_SIZE);
	if (ret == 0) {	/* if no error */
		/* Variable size record. Maybe, we have to read some more */
		int recsize = umsdos_evalrecsize (entry->name_len);

		if (recsize > UMSDOS_REC_SIZE) {
Printk ((KERN_DEBUG "umsdos_emd_dir_readentry /mn/: %d > %d!\n",
recsize, UMSDOS_REC_SIZE));
			ret = umsdos_emd_dir_read (filp, 
					((char *) entry) + UMSDOS_REC_SIZE,
					recsize - UMSDOS_REC_SIZE);
		}
	}
	Printk (("umsdos_emd_dir_readentry /mn/: ret=%d.\n", ret));
	if (entry && ret == 0) {
Printk (("umsdos_emd_dir_readentry /mn/: returning len=%d,name=%.*s\n",
(int) entry->name_len, (int) entry->name_len, entry->name));
	}
	return ret;
}



/*
 * Write an entry in the EMD file.
 * Return 0 if OK, -EIO if some error.
 *
 * Note: the caller must hold a lock on the parent directory.
 */
static int umsdos_writeentry (struct dentry *parent, struct umsdos_info *info,
				int free_entry)
{
	struct inode *dir = parent->d_inode;
	struct umsdos_dirent *entry = &info->entry;
	struct dentry *emd_dentry;
	int ret;
	struct umsdos_dirent entry0;
	struct file filp;

	emd_dentry = umsdos_get_emd_dentry(parent);
	ret = PTR_ERR(emd_dentry);
	if (IS_ERR(emd_dentry))
		goto out;
	/* make sure there's an EMD file */
	ret = -EIO;
	if (!emd_dentry->d_inode) {
		printk(KERN_WARNING
			"umsdos_writeentry: no EMD file in %s/%s\n",
			parent->d_parent->d_name.name, parent->d_name.name);
		goto out_dput;
	}

	if (free_entry) {
		/* #Specification: EMD file / empty entries
		 * Unused entries in the EMD file are identified
		 * by the name_len field equal to 0. However to
		 * help future extension (or bug correction :-( ),
		 * empty entries are filled with 0.
		 */
		memset (&entry0, 0, sizeof (entry0));
		entry = &entry0;
	} else if (entry->name_len > 0) {
		memset (entry->name + entry->name_len, '\0', 
			sizeof (entry->name) - entry->name_len);
		/* #Specification: EMD file / spare bytes
		 * 10 bytes are unused in each record of the EMD. They
		 * are set to 0 all the time, so it will be possible
		 * to do new stuff and rely on the state of those
		 * bytes in old EMD files.
		 */
		memset (entry->spare, 0, sizeof (entry->spare));
	}

	fill_new_filp (&filp, emd_dentry);
	filp.f_pos = info->f_pos;
	filp.f_reada = 0;
	filp.f_flags = O_RDWR;

	/* write the entry and update the parent timestamps */
	ret = umsdos_emd_dir_write (&filp, (char *) entry, info->recsize);
	if (!ret) {
		dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		mark_inode_dirty(dir);
	} else
		printk ("UMSDOS:  problem with EMD file:  can't write\n");

out_dput:
	dput(emd_dentry);
out:
	Printk (("umsdos_writeentry /mn/: returning %d...\n", ret));
	return ret;
}



#define CHUNK_SIZE (8*UMSDOS_REC_SIZE)
struct find_buffer {
	char buffer[CHUNK_SIZE];
	int pos;		/* read offset in buffer */
	int size;		/* Current size of buffer */
	struct file filp;
};



/*
 * Fill the read buffer and take care of the bytes remaining inside.
 * Unread bytes are simply moved to the beginning.
 * 
 * Return -ENOENT if EOF, 0 if OK, a negative error code if any problem.
 *
 * Note: the caller must hold a lock on the parent directory.
 */

static int umsdos_fillbuf (struct find_buffer *buf)
{
	struct inode *inode = buf->filp.f_dentry->d_inode;
	int mustmove = buf->size - buf->pos;
	int mustread, remain;
	int ret = -ENOENT;

	if (mustmove > 0) {
		memcpy (buf->buffer, buf->buffer + buf->pos, mustmove);
	}
	buf->pos = 0;
	mustread = CHUNK_SIZE - mustmove;
	remain = inode->i_size - buf->filp.f_pos;
	if (remain < mustread)
		mustread = remain;
	if (mustread > 0) {
		ret = umsdos_emd_dir_read (&buf->filp, buf->buffer + mustmove,
					 mustread);
		if (ret == 0)
			buf->size = mustmove + mustread;
	} else if (mustmove) {
		buf->size = mustmove;
		ret = 0;
	}
	return ret;
}



/*
 * General search, locate a name in the EMD file or an empty slot to
 * store it. if info->entry.name_len == 0, search the first empty
 * slot (of the proper size).
 * 
 * Return 0 if found, -ENOENT if not found, another error code if
 * other problem.
 * 
 * So this routine is used to either find an existing entry or to
 * create a new one, while making sure it is a new one. After you
 * get -ENOENT, you make sure the entry is stuffed correctly and
 * call umsdos_writeentry().
 * 
 * To delete an entry, you find it, zero out the entry (memset)
 * and call umsdos_writeentry().
 * 
 * All this to say that umsdos_writeentry must be called after this
 * function since it relies on the f_pos field of info.
 *
 * Note: the caller must hold a lock on the parent directory.
 */
/* #Specification: EMD file structure
 * The EMD file uses a fairly simple layout.  It is made of records
 * (UMSDOS_REC_SIZE == 64).  When a name can't be written in a single
 * record, multiple contiguous records are allocated.
 */

static int umsdos_find (struct dentry *parent, struct umsdos_info *info)
{
	struct umsdos_dirent *entry = &info->entry;
	int recsize = info->recsize;
	struct dentry *demd;
	struct inode *emd_dir;
	int ret = -ENOENT;
	struct find_buffer buf;
	struct {
		off_t posok;	/* Position available to store the entry */
		int found;	/* A valid empty position has been found. */
		off_t one;	/* One empty position -> maybe <- large enough */
		int onesize;	/* size of empty region starting at one */
	} empty;

Printk (("umsdos_find: locating %s in %s/%s\n",
entry->name, parent->d_parent->d_name.name, parent->d_name.name));

	/*
	 * Lookup the EMD file in the parent directory.
	 */
	demd = umsdos_get_emd_dentry(parent);
	ret = PTR_ERR(demd);
	if (IS_ERR(demd))
		goto out;
	/* make sure there's an EMD file ... */
	ret = -ENOENT;
	emd_dir = demd->d_inode;
	if (!emd_dir)
		goto out_dput;

Printk(("umsdos_find: found EMD file %s/%s, ino=%p\n",
demd->d_parent->d_name.name, demd->d_name.name, emd_dir));

	fill_new_filp (&buf.filp, demd);

	buf.pos = 0;
	buf.size = 0;

	empty.found = 0;
	empty.posok = emd_dir->i_size;
	empty.onesize = 0;
	while (1) {
		struct umsdos_dirent *rentry = (struct umsdos_dirent *)
						(buf.buffer + buf.pos);
		int file_pos = buf.filp.f_pos - buf.size + buf.pos;

		if (buf.pos == buf.size) {
			ret = umsdos_fillbuf (&buf);
			if (ret < 0) {
				/* Not found, so note where it can be added */
				info->f_pos = empty.posok;
				break;
			}
		} else if (rentry->name_len == 0) {
			/* We are looking for an empty section at least */
			/* as large as recsize. */
			if (entry->name_len == 0) {
				info->f_pos = file_pos;
				ret = 0;
				break;
			} else if (!empty.found) {
				if (empty.onesize == 0) {
					/* This is the first empty record of a section. */
					empty.one = file_pos;
				}
				/* grow the empty section */
				empty.onesize += UMSDOS_REC_SIZE;
				if (empty.onesize == recsize) {
					/* Here is a large enough section. */
					empty.posok = empty.one;
					empty.found = 1;
				}
			}
			buf.pos += UMSDOS_REC_SIZE;
		} else {
			int entry_size = umsdos_evalrecsize (rentry->name_len);

			if (buf.pos + entry_size > buf.size) {
				ret = umsdos_fillbuf (&buf);
				if (ret < 0) {
					/* Not found, so note where it can be added */
					info->f_pos = empty.posok;
					break;
				}
			} else {
				empty.onesize = 0;	/* Reset the free slot search. */
				if (entry->name_len == rentry->name_len
				    && memcmp (entry->name, rentry->name, rentry->name_len) == 0) {
					info->f_pos = file_pos;
					*entry = *rentry;
					ret = 0;
					break;
				} else {
					buf.pos += entry_size;
				}
			}
		}
	}
Printk(("umsdos_find: ready to mangle %s, len=%d, pos=%ld\n",
entry->name, entry->name_len, (long)info->f_pos));
	umsdos_manglename (info);

out_dput:
	dput(demd);

out:
	Printk (("umsdos_find: returning %d\n", ret));
	return ret;
}


/*
 * Add a new entry in the EMD file.
 * Return 0 if OK or a negative error code.
 * Return -EEXIST if the entry already exists.
 *
 * Complete the information missing in info.
 * 
 * N.B. What if the EMD file doesn't exist?
 */

int umsdos_newentry (struct dentry *parent, struct umsdos_info *info)
{
	int err, ret = -EEXIST;

	err = umsdos_find (parent, info);
	if (err && err == -ENOENT) {
		ret = umsdos_writeentry (parent, info, 0);
		Printk (("umsdos_writeentry EMD ret = %d\n", ret));
	}
	return ret;
}


/*
 * Create a new hidden link.
 * Return 0 if OK, an error code if not.
 */

/* #Specification: hard link / hidden name
 * When a hard link is created, the original file is renamed
 * to a hidden name. The name is "..LINKNNN" where NNN is a
 * number define from the entry offset in the EMD file.
 */
int umsdos_newhidden (struct dentry *parent, struct umsdos_info *info)
{
	int ret;

	umsdos_parse ("..LINK", 6, info);
	info->entry.name_len = 0;
	ret = umsdos_find (parent, info);
	if (ret == -ENOENT || ret == 0) {
		info->entry.name_len = sprintf (info->entry.name,
						"..LINK%ld", info->f_pos);
		ret = 0;
	}
	return ret;
}


/*
 * Remove an entry from the EMD file.
 * Return 0 if OK, a negative error code otherwise.
 * 
 * Complete the information missing in info.
 */

int umsdos_delentry (struct dentry *parent, struct umsdos_info *info, int isdir)
{
	int ret;

	ret = umsdos_find (parent, info);
	if (ret)
		goto out;
	if (info->entry.name_len == 0)
		goto out;

	if ((isdir != 0) != (S_ISDIR (info->entry.mode) != 0)) {
		if (S_ISDIR (info->entry.mode)) {
			ret = -EISDIR;
		} else {
			ret = -ENOTDIR;
		}
		goto out;
	}
	ret = umsdos_writeentry (parent, info, 1);

out:
	return ret;
}


/*
 * Verify that an EMD directory is empty.
 * Return: 
 * 0 if not empty,
 * 1 if empty (except for EMD file),
 * 2 if empty or no EMD file.
 */

int umsdos_isempty (struct dentry *dentry)
{
	struct dentry *demd;
	int ret = 2;
	struct file filp;

	demd = umsdos_get_emd_dentry(dentry);
	if (IS_ERR(demd))
		goto out;
	/* If the EMD file does not exist, it is certainly empty. :-) */
	if (!demd->d_inode)
		goto out_dput;

	fill_new_filp (&filp, demd);
	filp.f_flags = O_RDONLY;

	ret = 1;
	while (filp.f_pos < demd->d_inode->i_size) {
		struct umsdos_dirent entry;

		if (umsdos_emd_dir_readentry (&filp, &entry) != 0) {
			ret = 0;
			break;
		}
		if (entry.name_len != 0) {
			ret = 0;
			break;
		}
	}

out_dput:
	dput(demd);
out:
Printk(("umsdos_isempty: checked %s/%s, empty=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, ret));

	return ret;
}

/*
 * Locate an entry in a EMD directory.
 * Return 0 if OK, error code if not, generally -ENOENT.
 *
 * expect argument:
 * 	0: anything
 * 	1: file
 * 	2: directory
 */

int umsdos_findentry (struct dentry *parent, struct umsdos_info *info,
			int expect)
{		
	int ret;

	ret = umsdos_find (parent, info);
	if (ret)
		goto out;

	switch (expect) {
	case 1:
		if (S_ISDIR (info->entry.mode))
			ret = -EISDIR;
		break;
	case 2:
		if (!S_ISDIR (info->entry.mode))
			ret = -ENOTDIR;
	}

out:
	Printk (("umsdos_findentry: returning %d\n", ret));
	return ret;
}
