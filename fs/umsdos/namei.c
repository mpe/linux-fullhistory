/*
 *  linux/fs/umsdos/namei.c
 *
 *	Written 1993 by Jacques Gelinas 
 *	Inspired from linux/fs/msdos/... by Werner Almesberger
 *
 * Maintain and access the --linux alternate directory file.
*/
#ifdef MODULE
#include <linux/module.h>
#endif

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/msdos_fs.h>
#include <linux/umsdos_fs.h>
#include <linux/malloc.h>

#define PRINTK(x)
#define Printk(x)	printk x

#if 1
/*
	Wait for creation exclusivity.
	Return 0 if the dir was already available.
	Return 1 if a wait was necessary.
		When 1 is return, it means a wait was done. It does not
		mean the directory is available.
*/
static int umsdos_waitcreate(struct inode *dir)
{
	int ret = 0;
	if (dir->u.umsdos_i.u.dir_info.creating
		&& dir->u.umsdos_i.u.dir_info.pid != current->pid){
		sleep_on(&dir->u.umsdos_i.u.dir_info.p);
		ret = 1;
	}
	return ret;
}
/*
	Wait for any lookup process to finish
*/
static void umsdos_waitlookup (struct inode *dir)
{
	while (dir->u.umsdos_i.u.dir_info.looking){
		sleep_on(&dir->u.umsdos_i.u.dir_info.p);
	}
}
/*
	Lock all other process out of this directory.
*/
void umsdos_lockcreate (struct inode *dir)
{
	/* #Specification: file creation / not atomic
		File creation is a two step process. First we create (allocate)
		an entry in the EMD file and then (using the entry offset) we
		build a unique name for MSDOS. We create this name in the msdos
		space.

		We have to use semaphore (sleep_on/wake_up) to prevent lookup
		into a directory when we create a file or directory and to
		prevent creation while a lookup is going on. Since many lookup
		may happen at the same time, the semaphore is a counter.

		Only one creation is allowed at the same time. This protection
		may not be necessary. The problem arise mainly when a lookup
		or a readdir is done while a file is partially created. The
		lookup process see that as a "normal" problem and silently
		erase the file from the EMD file. Normal because a file
		may be erased during a MSDOS session, but not removed from
		the EMD file.

		The locking is done on a directory per directory basis. Each
		directory inode has its wait_queue.

		For some operation like hard link, things even get worse. Many
		creation must occur at once (atomic). To simplify the design
		a process is allowed to recursively lock the directory for
		creation. The pid of the locking process is kept along with
		a counter so a second level of locking is granted or not.
	*/
	/*
		Wait for any creation process to finish except
		if we (the process) own the lock
	*/
	while (umsdos_waitcreate(dir)!=0);
	dir->u.umsdos_i.u.dir_info.creating++;
	dir->u.umsdos_i.u.dir_info.pid = current->pid;
	umsdos_waitlookup (dir);
}
/*
	Lock all other process out of those two directories.
*/
static void umsdos_lockcreate2 (struct inode *dir1, struct inode *dir2)
{
	/*
		We must check that both directory are available before
		locking anyone of them. This is to avoid some deadlock.
		Thanks to dglaude@is1.vub.ac.be (GLAUDE DAVID) for pointing
		this to me.
	*/
	while (1){
		if (umsdos_waitcreate(dir1)==0
			&& umsdos_waitcreate(dir2)==0){
			/* We own both now */
			dir1->u.umsdos_i.u.dir_info.creating++;
			dir1->u.umsdos_i.u.dir_info.pid = current->pid;
			dir2->u.umsdos_i.u.dir_info.creating++;
			dir2->u.umsdos_i.u.dir_info.pid = current->pid;
			break;
		}
	}
	umsdos_waitlookup(dir1);
	umsdos_waitlookup(dir2);
}
/*
	Wait until creation is finish in this directory.
*/
void umsdos_startlookup (struct inode *dir)
{
	while (umsdos_waitcreate (dir) != 0);
	dir->u.umsdos_i.u.dir_info.looking++;
}
void check_page_tables(void);

/*
	Unlock the directory.
*/
void umsdos_unlockcreate (struct inode *dir)
{
	dir->u.umsdos_i.u.dir_info.creating--;
	if (dir->u.umsdos_i.u.dir_info.creating < 0){
		printk ("UMSDOS: dir->u.umsdos_i.u.dir_info.creating < 0: %d"
			,dir->u.umsdos_i.u.dir_info.creating);
	}
	wake_up (&dir->u.umsdos_i.u.dir_info.p);
}
/*
	Tell directory lookup is over.
*/
void umsdos_endlookup (struct inode *dir)
{
	dir->u.umsdos_i.u.dir_info.looking--;
	if (dir->u.umsdos_i.u.dir_info.looking < 0){
		printk ("UMSDOS: dir->u.umsdos_i.u.dir_info.looking < 0: %d"
			,dir->u.umsdos_i.u.dir_info.looking);
	}
	wake_up (&dir->u.umsdos_i.u.dir_info.p);
}
#else
static void umsdos_lockcreate (struct inode *dir){}
static void umsdos_lockcreate2 (struct inode *dir1, struct inode *dir2){}
void umsdos_startlookup (struct inode *dir){}
static void umsdos_unlockcreate (struct inode *dir){}
void umsdos_endlookup (struct inode *dir){}
#endif
static int umsdos_nevercreat(
	struct inode *dir,
	const char *name,		/* Name of the file to add */
	int len,
	int errcod)				/* Length of the name */
{
	int ret = 0;
	if (umsdos_is_pseudodos(dir,name,len)){
		/* #Specification: pseudo root / any file creation /DOS
			The pseudo sub-directory /DOS can't be created!
			EEXIST is returned.

			The pseudo sub-directory /DOS can't be removed!
			EPERM is returned.
		*/
		ret = -EPERM;
		ret = errcod;
	}else if (name[0] == '.'
		&& (len == 1 || (len == 2 && name[1] == '.'))){
		/* #Specification: create / . and ..
			If one try to creates . or .., it always fail and return
			EEXIST.

			If one try to delete . or .., it always fail and return
			EPERM.

			This should be test at the VFS layer level to avoid
			duplicating this in all file systems. Any comments ?
		*/
		ret = errcod;
	}
	return ret;
}
	
/*
	Add a new file (ordinary or special) into the alternate directory.
	The file is added to the real MSDOS directory. If successful, it
	is then added to the EDM file.

	Return the status of the operation. 0 mean success.
*/
static int umsdos_create_any (
	struct inode *dir,
	const char *name,		/* Name of the file to add */
	int len,				/* Length of the name */
	int mode,				/* Permission bit + file type ??? */
	int rdev,				/* major, minor or 0 for ordinary file */
							/* and symlinks */
	char flags,
	struct inode **result)	/* Will hold the inode of the newly created */
							/* file */
{
	int ret = umsdos_nevercreat(dir,name,len,-EEXIST);
	if (ret == 0){
		struct umsdos_info info;
		ret = umsdos_parse (name,len,&info);
		*result = NULL;
		if (ret == 0){
			info.entry.mode = mode;
			info.entry.rdev = rdev;
			info.entry.flags = flags;
			info.entry.uid = current->fsuid;
			info.entry.gid = (dir->i_mode & S_ISGID)
				? dir->i_gid : current->fsgid;
			info.entry.ctime = info.entry.atime = info.entry.mtime
				= CURRENT_TIME;
			info.entry.nlink = 1;
			umsdos_lockcreate(dir);
			ret = umsdos_newentry (dir,&info);
			if (ret == 0){
				dir->i_count++;
				ret = msdos_create (dir,info.fake.fname,info.fake.len
					,S_IFREG|0777,result);
				if (ret == 0){
					struct inode *inode = *result;
					umsdos_lookup_patch (dir,inode,&info.entry,info.f_pos);
					PRINTK (("inode %p[%d] ",inode,inode->i_count));
					PRINTK (("Creation OK: [%d] %s %d pos %d\n",dir->i_ino
						,info.fake.fname,current->pid,info.f_pos));
				}else{
					/* #Specification: create / file exist in DOS
						Here is a situation. Trying to create a file with
						UMSDOS. The file is unknown to UMSDOS but already
						exist in the DOS directory.

						Here is what we are NOT doing:

						We could silently assume that everything is fine
						and allows the creation to succeed.

						It is possible not all files in the partition
						are mean to be visible from linux. By trying to create
						those file in some directory, one user may get access
						to those file without proper permissions. Looks like
						a security hole to me. Off course sharing a file system
						with DOS is some kind of security hole :-)

						So ?

						We return EEXIST in this case.
						The same is true for directory creation.
					*/
					if (ret == -EEXIST){
						printk ("UMSDOS: out of sync, Creation error [%ld], "
							"deleting %s %d %d pos %ld\n",dir->i_ino
							,info.fake.fname,-ret,current->pid,info.f_pos);
					}
					umsdos_delentry (dir,&info,0);
				}
				PRINTK (("umsdos_create %s ret = %d pos %d\n"
					,info.fake.fname,ret,info.f_pos));
			}
			umsdos_unlockcreate(dir);
		}
	}
	iput (dir);
	return ret;
}
/*
	Initialise the new_entry from the old for a rename operation.
	(Only useful for umsdos_rename_f() below).
*/
static void umsdos_ren_init(
	struct umsdos_info *new_info,
	struct umsdos_info *old_info,
	int flags)		/* 0 == copy flags from old_name */
					/* != 0, this is the value of flags */
{
	new_info->entry.mode = old_info->entry.mode;
	new_info->entry.rdev = old_info->entry.rdev;
	new_info->entry.uid = old_info->entry.uid;
	new_info->entry.gid = old_info->entry.gid;
	new_info->entry.ctime = old_info->entry.ctime;
	new_info->entry.atime = old_info->entry.atime;
	new_info->entry.mtime = old_info->entry.mtime;
	new_info->entry.flags = flags ? flags : old_info->entry.flags;
	new_info->entry.nlink = old_info->entry.nlink;
}

#define chkstk() \
	if (STACK_MAGIC != *(unsigned long *)current->kernel_stack_page){\
		printk(KERN_ALERT "UMSDOS: %s magic %x != %lx ligne %d\n" \
		, current->comm,STACK_MAGIC \
		,*(unsigned long *)current->kernel_stack_page \
		,__LINE__); \
	}
	
/*
	Rename a file (move) in the file system.
*/
static int umsdos_rename_f(
	struct inode * old_dir,
	const char * old_name,
	int old_len,
	struct inode * new_dir,
	const char * new_name,
	int new_len,
	int flags)		/* 0 == copy flags from old_name */
					/* != 0, this is the value of flags */
{
	int ret = -EPERM;
	struct umsdos_info old_info;
	int old_ret = umsdos_parse (old_name,old_len,&old_info);
	struct umsdos_info new_info;
	int new_ret = umsdos_parse (new_name,new_len,&new_info);
chkstk();
	PRINTK (("umsdos_rename %d %d ",old_ret,new_ret));
	if (old_ret == 0 && new_ret == 0){
		umsdos_lockcreate2(old_dir,new_dir);
chkstk();
		PRINTK (("old findentry "));
		ret = umsdos_findentry(old_dir,&old_info,0);
chkstk();
		PRINTK (("ret %d ",ret));
		if (ret == 0){
			/* check sticky bit on old_dir */
			if ( !(old_dir->i_mode & S_ISVTX) || fsuser() ||
			    current->fsuid == old_info.entry.uid ||
			    current->fsuid == old_dir->i_uid ) {
				/* Does new_name already exist? */
				PRINTK(("new findentry "));
				ret = umsdos_findentry(new_dir,&new_info,0);
				if (ret != 0 || /* if destination file exists, are we allowed to replace it ? */
				    !(new_dir->i_mode & S_ISVTX) || fsuser() ||
				    current->fsuid == new_info.entry.uid ||
				    current->fsuid == new_dir->i_uid ) {
					PRINTK (("new newentry "));
					umsdos_ren_init(&new_info,&old_info,flags);
					ret = umsdos_newentry (new_dir,&new_info);
chkstk();
					PRINTK (("ret %d %d ",ret,new_info.fake.len));
					if (ret == 0){
						PRINTK (("msdos_rename "));
						old_dir->i_count++;
						new_dir->i_count++;	/* Both inode are needed later */
						ret = msdos_rename (old_dir
								    ,old_info.fake.fname,old_info.fake.len
								    ,new_dir
								    ,new_info.fake.fname,new_info.fake.len);
chkstk();
						PRINTK (("after m_rename ret %d ",ret));
						if (ret != 0){
							umsdos_delentry (new_dir,&new_info
									 ,S_ISDIR(new_info.entry.mode));
chkstk();
						}else{
							ret = umsdos_delentry (old_dir,&old_info
									       ,S_ISDIR(old_info.entry.mode));
chkstk();
							if (ret == 0){
								/*
								   This UMSDOS_lookup does not look very useful.
								   It makes sure that the inode of the file will
								   be correctly setup (umsdos_patch_inode()) in
								   case it is already in use.
								   
								   Not very efficient ...
								   */
								struct inode *inode;
								new_dir->i_count++;
								PRINTK (("rename lookup len %d %d -- ",new_len,new_info.entry.flags));
								ret = UMSDOS_lookup (new_dir,new_name,new_len
										     ,&inode);
chkstk();
								if (ret != 0){
									printk ("UMSDOS: partial rename for file %s\n"
										,new_info.entry.name);
								}else{
									/*
									   Update f_pos so notify_change will succeed
									   if the file was already in use.
									   */
									umsdos_set_dirinfo (inode,new_dir,new_info.f_pos);
chkstk();
									iput (inode);
								}
							}
						}
					}
				}else{
					/* sticky bit set on new_dir */
					PRINTK(("sticky set on new "));
					ret = -EPERM;
				}
			}else{
				/* sticky bit set on old_dir */
				PRINTK(("sticky set on old "));
				ret = -EPERM;
			}
		}
		umsdos_unlockcreate(old_dir);
		umsdos_unlockcreate(new_dir);
	}
	iput (old_dir);
	iput (new_dir);
	PRINTK (("\n"));
	return ret;
}
/*
	Setup un Symbolic link or a (pseudo) hard link
	Return a negative error code or 0 if ok.
*/
static int umsdos_symlink_x(
	struct inode * dir,
	const char * name,
	int len,
	const char * symname,	/* name will point to this path */
	int mode,
	char flags)
{
	/* #Specification: symbolic links / strategy
		A symbolic link is simply a file which hold a path. It is
		implemented as a normal MSDOS file (not very space efficient :-()

		I see 2 different way to do it. One is to place the link data
		in unused entry of the EMD file. The other is to have a separate
		file dedicated to hold all symbolic links data.

		Lets go for simplicity...
	*/
	struct inode *inode;
	int ret;
	dir->i_count++;		/* We keep the inode in case we need it */
						/* later */
	ret = umsdos_create_any (dir,name,len,mode,0,flags,&inode);
	PRINTK (("umsdos_symlink ret %d ",ret));
	if (ret == 0){
		int len = strlen(symname);
		struct file filp;
		filp.f_pos = 0;
		/* Make the inode acceptable to MSDOS */
		ret = umsdos_file_write_kmem (inode,&filp,(char*)symname,len);
		iput (inode);
		if (ret >= 0){
			if (ret != len){
				ret = -EIO;
				printk ("UMSDOS: "
					"Can't write symbolic link data\n");
			}else{
				ret = 0;
			}
		}
		if (ret != 0){
			UMSDOS_unlink (dir,name,len);
			dir = NULL;
		}
	}
	iput (dir);
	PRINTK (("\n"));
	return ret;
}
/*
	Setup un Symbolic link.
	Return a negative error code or 0 if ok.
*/
int UMSDOS_symlink(
	struct inode * dir,
	const char * name,
	int len,
	const char * symname)	/* name will point to this path */
{
	return umsdos_symlink_x (dir,name,len,symname,S_IFLNK|0777,0);
}
/*
	Add a link to an inode in a directory
*/
int UMSDOS_link (
	struct inode * oldinode,
	struct inode * dir,
	const char * name,
	int len)
{
	/* #Specification: hard link / strategy
		Well ... hard link are difficult to implement on top of an
		MsDOS fat file system. Unlike UNIX file systems, there are no
		inode. A directory entry hold the functionality of the inode
		and the entry.

		We will used the same strategy as a normal Unix file system
		(with inode) except we will do it symbolically (using paths).

		Because anything can happen during a DOS session (defragment,
		directory sorting, etc...), we can't rely on MsDOS pseudo
		inode number to record the link. For this reason, the link
		will be done using hidden symbolic links. The following
		scenario illustrate how it work.
		
		Given a file /foo/file

		#
			ln /foo/file /tmp/file2

			become internally

			mv /foo/file /foo/-LINK1
			ln -s /foo/-LINK1 /foo/file
			ln -s /foo/-LINK1 /tmp/file2
		#

		Using this strategy, we can operate on /foo/file or /foo/file2.
		We can remove one and keep the other, like a normal Unix hard link.
		We can rename /foo/file or /tmp/file2 independently.
			
		The entry -LINK1 will be hidden. It will hold a link count.
		When all link are erased, the hidden file is erased too.
	*/
	/* #Specification: weakness / hard link
		The strategy for hard link introduces a side effect that
		may or may not be acceptable. Here is the sequence

		#
		mkdir subdir1
		touch subdir1/file
		mkdir subdir2
		ln    subdir1/file subdir2/file
		rm    subdir1/file
		rmdir subdir1
		rmdir: subdir1: Directory not empty
		#

		This happen because there is an invisible file (--link) in
		subdir1 which is referenced by subdir2/file.

		Any idea ?
	*/
	/* #Specification: weakness / hard link / rename directory
		Another weakness of hard link come from the fact that
		it is based on hidden symbolic links. Here is an example.

		#
		mkdir /subdir1
		touch /subdir1/file
		mkdir /subdir2
		ln    /subdir1/file subdir2/file
		mv    /subdir1 subdir3
		ls -l /subdir2/file
		#

		Since /subdir2/file is a hidden symbolic link
		to /subdir1/..hlinkNNN, accessing it will fail since
		/subdir1 does not exist anymore (has been renamed).
	*/
	int ret = 0;
	if (S_ISDIR(oldinode->i_mode)){
		/* #Specification: hard link / directory
			A hard link can't be made on a directory. EPERM is returned
			in this case.
		*/
		ret = -EPERM;
	}else if ((ret = umsdos_nevercreat(dir,name,len,-EPERM))==0){
		struct inode *olddir;
		ret = umsdos_get_dirowner(oldinode,&olddir);
		PRINTK (("umsdos_link dir_owner = %d -> %p [%d] "
			,oldinode->u.umsdos_i.i_dir_owner,olddir,olddir->i_count));
		if (ret == 0){
			struct umsdos_dirent entry;
			umsdos_lockcreate2(dir,olddir);
			ret = umsdos_inode2entry (olddir,oldinode,&entry);
			if (ret == 0){
				PRINTK (("umsdos_link :%s: ino %d flags %d "
					,entry.name
					,oldinode->i_ino,entry.flags));
				if (!(entry.flags & UMSDOS_HIDDEN)){
					/* #Specification: hard link / first hard link
						The first time a hard link is done on a file, this
						file must be renamed and hidden. Then an internal
						symbolic link must be done on the hidden file.

						The second link is done after on this hidden file.

						It is expected that the Linux MSDOS file system
						keeps the same pseudo inode when a rename operation
						is done on a file in the same directory.
					*/
					struct umsdos_info info;
					ret = umsdos_newhidden (olddir,&info);
					if (ret == 0){
						olddir->i_count+=2;
						PRINTK (("olddir[%d] ",olddir->i_count));
						ret = umsdos_rename_f (olddir,entry.name
							,entry.name_len
							,olddir,info.entry.name,info.entry.name_len
							,UMSDOS_HIDDEN);
						if (ret == 0){
							char *path = (char*)kmalloc(PATH_MAX,GFP_KERNEL);
							if (path == NULL){
								ret = -ENOMEM;
							}else{
								PRINTK (("olddir[%d] ",olddir->i_count));
								ret = umsdos_locate_path (oldinode,path);
								PRINTK (("olddir[%d] ",olddir->i_count));
								if (ret == 0){
									olddir->i_count++;
									ret = umsdos_symlink_x (olddir
										,entry.name
										,entry.name_len,path
										,S_IFREG|0777,UMSDOS_HLINK);
									if (ret == 0){
										dir->i_count++;
										ret = umsdos_symlink_x (dir,name,len
											,path
											,S_IFREG|0777,UMSDOS_HLINK);
									}
								}
								kfree (path);
							}
						}
					}
				}else{
					char *path = (char*)kmalloc(PATH_MAX,GFP_KERNEL);
					if (path == NULL){
						ret = -ENOMEM;
					}else{
						ret = umsdos_locate_path (oldinode,path);
						if (ret == 0){
							dir->i_count++;
							ret = umsdos_symlink_x (dir,name,len,path
											,S_IFREG|0777,UMSDOS_HLINK);
						}
						kfree (path);
					}
				}
			}
			umsdos_unlockcreate(olddir);
			umsdos_unlockcreate(dir);
		}
		iput (olddir);
	}
	if (ret == 0){
		struct iattr newattrs;
		oldinode->i_nlink++;
		newattrs.ia_valid = 0;
		ret = UMSDOS_notify_change(oldinode, &newattrs);
	}
	iput (oldinode);
	iput (dir);
	PRINTK (("umsdos_link %d\n",ret));
	return ret;
}
/*
	Add a new file into the alternate directory.
	The file is added to the real MSDOS directory. If successful, it
	is then added to the EDM file.

	Return the status of the operation. 0 mean success.
*/
int UMSDOS_create (
	struct inode *dir,
	const char *name,		/* Name of the file to add */
	int len,				/* Length of the name */
	int mode,				/* Permission bit + file type ??? */
	struct inode **result)	/* Will hold the inode of the newly created */
							/* file */
{
	return umsdos_create_any (dir,name,len,mode,0,0,result);
}
/*
	Add a sub-directory in a directory
*/
int UMSDOS_mkdir(
	struct inode * dir,
	const char * name,
	int len,
	int mode)
{
	int ret = umsdos_nevercreat(dir,name,len,-EEXIST);
	if (ret == 0){
		struct umsdos_info info;
		ret = umsdos_parse (name,len,&info);
		PRINTK (("umsdos_mkdir %d\n",ret));
		if (ret == 0){
			info.entry.mode = mode | S_IFDIR;
			info.entry.rdev = 0;
			info.entry.uid = current->fsuid;
			info.entry.gid = (dir->i_mode & S_ISGID)
				? dir->i_gid : current->fsgid;
			info.entry.ctime = info.entry.atime = info.entry.mtime
				= CURRENT_TIME;
			info.entry.flags = 0;
			umsdos_lockcreate(dir);
			info.entry.nlink = 1;
			ret = umsdos_newentry (dir,&info);
			PRINTK (("newentry %d ",ret));
			if (ret == 0){
				dir->i_count++;
				ret = msdos_mkdir (dir,info.fake.fname,info.fake.len,mode);
				if (ret != 0){
					umsdos_delentry (dir,&info,1);
					/* #Specification: mkdir / Directory already exist in DOS
						We do the same thing as for file creation.
						For all user it is an error.
					*/
				}else{
					/* #Specification: mkdir / umsdos directory / create EMD
						When we created a new sub-directory in a UMSDOS
						directory (one with full UMSDOS semantic), we
						create immediately an EMD file in the new
						sub-directory so it inherit UMSDOS semantic.
					*/
					struct inode *subdir;
					ret = umsdos_real_lookup (dir,info.fake.fname
						,info.fake.len,&subdir);
					if (ret == 0){
						struct inode *result;
						ret = msdos_create (subdir,UMSDOS_EMD_FILE
							,UMSDOS_EMD_NAMELEN,S_IFREG|0777,&result);
						subdir = NULL;
						iput (result);
					}
					if (ret < 0){
						printk ("UMSDOS: Can't create empty --linux-.---\n");
					}
					iput (subdir);
				}
			}
			umsdos_unlockcreate(dir);
		}
	}
	PRINTK (("umsdos_mkdir %d\n",ret));
	iput (dir);
	return ret;
}
/*
	Add a new device special file into a directory.
*/
int UMSDOS_mknod(
	struct inode * dir,
	const char * name,
	int len,
	int mode,
	int rdev)
{
	/* #Specification: Special files / strategy
		Device special file, pipes, etc ... are created like normal
		file in the msdos file system. Of course they remain empty.

		One strategy was to create those files only in the EMD file
		since they were not important for MSDOS. The problem with
		that, is that there were not getting inode number allocated.
		The MSDOS filesystems is playing a nice game to fake inode
		number, so why not use it.

		The absence of inode number compatible with those allocated
		for ordinary files was causing major trouble with hard link
		in particular and other parts of the kernel I guess.
	*/
	struct inode *inode;
	int ret = umsdos_create_any (dir,name,len,mode,rdev,0,&inode);
	iput (inode);
	return ret;
}

/*
	Remove a sub-directory.
*/
int UMSDOS_rmdir(
	struct inode * dir,
	const char * name,
	int len)
{
	/* #Specification: style / iput strategy
		In the UMSDOS project, I am trying to apply a single
		programming style regarding inode management. Many
		entry point are receiving an inode to act on, and must
		do an iput() as soon as they are finished with
		the inode.

		For simple case, there is no problem. When you introduce
		error checking, you end up with many iput placed around the
		code.

		The coding style I use all around is one where I am trying
		to provide independent flow logic (I don't know how to
		name this). With this style, code is easier to understand
		but you rapidly get iput() all around. Here is an exemple
		of what I am trying to avoid.

		#
		if (a){
			...
			if(b){
				...
			}
			...
			if (c){
				// Complex state. Was b true ? 
				...
			}
			...
		}
		// Weird state
		if (d){
			// ...
		}
		// Was iput finally done ?
		return status;
		#

		Here is the style I am using. Still sometime I do the
		first when things are very simple (or very complicated :-( )

		#
		if (a){
			if (b){
				...
			}else if (c){
				// A single state gets here
			}
		}else if (d){
			...
		}
		return status;
		#

		Again, while this help clarifying the code, I often get a lot
		of iput(), unlike the first style, where I can place few 
		"strategic" iput(). "strategic" also mean, more difficult
		to place.

		So here is the style I will be using from now on in this project.
		There is always an iput() at the end of a function (which has
		to do an iput()). One iput by inode. There is also one iput()
		at the places where a successful operation is achieved. This
		iput() is often done by a sub-function (often from the msdos
		file system). So I get one too many iput() ? At the place
		where an iput() is done, the inode is simply nulled, disabling
		the last one.

		#
		if (a){
			if (b){
				...
			}else if (c){
				msdos_rmdir(dir,...);
				dir = NULL;
			}
		}else if (d){
			...
		}
		iput (dir);
		return status;
		#

		Note that the umsdos_lockcreate() and umsdos_unlockcreate() function
		pair goes against this practice of "forgetting" the inode as soon
		as possible.
	*/		
	int ret = umsdos_nevercreat(dir,name,len,-EPERM);
	if (ret == 0){
		struct inode *sdir;
		dir->i_count++;
		ret = UMSDOS_lookup (dir,name,len,&sdir);
		PRINTK (("rmdir lookup %d ",ret));
		if (ret == 0){
			int empty;
			umsdos_lockcreate(dir);
			if (sdir->i_count > 1){
				ret = -EBUSY;
			}else if ((empty = umsdos_isempty (sdir)) != 0){
				PRINTK (("isempty %d i_count %d ",empty,sdir->i_count));
				/* check sticky bit */
				if ( !(dir->i_mode & S_ISVTX) || fsuser() ||
				    current->fsuid == sdir->i_uid ||
				    current->fsuid == dir->i_uid ) {
					if (empty == 1){
						/* We have to removed the EMD file */
						ret = msdos_unlink(sdir,UMSDOS_EMD_FILE
								   ,UMSDOS_EMD_NAMELEN);
						sdir = NULL;
					}
					/* sdir must be free before msdos_rmdir() */
					iput (sdir);
					sdir = NULL;
					PRINTK (("isempty ret %d nlink %d ",ret,dir->i_nlink));
					if (ret == 0){
						struct umsdos_info info;
						dir->i_count++;
						umsdos_parse (name,len,&info);
						/* The findentry is there only to complete */
						/* the mangling */
						umsdos_findentry (dir,&info,2);
						ret = msdos_rmdir (dir,info.fake.fname
								   ,info.fake.len);
						if (ret == 0){
							ret = umsdos_delentry (dir,&info,1);
						}
					}
				}else{
					/* sticky bit set and we don't have permission */
					PRINTK(("sticky set "));
					ret = -EPERM;
				}
			}else{	
				/*
					The subdirectory is not empty, so leave it there
				*/
				ret = -ENOTEMPTY;
			}
			iput(sdir);
			umsdos_unlockcreate(dir);
		}	
	}
	iput (dir);
	PRINTK (("umsdos_rmdir %d\n",ret));
	return ret;
}
/*
	Remove a file from the directory.
*/
int UMSDOS_unlink (
	struct inode * dir,
	const char * name,
	int len)
{
	int ret = umsdos_nevercreat(dir,name,len,-EPERM);
	if (ret == 0){
		struct umsdos_info info;
		ret = umsdos_parse (name,len,&info);
		if (ret == 0){
			umsdos_lockcreate(dir);
			ret = umsdos_findentry(dir,&info,1);
			if (ret == 0){
				PRINTK (("UMSDOS_unlink %s ",info.fake.fname));
				/* check sticky bit */
				if ( !(dir->i_mode & S_ISVTX) || fsuser() ||
				    current->fsuid == info.entry.uid ||
				    current->fsuid == dir->i_uid ) {
					if (info.entry.flags & UMSDOS_HLINK){
						/* #Specification: hard link / deleting a link
						   When we deletes a file, and this file is a link
						   we must subtract 1 to the nlink field of the
						   hidden link.
						   
						   If the count goes to 0, we delete this hidden
						   link too.
						   */
						/*
						   First, get the inode of the hidden link
						   using the standard lookup function.
						   */
						struct inode *inode;
						dir->i_count++;
						ret = UMSDOS_lookup (dir,name,len,&inode);
						if (ret == 0){
							PRINTK (("unlink nlink = %d ",inode->i_nlink));
							inode->i_nlink--;
							if (inode->i_nlink == 0){
								struct inode *hdir = iget(inode->i_sb
											  ,inode->u.umsdos_i.i_dir_owner);
								struct umsdos_dirent entry;
								ret = umsdos_inode2entry (hdir,inode,&entry);
								if (ret == 0){
									ret = UMSDOS_unlink (hdir,entry.name
											     ,entry.name_len);
								}else{
									iput (hdir);
								}
							}else{
								struct iattr newattrs;
								newattrs.ia_valid = 0;
								ret = UMSDOS_notify_change (inode, &newattrs);
							}
							iput (inode);
						}
					}
					if (ret == 0){
						ret = umsdos_delentry (dir,&info,0);
						if (ret == 0){
							PRINTK (("Avant msdos_unlink %s ",info.fake.fname));
							dir->i_count++;
							ret = msdos_unlink_umsdos (dir,info.fake.fname
										   ,info.fake.len);
							PRINTK (("msdos_unlink %s %o ret %d ",info.fake.fname
								 ,info.entry.mode,ret));
						}
					}
				}else{
					/* sticky bit set and we've not got permission */
					PRINTK(("sticky set "));
					ret = -EPERM;
				}
			}
			umsdos_unlockcreate(dir);
		}
	}	
	iput (dir);
	PRINTK (("umsdos_unlink %d\n",ret));
	return ret;
}

/*
	Rename a file (move) in the file system.
*/
int UMSDOS_rename(
	struct inode * old_dir,
	const char * old_name,
	int old_len,
	struct inode * new_dir,
	const char * new_name,
	int new_len)
{
	/* #Specification: weakness / rename
		There is a case where UMSDOS rename has a different behavior
		than normal UNIX file system. Renaming an open file across
		directory boundary does not work. Renaming an open file within
		a directory does work however.

		The problem (not sure) is in the linux VFS msdos driver.
		I believe this is not a bug but a design feature, because
		an inode number represent some sort of directory address
		in the MSDOS directory structure. So moving the file into
		another directory does not preserve the inode number.
	*/
	int ret = umsdos_nevercreat(new_dir,new_name,new_len,-EEXIST);
	if (ret == 0){
		/* umsdos_rename_f eat the inode and we may need those later */
		old_dir->i_count++;
		new_dir->i_count++;
		ret = umsdos_rename_f (old_dir,old_name,old_len,new_dir,new_name
			,new_len,0);
		if (ret == -EEXIST){
			/* #Specification: rename / new name exist
			        If the destination name already exist, it will
				silently be removed. EXT2 does it this way
				and this is the spec of SUNOS. So does UMSDOS.

				If the destination is an empty directory it will
				also be removed.
			*/
			/* #Specification: rename / new name exist / possible flaw
				The code to handle the deletion of the target (file
				and directory) use to be in umsdos_rename_f, surrounded
				by proper directory locking. This was insuring that only
				one process could achieve a rename (modification) operation
				in the source and destination directory. This was also
				insuring the operation was "atomic".

				This has been changed because this was creating a kernel
				stack overflow (stack is only 4k in the kernel). To avoid
				the code doing the deletion of the target (if exist) has
				been moved to a upper layer. umsdos_rename_f is tried
				once and if it fails with EEXIST, the target is removed
				and umsdos_rename_f is done again.

				This makes the code cleaner and (not sure) solve a
				deadlock problem one tester was experiencing.

				The point is to mention that possibly, the semantic of
				"rename" may be wrong. Anyone dare to check that :-)
				Be aware that IF it is wrong, to produce the problem you
				will need two process trying to rename a file to the
				same target at the same time. Again, I am not sure it
				is a problem at all.
			*/
			/* This is not super efficient but should work */
			new_dir->i_count++;
			ret = UMSDOS_unlink (new_dir,new_name,new_len);
chkstk();
			PRINTK (("rename unlink ret %d %d -- ",ret,new_len));
			if (ret == -EISDIR){
				new_dir->i_count++;
				ret = UMSDOS_rmdir (new_dir,new_name,new_len);
chkstk();
				PRINTK (("rename rmdir ret %d -- ",ret));
			}
			if (ret == 0){
				ret = umsdos_rename_f (old_dir,old_name,old_len
					,new_dir,new_name,new_len,0);
				new_dir = old_dir = NULL;
			}
		}
	}
	iput (new_dir);
	iput (old_dir);
	return ret;
}

