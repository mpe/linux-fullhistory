/*
 *  linux/fs/umsdos/rdir.c
 *
 *  Written 1994 by Jacques Gelinas
 *
 *  Extended MS-DOS directory pure MS-DOS handling functions
 *  (For directory without EMD file).
 */

#include <linux/sched.h>
#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/errno.h>
#include <linux/stat.h>
#include <linux/limits.h>
#include <linux/umsdos_fs.h>
#include <linux/malloc.h>

#include <asm/uaccess.h>

#define PRINTK(x)
#define Printk(x) printk x


extern struct inode *pseudo_root;

struct RDIR_FILLDIR {
	void *dirbuf;
	filldir_t filldir;
	int real_root;
};

static int rdir_filldir(
	void * buf,
	const char *name,
	int name_len,
	off_t offset,
	ino_t ino)
{
  int ret = 0;
  struct RDIR_FILLDIR *d = (struct RDIR_FILLDIR*) buf;
  PRINTK ((KERN_DEBUG "rdir_filldir /mn/: entering\n"));
  if (d->real_root){
    PRINTK ((KERN_DEBUG "rdir_filldir /mn/: real root!\n"));
    /* real root of a pseudo_rooted partition */
    if (name_len != UMSDOS_PSDROOT_LEN
	|| memcmp(name,UMSDOS_PSDROOT_NAME,UMSDOS_PSDROOT_LEN)!=0){
      /* So it is not the /linux directory */
      if (name_len == 2
	  && name[0] == '.'
	  && name[1] == '.'){
				/* Make sure the .. entry points back to the pseudo_root */
	ino = pseudo_root->i_ino;
      }
      ret = d->filldir (d->dirbuf,name,name_len,offset,ino);
    }
  }else{
    /* Any DOS directory */
    PRINTK ((KERN_DEBUG "rdir_filldir /mn/: calling d->filldir (%p) for %.*s (%lu)\n", d->filldir, name_len, name, ino));
    ret = d->filldir (d->dirbuf, name, name_len, offset, ino);
  }
  return ret;
}


static int UMSDOS_rreaddir (
	struct file *filp,
	void *dirbuf,
	filldir_t filldir)
{
  struct RDIR_FILLDIR bufk;
  struct inode *dir = filp->f_dentry->d_inode;
  
  PRINTK ((KERN_DEBUG "UMSDOS_rreaddir /mn/: entering %p %p\n", filldir, dirbuf));

  
  bufk.filldir = filldir;
  bufk.dirbuf = dirbuf;
  bufk.real_root = pseudo_root
    && dir == iget(dir->i_sb,UMSDOS_ROOT_INO)
    && dir == iget(pseudo_root->i_sb,UMSDOS_ROOT_INO);
  PRINTK ((KERN_DEBUG "UMSDOS_rreaddir /mn/: calling fat_readdir with filldir=%p and exiting\n",filldir));
  return fat_readdir(filp, &bufk, rdir_filldir);
}


/*
  Lookup into a non promoted directory.
  If the result is a directory, make sure we find out if it is
  a promoted one or not (calling umsdos_setup_dir_inode(inode)).
*/
int umsdos_rlookup_x(
   struct inode *dir,
   struct dentry *dentry,
   int nopseudo)   /* Don't care about pseudo root mode */
     /* so locating "linux" will work */
{
  int len = dentry->d_name.len;
  const char *name = dentry->d_name.name;
  struct inode *inode;
  int ret;
  if (pseudo_root
      && len == 2
      && name[0] == '.'
      && name[1] == '.'
      && dir == iget(dir->i_sb,UMSDOS_ROOT_INO)
      && dir == iget(pseudo_root->i_sb,UMSDOS_ROOT_INO) ){
    /*    *result = pseudo_root;*/
    Printk ((KERN_WARNING "umsdos_rlookup_x: we are at pseudo-root thingy?\n"));
    pseudo_root->i_count++;
    ret = 0;
    /* #Specification: pseudo root / DOS/..
       In the real root directory (c:\), the directory ..
       is the pseudo root (c:\linux).
    */
  }else{
    ret = umsdos_real_lookup (dir, dentry); inode=dentry->d_inode;

#if 0
    Printk ((KERN_DEBUG "umsdos_rlookup_x: umsdos_real_lookup for %.*s in %lu returned %d\n", len, name, dir->i_ino, ret));
    Printk ((KERN_DEBUG "umsdos_rlookup_x: umsdos_real_lookup: inode is %p resolving to ", inode));
    if (inode) {	/* /mn/ FIXME: DEL_ME */
        Printk ((KERN_DEBUG "i_ino=%lu\n", inode->i_ino));
    } else {
        Printk ((KERN_DEBUG "NONE!\n"));
    }
#endif
    
    if ((ret == 0) && inode){
      
      if (pseudo_root && inode == pseudo_root && !nopseudo){
      /* #Specification: pseudo root / DOS/linux
	 Even in the real root directory (c:\), the directory
	 /linux won't show
      */
        Printk ((KERN_WARNING "umsdos_rlookup_x: do the pseudo-thingy...\n"));
	ret = -ENOENT;
	iput (pseudo_root);
       
      }else if (S_ISDIR(inode->i_mode)){
	/* We must place the proper function table */
	/* depending if this is a MsDOS directory or an UMSDOS directory */
        Printk ((KERN_DEBUG "umsdos_rlookup_x: setting up setup_dir_inode %lu...\n", inode->i_ino));
	umsdos_setup_dir_inode (inode);
      }
    }
  }
  iput (dir);
  PRINTK ((KERN_DEBUG "umsdos_rlookup_x: returning %d\n", ret));
  return ret;
}


int UMSDOS_rlookup(
	struct inode *dir,
	struct dentry *dentry
	)
{
  PRINTK ((KERN_DEBUG "UMSDOS_rlookup /mn/: executing umsdos_rlookup_x for ino=%lu in %.*s\n", dir->i_ino, (int) dentry->d_name.len, dentry->d_name.name));
  return umsdos_rlookup_x(dir,dentry,0);
}


static int UMSDOS_rrmdir (
	struct inode *dir,
	struct dentry *dentry)
{
  /* #Specification: dual mode / rmdir in a DOS directory
     In a DOS (not EMD in it) directory, we use a reverse strategy
     compared with an Umsdos directory. We assume that a subdirectory
     of a DOS directory is also a DOS directory. This is not always
     true (umssync may be used anywhere), but make sense.
     
     So we call msdos_rmdir() directly. If it failed with a -ENOTEMPTY
     then we check if it is a Umsdos directory. We check if it is
     really empty (only . .. and --linux-.--- in it). If it is true
     we remove the EMD and do a msdos_rmdir() again.

     In a Umsdos directory, we assume all subdirectory are also
     Umsdos directory, so we check the EMD file first.
  */
  int ret;
  if (umsdos_is_pseudodos(dir,dentry)){
    /* #Specification: pseudo root / rmdir /DOS
       The pseudo sub-directory /DOS can't be removed!
       This is done even if the pseudo root is not a Umsdos
       directory anymore (very unlikely), but an accident (under
       MsDOS) is always possible.
       
       EPERM is returned.
    */
    ret = -EPERM;
  }else{
    umsdos_lockcreate (dir);
    dir->i_count++;
    ret = msdos_rmdir (dir,dentry);
    if (ret == -ENOTEMPTY){
      struct inode *sdir;
      dir->i_count++;
      
      ret = UMSDOS_rlookup (dir,dentry);
      sdir = dentry->d_inode;
      PRINTK (("rrmdir lookup %d ",ret));
      if (ret == 0){
	int empty;
	if ((empty = umsdos_isempty (sdir)) != 0){
	  PRINTK (("isempty %d i_count %d ",empty,
		   atomic_read(&sdir->i_count)));
	  if (empty == 2){
	    /*
	      Not a Umsdos directory, so the previous msdos_rmdir
	      was not lying :-)
	    */
	    ret = -ENOTEMPTY;
	  }else if (empty == 1){
	    /* We have to removed the EMD file */
	    struct dentry *temp;
	    temp = creat_dentry (UMSDOS_EMD_FILE, UMSDOS_EMD_NAMELEN, NULL);
	    ret = msdos_unlink(sdir, temp);
	    sdir = NULL;
	    if (ret == 0){
	      dir->i_count++;
	      ret = msdos_rmdir (dir,dentry);
	    }
	  }
	}else{
	  ret = -ENOTEMPTY;
	}
	iput (sdir);
      }
    }
    umsdos_unlockcreate (dir);
  }
  iput (dir);
  return ret;
}

/* #Specification: dual mode / introduction
   One goal of UMSDOS is to allow a practical and simple coexistence
   between MsDOS and Linux in a single partition. Using the EMD file
   in each directory, UMSDOS add Unix semantics and capabilities to
   normal DOS file system. To help and simplify coexistence, here is
   the logic related to the EMD file.
   
   If it is missing, then the directory is managed by the MsDOS driver.
   The names are limited to DOS limits (8.3). No links, no device special
   and pipe and so on.

   If it is there, it is the directory. If it is there but empty, then
   the directory looks empty. The utility umssync allows synchronisation
   of the real DOS directory and the EMD.

   Whenever umssync is applied to a directory without EMD, one is
   created on the fly. The directory is promoted to full unix semantic.
   Of course, the ls command will show exactly the same content as before
   the umssync session.

   It is believed that the user/admin will promote directories to unix
   semantic as needed.

   The strategy to implement this is to use two function table (struct
   inode_operations). One for true UMSDOS directory and one for directory
   with missing EMD.

   Functions related to the DOS semantic (but aware of UMSDOS) generally
   have a "r" prefix (r for real) such as UMSDOS_rlookup, to differentiate
   from the one with full UMSDOS semantic.
*/
static struct file_operations umsdos_rdir_operations = {
  NULL,				/* lseek - default */
  UMSDOS_dir_read,		/* read */
  NULL,				/* write - bad */
  UMSDOS_rreaddir,		/* readdir */
  NULL,				/* poll - default */
  UMSDOS_ioctl_dir,		/* ioctl - default */
  NULL,				/* mmap */
  NULL,				/* no special open code */
  NULL,				/* no special release code */
  NULL				/* fsync */
};

struct inode_operations umsdos_rdir_inode_operations = {
  &umsdos_rdir_operations,	/* default directory file-ops */
  msdos_create,			/* create */
  UMSDOS_rlookup,		/* lookup */
  NULL,				/* link */
  msdos_unlink,			/* unlink */
  NULL,				/* symlink */
  msdos_mkdir,			/* mkdir */
  UMSDOS_rrmdir,		/* rmdir */
  NULL,				/* mknod */
  msdos_rename,			/* rename */
  NULL,				/* readlink */
  NULL,				/* followlink */
  NULL,				/* readpage */
  NULL,				/* writepage */
  NULL,				/* bmap */
  NULL,				/* truncate */
  NULL,				/* permission */
  NULL,				/* smap */
  NULL,				/* updatepage */
  NULL,				/* revalidate */
};


