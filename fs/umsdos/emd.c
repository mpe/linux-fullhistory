/*
 *  linux/fs/umsdos/emd.c
 *
 *  Written 1993 by Jacques Gelinas
 *
 *  Extended MS-DOS directory handling functions
 */
#ifdef MODULE
#include <linux/module.h>
#endif

#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/kernel.h>
#include <asm/segment.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/msdos_fs.h>
#include <linux/umsdos_fs.h>

#define PRINTK(x)
#define Printk(x) printk x

int umsdos_readdir_kmem(
	struct inode *inode,
	struct file *filp,
	struct dirent *dirent,
	int count)
{
	int ret;
	int old_fs = get_fs();
	set_fs (KERNEL_DS);
	ret = msdos_readdir(inode,filp,dirent,count);
	set_fs (old_fs);
	return ret;
}
/*
	Read a file into kernel space memory
*/
int umsdos_file_read_kmem(
	struct inode *inode,
	struct file *filp,
	char *buf,
	int count)
{
	int ret;
	int old_fs = get_fs();	
	set_fs (KERNEL_DS);
	ret = msdos_file_read(inode,filp,buf,count);
	set_fs (old_fs);
	return ret;
}
/*
	Write to a file from kernel space
*/
int umsdos_file_write_kmem(
	struct inode *inode,
	struct file *filp,
	char *buf,
	int count)
{
	int ret;
	int old_fs = get_fs();
	set_fs (KERNEL_DS);
	ret = msdos_file_write(inode,filp,buf,count);
	set_fs (old_fs);
	return ret;
}


/*
	Write a block of bytes into one EMD file.
	The block of data is NOT in user space.

	Return 0 if ok, a negative error code if not.
*/
int umsdos_emd_dir_write (
	struct inode *emd_dir,
	struct file *filp,
	char *buf,	/* buffer in kernel memory, not in user space */
	int count)
{
	int written;
	filp->f_flags = 0;
	written = umsdos_file_write_kmem (emd_dir,filp,buf,count);
	return written != count ? -EIO : 0;
}
/*
	Read a block of bytes from one EMD file.
	The block of data is NOT in user space.
	Return 0 if ok, -EIO if any error.
*/
int umsdos_emd_dir_read (
	struct inode *emd_dir,
	struct file *filp,
	char *buf,	/* buffer in kernel memory, not in user space */
	int count)
{
	int ret = 0;
	int sizeread;
	filp->f_flags = 0;
	sizeread = umsdos_file_read_kmem (emd_dir,filp,buf,count);
	if (sizeread != count){
		printk ("UMSDOS: problem with EMD file. Can't read\n");
		ret = -EIO;
	}
	return ret;

}
/*
	Locate the EMD file in a directory and optionally, creates it.

	Return NULL if error. If ok, dir->u.umsdos_i.emd_inode 
*/
struct inode *umsdos_emd_dir_lookup(struct inode *dir, int creat)
{
	struct inode *ret = NULL;
	if (dir->u.umsdos_i.i_emd_dir != 0){
		ret = iget (dir->i_sb,dir->u.umsdos_i.i_emd_dir);
		PRINTK (("deja trouve %d %x [%d] "
			,dir->u.umsdos_i.i_emd_dir,ret,ret->i_count));
	}else{
		umsdos_real_lookup (dir,UMSDOS_EMD_FILE,UMSDOS_EMD_NAMELEN,&ret);
		PRINTK (("emd_dir_lookup "));
		if (ret != NULL){
			PRINTK (("Find --linux "));
			dir->u.umsdos_i.i_emd_dir = ret->i_ino;
		}else if (creat){
			int code;
			PRINTK (("avant create "));
			dir->i_count++;
			code = msdos_create (dir,UMSDOS_EMD_FILE,UMSDOS_EMD_NAMELEN
				,S_IFREG|0777,&ret);
			PRINTK (("Creat EMD code %d ret %x ",code,ret));
			if (ret != NULL){
				dir->u.umsdos_i.i_emd_dir = ret->i_ino;
			}else{
				printk ("UMSDOS: Can't create EMD file\n");
			}
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
	int ret = umsdos_emd_dir_read(emd_dir,filp,(char*)entry,UMSDOS_REC_SIZE);
	if (ret == 0){
		/* Variable size record. Maybe, we have to read some more */
		int recsize = umsdos_evalrecsize (entry->name_len);
		if (recsize > UMSDOS_REC_SIZE){
			ret = umsdos_emd_dir_read(emd_dir,filp
				,((char*)entry)+UMSDOS_REC_SIZE,recsize - UMSDOS_REC_SIZE);
			
		}
	}
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
	filp.f_pos = info->f_pos;
	filp.f_reada = 0;
	ret = umsdos_emd_dir_write(emd_dir,&filp,(char*)entry,info->recsize);
	if (ret != 0){
		printk ("UMSDOS: problem with EMD file. Can't write\n");
	}else{
		dir->i_ctime = dir->i_mtime = CURRENT_TIME;
		dir->i_dirt = 1;
	}
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
	if (mustmove > 0){
		memcpy (buf->buffer,buf->buffer+buf->pos,mustmove);
	}
	buf->pos = 0;
	mustread = CHUNK_SIZE - mustmove;
	remain = inode->i_size - buf->filp.f_pos;
	if (remain < mustread) mustread = remain;
	if (mustread > 0){
		ret = umsdos_emd_dir_read (inode,&buf->filp,buf->buffer+mustmove
			,mustread);
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
		buf.pos = 0;
		buf.size = 0;
		buf.filp.f_pos = 0;
		buf.filp.f_reada = 1;
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
		PRINTK (("umsdos_newentry EDM ret = %d\n",ret));
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
	int ret = 2;
	struct inode *emd_dir = umsdos_emd_dir_lookup(dir,0);
	/* If the EMD file does not exist, it is certainly empty :-) */
	if (emd_dir != NULL){
		struct file filp;
		/* Find an empty slot */
		filp.f_pos = 0;
		filp.f_reada = 1;
		filp.f_flags = O_RDONLY;
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

