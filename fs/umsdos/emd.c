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

#include <asm/uaccess.h>

#include <asm/delay.h>

#define PRINTK(x)
#define Printk(x) printk x

/*
 * makes dentry. for name name with length len. /mn/
 * if inode is not NULL, puts it also.
 *
 */
 
struct dentry *creat_dentry (const char *name, const int len, const struct inode *inode)
{
    struct dentry *ret, *parent=NULL;	/* FIXME /mn/: whatis parent ?? */
    struct qstr qname;
    
    if (inode)
      Printk (("/mn/ creat_dentry: creating dentry with inode=%d for %20s\n", inode->i_ino, name));
    else
      Printk (("/mn/ creat_dentry: creating empty dentry for %20s\n", name));

    qname.name = name;
    qname.len = len;
    qname.hash = 0;

    ret = d_alloc (parent,&qname);	/* create new dentry */
    ret->d_inode = inode;
}



/*
 *	Read a file into kernel space memory
 *	returns how many bytes read (from fat_file_read)
 */

ssize_t umsdos_file_read_kmem (struct inode *emd_dir,
			    struct file *filp,
			    char *buf,
			    size_t count,
			    loff_t *offs
			    )
{
    int ret;

    struct dentry *old_dentry;
    mm_segment_t old_fs = get_fs();

    set_fs (KERNEL_DS);

    old_dentry=filp->f_dentry;	/* save it */
    filp->f_dentry = creat_dentry (UMSDOS_EMD_FILE, UMSDOS_EMD_NAMELEN, emd_dir);
    *offs = filp->f_pos;
    
    Printk ((KERN_DEBUG "umsdos_file_read_kmem /mn/: Checkin: filp=%p, buf=%p, size=%ld, offs=%p\n", filp, buf, count, offs));
    Printk ((KERN_DEBUG "  using emd=%d\n", emd_dir->i_ino));
    Printk ((KERN_DEBUG "  inode=%d, i_size=%d\n", filp->f_dentry->d_inode->i_ino,filp->f_dentry->d_inode->i_size));
    Printk ((KERN_DEBUG "  ofs=%ld\n", *offs));
    Printk ((KERN_DEBUG "  f_pos=%ld\n", filp->f_pos));
    Printk ((KERN_DEBUG "  name=%12s\n", filp->f_dentry->d_name.name));
    Printk ((KERN_DEBUG "  i_binary(sb)=%d\n", MSDOS_I(filp->f_dentry->d_inode)->i_binary ));
    Printk ((KERN_DEBUG "  f_count=%d, f_flags=%d\n", filp->f_count, filp->f_flags));
    Printk ((KERN_DEBUG "  f_owner=%d\n", filp->f_owner));
    Printk ((KERN_DEBUG "  f_version=%ld\n", filp->f_version));
    Printk ((KERN_DEBUG "  f_reada=%ld, f_ramax=%ld, f_raend=%ld, f_ralen=%ld, f_rawin=%ld\n", filp->f_reada, filp->f_ramax, filp->f_raend, filp->f_ralen, filp->f_rawin));

    ret = fat_file_read(filp,buf,count,offs);
    Printk ((KERN_DEBUG "fat_file_read returned with %ld!\n", ret));

    filp->f_pos = *offs;	/* we needed *filp only for this? grrrr... /mn/ */
    				/* FIXME: I have no idea what f_pos is used for. It seems to be used this way before offs was introduced.
    				   this probably needs fixing /mn/ */

    filp->f_dentry=old_dentry;			/* restore orig. dentry (it is dentry of file we need info about. Dunno why it gets passed to us
    						   since we have no use for it, expect to store totally unrelated data of offset of EMD_FILE
    						   end not directory in it. But what the hell now... fat_file_read requires it also, but prolly expects
    						   it to be file* of EMD not file we want to read EMD entry about... ugh. complicated to explain :) /mn/ */
    						   
    						 /* FIXME: we probably need to destroy originl filp->f_dentry first ? Do we ? And how ? this way we leave all sorts of dentries, inodes etc. lying around */
    						 /* Also FIXME: all the same problems in umsdos_file_write_kmem */

    Printk ((KERN_DEBUG "  (ret) using emd=%d\n", emd_dir->i_ino));
    Printk ((KERN_DEBUG "  (ret) inode=%d, i_size=%d\n", filp->f_dentry->d_inode->i_ino,filp->f_dentry->d_inode->i_size));
    Printk ((KERN_DEBUG "  (ret) ofs=%ld\n", *offs));
    Printk ((KERN_DEBUG "  (ret) f_pos=%ld\n", filp->f_pos));
    Printk ((KERN_DEBUG "  (ret) name=%12s\n", filp->f_dentry->d_name.name));
    Printk ((KERN_DEBUG "  (ret) i_binary(sb)=%d\n", MSDOS_I(filp->f_dentry->d_inode)->i_binary ));
    Printk ((KERN_DEBUG "  (ret) f_count=%d, f_flags=%d\n", filp->f_count, filp->f_flags));
    Printk ((KERN_DEBUG "  (ret) f_owner=%d\n", filp->f_owner));
    Printk ((KERN_DEBUG "  (ret) f_version=%ld\n", filp->f_version));
    Printk ((KERN_DEBUG "  (ret) f_reada=%ld, f_ramax=%ld, f_raend=%ld, f_ralen=%ld, f_rawin=%ld\n", filp->f_reada, filp->f_ramax, filp->f_raend, filp->f_ralen, filp->f_rawin));

#if 0
    {
      struct umsdos_dirent *mydirent=buf;

      Printk ((KERN_DEBUG "  (DDD) uid=%d\n",mydirent->uid));
      Printk ((KERN_DEBUG "  (DDD) gid=%d\n",mydirent->gid));
      Printk ((KERN_DEBUG "  (DDD) name=>%20s<\n",mydirent->name));
    }
#endif  
    
    set_fs (old_fs);
    return ret;
}


/*
	Write to a file from kernel space
*/
ssize_t umsdos_file_write_kmem (struct inode *emd_dir,
				struct file *filp,
				const char *buf,
				size_t  count,
				loff_t *offs
				)
{
	int ret;
	mm_segment_t old_fs = get_fs();
	struct dentry *old_dentry;	/* FIXME /mn/: whatis parent ?? */
	
	set_fs (KERNEL_DS);
	ret = fat_file_write(filp,buf,count,offs);

	old_dentry=filp->f_dentry;	/* save it */
	filp->f_dentry = creat_dentry (UMSDOS_EMD_FILE, UMSDOS_EMD_NAMELEN, emd_dir);

	*offs = filp->f_pos;

	ret = fat_file_write (filp,buf,count,offs);
	PRINTK ((KERN_DEBUG "fat_file_write returned with %ld!\n", ret));

	filp->f_pos = *offs;
	filp->f_dentry=old_dentry;

	set_fs (old_fs);
	return ret;
}


/*
	Write a block of bytes into one EMD file.
	The block of data is NOT in user space.

	Return 0 if ok, a negative error code if not.
*/
ssize_t umsdos_emd_dir_write (struct inode *emd_dir,
			   struct file *filp,
			   char *buf,	/* buffer in kernel memory, not in user space */
			   size_t  count,
			   loff_t *offs
			   )
{
	int written;
	loff_t myofs=0;
	
#ifdef __BIG_ENDIAN
	struct umsdos_dirent *d = (struct umsdos_dirent *)buf;
#endif
	filp->f_flags = 0;
#ifdef __BIG_ENDIAN	
	d->nlink = cpu_to_le16 (d->nlink);
	d->uid = cpu_to_le16 (d->uid);
	d->gid = cpu_to_le16 (d->gid);
	d->atime = cpu_to_le32 (d->atime);
	d->mtime = cpu_to_le32 (d->mtime);
	d->ctime = cpu_to_le32 (d->ctime);
	d->rdev = cpu_to_le16 (d->rdev);
	d->mode = cpu_to_le16 (d->mode);
#endif	
	
	if (offs) myofs=*offs;	/* if offs is not NULL, read it */
	written = umsdos_file_write_kmem (emd_dir, filp, buf, count, &myofs);
	if (offs) *offs=myofs;	/* if offs is not NULL, store myofs there */
	
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
	return written != count ? -EIO : 0;
}



/*
 *	Read a block of bytes from one EMD file.
 *	The block of data is NOT in user space.
 *	Return 0 if ok, -EIO if any error.
 */
 
ssize_t umsdos_emd_dir_read (struct inode *emd_dir,
	struct file *filp,
	char *buf,	/* buffer in kernel memory, not in user space */
	size_t count,
	loff_t *offs
	)
{
	loff_t myofs=0;
	long int ret = 0;
	int sizeread;
	
	
#ifdef __BIG_ENDIAN
	struct umsdos_dirent *d = (struct umsdos_dirent *)buf;
#endif

	if (offs) myofs=*offs;	/* if offs is not NULL, read it */
	filp->f_flags = 0;
	sizeread = umsdos_file_read_kmem (emd_dir, filp, buf, count, &myofs);
	if (sizeread != count){
		printk ("UMSDOS: problem with EMD file. Can't read pos = %Ld (%d != %d)\n"
			,filp->f_pos,sizeread,count);
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
	if (offs) *offs=myofs;	/* if offs is not NULL, store myofs there */
	return ret;

}




/*
	Locate the EMD file in a directory .

	Return NULL if error. If ok, dir->u.umsdos_i.emd_inode 
*/
struct inode *umsdos_emd_dir_lookup (struct inode *dir, int creat)
{
    struct inode *ret = NULL;
    int res;
    Printk (("Entering umsdos_emd_dir_lookup\n"));
    if (dir->u.umsdos_i.i_emd_dir != 0){
	ret = iget (dir->i_sb,dir->u.umsdos_i.i_emd_dir);
	Printk (("umsdos_emd_dir_lookup: deja trouve %ld %p\n"
		 ,dir->u.umsdos_i.i_emd_dir,ret));
    } else {
      Printk ((KERN_DEBUG "umsdos /mn/: Looking for %20s -", UMSDOS_EMD_FILE));
      res = compat_umsdos_real_lookup (dir, UMSDOS_EMD_FILE, UMSDOS_EMD_NAMELEN, &ret);
      Printk ((KERN_DEBUG "-returned %d\n", res));
	Printk ((KERN_DEBUG "emd_dir_lookup "));
	if (ret != NULL){
	    Printk ((KERN_DEBUG "Found --linux "));
	    dir->u.umsdos_i.i_emd_dir = ret->i_ino;
	} else if (creat) {
                        int code;
                        Printk ((KERN_ERR " * ERROR * /mn/: creat not yet implemented!!!!" ));
                        Printk ((KERN_DEBUG "avant create "));
                        atomic_inc(&dir->i_count);

                        code = compat_msdos_create (dir,UMSDOS_EMD_FILE,UMSDOS_EMD_NAMELEN
                                ,S_IFREG|0777,&ret);
                        Printk ((KERN_DEBUG "Creat EMD code %d ret %x ",code,ret));
                        if (ret != NULL){
                                dir->u.umsdos_i.i_emd_dir = ret->i_ino;
                        }else{
                                printk ("UMSDOS: Can't create EMD file\n");
                        }
	}
	
	if (ret != NULL){
	  /* Disable UMSDOS_notify_change() for EMD file */
	  ret->u.umsdos_i.i_emd_owner = 0xffffffff;
	}

    }
    
    Printk ((KERN_DEBUG "umsdos_emd_dir_lookup returning %p /mn/\n", ret));
    if (ret != NULL) Printk ((KERN_DEBUG " debug : returning ino=%d\n",ret->i_ino));
    return ret;
}

/*
	creates an EMD file

	Return NULL if error. If ok, dir->u.umsdos_i.emd_inode 
*/

struct inode *umsdos_emd_dir_create(struct inode *dir, struct dentry *dentry,int mode)
{
	struct inode *ret = NULL;
	if (dir->u.umsdos_i.i_emd_dir != 0){
		ret = iget (dir->i_sb,dir->u.umsdos_i.i_emd_dir);
		Printk (("deja trouve %d %x",dir->u.umsdos_i.i_emd_dir,ret));
	}else{
	    
	    int code;
	    Printk (("avant create "));
	    dir->i_count++;
	    /*			
	       code = msdos_create (dir,UMSDOS_EMD_FILE,UMSDOS_EMD_NAMELEN
	       ,S_IFREG|0777,&ret);
	       
	       FIXME, I think I need a new dentry here
	    */
	    code = compat_msdos_create (dir,UMSDOS_EMD_FILE,UMSDOS_EMD_NAMELEN, S_IFREG|0777, &ret);
	    Printk (("Creat EMD code %d ret %x ",code,ret));
	    if (ret != NULL){
		dir->u.umsdos_i.i_emd_dir = ret->i_ino;
	    }else{
		printk ("UMSDOS: Can't create EMD file\n");
	    }
	}

	if (ret != NULL){
	    /* Disable UMSDOS_notify_change() for EMD file */
	    ret->u.umsdos_i.i_emd_owner = 0xffffffff;
	}
	return ret;
}



/*
	Read an entry from the EMD file.
	Support variable length record.
	Return -EIO if error, 0 if ok.
*/
int umsdos_emd_dir_readentry (
	struct inode *emd_dir,
	struct file *filp,
	struct umsdos_dirent *entry)
{
	int ret;
	Printk ((KERN_DEBUG "umsdos_emd_dir_readentry /mn/: entering.\n"));
	Printk (("umsdos_emd_dir_readentry /mn/: trying to lookup %12s (ino=%d) using EMD %d\n", filp->f_dentry->d_name.name, filp->f_dentry->d_inode->i_ino, emd_dir->i_ino));
    
	ret = umsdos_emd_dir_read(emd_dir, filp, (char*)entry, UMSDOS_REC_SIZE, NULL);
	if (ret == 0){ /* note /mn/: is this wrong? ret is allways 0 or -EIO. but who knows. It used to work this way... */
		/* Variable size record. Maybe, we have to read some more */
		int recsize = umsdos_evalrecsize (entry->name_len);
		Printk ((KERN_DEBUG "umsdos_emd_dir_readentry /mn/: FIXME if %d > %d?\n",recsize, UMSDOS_REC_SIZE));
		if (recsize > UMSDOS_REC_SIZE){
			ret = umsdos_emd_dir_read(emd_dir, filp
				,((char*)entry)+UMSDOS_REC_SIZE,recsize - UMSDOS_REC_SIZE,NULL);
			
		}
	}
	Printk (("umsdos_emd_dir_readentry /mn/: returning %d.\n", ret));
	return ret;
}




/*
	Write an entry in the EMD file.
	Return 0 if ok, -EIO if some error.
*/
int umsdos_writeentry (
	struct inode *dir,
	struct inode *emd_dir,
	struct umsdos_info *info,
	int free_entry)		/* This entry is deleted, so Write all 0's */
{
	int ret = 0;
	struct file filp;
	struct umsdos_dirent *entry = &info->entry;
	struct umsdos_dirent entry0;
	
	Printk (("umsdos_writeentry /mn/: entering...\n"));
	
	Printk ((KERN_ERR "umsdos_writeentry /mn/: FIXME! this is READ ONLY FOR NOW. RETURNING...\n"));
	return -EIO;
	
	if (free_entry){
		/* #Specification: EMD file / empty entries
			Unused entry in the EMD file are identify
			by the name_len field equal to 0. However to
			help future extension (or bug correction :-( ),
			empty entries are filled with 0.
		*/
		memset (&entry0,0,sizeof(entry0));
		entry = &entry0;
	}else if (entry->name_len > 0){
		memset (entry->name+entry->name_len,'\0'
			,sizeof(entry->name)-entry->name_len);
		/* #Specification: EMD file / spare bytes
			10 bytes are unused in each record of the EMD. They
			are set to 0 all the time. So it will be possible
			to do new stuff and rely on the state of those
			bytes in old EMD file around.
		*/
		memset (entry->spare,0,sizeof(entry->spare));
	}

	Printk (("umsdos_writeentry /mn/: if passed...\n"));

	filp.f_pos = info->f_pos;
	filp.f_reada = 0;
	ret = umsdos_emd_dir_write(emd_dir, &filp,(char*)entry,info->recsize,NULL);
	if (ret != 0){
		printk ("UMSDOS: problem with EMD file. Can't write\n");
	}else{
		dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		/* dir->i_dirt = 1; FIXME iput/dput ??? */
	}

	Printk (("umsdos_writeentry /mn/: returning...\n"));
	return ret;
}

#define CHUNK_SIZE (8*UMSDOS_REC_SIZE)
struct find_buffer{
	char buffer[CHUNK_SIZE];
	int pos;	/* read offset in buffer */
	int size;	/* Current size of buffer */
	struct file filp;
};





/*
	Fill the read buffer and take care of the byte remaining inside.
	Unread bytes are simply move to the beginning.

	Return -ENOENT if EOF, 0 if ok, a negative error code if any problem.
*/
static int umsdos_fillbuf (
	struct inode *inode,
	struct find_buffer *buf)
{
	int ret = -ENOENT;
	int mustmove = buf->size - buf->pos;
	int mustread;
	int remain;
	
	Printk ((KERN_DEBUG "Entering umsdos_fillbuf, for inode %d, buf=%p\n",inode->i_ino, buf));
	
	if (mustmove > 0){
		memcpy (buf->buffer,buf->buffer+buf->pos,mustmove);
	}
	buf->pos = 0;
	mustread = CHUNK_SIZE - mustmove;
	remain = inode->i_size - buf->filp.f_pos;
	if (remain < mustread) mustread = remain;
	if (mustread > 0){
		ret = umsdos_emd_dir_read (inode, &buf->filp,buf->buffer+mustmove
			,mustread,NULL);
		if (ret == 0) buf->size = mustmove + mustread;		
	}else if (mustmove){
		buf->size = mustmove;
		ret = 0;
	}
	return ret;
}



/*
	General search, locate a name in the EMD file or an empty slot to
	store it. if info->entry.name_len == 0, search the first empty
	slot (of the proper size).

	Caller must do iput on *pt_emd_dir.

	Return 0 if found, -ENOENT if not found, another error code if
	other problem.

	So this routine is used to either find an existing entry or to
	create a new one, while making sure it is a new one. After you
	get -ENOENT, you make sure the entry is stuffed correctly and
	call umsdos_writeentry().

	To delete an entry, you find it, zero out the entry (memset)
	and call umsdos_writeentry().

	All this to say that umsdos_writeentry must be call after this
	function since it rely on the f_pos field of info.
*/
static int umsdos_find (
	struct inode *dir,
	struct umsdos_info *info,		/* Hold name and name_len */
									/* Will hold the entry found */
	struct inode **pt_emd_dir)		/* Will hold the emd_dir inode */
									/* or NULL if not found */
{
	/* #Specification: EMD file structure
		The EMD file uses a fairly simple layout. It is made of records
		(UMSDOS_REC_SIZE == 64). When a name can't be written is a single
		record, multiple contiguous record are allocated.
	*/
	int ret = -ENOENT;
	/* FIXME */
	struct inode *emd_dir = umsdos_emd_dir_lookup(dir,1);
	if (emd_dir != NULL){
		struct umsdos_dirent *entry = &info->entry;
		int recsize = info->recsize;
		struct {
			off_t posok;	/* Position available to store the entry */
			int found;		/* A valid empty position has been found */
			off_t one;		/* One empty position -> maybe <- large enough */
			int onesize;	/* size of empty region starting at one */
		}empty;
		/* Read several entries at a time to speed up the search */
		struct find_buffer buf;
		struct dentry *dentry;

		memset (&buf.filp, 0, sizeof (buf.filp));

		dentry = creat_dentry ("umsfind-mn", 10, emd_dir);
	
		buf.filp.f_pos = 0;
		buf.filp.f_reada = 1;
		buf.filp.f_flags = O_RDONLY;
		buf.filp.f_dentry = dentry;
        	buf.filp.f_op = &umsdos_file_operations;	/* /mn/ - we have to fill it with dummy values so we won't segfault */

		buf.pos = 0;
		buf.size = 0;

		empty.found = 0;
		empty.posok = emd_dir->i_size;
		empty.onesize = 0;
		while (1){
			struct umsdos_dirent *rentry = (struct umsdos_dirent*)
				(buf.buffer + buf.pos);
			int file_pos = buf.filp.f_pos - buf.size + buf.pos;
			if (buf.pos == buf.size){
				ret = umsdos_fillbuf (emd_dir,&buf);
				if (ret < 0){
					/* Not found, so note where it can be added */
					info->f_pos = empty.posok;
					break;
				}
			}else if (rentry->name_len == 0){
				/* We are looking for an empty section at least */
				/* recsize large */
				if (entry->name_len == 0){
					info->f_pos = file_pos;
					ret = 0;
					break;
				}else if (!empty.found){
					if (empty.onesize == 0){
						/* This is the first empty record of a section */
						empty.one = file_pos;
					}
					/* grow the empty section */
					empty.onesize += UMSDOS_REC_SIZE;
					if (empty.onesize == recsize){
						/* here is a large enough section */
						empty.posok = empty.one;
						empty.found = 1;
					}
				}
				buf.pos += UMSDOS_REC_SIZE;
			}else{
				int entry_size = umsdos_evalrecsize(rentry->name_len);
				if (buf.pos+entry_size > buf.size){
					ret = umsdos_fillbuf (emd_dir,&buf);
					if (ret < 0){
						/* Not found, so note where it can be added */
						info->f_pos = empty.posok;
						break;
					}
				}else{
					empty.onesize = 0;	/* Reset the free slot search */
					if (entry->name_len == rentry->name_len
						&& memcmp(entry->name,rentry->name,rentry->name_len)
							==0){
						info->f_pos = file_pos;
						*entry = *rentry;
						ret = 0;
						break;
					}else{
						buf.pos += entry_size;
					}
				}
			}	
		}
		umsdos_manglename(info);
	}
	*pt_emd_dir = emd_dir;
	return ret;
}

/*
	Add a new entry in the emd file
	Return 0 if ok or a negative error code.
	Return -EEXIST if the entry already exist.

	Complete the information missing in info.
*/
int umsdos_newentry (
	struct inode *dir,
	struct umsdos_info *info)
{
	struct inode *emd_dir;
	int ret = umsdos_find (dir,info,&emd_dir);
	if (ret == 0){
		ret = -EEXIST;
	}else if (ret == -ENOENT){
		ret = umsdos_writeentry(dir,emd_dir,info,0);
		Printk (("umsdos_newentry EDM ret = %d\n",ret));
	}
	iput (emd_dir);
	return ret;
}

/*
	Create a new hidden link.
	Return 0 if ok, an error code if not.
*/
int umsdos_newhidden (
	struct inode *dir,
	struct umsdos_info *info)
{
	struct inode *emd_dir;
	int ret;
	umsdos_parse ("..LINK",6,info);
	info->entry.name_len = 0;
	ret = umsdos_find (dir,info,&emd_dir);
	iput (emd_dir);
	if (ret == -ENOENT || ret == 0){
		/* #Specification: hard link / hidden name
			When a hard link is created, the original file is renamed
			to a hidden name. The name is "..LINKNNN" where NNN is a
			number define from the entry offset in the EMD file.
		*/
		info->entry.name_len = sprintf (info->entry.name,"..LINK%ld"
			,info->f_pos);
		ret = 0;
	}
	return ret;
}
/*
	Remove an entry from the emd file
	Return 0 if ok, a negative error code otherwise.

	Complete the information missing in info.
*/
int umsdos_delentry (
	struct inode *dir,
	struct umsdos_info *info,
	int isdir)
{
	struct inode *emd_dir;
	int ret = umsdos_find (dir,info,&emd_dir);
	if (ret == 0){
		if (info->entry.name_len != 0){
			if ((isdir != 0) != (S_ISDIR(info->entry.mode) != 0)){
				if (S_ISDIR(info->entry.mode)){
					ret = -EISDIR;
				}else{
					ret = -ENOTDIR;
				}
			}else{
				ret = umsdos_writeentry(dir,emd_dir,info,1);
			}
		}
	}
	iput(emd_dir);
	return ret;
}


/*
	Verify is a EMD directory is empty.
	Return 0 if not empty
		   1 if empty
		   2 if empty, no EMD file.
*/
int umsdos_isempty (struct inode *dir)
{
	struct dentry *dentry;
	
	int ret = 2;
	struct inode *emd_dir = umsdos_emd_dir_lookup(dir,0);
	/* If the EMD file does not exist, it is certainly empty :-) */
	if (emd_dir != NULL){
		struct file filp;
		/* Find an empty slot */
		memset (&filp, 0, sizeof (filp));

		dentry = creat_dentry ("isempty-mn", 10, dir);
	
		filp.f_pos = 0;
		filp.f_reada = 1;
		filp.f_flags = O_RDONLY;
		filp.f_dentry = dentry;
        	filp.f_op = &umsdos_file_operations;	/* /mn/ - we have to fill it with dummy values so we won't segfault */
        	
		ret = 1;
		while (filp.f_pos < emd_dir->i_size){
			struct umsdos_dirent entry;
			if (umsdos_emd_dir_readentry(emd_dir,&filp,&entry)!=0){
				ret = 0;
				break;
			}else if (entry.name_len != 0){
				ret = 0;
				break;
			}	
		}
		iput (emd_dir);
	}
	return ret;
}

/*
	Locate an entry in a EMD directory.
	Return 0 if ok, errcod if not, generally -ENOENT.
*/
int umsdos_findentry (
	struct inode *dir,
	struct umsdos_info *info,
	int expect)		/* 0: anything */
					/* 1: file */
					/* 2: directory */
{
	struct inode *emd_dir;
	int ret = umsdos_find (dir,info,&emd_dir);
	if (ret == 0){
		if (expect != 0){
			if (S_ISDIR(info->entry.mode)){
				if (expect != 2) ret = -EISDIR;
			}else if (expect == 2){
				ret = -ENOTDIR;
			}
		}
	}
	iput (emd_dir);
	return ret;
}

