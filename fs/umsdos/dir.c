/*
 *  linux/fs/umsdos/dir.c
 *
 *  Written 1993 by Jacques Gelinas
 *	Inspired from linux/fs/msdos/... : Werner Almesberger
 *
 *  Extended MS-DOS directory handling functions
 */

#ifdef MODULE
#include <linux/module.h>
#endif

#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/string.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/limits.h>
#include <linux/umsdos_fs.h>
#include <linux/malloc.h>

#define PRINTK(x)
#define Printk(x) printk x

#define UMSDOS_SPECIAL_DIRFPOS	3
extern struct inode *pseudo_root;
/*
	So  grep *  doesn't complain in the presence of directories.
*/
int UMSDOS_dir_read(struct inode *inode,struct file *filp,char *buf,
    int count)
{
	return -EISDIR;
}
#define NAME_OFFSET(de) ((int) ((de)->d_name - (char *) (de)))
#define ROUND_UP(x) (((x)+3) & ~3)

/*
	Read count directory entries from directory filp
	Return a negative value from linux/errno.h.
	Return > 0 if success (The amount of byte written in
	dirent round_up to a word size (32 bits).

	This function is used by the normal readdir VFS entry point and by
	some function who try to find out info on a file from a pure MSDOS
	inode. See umsdos_locate_ancestor() below.
*/
static int umsdos_readdir_x(
	struct inode *dir,		/* Point to a description of the super block */
	struct file *filp,		/* Point to a directory which is read */
    struct dirent *dirent,	/* Will hold count directory entry */
	int dirent_in_fs,		/* dirent point in user's space ? */
	int count,
	struct umsdos_dirent *u_entry,	/* Optional umsdos entry */
	int follow_hlink,
	off_t *pt_f_pos)		/* will hold the offset of the entry in EMD */
{
	int ret = 0;
	
	umsdos_startlookup(dir);	
	if (filp->f_pos == UMSDOS_SPECIAL_DIRFPOS
		&& dir == pseudo_root
		&& dirent_in_fs){
		/*
			We don't need to simulate this pseudo directory
			when umsdos_readdir_x is called for internal operation
			of umsdos. This is why dirent_in_fs is tested
		*/
		/* #Specification: pseudo root / directory /DOS
			When umsdos operates in pseudo root mode (C:\linux is the
			linux root), it simulate a directory /DOS which points to
			the real root of the file system.
		*/
		put_fs_long(dir->i_sb->s_mounted->i_ino,&dirent->d_ino);
		memcpy_tofs (dirent->d_name,"DOS",3);
		put_fs_byte(0,dirent->d_name+3);
		put_fs_word (3,&dirent->d_reclen);
		if (u_entry != NULL) u_entry->flags = 0;
		ret = ROUND_UP(NAME_OFFSET(dirent) + 3 + 1);
		filp->f_pos++;
	}else if (filp->f_pos < 2
		|| (dir != dir->i_sb->s_mounted && filp->f_pos == 32)){
		/* #Specification: readdir / . and ..
			The msdos filesystem manage the . and .. entry properly
			so the EMD file won't hold any info about it.

			In readdir, we assume that for the root directory
			the read position will be 0 for ".", 1 for "..". For
			a non root directory, the read position will be 0 for "."
			and 32 for "..".
		*/
		/*
			This is a trick used by the msdos file system (fs/msdos/dir.c)
			to manage . and .. for the root directory of a file system.
			Since there is no such entry in the root, fs/msdos/dir.c
			use the following:

			if f_pos == 0, return ".".
			if f_pos == 1, return "..".

			So let msdos handle it

			Since umsdos entries are much larger, we share the same f_pos.
			if f_pos is 0 or 1 or 32, we are clearly looking at . and
			..

			As soon as we get f_pos == 2 or f_pos == 64, then back to
			0, but this time we are reading the EMD file.

			Well, not so true. The problem, is that UMSDOS_REC_SIZE is
			also 64, so as soon as we read the first record in the
			EMD, we are back at offset 64. So we set the offset
			to UMSDOS_SPECIAL_DIRFPOS(3) as soon as we have read the
			.. entry from msdos.
		*/
		ret = msdos_readdir(dir,filp,dirent,count);
		if (filp->f_pos == 64) filp->f_pos = UMSDOS_SPECIAL_DIRFPOS;
		if (u_entry != NULL) u_entry->flags = 0;
	}else{
		struct inode *emd_dir = umsdos_emd_dir_lookup(dir,0);
		if (emd_dir != NULL){
			if (filp->f_pos <= UMSDOS_SPECIAL_DIRFPOS+1) filp->f_pos = 0;
			PRINTK (("f_pos %ld i_size %d\n",filp->f_pos,emd_dir->i_size));
			ret = 0;
			while (filp->f_pos < emd_dir->i_size){
				struct umsdos_dirent entry;
				off_t cur_f_pos = filp->f_pos;
				if (umsdos_emd_dir_readentry (emd_dir,filp,&entry)!=0){
					ret = -EIO;
					break;
				}else if (entry.name_len != 0){
					/* #Specification: umsdos / readdir
						umsdos_readdir() should fill a struct dirent with
						an inode number. The cheap way to get it is to
						do a lookup in the MSDOS directory for each
						entry processed by the readdir() function.
						This is not very efficient, but very simple. The
						other way around is to maintain a copy of the inode
						number in the EMD file. This is a problem because
						this has to be maintained in sync using tricks.
						Remember that MSDOS (the OS) does not update the
						modification time (mtime) of a directory. There is
						no easy way to tell that a directory was modified
						during a DOS session and synchronise the EMD file.

						Suggestion welcome.

						So the easy way is used!
					*/
					struct umsdos_info info;
					struct inode *inode;
					int lret;
					umsdos_parse (entry.name,entry.name_len,&info);
					info.f_pos = cur_f_pos;
					*pt_f_pos = cur_f_pos;
					umsdos_manglename (&info);
					lret = umsdos_real_lookup (dir,info.fake.fname
						,info.fake.len,&inode);
					PRINTK (("Cherche inode de %s lret %d flags %d\n"
						,info.fake.fname,lret,entry.flags));
					if (lret == 0
						&& (entry.flags & UMSDOS_HLINK)
						&& follow_hlink){
						struct inode *rinode;
						lret = umsdos_hlink2inode (inode,&rinode);
						inode = rinode;
					}
					if (lret == 0){
						/* #Specification: pseudo root / reading real root
							The pseudo root (/linux) is logically
							erased from the real root. This mean that
							ls /DOS, won't show "linux". This avoids
							infinite recursion /DOS/linux/DOS/linux while
							walking the file system.
						*/
						if (inode != pseudo_root){
							PRINTK (("Trouve ino %d ",inode->i_ino));
							if (dirent_in_fs){
								put_fs_long(inode->i_ino,&dirent->d_ino);
								memcpy_tofs (dirent->d_name,entry.name
									,entry.name_len);
								put_fs_byte(0,dirent->d_name+entry.name_len);
								put_fs_word (entry.name_len
									,&dirent->d_reclen);
								/* In this case, the caller only needs */
								/* flags */
								if (u_entry != NULL){
									u_entry->flags = entry.flags;
								}
							}else{
								dirent->d_ino = inode->i_ino;
								memcpy (dirent->d_name,entry.name
									,entry.name_len);
								dirent->d_name[entry.name_len] = '\0';
								dirent->d_reclen = entry.name_len;
								if (u_entry != NULL) *u_entry = entry;
							}
							ret = ROUND_UP(NAME_OFFSET(dirent) + entry.name_len + 1);
							iput (inode);
							break;
						}
						iput (inode);
					}else{
						/* #Specification: umsdos / readdir / not in MSDOS
							During a readdir operation, if the file is not
							in the MSDOS directory anymore, the entry is
							removed from the EMD file silently.
						*/
						ret = umsdos_writeentry (dir,emd_dir,&info,1);
						if (ret != 0){
							break;
						}
					}
				}
			}
			iput(emd_dir);
		}
	}
	umsdos_endlookup(dir);	
	PRINTK (("read dir %p pos %d ret %d\n",dir,filp->f_pos,ret));
	return ret;
}
/*
	Read count directory entries from directory filp
	Return a negative value from linux/errno.h.
	Return > 0 if success (the amount of byte written to dirent)
*/
static int UMSDOS_readdir(
	struct inode *dir,		/* Point to a description of the super block */
	struct file *filp,		/* Point to a directory which is read */
    struct dirent *dirent,	/* Will hold count directory entry */
	int count)
{
	int ret = -ENOENT;
	while (1){
		struct umsdos_dirent entry;
		off_t f_pos;
		ret = umsdos_readdir_x (dir,filp,dirent,1,count,&entry,1,&f_pos);
		if (ret <= 0 || !(entry.flags & UMSDOS_HIDDEN)) break;
	}
	return ret;
}
/*
	Complete the inode content with info from the EMD file
*/
void umsdos_lookup_patch (
	struct inode *dir,
	struct inode *inode,
	struct umsdos_dirent *entry,
	off_t  emd_pos)
{
	/*
		This function modify the state of a dir inode. It decides
		if the dir is a umsdos dir or a dos dir. This is done
		deeper in umsdos_patch_inode() called at the end of this function.

		umsdos_patch_inode() may block because it is doing disk access.
		At the same time, another process may get here to initialise
		the same dir inode. There is 3 cases.

		1-The inode is already initialised. We do nothing.
		2-The inode is not initialised. We lock access and do it.
		3-Like 2 but another process has lock the inode, so we try
		  to lock it and right after check if initialisation is still
		  needed.


		Thanks to the mem option of the kernel command line, it was
		possible to consistently reproduce this problem by limiting
		my mem to 4 meg and running X.
	*/
	/*
		Do this only if the inode is freshly read, because we will lose
		the current (updated) content.
	*/
	/*
		A lookup of a mount point directory yield the inode into
		the other fs, so we don't care about initialising it. iget()
		does this automatically.
	*/
	if (inode->i_sb == dir->i_sb && !umsdos_isinit(inode)){
		if (S_ISDIR(inode->i_mode)) umsdos_lockcreate(inode);
		if (!umsdos_isinit(inode)){
			/* #Specification: umsdos / lookup / inode info
				After successfully reading an inode from the MSDOS
				filesystem, we use the EMD file to complete it.
				We update the following field.

				uid, gid, atime, ctime, mtime, mode.

				We rely on MSDOS for mtime. If the file
				was modified during an MSDOS session, at least
				mtime will be meaningful. We do this only for regular
				file.
				
				We don't rely on MSDOS for mtime for directory because
				the MSDOS directory date is creation time (strange
				MSDOS behavior) which fit nowhere in the three UNIX
				time stamp.
			*/
			if (S_ISREG(entry->mode)) entry->mtime = inode->i_mtime;
			inode->i_mode  = entry->mode;
			inode->i_rdev  = entry->rdev;
			inode->i_atime = entry->atime;
			inode->i_ctime = entry->ctime;
			inode->i_mtime = entry->mtime;
			inode->i_uid   = entry->uid;
			inode->i_gid   = entry->gid;
			/* #Specification: umsdos / i_nlink
				The nlink field of an inode is maintain by the MSDOS file system
				for directory and by UMSDOS for other file. The logic is that
				MSDOS is already figuring out what to do for directories and
				does nothing for other files. For MSDOS, there are no hard link
				so all file carry nlink==1. UMSDOS use some info in the
				EMD file to plug the correct value.
			*/
			if (!S_ISDIR(entry->mode)){
				if (entry->nlink > 0){
					inode->i_nlink = entry->nlink;
				}else{
					printk ("UMSDOS: lookup_patch entry->nlink < 1 ???\n");
				}
			}
			umsdos_patch_inode(inode,dir,emd_pos);
		}
		if (S_ISDIR(inode->i_mode)) umsdos_unlockcreate(inode);
if (inode->u.umsdos_i.i_emd_owner==0) printk ("emd_owner still 0 ???\n");
	}
}
/*
	Locate entry of an inode in a directory.
	Return 0 or a negative error code.

	Normally, this function must succeed. It means a strange corruption
	in the file system if not.
*/
int umsdos_inode2entry (
	struct inode *dir,
	struct inode *inode,
	struct umsdos_dirent *entry)	/* Will hold the entry */
{
	int ret = -ENOENT;
	if (inode == pseudo_root){
		/*
			Quick way to find the name.
			Also umsdos_readdir_x won't show /linux anyway
		*/
		memcpy (entry->name,UMSDOS_PSDROOT_NAME,UMSDOS_PSDROOT_LEN+1);
		entry->name_len = UMSDOS_PSDROOT_LEN;
		ret = 0;
	}else{
		struct inode *emddir = umsdos_emd_dir_lookup(dir,0);
		iput (emddir);
		if (emddir == NULL){
			/* This is a DOS directory */
			struct file filp;
			filp.f_reada = 1;
			filp.f_pos = 0;
			while (1){
				struct dirent dirent;
				if (umsdos_readdir_kmem (dir,&filp,&dirent,1) <= 0){
					printk ("UMSDOS: can't locate inode %ld in DOS directory???\n"
						,inode->i_ino);
				}else if (dirent.d_ino == inode->i_ino){
					ret = 0;
					memcpy (entry->name,dirent.d_name,dirent.d_reclen);
					entry->name[dirent.d_reclen] = '\0';
					entry->name_len = dirent.d_reclen;
					inode->u.umsdos_i.i_dir_owner = dir->i_ino;
					inode->u.umsdos_i.i_emd_owner = 0;
					umsdos_setup_dir_inode(inode);
					break;
				}
			}
		}else{
			/* skip . and .. see umsdos_readdir_x() */
			struct file filp;
			filp.f_reada = 1;
			filp.f_pos = UMSDOS_SPECIAL_DIRFPOS;
			while (1){
				struct dirent dirent;
				off_t f_pos;
				if (umsdos_readdir_x(dir,&filp,&dirent
					,0,1,entry,0,&f_pos) <= 0){
					printk ("UMSDOS: can't locate inode %ld in EMD file???\n"
						,inode->i_ino);
					break;
				}else if (dirent.d_ino == inode->i_ino){
					ret = 0;
					umsdos_lookup_patch (dir,inode,entry,f_pos);
					break;
				}
			}
		}
	}
	return ret;
}
/*
	Locate the parent of a directory and the info on that directory
	Return 0 or a negative error code.
*/
static int umsdos_locate_ancestor (
	struct inode *dir,
	struct inode **result,
	struct umsdos_dirent *entry)
{
	int ret;
	umsdos_patch_inode (dir,NULL,0);
	ret = umsdos_real_lookup (dir,"..",2,result);
	PRINTK (("result %d %x ",ret,*result));
	if (ret == 0){
		struct inode *adir = *result;
		ret = umsdos_inode2entry (adir,dir,entry);
	}
	PRINTK (("\n"));
	return ret;
}
/*
	Build the path name of an inode (relative to the file system.
	This function is need to set (pseudo) hard link.

	It uses the same strategy as the standard getcwd().
*/
int umsdos_locate_path (
	struct inode *inode,
	char *path)
{
	int ret = 0;
	struct inode *dir = inode;
	char *bpath = (char*)kmalloc(PATH_MAX,GFP_KERNEL);
	if (bpath == NULL){
		ret = -ENOMEM;
	}else{
		struct umsdos_dirent entry;
		char *ptbpath = bpath+PATH_MAX-1;
		*ptbpath = '\0';
		PRINTK (("locate_path mode %x ",inode->i_mode));
		if (!S_ISDIR(inode->i_mode)){
			ret = umsdos_get_dirowner (inode,&dir);
			PRINTK (("locate_path ret %d ",ret));
			if (ret == 0){
				ret = umsdos_inode2entry (dir,inode,&entry);
				if (ret == 0){
					ptbpath -= entry.name_len;
					memcpy (ptbpath,entry.name,entry.name_len);
					PRINTK (("ptbpath :%s: ",ptbpath));
				}
			}
		}else{
			dir->i_count++;
		}
		if (ret == 0){
			while (dir != dir->i_sb->s_mounted){
				struct inode *adir;
				ret = umsdos_locate_ancestor (dir,&adir,&entry);
				iput (dir);
				dir = NULL;
				PRINTK (("ancestor %d ",ret));
				if (ret == 0){
					*--ptbpath = '/';
					ptbpath -= entry.name_len;
					memcpy (ptbpath,entry.name,entry.name_len);
					dir = adir;
					PRINTK (("ptbpath :%s: ",ptbpath));
				}else{
					break;
				}
			}
		}
		strcpy (path,ptbpath);
		kfree (bpath);
	}
	PRINTK (("\n"));
	iput (dir);
	return ret;
}

/*
	Return != 0 if an entry is the pseudo DOS entry in the pseudo root.
*/
int umsdos_is_pseudodos (
	struct inode *dir,
	const char *name,
	int len)
{
	/* #Specification: pseudo root / DOS hard coded
		The pseudo sub-directory DOS in the pseudo root is hard coded.
		The name is DOS. This is done this way to help standardised
		the umsdos layout. The idea is that from now on /DOS is
		a reserved path and nobody will think of using such a path
		for a package.
	*/
	return dir == pseudo_root
		&& len == 3
		&& name[0] == 'D' && name[1] == 'O' && name[2] == 'S';
}
/*
	Check if a file exist in the current directory.
	Return 0 if ok, negative error code if not (ex: -ENOENT).
*/
static int umsdos_lookup_x (
	struct inode *dir,
	const char *name,
	int len,
	struct inode **result,	/* Will hold inode of the file, if successful */
	int nopseudo)			/* Don't care about pseudo root mode */
{
	int ret = -ENOENT;
	*result = NULL;
	umsdos_startlookup(dir);	
	if (len == 1 && name[0] == '.'){
		*result = dir;
		dir->i_count++;
		ret = 0;
	}else if (len == 2 && name[0] == '.' && name[1] == '.'){
		if (pseudo_root != NULL && dir == pseudo_root->i_sb->s_mounted){
			/* #Specification: pseudo root / .. in real root
				Whenever a lookup is those in the real root for
				the directory .., and pseudo root is active, the
				pseudo root is returned.
			*/
			ret = 0;
			*result = pseudo_root;
			pseudo_root->i_count++;
		}else{
			/* #Specification: locating .. / strategy
				We use the msdos filesystem to locate the parent directory.
				But it is more complicated than that.
				
				We have to step back even further to
				get the parent of the parent, so we can get the EMD
				of the parent of the parent. Using the EMD file, we can
				locate all the info on the parent, such a permissions
				and owner.
			*/
			ret = umsdos_real_lookup (dir,"..",2,result);
			PRINTK (("ancestor ret %d dir %p *result %p ",ret,dir,*result));
			if (ret == 0
				&& *result != dir->i_sb->s_mounted
				&& *result != pseudo_root){
				struct inode *aadir;
				struct umsdos_dirent entry;
				ret = umsdos_locate_ancestor (*result,&aadir,&entry);
				iput (aadir);
			}
		}
	}else if (umsdos_is_pseudodos(dir,name,len)){
		/* #Specification: pseudo root / lookup(DOS)
			A lookup of DOS in the pseudo root will always succeed
			and return the inode of the real root.
		*/
		*result = dir->i_sb->s_mounted;
		(*result)->i_count++;
		ret = 0;
	}else{
		struct umsdos_info info;
		ret = umsdos_parse (name,len,&info);
		if (ret == 0) ret = umsdos_findentry (dir,&info,0);
		PRINTK (("lookup %s pos %d ret %d len %d ",info.fake.fname,info.f_pos,ret
			,info.fake.len));
		if (ret == 0){
			/* #Specification: umsdos / lookup
				A lookup for a file is done in two step. First, we locate
				the file in the EMD file. If not present, we return
				an error code (-ENOENT). If it is there, we repeat the
				operation on the msdos file system. If this fails, it means
				that the file system is not in sync with the emd file.
				We silently remove this entry from the emd file,
				and return ENOENT.
			*/
			struct inode *inode;
			ret = umsdos_real_lookup (dir,info.fake.fname,info.fake.len,result);
			inode = *result;
			if (inode == NULL){
				printk ("UMSDOS: Erase entry %s, out of sync with MsDOS\n"
					,info.fake.fname);
				umsdos_delentry (dir,&info,S_ISDIR(info.entry.mode));
			}else{
				umsdos_lookup_patch (dir,inode,&info.entry,info.f_pos);
				PRINTK (("lookup ino %d flags %d\n",inode->i_ino
					,info.entry.flags));
				if (info.entry.flags & UMSDOS_HLINK){
					ret = umsdos_hlink2inode (inode,result);
				}
				if (*result == pseudo_root && !nopseudo){
					/* #Specification: pseudo root / dir lookup
						For the same reason as readdir, a lookup in /DOS for
						the pseudo root directory (linux) will fail.
					*/
					/*
						This has to be allowed for resolving hard link
						which are recorded independently of the pseudo-root
						mode.
					*/
					iput (pseudo_root);
					*result = NULL;
					ret = -ENOENT;
				}
			}
		}
	}
	umsdos_endlookup(dir);	
	iput (dir);
	return ret;
}
/*
	Check if a file exist in the current directory.
	Return 0 if ok, negative error code if not (ex: -ENOENT).
*/
int UMSDOS_lookup (
	struct inode *dir,
	const char *name,
	int len,
	struct inode **result)	/* Will hold inode of the file, if successful */
{
	return umsdos_lookup_x(dir,name,len,result,0);
}
/*
	Locate the inode pointed by a (pseudo) hard link
	Return 0 if ok, a negative error code if not.
*/
int umsdos_hlink2inode (struct inode *hlink, struct inode **result)
{
	int ret = -EIO;
	char *path = (char*)kmalloc(PATH_MAX,GFP_KERNEL);
	*result = NULL;
	if (path == NULL){
		ret = -ENOMEM;
		iput (hlink);
	}else{
		struct file filp;
		filp.f_reada = 1;
		filp.f_pos = 0;
		PRINTK (("hlink2inode "));
		if (umsdos_file_read_kmem (hlink,&filp,path,hlink->i_size)
			==hlink->i_size){
			struct inode *dir;
			char *pt = path;
			dir = hlink->i_sb->s_mounted;
			path[hlink->i_size] = '\0';
			iput (hlink);
			dir->i_count++;
			while (1){
				char *start = pt;
				int len;
				while (*pt != '\0' && *pt != '/') pt++;
				len = (int)(pt - start);
				if (*pt == '/') *pt++ = '\0';
				if (dir->u.umsdos_i.i_emd_dir == 0){
					/* This is a DOS directory */
					ret = umsdos_rlookup_x(dir,start,len,result,1);
				}else{
					ret = umsdos_lookup_x(dir,start,len,result,1);
				}
				PRINTK (("h2n lookup :%s: -> %d ",start,ret));
				if (ret == 0 && *pt != '\0'){
					dir = *result;
				}else{
					break;
				}
			}
		}else{
			iput (hlink);
		}
		PRINTK (("hlink2inode ret = %d %p -> %p\n",ret,hlink,*result));
		kfree (path);
	}
	return ret;
}

static struct file_operations umsdos_dir_operations = {
	NULL,				/* lseek - default */
	UMSDOS_dir_read,	/* read */
	NULL,				/* write - bad */
	UMSDOS_readdir,		/* readdir */
	NULL,				/* select - default */
	UMSDOS_ioctl_dir,	/* ioctl - default */
	NULL,				/* mmap */
	NULL,				/* no special open code */
	NULL,				/* no special release code */
	NULL				/* fsync */
};

struct inode_operations umsdos_dir_inode_operations = {
	&umsdos_dir_operations,	/* default directory file-ops */
	UMSDOS_create,		/* create */
	UMSDOS_lookup,		/* lookup */
	UMSDOS_link,		/* link */
	UMSDOS_unlink,		/* unlink */
	UMSDOS_symlink,		/* symlink */
	UMSDOS_mkdir,		/* mkdir */
	UMSDOS_rmdir,		/* rmdir */
	UMSDOS_mknod,		/* mknod */
	UMSDOS_rename,		/* rename */
	NULL,			/* readlink */
	NULL,			/* follow_link */
	NULL,			/* bmap */
	NULL,			/* truncate */
	NULL			/* permission */
};










