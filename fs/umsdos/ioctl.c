/*
 *  linux/fs/umsdos/ioctl.c
 *
 *  Written 1993 by Jacques Gelinas
 *
 *  Extended MS-DOS ioctl directory handling functions
 */
#ifdef MODULE
#include <linux/module.h>
#endif

#include <asm/segment.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/umsdos_fs.h>

#define PRINTK(x)
#define Printk(x) printk x

/*
	Perform special function on a directory
*/
int UMSDOS_ioctl_dir (
	struct inode *dir,
	struct file *filp,
	unsigned int cmd,
	unsigned long data)
{
	int ret = -EPERM;
	/* #Specification: ioctl / acces
		Only root (effective id) is allowed to do IOCTL on directory
		in UMSDOS. EPERM is returned for other user.
	*/
	if (current->euid == 0
		|| cmd == UMSDOS_GETVERSION){
		struct umsdos_ioctl *idata = (struct umsdos_ioctl *)data;
		ret = -EINVAL;
		/* #Specification: ioctl / prototypes
			The official prototype for the umsdos ioctl on directory
			is:

			int ioctl (
				int fd,		// File handle of the directory
				int cmd,	// command
				struct umsdos_ioctl *data)

			The struct and the commands are defined in linux/umsdos_fs.h.

			umsdos_progs/umsdosio.c provide an interface in C++ to all
			these ioctl. umsdos_progs/udosctl is a small utility showing
			all this.

			These ioctl generally allow one to work on the EMD or the
			DOS directory independently. These are essential to implement
			the synchronise.
		*/
		PRINTK (("ioctl %d ",cmd));
		if (cmd == UMSDOS_GETVERSION){
			/* #Specification: ioctl / UMSDOS_GETVERSION
				The field version and release of the structure
				umsdos_ioctl are filled with the version and release
				number of the fs code in the kernel. This will allow
				some form of checking. Users won't be able to run
				incompatible utility such as the synchroniser (umssync).
				umsdos_progs/umsdosio.c enforce this checking.

				Return always 0.
			*/
			put_fs_byte (UMSDOS_VERSION,&idata->version);
			put_fs_byte (UMSDOS_RELEASE,&idata->release);
			ret = 0;
		}else if (cmd == UMSDOS_READDIR_DOS){
			/* #Specification: ioctl / UMSDOS_READDIR_DOS
				One entry is read from the DOS directory at the current
				file position. The entry is put as is in the dos_dirent
				field of struct umsdos_ioctl.

				Return > 0 if success.
			*/
			ret = msdos_readdir(dir,filp,&idata->dos_dirent,1);
		}else if (cmd == UMSDOS_READDIR_EMD){
			/* #Specification: ioctl / UMSDOS_READDIR_EMD
				One entry is read from the EMD at the current
				file position. The entry is put as is in the umsdos_dirent
				field of struct umsdos_ioctl. The corresponding mangled
				DOS entry name is put in the dos_dirent field.

				All entries are read including hidden links. Blank
				entries are skipped.

				Return > 0 if success.
			*/
			struct inode *emd_dir = umsdos_emd_dir_lookup (dir,0);
			if (emd_dir != NULL){
				while (1){
					if (filp->f_pos >= emd_dir->i_size){
						ret = 0;
						break;
					}else{
						struct umsdos_dirent entry;
						off_t f_pos = filp->f_pos;
						ret = umsdos_emd_dir_readentry (emd_dir,filp,&entry);
						if (ret < 0){
							break;
						}else if (entry.name_len > 0){
							struct umsdos_info info;
							ret = entry.name_len;
							umsdos_parse (entry.name,entry.name_len,&info);
							info.f_pos = f_pos;
							umsdos_manglename(&info);
							memcpy_tofs(&idata->umsdos_dirent,&entry
								,sizeof(entry));
							memcpy_tofs(&idata->dos_dirent.d_name
								,info.fake.fname,info.fake.len+1);
							break;
						}
					}
				}
				iput (emd_dir);
			}else{
				/* The absence of the EMD is simply seen as an EOF */
				ret = 0;
			}
		}else if (cmd == UMSDOS_INIT_EMD){
			/* #Specification: ioctl / UMSDOS_INIT_EMD
				The UMSDOS_INIT_EMD command make sure the EMD
				exist for a directory. If it does not, it is
				created. Also, it makes sure the directory functions
				table (struct inode_operations) is set to the UMSDOS
				semantic. This mean that umssync may be applied to
				an "opened" msdos directory, and it will change behavior
				on the fly.

				Return 0 if success.
			*/
			extern struct inode_operations umsdos_rdir_inode_operations;
			struct inode *emd_dir = umsdos_emd_dir_lookup (dir,1);
			ret = emd_dir != NULL;
			iput (emd_dir);
					
			dir->i_op = ret
				? &umsdos_dir_inode_operations
				: &umsdos_rdir_inode_operations;
		}else{
			struct umsdos_ioctl data;
			memcpy_fromfs (&data,idata,sizeof(data));
			if (cmd == UMSDOS_CREAT_EMD){
				/* #Specification: ioctl / UMSDOS_CREAT_EMD
					The umsdos_dirent field of the struct umsdos_ioctl is used
					as is to create a new entry in the EMD of the directory.
					The DOS directory is not modified.
					No validation is done (yet).

					Return 0 if success.
				*/
				struct umsdos_info info;
				/* This makes sure info.entry and info in general is correctly */
				/* initialised */
				memcpy (&info.entry,&data.umsdos_dirent
					,sizeof(data.umsdos_dirent));
				umsdos_parse (data.umsdos_dirent.name
					,data.umsdos_dirent.name_len,&info);
				ret = umsdos_newentry (dir,&info);
			}else if (cmd == UMSDOS_RENAME_DOS){
				/* #Specification: ioctl / UMSDOS_RENAME_DOS
					A file or directory is rename in a DOS directory
					(not moved across directory). The source name
					is in the dos_dirent.name field and the destination
					is in umsdos_dirent.name field.

					This ioctl allows umssync to rename a mangle file
					name before syncing it back in the EMD.
				*/
				dir->i_count += 2;
				ret = msdos_rename (dir
					,data.dos_dirent.d_name,data.dos_dirent.d_reclen
					,dir
					,data.umsdos_dirent.name,data.umsdos_dirent.name_len);
			}else if (cmd == UMSDOS_UNLINK_EMD){
				/* #Specification: ioctl / UMSDOS_UNLINK_EMD
					The umsdos_dirent field of the struct umsdos_ioctl is used
					as is to remove an entry from the EMD of the directory.
					No validation is done (yet). The mode field is used
					to validate S_ISDIR or S_ISREG.

					Return 0 if success.
				*/
				struct umsdos_info info;
				/* This makes sure info.entry and info in general is correctly */
				/* initialised */
				memcpy (&info.entry,&data.umsdos_dirent
					,sizeof(data.umsdos_dirent));
				umsdos_parse (data.umsdos_dirent.name
					,data.umsdos_dirent.name_len,&info);
				ret = umsdos_delentry (dir,&info
					,S_ISDIR(data.umsdos_dirent.mode));
			}else if (cmd == UMSDOS_UNLINK_DOS){
				/* #Specification: ioctl / UMSDOS_UNLINK_DOS
					The dos_dirent field of the struct umsdos_ioctl is used to
					execute a msdos_unlink operation. The d_name and d_reclen
					fields are used.

					Return 0 if success.
				*/
				dir->i_count++;
				ret = msdos_unlink (dir,data.dos_dirent.d_name
					,data.dos_dirent.d_reclen);
			}else if (cmd == UMSDOS_RMDIR_DOS){
				/* #Specification: ioctl / UMSDOS_RMDIR_DOS
					The dos_dirent field of the struct umsdos_ioctl is used to
					execute a msdos_unlink operation. The d_name and d_reclen
					fields are used.

					Return 0 if success.
				*/
				dir->i_count++;
				ret = msdos_rmdir (dir,data.dos_dirent.d_name
					,data.dos_dirent.d_reclen);
			}else if (cmd == UMSDOS_STAT_DOS){
				/* #Specification: ioctl / UMSDOS_STAT_DOS
					The dos_dirent field of the struct umsdos_ioctl is
					used to execute a stat operation in the DOS directory.
					The d_name and d_reclen fields are used.

					The following field of umsdos_ioctl.stat are filled.

					st_ino,st_mode,st_size,st_atime,st_mtime,st_ctime,
					Return 0 if success.
				*/
				struct inode *inode;
				ret = umsdos_real_lookup (dir,data.dos_dirent.d_name
					,data.dos_dirent.d_reclen,&inode);
				if (ret == 0){
					data.stat.st_ino = inode->i_ino;
					data.stat.st_mode = inode->i_mode;
					data.stat.st_size = inode->i_size;
					data.stat.st_atime = inode->i_atime;
					data.stat.st_ctime = inode->i_ctime;
					data.stat.st_mtime = inode->i_mtime;
					memcpy_tofs (&idata->stat,&data.stat,sizeof(data.stat));
					iput (inode);
				}
			}else if (cmd == UMSDOS_DOS_SETUP){
				/* #Specification: ioctl / UMSDOS_DOS_SETUP
					The UMSDOS_DOS_SETUP ioctl allow changing the
					default permission of the MsDOS file system driver
					on the fly. The MsDOS driver apply global permission
					to every file and directory. Normally these permissions
					are controlled by a mount option. This is not
					available for root partition, so a special utility
					(umssetup) is provided to do this, normally in
					/etc/rc.local.

					Be aware that this apply ONLY to MsDOS directory
					(those without EMD --linux-.---). Umsdos directory
					have independent (standard) permission for each
					and every file.

					The field umsdos_dirent provide the information needed.
					umsdos_dirent.uid and gid sets the owner and group.
					umsdos_dirent.mode set the permissions flags.
				*/
				dir->i_sb->u.msdos_sb.fs_uid = data.umsdos_dirent.uid;
				dir->i_sb->u.msdos_sb.fs_gid = data.umsdos_dirent.gid;
				dir->i_sb->u.msdos_sb.fs_umask = data.umsdos_dirent.mode;
				ret = 0;
			}
		}
	}
	PRINTK (("ioctl return %d\n",ret));
	return ret;
}



