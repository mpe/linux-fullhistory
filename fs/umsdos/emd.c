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
	int ret;

	mm_segment_t old_fs = get_fs ();

	set_fs (KERNEL_DS);

	PRINTK ((KERN_DEBUG "umsdos_file_read_kmem /mn/: Checkin: filp=%p, buf=%p, size=%d\n", filp, buf, count));
	PRINTK ((KERN_DEBUG "  inode=%lu, i_size=%lu\n", filp->f_dentry->d_inode->i_ino, filp->f_dentry->d_inode->i_size));
	PRINTK ((KERN_DEBUG "  f_pos=%Lu\n", filp->f_pos));
	PRINTK ((KERN_DEBUG "  name=%.*s\n", (int) filp->f_dentry->d_name.len, filp->f_dentry->d_name.name));
	PRINTK ((KERN_DEBUG "  i_binary(sb)=%d\n", MSDOS_I (filp->f_dentry->d_inode)->i_binary));
	PRINTK ((KERN_DEBUG "  f_count=%d, f_flags=%d\n", filp->f_count, filp->f_flags));
	PRINTK ((KERN_DEBUG "  f_owner=%d\n", filp->f_owner.uid));
	PRINTK ((KERN_DEBUG "  f_version=%ld\n", filp->f_version));
	PRINTK ((KERN_DEBUG "  f_reada=%ld, f_ramax=%ld, f_raend=%ld, f_ralen=%ld, f_rawin=%ld\n", filp->f_reada, filp->f_ramax, filp->f_raend, filp->f_ralen, filp->f_rawin));

	MSDOS_I (filp->f_dentry->d_inode)->i_binary = 2;

	ret = fat_file_read (filp, buf, count, &filp->f_pos);
	PRINTK ((KERN_DEBUG "fat_file_read returned with %d!\n", ret));

	PRINTK ((KERN_DEBUG "  (ret) inode=%lu, i_size=%lu\n", filp->f_dentry->d_inode->i_ino, filp->f_dentry->d_inode->i_size));
	PRINTK ((KERN_DEBUG "  (ret) f_pos=%Lu\n", filp->f_pos));
	PRINTK ((KERN_DEBUG "  (ret) name=%.*s\n", (int) filp->f_dentry->d_name.len, filp->f_dentry->d_name.name));
	PRINTK ((KERN_DEBUG "  (ret) i_binary(sb)=%d\n", MSDOS_I (filp->f_dentry->d_inode)->i_binary));
	PRINTK ((KERN_DEBUG "  (ret) f_count=%d, f_flags=%d\n", filp->f_count, filp->f_flags));
	PRINTK ((KERN_DEBUG "  (ret) f_owner=%d\n", filp->f_owner.uid));
	PRINTK ((KERN_DEBUG "  (ret) f_version=%ld\n", filp->f_version));
	PRINTK ((KERN_DEBUG "  (ret) f_reada=%ld, f_ramax=%ld, f_raend=%ld, f_ralen=%ld, f_rawin=%ld\n", filp->f_reada, filp->f_ramax, filp->f_raend, filp->f_ralen, filp->f_rawin));

#if 0
	{
		struct umsdos_dirent *mydirent = buf;

		Printk ((KERN_DEBUG "  (DDD) uid=%d\n", mydirent->uid));
		Printk ((KERN_DEBUG "  (DDD) gid=%d\n", mydirent->gid));
		Printk ((KERN_DEBUG "  (DDD) name=>%.20s<\n", mydirent->name));
	}
#endif

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
	ssize_t ret;
	mm_segment_t old_fs = get_fs ();

	set_fs (KERNEL_DS);

	PRINTK ((KERN_DEBUG "umsdos_file_write_kmem /mn/: Checkin: filp=%p, buf=%p, size=%d\n", filp, buf, count));
	PRINTK ((KERN_DEBUG "  struct dentry=%p\n", filp->f_dentry));
	PRINTK ((KERN_DEBUG "  struct inode=%p\n", filp->f_dentry->d_inode));
	PRINTK ((KERN_DEBUG "  inode=%lu, i_size=%lu\n", filp->f_dentry->d_inode->i_ino, filp->f_dentry->d_inode->i_size));
	PRINTK ((KERN_DEBUG "  f_pos=%Lu\n", filp->f_pos));
	PRINTK ((KERN_DEBUG "  name=%.*s\n", (int) filp->f_dentry->d_name.len, filp->f_dentry->d_name.name));
	PRINTK ((KERN_DEBUG "  i_binary(sb)=%d\n", MSDOS_I (filp->f_dentry->d_inode)->i_binary));
	PRINTK ((KERN_DEBUG "  f_count=%d, f_flags=%d\n", filp->f_count, filp->f_flags));
	PRINTK ((KERN_DEBUG "  f_owner=%d\n", filp->f_owner.uid));
	PRINTK ((KERN_DEBUG "  f_version=%ld\n", filp->f_version));
	PRINTK ((KERN_DEBUG "  f_reada=%ld, f_ramax=%ld, f_raend=%ld, f_ralen=%ld, f_rawin=%ld\n", filp->f_reada, filp->f_ramax, filp->f_raend, filp->f_ralen, filp->f_rawin));

	/* note: i_binary=2 is for CVF-FAT. We put it here, instead of
	 * umsdos_file_write_kmem, since it is also wise not to compress symlinks
	 * (in the unlikely event that they are > 512 bytes and can be compressed 
	 * FIXME: should we set it when reading symlinks too? */

	MSDOS_I (filp->f_dentry->d_inode)->i_binary = 2;

	ret = fat_file_write (filp, buf, count, &filp->f_pos);
	Printk ((KERN_DEBUG "fat_file_write returned with %ld!\n", (long int) ret));

	set_fs (old_fs);
	return ret;
}


/*
 *    Write to a file from kernel space.
 */

ssize_t umsdos_file_write_kmem (struct file *filp,
				const char *buf,
				size_t count)
{
	int ret;

	Printk ((KERN_DEBUG " STARTED WRITE_KMEM /mn/\n"));
	ret = umsdos_file_write_kmem_real (filp, buf, count);

#warning Should d_drop be here ?
#if 0
	d_drop (filp->f_dentry);
#endif

	return ret;
}




/*
 * Write a block of bytes into one EMD file.
 * The block of data is NOT in user space.
 * 
 * Return 0 if OK, a negative error code if not.
 */

ssize_t umsdos_emd_dir_write (	struct file *filp,
				char *buf,	/* buffer in kernel memory, not in user space */
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
	Printk (("umsdos_emd_dir_write /mn/: calling write_kmem with %p, %p, %d, %Ld\n", filp, buf, count, filp->f_pos));
	written = umsdos_file_write_kmem (filp, buf, count);
	Printk (("umsdos_emd_dir_write /mn/: write_kmem returned\n"));

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

#if UMS_DEBUG
	if (written != count)
		Printk ((KERN_ERR "umsdos_emd_dir_write: ERROR: written (%d) != count (%d)\n", written, count));
#endif


	return written != count ? -EIO : 0;
}



/*
 *      Read a block of bytes from one EMD file.
 *      The block of data is NOT in user space.
 *      Return 0 if OK, -EIO if any error.
 */

ssize_t umsdos_emd_dir_read (struct file *filp,
			     char *buf,		/* buffer in kernel memory, not in user space */
			     size_t count)
{
	long int ret = 0;
	int sizeread;


#ifdef __BIG_ENDIAN
	struct umsdos_dirent *d = (struct umsdos_dirent *) buf;

#endif

	filp->f_flags = 0;
	sizeread = umsdos_file_read_kmem (filp, buf, count);
	if (sizeread != count) {
		printk ("UMSDOS:  problem with EMD file:  can't read pos = %Ld (%d != %d)\n", filp->f_pos, sizeread, count);
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
 * this checks weather filp points to directory or file,
 * and if directory, it assumes that it has not yet been
 * converted to point to EMD_FILE, and fixes it
 *
 * calling code should save old filp->f_dentry, call fix_emd_filp
 * and if it succeeds (return code 0), do fin_dentry (filp->f_dentry)
 * when it is over. It should also restore old filp->f_dentry.
 *
 */

int fix_emd_filp (struct file *filp)
{
	struct inode *dir=filp->f_dentry->d_inode;
	struct inode *emd_dir;
  
	/* is current file (which should be EMD or directory) EMD? */
	if (dir->u.umsdos_i.i_emd_owner == 0xffffffff) {
		dget (filp->f_dentry);
		Printk ((KERN_WARNING "\nfix_emd_filp: EMD already done (should not be !)\n\n"));
		return 0;
	}
	/* it is not, we need to make it so */
	
	emd_dir = umsdos_emd_dir_lookup (dir, 0);
	if (emd_dir == NULL) {
		Printk ((KERN_ERR "\nfix_emd_filp: EMD not found (should never happen)!!!\n\n"));
		return -99;
	}
  
	filp->f_dentry = creat_dentry (UMSDOS_EMD_FILE, UMSDOS_EMD_NAMELEN, emd_dir, filp->f_dentry);	/* filp->f_dentry is dir containing EMD file, so it IS the parent dentry... */

	return 0;
}


/*
 * Locate the EMD file in a directory.
 * 
 * Return NULL if error, dir->u.umsdos_i.emd_inode if OK. 
 * caller must iput() returned inode when finished with it!
 */

struct inode *umsdos_emd_dir_lookup (struct inode *dir, int creat)
{
	struct inode *ret = NULL;
	struct dentry *d_dir=NULL, *dlook=NULL;
	int rv;

	Printk ((KERN_DEBUG "Entering umsdos_emd_dir_lookup\n"));
	if (!dir) printk (KERN_CRIT "umsdos FATAL: should never happen: dir=NULL!\n");
	check_inode (dir);
	
	if (dir->u.umsdos_i.i_emd_dir != 0) {
		ret = iget (dir->i_sb, dir->u.umsdos_i.i_emd_dir);
		Printk (("umsdos_emd_dir_lookup: deja trouve %ld %p\n", dir->u.umsdos_i.i_emd_dir, ret));
	} else {
		PRINTK ((KERN_DEBUG "umsdos /mn/: Looking for %.*s -", UMSDOS_EMD_NAMELEN, UMSDOS_EMD_FILE));

		d_dir = geti_dentry (dir);
		dlook = creat_dentry (UMSDOS_EMD_FILE, UMSDOS_EMD_NAMELEN, NULL, d_dir);
		rv = umsdos_real_lookup (dir, dlook);
		
		PRINTK ((KERN_DEBUG "-returned %d\n", rv));
		Printk ((KERN_INFO "emd_dir_lookup "));
		
		ret = dlook->d_inode;
		if (ret) {
			Printk (("Found --linux "));
			dir->u.umsdos_i.i_emd_dir = ret->i_ino;
			inc_count (ret);	/* we'll need the inode */
			fin_dentry (dlook);	/* but not dentry */
			check_inode (ret);
		} else if (creat) {
			int code;
			
			Printk ((" * ERROR * /mn/: creat not yet implemented? not fixed? "));
			Printk (("avant create "));
			inc_count (dir);

			check_inode (ret);
			code = compat_msdos_create (dir, UMSDOS_EMD_FILE, UMSDOS_EMD_NAMELEN, S_IFREG | 0777, &ret);
			check_inode (ret);
			Printk (("Creat EMD code %d ret %p ", code, ret));
			if (ret != NULL) {
				Printk ((" ino=%lu", ret->i_ino));
				dir->u.umsdos_i.i_emd_dir = ret->i_ino;
			} else {
				printk (KERN_WARNING "UMSDOS: Can't create EMD file\n");
			}
		}
		
		if (ret != NULL) {
			/* Disable UMSDOS_notify_change() for EMD file */
			/* inc_count (ret); // we need to return with incremented inode. FIXME: didn't umsdos_real_lookup already did that? and compat_msdos_create ? */
			ret->u.umsdos_i.i_emd_owner = 0xffffffff;
		}
	}

#if UMS_DEBUG
	Printk ((KERN_DEBUG "umsdos_emd_dir_lookup returning %p /mn/\n", ret));
	if (ret != NULL)
		Printk ((KERN_DEBUG " returning ino=%lu\n", ret->i_ino));
#endif
	return ret;
}



/*
 * creates an EMD file
 * 
 * Return NULL if error, dir->u.umsdos_i.emd_inode if OK. 
 */

struct inode *umsdos_emd_dir_create (struct inode *dir, struct dentry *dentry, int mode)
{
	struct inode *ret = NULL;

	if (dir->u.umsdos_i.i_emd_dir != 0) {
		ret = iget (dir->i_sb, dir->u.umsdos_i.i_emd_dir);
		Printk (("deja trouve %lu %p", dir->u.umsdos_i.i_emd_dir, ret));
	} else {

		int code;

		Printk (("avant create "));
		inc_count (dir);
		code = compat_msdos_create (dir, UMSDOS_EMD_FILE, UMSDOS_EMD_NAMELEN, S_IFREG | 0777, &ret);
		Printk (("Creat EMD code %d ret %p ", code, ret));
		if (ret != NULL) {
			dir->u.umsdos_i.i_emd_dir = ret->i_ino;
		} else {
			printk ("UMSDOS: Can't create EMD file\n");
		}
	}

	if (ret != NULL) {
		/* Disable UMSDOS_notify_change() for EMD file */
		ret->u.umsdos_i.i_emd_owner = 0xffffffff;
	}
	return ret;
}



/*
 * Read an entry from the EMD file.
 * Support variable length record.
 * Return -EIO if error, 0 if OK.
 *
 * does not change {d,i}_count
 */

int umsdos_emd_dir_readentry (	     struct file *filp,
				     struct umsdos_dirent *entry)
{
	int ret;

	Printk ((KERN_DEBUG "umsdos_emd_dir_readentry /mn/: entering.\n"));
	Printk (("umsdos_emd_dir_readentry /mn/: reading EMD %.*s (ino=%lu) at pos=%d\n", (int) filp->f_dentry->d_name.len, filp->f_dentry->d_name.name, filp->f_dentry->d_inode->i_ino, (int) filp->f_pos));

	ret = umsdos_emd_dir_read (filp, (char *) entry, UMSDOS_REC_SIZE);
	if (ret == 0) {	/* if no error */
		/* Variable size record. Maybe, we have to read some more */
		int recsize = umsdos_evalrecsize (entry->name_len);

		if (recsize > UMSDOS_REC_SIZE) {
			Printk ((KERN_DEBUG "umsdos_emd_dir_readentry /mn/: %d > %d!\n", recsize, UMSDOS_REC_SIZE));
			ret = umsdos_emd_dir_read (filp, ((char *) entry) + UMSDOS_REC_SIZE, recsize - UMSDOS_REC_SIZE);
		}
	}
	Printk (("umsdos_emd_dir_readentry /mn/: ret=%d.\n", ret));
	if (entry && ret == 0) {
		Printk (("umsdos_emd_dir_readentry /mn/: returning len=%d,name=%.*s\n", (int) entry->name_len, (int) entry->name_len, entry->name));
	}
	return ret;
}




/*
 * Write an entry in the EMD file.
 * Return 0 if OK, -EIO if some error.
 */

int umsdos_writeentry (	      struct inode *dir,
			      struct inode *emd_dir,
			      struct umsdos_info *info,
			      int free_entry)

{				/* This entry is deleted, so write all 0's. */
	int ret = 0;
	struct dentry *emd_dentry;
	struct file filp;
	struct umsdos_dirent *entry = &info->entry;
	struct umsdos_dirent entry0;

	fill_new_filp (&filp, NULL);

	Printk (("umsdos_writeentry /mn/: entering...\n"));
	emd_dentry = geti_dentry (emd_dir);

	if (free_entry) {
		/* #Specification: EMD file / empty entries
		 * Unused entry in the EMD file are identified
		 * by the name_len field equal to 0. However to
		 * help future extension (or bug correction :-( ),
		 * empty entries are filled with 0.
		 */
		memset (&entry0, 0, sizeof (entry0));
		entry = &entry0;
	} else if (entry->name_len > 0) {
		memset (entry->name + entry->name_len, '\0', sizeof (entry->name) - entry->name_len);
		/* #Specification: EMD file / spare bytes
		 * 10 bytes are unused in each record of the EMD. They
		 * are set to 0 all the time, so it will be possible
		 * to do new stuff and rely on the state of those
		 * bytes in old EMD files.
		 */
		memset (entry->spare, 0, sizeof (entry->spare));
	}
	Printk (("umsdos_writeentry /mn/: if passed...\n"));

	if (!info)
		printk (KERN_ERR "UMSDOS:  /mn/ info is empty!  Oops!\n");
	filp.f_pos = info->f_pos;
	filp.f_reada = 0;
	filp.f_flags = O_RDWR;
	filp.f_dentry = emd_dentry;
	filp.f_op = &umsdos_file_operations;	/* /mn/ - We have to fill it with dummy values so we won't segfault. */

	ret = umsdos_emd_dir_write (&filp, (char *) entry, info->recsize);
	Printk (("emd_dir_write returned with %d!\n", ret));
	if (ret != 0) {
		printk ("UMSDOS:  problem with EMD file:  can't write\n");
	} else {
		dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		/* dir->i_dirt = 1; FIXME iput/dput ??? */
	}

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
 * Fill the read buffer and take care of the byte remaining inside.
 * Unread bytes are simply move to the beginning.
 * 
 * Return -ENOENT if EOF, 0 if OK, a negative error code if any problem.
 */

static int umsdos_fillbuf (
				  struct inode *inode,
				  struct find_buffer *buf)
{
	int ret = -ENOENT;
	int mustmove = buf->size - buf->pos;
	int mustread;
	int remain;
	struct inode *old_ino;

	PRINTK ((KERN_DEBUG "Entering umsdos_fillbuf, for inode %lu, buf=%p\n", inode->i_ino, buf));

	if (mustmove > 0) {
		memcpy (buf->buffer, buf->buffer + buf->pos, mustmove);
	}
	buf->pos = 0;
	mustread = CHUNK_SIZE - mustmove;
	remain = inode->i_size - buf->filp.f_pos;
	if (remain < mustread)
		mustread = remain;
	if (mustread > 0) {
		old_ino = buf->filp.f_dentry->d_inode;	/* FIXME: do we need to save/restore it ? */
		buf->filp.f_dentry->d_inode = inode;
		ret = umsdos_emd_dir_read (&buf->filp, buf->buffer + mustmove, mustread);
		buf->filp.f_dentry->d_inode = old_ino;
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
 * Caller must do iput on *pt_emd_dir.
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
 * All this to say that umsdos_writeentry must be call after this
 * function since it rely on the f_pos field of info.
 *
 * calling code is expected to iput() returned *pt_emd_dir
 *
 */

static int umsdos_find (       struct inode *dir,
			       struct umsdos_info *info,	/* Hold name and name_len */
								/* Will hold the entry found */
			       struct inode **pt_emd_dir)	/* Will hold the emd_dir inode or NULL if not found */

{
	/* #Specification: EMD file structure
	 * The EMD file uses a fairly simple layout.  It is made of records
	 * (UMSDOS_REC_SIZE == 64).  When a name can't be written in a single
	 * record, multiple contiguous records are allocated.
	 */
	int ret = -ENOENT;
	struct inode *emd_dir;
	struct umsdos_dirent *entry = &info->entry;

	Printk (("umsdos_find: locating %.*s in dir %lu\n", entry->name_len, entry->name, dir->i_ino));
	check_inode (dir);

	emd_dir = umsdos_emd_dir_lookup (dir, 1);
	if (emd_dir != NULL) {
		int recsize = info->recsize;
		struct {
			off_t posok;	/* Position available to store the entry */
			int found;	/* A valid empty position has been found. */
			off_t one;	/* One empty position -> maybe <- large enough */
			int onesize;	/* size of empty region starting at one */
		} empty;

		/* Read several entries at a time to speed up the search. */
		struct find_buffer buf;
		struct dentry *demd;

		Printk (("umsdos_find: check emd_dir...\n"));
		check_inode (emd_dir);
		
#if 0		/* FIXME! not needed. but there are count wraps. somewhere before umsdos_find there should be inc_count/iput pair around umsdos_find call.... */
		inc_count (emd_dir);	/* since we are going to fin_dentry, and need emd_dir afterwards -- caling code will iput() it */
#endif		
		demd = geti_dentry (emd_dir);
		if (demd) {
			dget (demd);	/* because we'll have to dput it */
		} else {
			/*
			 * We don't have dentry alias for this inode. Too bad.
			 * So we'll fake something (as best as we can).
			 * (maybe we should do it in any case just to keep it simple?)
			 *
			 * Note that this is legal for EMD file, since in some places
			 * we keep inode, but discard dentry (since we would have no way
			 * to discard it later). Yes, this probably should be fixed somehow,
			 * it is just that I don't have idea how right now, and I've spent
			 * quite some time to track it down why it dies here. Maybe new emd_dir_lookup
			 * which returns dentry ? hmmmm... FIXME...
			 *
			 */
		 	Printk ((KERN_WARNING "umsdos_find: inode has no alias for EMD inode, fake it\n"));
		 	demd = creat_dentry ("@emd_find@", 10, emd_dir, NULL);
		}
		
		check_dentry_path (demd, " EMD_DIR_DENTRY umsdos_find");
		
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
				ret = umsdos_fillbuf (emd_dir, &buf);
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
					ret = umsdos_fillbuf (emd_dir, &buf);
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
		umsdos_manglename (info);
		fin_dentry (demd);
	}
	*pt_emd_dir = emd_dir;

	Printk (("umsdos_find: returning %d\n", ret));
	return ret;
}


/*
 * Add a new entry in the EMD file.
 * Return 0 if OK or a negative error code.
 * Return -EEXIST if the entry already exists.
 * 
 * Complete the information missing in info.
 */

int umsdos_newentry (	    struct inode *dir,
			    struct umsdos_info *info)
{
	struct inode *emd_dir;
	int ret = umsdos_find (dir, info, &emd_dir);

	if (ret == 0) {
		ret = -EEXIST;
	} else if (ret == -ENOENT) {
		ret = umsdos_writeentry (dir, emd_dir, info, 0);
		Printk (("umsdos_writeentry EMD ret = %d\n", ret));
	}
	iput (emd_dir);
	return ret;
}


/*
 * Create a new hidden link.
 * Return 0 if OK, an error code if not.
 */

int umsdos_newhidden (	     struct inode *dir,
			     struct umsdos_info *info)
{
	struct inode *emd_dir;
	int ret;

	umsdos_parse ("..LINK", 6, info);
	info->entry.name_len = 0;
	ret = umsdos_find (dir, info, &emd_dir);
	iput (emd_dir);
	if (ret == -ENOENT || ret == 0) {
		/* #Specification: hard link / hidden name
		 * When a hard link is created, the original file is renamed
		 * to a hidden name. The name is "..LINKNNN" where NNN is a
		 * number define from the entry offset in the EMD file.
		 */
		info->entry.name_len = sprintf (info->entry.name, "..LINK%ld", info->f_pos);
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

int umsdos_delentry (	    struct inode *dir,
			    struct umsdos_info *info,
			    int isdir)
{
	struct inode *emd_dir;
	int ret = umsdos_find (dir, info, &emd_dir);

	if (ret == 0) {
		if (info->entry.name_len != 0) {
			if ((isdir != 0) != (S_ISDIR (info->entry.mode) != 0)) {
				if (S_ISDIR (info->entry.mode)) {
					ret = -EISDIR;
				} else {
					ret = -ENOTDIR;
				}
			} else {
				ret = umsdos_writeentry (dir, emd_dir, info, 1);
			}
		}
	}
	iput (emd_dir);
	return ret;
}


/*
 * Verify that a EMD directory is empty. Return 
 * 0 if not empty,
 * 1 if empty,
 * 2 if empty or no EMD file.
 */

int umsdos_isempty (struct inode *dir)
{
	struct dentry *dentry, *d_dir;

	int ret = 2;
	struct inode *emd_dir = umsdos_emd_dir_lookup (dir, 0);

	/* If the EMD file does not exist, it is certainly empty. :-) */
	if (emd_dir != NULL) {
		struct file filp;

		d_dir = geti_dentry (dir);
		dentry = creat_dentry (UMSDOS_EMD_FILE, UMSDOS_EMD_NAMELEN, emd_dir, d_dir);
		check_dentry_path (dentry, "umsdos_isempty BEGIN");
		fill_new_filp (&filp, dentry);
		filp.f_pos = 0;
		filp.f_flags = O_RDONLY;

		ret = 1;
		while (filp.f_pos < emd_dir->i_size) {
			struct umsdos_dirent entry;

			if (umsdos_emd_dir_readentry (&filp, &entry) != 0) {
				ret = 0;
				break;
			} else if (entry.name_len != 0) {
				ret = 0;
				break;
			}
		}
		fin_dentry (dentry);
		check_dentry_path (dentry, "umsdos_isempty END");
		iput (emd_dir);
	}
	return ret;
}

/*
 * Locate an entry in a EMD directory.
 * Return 0 if OK, error code if not, generally -ENOENT.
 *
 * does not change i_count
 */

int umsdos_findentry (	     struct inode *dir,
			     struct umsdos_info *info,
			     int expect)
				/* 0: anything */
				/* 1: file */
				/* 2: directory */
{		
	struct inode *emd_dir=NULL;
	int ret = umsdos_find (dir, info, &emd_dir);

	if (ret == 0) {
		if (expect != 0) {
			if (S_ISDIR (info->entry.mode)) {
				if (expect != 2)
					ret = -EISDIR;
			} else if (expect == 2) {
				ret = -ENOTDIR;
			}
		}
	}
	iput (emd_dir);
	Printk (("umsdos_findentry: returning %d\n", ret));
	return ret;
}
