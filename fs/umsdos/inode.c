/*
 *  linux/fs/umsdos/inode.c
 *
 *	Written 1993 by Jacques Gelinas 
 *	Inspired from linux/fs/msdos/... by Werner Almesberger
 *
 */

#ifdef MODULE
#include <linux/module.h>
#include <linux/version.h>
#else
#define MOD_INC_USE_COUNT
#define MOD_DEC_USE_COUNT
#endif

#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <asm/segment.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/umsdos_fs.h>

struct inode *pseudo_root=NULL;		/* Useful to simulate the pseudo DOS */
									/* directory. See UMSDOS_readdir_x() */

/* #Specification: convention / PRINTK Printk and printk
	Here is the convention for the use of printk inside fs/umsdos

	printk carry important message (error or status).
	Printk is for debugging (it is a macro defined at the beginning of
		   most source.
	PRINTK is a nulled Printk macro.

	This convention makes the source easier to read, and Printk easier
	to shut off.
*/
#define PRINTK(x)
#define Printk(x) printk x


void UMSDOS_put_inode(struct inode *inode)
{
	PRINTK (("put inode %x owner %x pos %d dir %x\n",inode
		,inode->u.umsdos_i.i_emd_owner,inode->u.umsdos_i.pos
		,inode->u.umsdos_i.i_emd_dir));
	if (inode != NULL && inode == pseudo_root){
		printk ("Umsdos: Oops releasing pseudo_root. Notify jacques@solucorp.qc.ca\n");
	}
	msdos_put_inode(inode);
}


void UMSDOS_put_super(struct super_block *sb)
{
	msdos_put_super(sb);
	MOD_DEC_USE_COUNT;
}


void UMSDOS_statfs(struct super_block *sb,struct statfs *buf)
{
	msdos_statfs(sb,buf);
}


/*
	Call msdos_lookup, but set back the original msdos function table.
	Return 0 if ok, or a negative error code if not.
*/
int umsdos_real_lookup (
	struct inode *dir,
	const char *name,
	int len,
	struct inode **result)	/* Will hold inode of the file, if successful */
{
	int ret;
	dir->i_count++;
	ret = msdos_lookup (dir,name,len,result);
	return ret;
}
/*
	Complete the setup of an directory inode.
	First, it completes the function pointers, then
	it locates the EMD file. If the EMD is there, then plug the
	umsdos function table. If not, use the msdos one.
*/
void umsdos_setup_dir_inode (struct inode *inode)
{
	inode->u.umsdos_i.i_emd_dir = 0;
	{
		struct inode *emd_dir = umsdos_emd_dir_lookup (inode,0);
		extern struct inode_operations umsdos_rdir_inode_operations;
		inode->i_op = emd_dir != NULL
			? &umsdos_dir_inode_operations
			: &umsdos_rdir_inode_operations;
		iput (emd_dir);
	}
}
/*
	Add some info into an inode so it can find its owner quickly
*/
void umsdos_set_dirinfo(
	struct inode *inode,
	struct inode *dir,
	off_t f_pos)
{
	struct inode *emd_owner = umsdos_emd_dir_lookup(dir,1);
	inode->u.umsdos_i.i_dir_owner = dir->i_ino;
	inode->u.umsdos_i.i_emd_owner = emd_owner->i_ino;
	iput (emd_owner);
	inode->u.umsdos_i.pos = f_pos;
}
/*
	Tells if an Umsdos inode has been "patched" once.
	Return != 0 if so.
*/
int umsdos_isinit (struct inode *inode)
{
#if	1
	return inode->u.umsdos_i.i_emd_owner != 0;
#elif 0
	return inode->i_atime != 0;
#else
	return inode->i_count > 1;
#endif
}
/*
	Connect the proper tables in the inode and add some info.
*/
void umsdos_patch_inode (
	struct inode *inode,
	struct inode *dir,		/* May be NULL */
	off_t f_pos)
{
	/*
		This function is called very early to setup the inode, somewhat
		too early (called by UMSDOS_read_inode). At this point, we can't
		do to much, such as lookup up EMD files and so on. This causes
		confusion in the kernel. This is why some initialisation
		will be done when dir != NULL only.

		UMSDOS do run piggy back on top of msdos fs. It looks like something
		is missing in the VFS to accommodate stacked fs. Still unclear what
		(quite honestly).

		Well, maybe one! A new entry "may_unmount" which would allow
		the stacked fs to allocate some inode permanently and release
		them at the end. Doing that now introduce a problem. unmount
		always fail because some inodes are in use.
	*/
	if (!umsdos_isinit(inode)){
		inode->u.umsdos_i.i_emd_dir = 0;
		if (S_ISREG(inode->i_mode)){
			if (inode->i_op->bmap != NULL){
				inode->i_op = &umsdos_file_inode_operations;
			}else{
				inode->i_op = &umsdos_file_inode_operations_no_bmap;
			}
		}else if (S_ISDIR(inode->i_mode)){
			if (dir != NULL){
				umsdos_setup_dir_inode(inode);
			}
		}else if (S_ISLNK(inode->i_mode)){
			inode->i_op = &umsdos_symlink_inode_operations;
		}else if (S_ISCHR(inode->i_mode)){
			inode->i_op = &chrdev_inode_operations;
		}else if (S_ISBLK(inode->i_mode)){
			inode->i_op = &blkdev_inode_operations;
		}else if (S_ISFIFO(inode->i_mode)){
			init_fifo(inode);
		}
		if (dir != NULL){
			/* #Specification: inode / umsdos info
				The first time an inode is seen (inode->i_count == 1),
				the inode number of the EMD file which control this inode
				is tagged to this inode. It allows operation such
				as notify_change to be handled.
			*/
			/*
				This is done last because it also control the
				status of umsdos_isinit()
			*/
			umsdos_set_dirinfo (inode,dir,f_pos);
		}
	}else if (dir != NULL){
		/*
			Test to see if the info is maintained.
			This should be removed when the file system will be proven.
		*/
		struct inode *emd_owner = umsdos_emd_dir_lookup(dir,1);
		iput (emd_owner);
		if (emd_owner->i_ino != inode->u.umsdos_i.i_emd_owner){
			printk ("UMSDOS: *** EMD_OWNER ??? *** ino = %ld %ld <> %ld "
				,inode->i_ino,emd_owner->i_ino,inode->u.umsdos_i.i_emd_owner);
		}
	}
}
/*
	Get the inode of the directory which owns this inode.
	Return 0 if ok, -EIO if error.
*/
int umsdos_get_dirowner(
	struct inode *inode,
	struct inode **result)	/* Hold NULL if any error */
							/* else, the inode of the directory */
{
	int ret = -EIO;
	unsigned long ino = inode->u.umsdos_i.i_dir_owner;
	*result = NULL;
	if (ino == 0){
		printk ("UMSDOS: umsdos_get_dirowner ino == 0\n");
	}else{
		struct inode *dir = *result = iget(inode->i_sb,ino);
		if (dir != NULL){
			umsdos_patch_inode (dir,NULL,0);
			ret = 0;
		}
	}
	return ret;
}
/*
	Load an inode from disk.
*/
void UMSDOS_read_inode(struct inode *inode)
{
	PRINTK (("read inode %x ino = %d ",inode,inode->i_ino));
	msdos_read_inode(inode);
	PRINTK (("ino = %d %d\n",inode->i_ino,inode->i_count));
	if (S_ISDIR(inode->i_mode)
		&& (inode->u.umsdos_i.u.dir_info.creating != 0
			|| inode->u.umsdos_i.u.dir_info.looking != 0
			|| inode->u.umsdos_i.u.dir_info.p != NULL)){
		Printk (("read inode %d %d %p\n"
			,inode->u.umsdos_i.u.dir_info.creating
			,inode->u.umsdos_i.u.dir_info.looking
			,inode->u.umsdos_i.u.dir_info.p));
	}
	/* #Specification: Inode / post initialisation
		To completely initialise an inode, we need access to the owner
		directory, so we can locate more info in the EMD file. This is
		not available the first time the inode is access, we use
		a value in the inode to tell if it has been finally initialised.

		At first, we have tried testing i_count but it was causing
		problem. It is possible that two or more process use the
		newly accessed inode. While the first one block during
		the initialisation (probably while reading the EMD file), the
		others believe all is well because i_count > 1. They go banana
		with a broken inode. See umsdos_lookup_patch and umsdos_patch_inode.
	*/
	umsdos_patch_inode(inode,NULL,0);
}

/*
	Update the disk with the inode content
*/
void UMSDOS_write_inode(struct inode *inode)
{
	struct iattr newattrs;

	PRINTK (("UMSDOS_write_inode emd %d\n",inode->u.umsdos_i.i_emd_owner));
	msdos_write_inode(inode);
	newattrs.ia_mtime = inode->i_mtime;
	newattrs.ia_atime = inode->i_atime;
	newattrs.ia_ctime = inode->i_ctime;
	newattrs.ia_valid = ATTR_MTIME | ATTR_ATIME | ATTR_CTIME;
	/*
		UMSDOS_notify_change is convenient to call here
		to update the EMD entry associated with this inode.
		But it has the side effect to re"dirt" the inode.
	*/
	UMSDOS_notify_change (inode, &newattrs);
	inode->i_dirt = 0;
}

int UMSDOS_notify_change(struct inode *inode, struct iattr *attr)
{
	int ret = 0;

	if ((ret = inode_change_ok(inode, attr)) != 0) 
		return ret;

	if (inode->i_nlink > 0){
		/* #Specification: notify_change / i_nlink > 0
			notify change is only done for inode with nlink > 0. An inode
			with nlink == 0 is no longer associated with any entry in
			the EMD file, so there is nothing to update.
		*/
		unsigned long i_emd_owner = inode->u.umsdos_i.i_emd_owner;
		if (inode == inode->i_sb->s_mounted){
			/* #Specification: root inode / attributes
				I don't know yet how this should work. Normally
				the attributes (permissions bits, owner, times) of
				a directory are stored in the EMD file of its parent.

				One thing we could do is store the attributes of the root
				inode in its own EMD file. A simple entry named "." could
				be used for this special case. It would be read once
				when the file system is mounted and update in
				UMSDOS_notify_change() (right here).

				I am not sure of the behavior of the root inode for
				a real UNIX file system. For now, this is a nop.
			*/
		}else if (i_emd_owner != 0xffffffff && i_emd_owner != 0){
			/* This inode is not a EMD file nor an inode used internally
				by MSDOS, so we can update its status.
				See emd.c
			*/
			struct inode *emd_owner = iget (inode->i_sb,i_emd_owner);
			PRINTK (("notify change %p ",inode));
			if (emd_owner == NULL){
				printk ("UMSDOS: emd_owner = NULL ???");
				ret = -EPERM;
			}else{
				struct file filp;
				struct umsdos_dirent entry;
				filp.f_pos = inode->u.umsdos_i.pos;
				filp.f_reada = 0;
				PRINTK (("pos = %d ",filp.f_pos));
				/* Read only the start of the entry since we don't touch */
				/* the name */
				ret = umsdos_emd_dir_read (emd_owner,&filp,(char*)&entry
					,UMSDOS_REC_SIZE);
				if (ret == 0){
					if (attr->ia_valid & ATTR_UID) 
						entry.uid = attr->ia_uid;
					if (attr->ia_valid & ATTR_GID) 
						entry.gid = attr->ia_gid;
					if (attr->ia_valid & ATTR_MODE) 
						entry.mode = attr->ia_mode;
					if (attr->ia_valid & ATTR_ATIME) 
						entry.atime = attr->ia_atime;
					if (attr->ia_valid & ATTR_MTIME) 
						entry.mtime = attr->ia_mtime;
					if (attr->ia_valid & ATTR_CTIME) 
						entry.ctime = attr->ia_ctime;

					entry.nlink = inode->i_nlink;
					filp.f_pos = inode->u.umsdos_i.pos;
					ret = umsdos_emd_dir_write (emd_owner,&filp,(char*)&entry
						,UMSDOS_REC_SIZE);

					PRINTK (("notify pos %d ret %d nlink %d "
						,inode->u.umsdos_i.pos
						,ret,entry.nlink));
					/* #Specification: notify_change / msdos fs
						notify_change operation are done only on the
						EMD file. The msdos fs is not even called.
					*/
				}
				iput (emd_owner);
			}
			PRINTK (("\n"));
		}
	}
	if (ret == 0) 
		inode_setattr(inode, attr);
	return ret;
}

/* #Specification: function name / convention
	A simple convention for function name has been used in
	the UMSDOS file system. First all function use the prefix
	umsdos_ to avoid name clash with other part of the kernel.

	And standard VFS entry point use the prefix UMSDOS (upper case)
	so it's easier to tell them apart.
*/

static struct super_operations umsdos_sops = { 
	UMSDOS_read_inode,
	UMSDOS_notify_change,
	UMSDOS_write_inode,
	UMSDOS_put_inode,
	UMSDOS_put_super,
	NULL, /* added in 0.96c */
	UMSDOS_statfs,
	NULL
};

/*
	Read the super block of an Extended MS-DOS FS.
*/
struct super_block *UMSDOS_read_super(
	struct super_block *s,
	void *data,
	int silent)
{
	/* #Specification: mount / options
		Umsdos run on top of msdos. Currently, it supports no
		mount option, but happily pass all option received to
		the msdos driver. I am not sure if all msdos mount option
		make sense with Umsdos. Here are at least those who
		are useful.
			uid=
			gid=

		These options affect the operation of umsdos in directories
		which do not have an EMD file. They behave like normal
		msdos directory, with all limitation of msdos.
	*/
	struct super_block *sb;
	MOD_INC_USE_COUNT;
	sb = msdos_read_super(s,data,silent);
	printk ("UMSDOS Beta 0.6 (compatibility level %d.%d, fast msdos)\n"
		,UMSDOS_VERSION,UMSDOS_RELEASE);
	if (sb != NULL){
		sb->s_op = &umsdos_sops;
		PRINTK (("umsdos_read_super %p\n",sb->s_mounted));
		umsdos_setup_dir_inode (sb->s_mounted);
		PRINTK (("End umsdos_read_super\n"));
		if (s == super_blocks){
			/* #Specification: pseudo root / mount
				When a umsdos fs is mounted, a special handling is done
				if it is the root partition. We check for the presence
				of the file /linux/etc/init or /linux/etc/rc or
				/linux/sbin/init. If one is there, we do a chroot("/linux").

				We check both because (see init/main.c) the kernel
				try to exec init at different place and if it fails
				it tries /bin/sh /etc/rc. To be consistent with
				init/main.c, many more test would have to be done
				to locate init. Any complain ?

				The chroot is done manually in init/main.c but the
				info (the inode) is located at mount time and store
				in a global variable (pseudo_root) which is used at
				different place in the umsdos driver. There is no
				need to store this variable elsewhere because it
				will always be one, not one per mount.

				This feature allows the installation
				of a linux system within a DOS system in a subdirectory.
	
				A user may install its linux stuff in c:\linux
				avoiding any clash with existing DOS file and subdirectory.
				When linux boots, it hides this fact, showing a normal
				root directory with /etc /bin /tmp ...

				The word "linux" is hardcoded in /usr/include/linux/umsdos_fs.h
				in the macro UMSDOS_PSDROOT_NAME.
			*/

			struct inode *pseudo;
			Printk (("Mounting root\n"));
			if (umsdos_real_lookup (sb->s_mounted,UMSDOS_PSDROOT_NAME
					,UMSDOS_PSDROOT_LEN,&pseudo)==0
				&& S_ISDIR(pseudo->i_mode)){
				struct inode *etc = NULL;
				struct inode *sbin = NULL;
				int pseudo_ok = 0;
				Printk (("/%s is there\n",UMSDOS_PSDROOT_NAME));
				if (umsdos_real_lookup (pseudo,"etc",3,&etc)==0
					&& S_ISDIR(etc->i_mode)){
					struct inode *init = NULL;
					struct inode *rc = NULL;
					Printk (("/%s/etc is there\n",UMSDOS_PSDROOT_NAME));
					if ((umsdos_real_lookup (etc,"init",4,&init)==0
							&& S_ISREG(init->i_mode))
						|| (umsdos_real_lookup (etc,"rc",2,&rc)==0
							&& S_ISREG(rc->i_mode))){
						pseudo_ok = 1;
					}
					iput (init);
					iput (rc);
				}
				if (!pseudo_ok
					&& umsdos_real_lookup (pseudo,"sbin",4,&sbin)==0
					&& S_ISDIR(sbin->i_mode)){
					struct inode *init = NULL;
					Printk (("/%s/sbin is there\n",UMSDOS_PSDROOT_NAME));
					if (umsdos_real_lookup (sbin,"init",4,&init)==0
							&& S_ISREG(init->i_mode)){
						pseudo_ok = 1;
					}
					iput (init);
				}
				if (pseudo_ok){
					umsdos_setup_dir_inode (pseudo);
					Printk (("Activating pseudo root /%s\n",UMSDOS_PSDROOT_NAME));
					pseudo_root = pseudo;
					pseudo->i_count++;
					pseudo = NULL;
				}
				iput (sbin);
				iput (etc);
			}
			iput (pseudo);
		}
	} else {
		MOD_DEC_USE_COUNT;
	}
	return sb;
}


#ifdef MODULE

char kernel_version[] = UTS_RELEASE;

static struct file_system_type umsdos_fs_type = {
	UMSDOS_read_super, "umsdos", 1, NULL
};

int init_module(void)
{
	register_filesystem(&umsdos_fs_type);
	return 0;
}

void cleanup_module(void)
{
	unregister_filesystem(&umsdos_fs_type);
}

#endif

