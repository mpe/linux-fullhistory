/*
 *  fs.c
 *  NTFS driver for Linux 2.1
 *
 *  Copyright (C) 1995-1997 Martin von Löwis
 *  Copyright (C) 1996 Richard Russon
 *  Copyright (C) 1996-1997 Régis Duchesne
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef NTFS_IN_LINUX_KERNEL
#include <linux/config.h>
#endif

#include "ntfstypes.h"
#include "struct.h"
#include "util.h"
#include "inode.h"
#include "super.h"
#include "dir.h"
#include "support.h"
#include "macros.h"
#include "sysctl.h"
#include <linux/module.h>
#include <asm/uaccess.h>
#include <linux/nls.h>
#include <linux/locks.h>
#include <linux/init.h>

/* Forward declarations */
static struct inode_operations ntfs_dir_inode_operations;

#define ITEM_SIZE 2040

/* io functions to user space */
static void ntfs_putuser(ntfs_io* dest,void *src,ntfs_size_t len)
{
	copy_to_user(dest->param,src,len);
	dest->param+=len;
}

#ifdef CONFIG_NTFS_RW
struct ntfs_getuser_update_vm_s{
	const char *user;
	struct inode *ino;
	loff_t off;
};

static void ntfs_getuser_update_vm (void *dest, ntfs_io *src, ntfs_size_t len)
{
	struct ntfs_getuser_update_vm_s *p = src->param;
	copy_from_user (dest, p->user, len);
	update_vm_cache (p->ino, p->off, dest, len);
	p->user += len;
	p->off += len;
}
#endif

static ssize_t
ntfs_read(struct file * filp, char *buf, size_t count, loff_t *off)
{
	int error;
	ntfs_io io;
	ntfs_inode *ino=NTFS_LINO2NINO(filp->f_dentry->d_inode);

	/* inode is not properly initialized */
	if(!ino)return -EINVAL;
	ntfs_debug(DEBUG_OTHER, "ntfs_read %x,%x,%x ->",
		   (unsigned)ino->i_number,(unsigned)*off,(unsigned)count);
	/* inode has no unnamed data attribute */
	if(!ntfs_find_attr(ino,ino->vol->at_data,NULL))
		return -EINVAL;
	
	/* read the data */
	io.fn_put=ntfs_putuser;
	io.fn_get=0;
	io.param=buf;
	io.size=count;
	error=ntfs_read_attr(ino,ino->vol->at_data,NULL,*off,&io);
	if(error)return -error;
	
	*off+=io.size;
	return io.size;
}

#ifdef CONFIG_NTFS_RW
static ssize_t
ntfs_write(struct file *filp,const char* buf,size_t count,loff_t *pos)
{
	int ret;
	ntfs_io io;
	struct inode *inode = filp->f_dentry->d_inode;
	ntfs_inode *ino = NTFS_LINO2NINO(inode);
	struct ntfs_getuser_update_vm_s param;

	if (!ino)
		return -EINVAL;
	ntfs_debug (DEBUG_LINUX, "ntfs_write %x,%x,%x ->\n",
	       (unsigned)ino->i_number, (unsigned)*pos, (unsigned)count);
	/* Allows to lock fs ro at any time */
	if (inode->i_sb->s_flags & MS_RDONLY)
		return -ENOSPC;
	if (!ntfs_find_attr(ino,ino->vol->at_data,NULL))
		return -EINVAL;

	/* Evaluating O_APPEND is the file system's job... */
	if (filp->f_flags & O_APPEND)
		*pos = inode->i_size;
	param.user = buf;
	param.ino = inode;
	param.off = *pos;
	io.fn_put = 0;
	io.fn_get = ntfs_getuser_update_vm;
	io.param = &param;
	io.size = count;
	ret = ntfs_write_attr (ino, ino->vol->at_data, NULL, *pos, &io);
	ntfs_debug (DEBUG_LINUX, "write -> %x\n", ret);
	if(ret<0)
		return -EINVAL;

	*pos += io.size;
	if (*pos > inode->i_size)
		inode->i_size = *pos;
	mark_inode_dirty (filp->f_dentry->d_inode);
	return io.size;
}
#endif

struct ntfs_filldir{
	struct inode *dir;
	filldir_t filldir;
	unsigned int type;
	ntfs_u32 ph,pl;
	void *dirent;
	char *name;
	int namelen;
};
	
static int ntfs_printcb(ntfs_u8 *entry,void *param)
{
	struct ntfs_filldir* nf=param;
	int flags=NTFS_GETU8(entry+0x51);
	int show_hidden=0;
	int length=NTFS_GETU8(entry+0x50);
	int inum=NTFS_GETU32(entry);
	int error;
#ifdef NTFS_NGT_NT_DOES_LOWER
	int i,to_lower=0;
#endif
	switch(nf->type){
	case ngt_dos:
		/* Don't display long names */
		if((flags & 2)==0)
			return 0;
		break;
	case ngt_nt:
		/* Don't display short-only names */
		switch(flags&3){
		case 2: return 0;
#ifdef NTFS_NGT_NT_DOES_LOWER
		case 3: to_lower=1;
#endif
		}
		break;
	case ngt_posix:
		break;
	case ngt_full:
		show_hidden=1;
		break;
	}
	if(!show_hidden && ((NTFS_GETU8(entry+0x48) & 2)==2)){
		ntfs_debug(DEBUG_OTHER,"Skipping hidden file\n");
		return 0;
	}
	nf->name=0;
	if(ntfs_encodeuni(NTFS_INO2VOL(nf->dir),(ntfs_u16*)(entry+0x52),
			  length,&nf->name,&nf->namelen)){
		ntfs_debug(DEBUG_OTHER,"Skipping unrepresentable file\n");
		if(nf->name)ntfs_free(nf->name);
		return 0;
	}
	/* Do not return ".", as this is faked */
	if(length==1 && *nf->name=='.')
		return 0;
#ifdef NTFS_NGT_NT_DOES_LOWER
	if(to_lower)
		for(i=0;i<nf->namelen;i++)
			/* This supports ASCII only. Since only DOS-only
			   names get converted, and since those are restricted
			   to ASCII, this should be correct */
			if(nf->name[i]>='A' && nf->name[i]<='Z')
				nf->name[i]+='a'-'A';
#endif
	nf->name[nf->namelen]=0;
	ntfs_debug(DEBUG_OTHER, "readdir got %s,len %d\n",nf->name,nf->namelen);
	/* filldir expects an off_t rather than an loff_t.
	   Hope we don't have more than 65535 index records */
	error=nf->filldir(nf->dirent,nf->name,nf->namelen,
			(nf->ph<<16)|nf->pl,inum);
	ntfs_free(nf->name);
	/* Linux filldir errors are negative, other errors positive */
	return error;
}

/* readdir returns '..', then '.', then the directory entries in sequence
   As the root directory contains a entry for itself, '.' is not emulated
   for the root directory */
static int ntfs_readdir(struct file* filp, void *dirent, filldir_t filldir)
{
	struct ntfs_filldir cb;
	int error;
	struct inode *dir=filp->f_dentry->d_inode;

	ntfs_debug(DEBUG_OTHER, "ntfs_readdir ino %x mode %x\n",
	       (unsigned)dir->i_ino,(unsigned int)dir->i_mode);
	if(!dir || (dir->i_ino==0) || !S_ISDIR(dir->i_mode))return -EBADF;

	ntfs_debug(DEBUG_OTHER, "readdir: Looking for file %x dircount %d\n",
	       (unsigned)filp->f_pos,dir->i_count);
	cb.pl=filp->f_pos & 0xFFFF;
	cb.ph=filp->f_pos >> 16;
	/* end of directory */
	if(cb.ph==0xFFFF){
		/* FIXME: Maybe we can return those with the previous call */
		switch(cb.pl){
		case 0: filldir(dirent,".",1,filp->f_pos,dir->i_ino);
			filp->f_pos=0xFFFF0001;
			return 0;
			/* FIXME: parent directory */
		case 1: filldir(dirent,"..",2,filp->f_pos,0);
			filp->f_pos=0xFFFF0002;
			return 0;
		}
		ntfs_debug(DEBUG_OTHER, "readdir: EOD\n");
		return 0;
	}
	cb.dir=dir;
	cb.filldir=filldir;
	cb.dirent=dirent;
	cb.type=NTFS_INO2VOL(dir)->ngt;
	do{
		ntfs_debug(DEBUG_OTHER,"looking for next file\n");
		error=ntfs_getdir_unsorted(NTFS_LINO2NINO(dir),&cb.ph,&cb.pl,
				   ntfs_printcb,&cb);
	}while(!error && cb.ph!=0xFFFFFFFF);
	filp->f_pos=(cb.ph<<16)|cb.pl;
	ntfs_debug(DEBUG_OTHER, "new position %x\n",(unsigned)filp->f_pos);
        /* -EINVAL is on user buffer full. This is not considered 
	   as an error by sys_getdents */
	if(error<0) 
		error=0;
	/* Otherwise (device error, inconsistent data), switch the sign */
	return -error;
}

/* Copied from vfat driver */
static int simple_getbool(char *s, int *setval)
{
	if (s) {
		if (!strcmp(s,"1") || !strcmp(s,"yes") || !strcmp(s,"true")) {
			*setval = 1;
		} else if (!strcmp(s,"0") || !strcmp(s,"no") || !strcmp(s,"false")) {
			*setval = 0;
		} else {
			return 0;
		}
	} else {
		*setval = 1;
	}
	return 1;
}

/* Parse the (re)mount options */
static int parse_options(ntfs_volume* vol,char *opt)
{
	char *value;

	vol->uid=vol->gid=0;
	vol->umask=0077;
	vol->ngt=ngt_nt;
	vol->nls_map=0;
	vol->nct=0;
	if(!opt)goto done;

	for(opt = strtok(opt,",");opt;opt=strtok(NULL,","))
	{
		if ((value = strchr(opt, '=')) != NULL)
			*value++='\0';
		if(strcmp(opt,"uid")==0)
		{
			if(!value || !*value)goto needs_arg;
			vol->uid=simple_strtoul(value,&value,0);
			if(*value){
				printk(KERN_ERR "NTFS: uid invalid argument\n");
				return 0;
			}
		}else if(strcmp(opt, "gid") == 0)
		{
			if(!value || !*value)goto needs_arg;
			vol->gid=simple_strtoul(value,&value,0);
			if(*value){
				printk(KERN_ERR "gid invalid argument\n");
				return 0;
			}
		}else if(strcmp(opt, "umask") == 0)
		{
			if(!value || !*value)goto needs_arg;
			vol->umask=simple_strtoul(value,&value,0);
			if(*value){
				printk(KERN_ERR "umask invalid argument\n");
				return 0;
			}
		}else if(strcmp(opt, "iocharset") == 0){
			if(!value || !*value)goto needs_arg;
			vol->nls_map=load_nls(value);
			vol->nct |= nct_map;
			if(!vol->nls_map){
				printk(KERN_ERR "NTFS: charset not found");
				return 0;
			}
		}else if(strcmp(opt, "posix") == 0){
			int val;
			if(!value || !*value)goto needs_arg;
			if(!simple_getbool(value,&val))
				goto needs_bool;
			vol->ngt=val?ngt_posix:ngt_nt;
		}else if(strcmp(opt,"utf8") == 0){
			int val=0;
			if(!value || !*value)
				val=1;
			else if(!simple_getbool(value,&val))
				goto needs_bool;
			if(val)
				vol->nct|=nct_utf8;
		}else if(strcmp(opt,"uni_xlate") == 0){
			int val=0;
			/* no argument: uni_vfat.
			   boolean argument: uni_vfat.
			   "2": uni.
			*/
			if(!value || !*value)
				val=1;
			else if(strcmp(value,"2")==0)
				vol->nct |= nct_uni_xlate;
			else if(!simple_getbool(value,&val))
				goto needs_bool;
			if(val)
				vol->nct |= nct_uni_xlate_vfat | nct_uni_xlate;
		}else{
			printk(KERN_ERR "NTFS: unkown option '%s'\n", opt);
			return 0;
		}
	}
	if(vol->nct & nct_utf8 & (nct_map | nct_uni_xlate)){
		printk(KERN_ERR "utf8 cannot be combined with iocharset or uni_xlate\n");
		return 0;
	}
 done:
	if((vol->nct & (nct_uni_xlate | nct_map | nct_utf8))==0)
		/* default to UTF-8 */
		vol->nct=nct_utf8;
	if(!vol->nls_map)
		vol->nls_map=load_nls_default();
	return 1;

 needs_arg:
	printk(KERN_ERR "NTFS: %s needs an argument",opt);
	return 0;
 needs_bool:
	printk(KERN_ERR "NTFS: %s needs boolean argument",opt);
	return 0;
}
			
static int ntfs_lookup(struct inode *dir, struct dentry *d)
{
	struct inode *res=0;
	char *item=0;
	ntfs_iterate_s walk;
	int error;
	ntfs_debug(DEBUG_NAME1, "Looking up %s in %x\n",d->d_name.name,
		   (unsigned)dir->i_ino);
	/* convert to wide string */
	error=ntfs_decodeuni(NTFS_INO2VOL(dir),(char*)d->d_name.name,
			     d->d_name.len,&walk.name,&walk.namelen);
	if(error)
		return error;
	item=ntfs_malloc(ITEM_SIZE);
	/* ntfs_getdir will place the directory entry into item,
	   and the first long long is the MFT record number */
	walk.type=BY_NAME;
	walk.dir=NTFS_LINO2NINO(dir);
	walk.result=item;
	if(ntfs_getdir_byname(&walk))
	{
		res=iget(dir->i_sb,NTFS_GETU32(item));
	}
	d_add(d,res);
	ntfs_free(item);
	ntfs_free(walk.name);
	/* Always return success, the dcache will handle negative entries. */
	return 0;
}

static struct file_operations ntfs_file_operations_nommap = {
	NULL, /* lseek */
	ntfs_read,
#ifdef CONFIG_NTFS_RW
	ntfs_write,
#else
	NULL,
#endif
	NULL, /* readdir */
	NULL, /* select */
	NULL, /* ioctl */
	NULL, /* mmap */
	NULL, /* open */
	NULL, /* flush */
	NULL, /* release */
	NULL, /* fsync */
	NULL, /* fasync */
	NULL, /* check_media_change */
	NULL, /* revalidate */
	NULL, /* lock */
};

static struct inode_operations ntfs_inode_operations_nobmap = {
	&ntfs_file_operations_nommap,
	NULL, /* create */
	NULL, /* lookup */
	NULL, /* link */
	NULL, /* unlink */
	NULL, /* symlink */
	NULL, /* mkdir */
	NULL, /* rmdir */
	NULL, /* mknod */
	NULL, /* rename */
	NULL, /* readlink */
	NULL, /* follow_link */
	NULL, /* readpage */
	NULL, /* writepage */
	NULL, /* bmap */
	NULL, /* truncate */
	NULL, /* permission */
	NULL, /* smap */
	NULL, /* updatepage */
	NULL, /* revalidate */
};

#ifdef CONFIG_NTFS_RW
static int
ntfs_create(struct inode* dir,struct dentry *d,int mode)
{
	struct inode *r=0;
	ntfs_inode *ino=0;
	ntfs_volume *vol;
	int error=0;
	ntfs_attribute *si;

	r=get_empty_inode();
	if(!r){
		error=ENOMEM;
		goto fail;
	}

	ntfs_debug(DEBUG_OTHER, "ntfs_create %s\n",d->d_name.name);
	vol=NTFS_INO2VOL(dir);
#ifdef NTFS_IN_LINUX_KERNEL
	ino=NTFS_LINO2NINO(r);
#else
	ino=ntfs_malloc(sizeof(ntfs_inode));
	if(!ino){
		error=ENOMEM;
		goto fail;
	}
	r->u.generic_ip=ino;
#endif
	error=ntfs_alloc_file(NTFS_LINO2NINO(dir),ino,(char*)d->d_name.name,
			       d->d_name.len);
	if(error)goto fail;
	error=ntfs_update_inode(ino);
	if(error)goto fail;
	error=ntfs_update_inode(NTFS_LINO2NINO(dir));
	if(error)goto fail;

	r->i_uid=vol->uid;
	r->i_gid=vol->gid;
	r->i_nlink=1;
	r->i_sb=dir->i_sb;
	/* FIXME: dirty? dev? */
	/* get the file modification times from the standard information */
	si=ntfs_find_attr(ino,vol->at_standard_information,NULL);
	if(si){
		char *attr=si->d.data;
		r->i_atime=ntfs_ntutc2unixutc(NTFS_GETU64(attr+0x18));
		r->i_ctime=ntfs_ntutc2unixutc(NTFS_GETU64(attr));
		r->i_mtime=ntfs_ntutc2unixutc(NTFS_GETU64(attr+8));
	}
	/* It's not a directory */
	r->i_op=&ntfs_inode_operations_nobmap;
	r->i_mode=S_IFREG|S_IRUGO;
#ifdef CONFIG_NTFS_RW
	r->i_mode|=S_IWUGO;
#endif
	r->i_mode &= ~vol->umask;

	d_instantiate(d,r);
	return 0;
 fail:
	#ifndef NTFS_IN_LINUX_KERNEL
	if(ino)ntfs_free(ino);
	#endif
	if(r)iput(r);
	return -error;
}

static int
_linux_ntfs_mkdir(struct inode *dir, struct dentry* d, int mode)
{
	int error;
	struct inode *r = 0;
	ntfs_volume *vol;
	ntfs_inode *ino;
	ntfs_attribute *si;

	ntfs_debug (DEBUG_DIR1, "mkdir %s in %x\n",d->d_name.name, dir->i_ino);
	error = ENAMETOOLONG;
	if (d->d_name.len > /* FIXME */255)
		goto out;

	error = EIO;
	r = get_empty_inode();
	if (!r)
		goto out;
	
	vol = NTFS_INO2VOL(dir);
#ifdef NTFS_IN_LINUX_KERNEL
	ino = NTFS_LINO2NINO(r);
#else
	ino = ntfs_malloc(sizeof(ntfs_inode));
	error = ENOMEM;
	if(!ino)
		goto out;
	r->u.generic_ip = ino;
#endif
	error = ntfs_mkdir(NTFS_LINO2NINO(dir), 
			   d->d_name.name, d->d_name.len, ino);
	if(error)
		goto out;
	r->i_uid = vol->uid;
	r->i_gid = vol->gid;
	r->i_nlink = 1;
	r->i_sb = dir->i_sb;
	si = ntfs_find_attr(ino,vol->at_standard_information,NULL);
	if(si){
		char *attr = si->d.data;
		r->i_atime = ntfs_ntutc2unixutc(NTFS_GETU64(attr+0x18));
		r->i_ctime = ntfs_ntutc2unixutc(NTFS_GETU64(attr));
		r->i_mtime = ntfs_ntutc2unixutc(NTFS_GETU64(attr+8));
	}
	/* It's a directory */
	r->i_op = &ntfs_dir_inode_operations;
	r->i_mode = S_IFDIR|S_IRUGO|S_IXUGO;
#ifdef CONFIG_NTFS_RW
	r->i_mode|=S_IWUGO;
#endif
	r->i_mode &= ~vol->umask;	
	
	d_instantiate(d, r);
	error = 0;
 out:
 	ntfs_debug (DEBUG_DIR1, "mkdir returns %d\n", -error);
	return -error;
}
#endif

static int 
ntfs_bmap(struct inode *ino,int block)
{
	int ret=ntfs_vcn_to_lcn(NTFS_LINO2NINO(ino),block);
	ntfs_debug(DEBUG_OTHER, "bmap of %lx,block %x is %x\n",
	       ino->i_ino,block,ret);
	ntfs_error("bmap of %lx,block %x is %x\n", ino->i_ino,block,ret);
	ntfs_error("super %x\n", ino->i_sb->s_blocksize); 
	return (ret==-1) ? 0:ret;
}

static struct file_operations ntfs_file_operations = {
	NULL, /* lseek */
	ntfs_read,
#ifdef CONFIG_NTFS_RW
	ntfs_write,
#else
	NULL,
#endif
	NULL, /* readdir */
	NULL, /* select */
	NULL, /* ioctl */
	generic_file_mmap,
	NULL, /* open */
	NULL, /* flush */
	NULL, /* release */
	NULL, /* fsync */
	NULL, /* fasync */
	NULL, /* check_media_change */
	NULL, /* revalidate */
	NULL, /* lock */
};

static struct inode_operations ntfs_inode_operations = {
	&ntfs_file_operations,
	NULL, /* create */
	NULL, /* lookup */
	NULL, /* link */
	NULL, /* unlink */
	NULL, /* symlink */
	NULL, /* mkdir */
	NULL, /* rmdir */
	NULL, /* mknod */
	NULL, /* rename */
	NULL, /* readlink */
	NULL, /* follow_link */
	generic_readpage,
	NULL, /* writepage */
	ntfs_bmap,
	NULL, /* truncate */
	NULL, /* permission */
	NULL, /* smap */
	NULL, /* updatepage */
	NULL, /* revalidate */
};

static struct file_operations ntfs_dir_operations = {
	NULL, /* lseek */
	NULL, /* read */
	NULL, /* write */
	ntfs_readdir, /* readdir */
	NULL, /* poll */
	NULL, /* ioctl */
	NULL, /* mmap */
	NULL, /* open */
	NULL, /* flush */
	NULL, /* release */
	NULL, /* fsync */
	NULL, /* fasync */
	NULL, /* check_media_change */
	NULL, /* revalidate */
	NULL, /* lock */
};

static struct inode_operations ntfs_dir_inode_operations = {
	&ntfs_dir_operations,
#ifdef CONFIG_NTFS_RW
	ntfs_create, /* create */
#else
	NULL,
#endif
	ntfs_lookup, /* lookup */
	NULL, /* link */
	NULL, /* unlink */
	NULL, /* symlink */
#ifdef CONFIG_NTFS_RW
	_linux_ntfs_mkdir, /* mkdir */
#else
	NULL,
#endif
	NULL, /* rmdir */
	NULL, /* mknod */
	NULL, /* rename */
	NULL, /* readlink */
	NULL, /* follow_link */
	NULL, /* readpage */
	NULL, /* writepage */
	NULL, /* bmap */
	NULL, /* truncate */
	NULL, /* permission */
	NULL, /* smap */
	NULL, /* updatepage */
	NULL, /* revalidate */
};

/* ntfs_read_inode is called by the Virtual File System (the kernel layer that
 * deals with filesystems) when iget is called requesting an inode not already
 * present in the inode table. Typically filesystems have separate
 * inode_operations for directories, files and symlinks.
 */
static void ntfs_read_inode(struct inode* inode)
{
	ntfs_volume *vol;
	int can_mmap=0;
	ntfs_inode *ino;
	ntfs_attribute *data;
	ntfs_attribute *si;

	vol=NTFS_INO2VOL(inode);
	inode->i_op=NULL;
	inode->i_mode=0;
	ntfs_debug(DEBUG_OTHER, "ntfs_read_inode %x\n",(unsigned)inode->i_ino);

	switch(inode->i_ino)
	{
		/* those are loaded special files */
	case FILE_MFT:
		ntfs_error("Trying to open MFT\n");return;
	default:
		#ifdef NTFS_IN_LINUX_KERNEL
		ino=&inode->u.ntfs_i;
		#else
		ino=(ntfs_inode*)ntfs_malloc(sizeof(ntfs_inode));
		inode->u.generic_ip=ino;
		#endif
		if(!ino || ntfs_init_inode(ino,
					   NTFS_INO2VOL(inode),inode->i_ino))
		{
			ntfs_debug(DEBUG_OTHER, "NTFS:Error loading inode %x\n",
			       (unsigned int)inode->i_ino);
			return;
		}
	}
	/* Set uid/gid from mount options */
	inode->i_uid=vol->uid;
	inode->i_gid=vol->gid;
	inode->i_nlink=1;
	/* Use the size of the data attribute as file size */
	data = ntfs_find_attr(ino,vol->at_data,NULL);
	if(!data)
	{
		inode->i_size=0;
		can_mmap=0;
	}
	else
	{
		inode->i_size=data->size;
		can_mmap=!data->resident && !data->compressed;
	}
	/* get the file modification times from the standard information */
	si=ntfs_find_attr(ino,vol->at_standard_information,NULL);
	if(si){
		char *attr=si->d.data;
		inode->i_atime=ntfs_ntutc2unixutc(NTFS_GETU64(attr+0x18));
		inode->i_ctime=ntfs_ntutc2unixutc(NTFS_GETU64(attr));
		inode->i_mtime=ntfs_ntutc2unixutc(NTFS_GETU64(attr+8));
	}
	/* if it has an index root, it's a directory */
	if(ntfs_find_attr(ino,vol->at_index_root,"$I30"))
	{
		ntfs_attribute *at;
		at = ntfs_find_attr (ino, vol->at_index_allocation, "$I30");
		inode->i_size = at ? at->size : 0;
	  
		inode->i_op=&ntfs_dir_inode_operations;
		inode->i_mode=S_IFDIR|S_IRUGO|S_IXUGO;
	}
	else
	{
		inode->i_op=can_mmap ? &ntfs_inode_operations : 
			&ntfs_inode_operations_nobmap;
		inode->i_mode=S_IFREG|S_IRUGO;
	}
#ifdef CONFIG_NTFS_RW
	if(!data || !data->compressed)
		inode->i_mode|=S_IWUGO;
#endif
	inode->i_mode &= ~vol->umask;
}

#ifdef CONFIG_NTFS_RW
static void 
ntfs_write_inode (struct inode *ino)
{
	ntfs_debug (DEBUG_LINUX, "ntfs:write inode %x\n", ino->i_ino);
	ntfs_update_inode (NTFS_LINO2NINO (ino));
}
#endif

static void _ntfs_clear_inode(struct inode *ino)
{
	ntfs_debug(DEBUG_OTHER, "ntfs_clear_inode %lx\n",ino->i_ino);
#ifdef NTFS_IN_LINUX_KERNEL
	if(ino->i_ino!=FILE_MFT)
		ntfs_clear_inode(&ino->u.ntfs_i);
#else
	if(ino->i_ino!=FILE_MFT && ino->u.generic_ip)
	{
		ntfs_clear_inode(ino->u.generic_ip);
		ntfs_free(ino->u.generic_ip);
		ino->u.generic_ip=0;
	}
#endif
	return;
}

/* Called when umounting a filesystem by do_umount() in fs/super.c */
static void ntfs_put_super(struct super_block *sb)
{
	ntfs_volume *vol;

	ntfs_debug(DEBUG_OTHER, "ntfs_put_super\n");

	vol=NTFS_SB2VOL(sb);

	ntfs_release_volume(vol);
	if(vol->nls_map)
		unload_nls(vol->nls_map);
#ifndef NTFS_IN_LINUX_KERNEL
	ntfs_free(vol);
#endif
	ntfs_debug(DEBUG_OTHER, "ntfs_put_super: done\n");
	MOD_DEC_USE_COUNT;
}

/* Called by the kernel when asking for stats */
static int ntfs_statfs(struct super_block *sb, struct statfs *sf, int bufsize)
{
	struct statfs fs;
	struct inode *mft;
	ntfs_volume *vol;

	ntfs_debug(DEBUG_OTHER, "ntfs_statfs\n");
	vol=NTFS_SB2VOL(sb);
	memset(&fs,0,sizeof(fs));
	fs.f_type=NTFS_SUPER_MAGIC;
	fs.f_bsize=vol->clustersize;

	fs.f_blocks=ntfs_get_volumesize(NTFS_SB2VOL(sb));
	fs.f_bfree=ntfs_get_free_cluster_count(vol->bitmap);
	fs.f_bavail=fs.f_bfree;

	/* Number of files is limited by free space only, so we lie here */
	fs.f_ffree=0;
	mft=iget(sb,FILE_MFT);
	fs.f_files=mft->i_size/vol->mft_recordsize;
	iput(mft);

	/* should be read from volume */
	fs.f_namelen=255;
	copy_to_user(sf,&fs,bufsize);
	return 0;
}

/* Called when remounting a filesystem by do_remount_sb() in fs/super.c */
static int ntfs_remount_fs(struct super_block *sb, int *flags, char *options)
{
	if(!parse_options(NTFS_SB2VOL(sb), options))
		return -EINVAL;
	return 0;
}

/* Define the super block operation that are implemented */
static struct super_operations ntfs_super_operations = {
	ntfs_read_inode,
#ifdef CONFIG_NTFS_RW
	ntfs_write_inode,
#else
	NULL,
#endif
	NULL, /* put_inode */
	NULL, /* delete_inode */
	NULL, /* notify_change */
	ntfs_put_super,
	NULL, /* write_super */
	ntfs_statfs,
	ntfs_remount_fs, /* remount */
	_ntfs_clear_inode, /* clear_inode */ 
};

/* Called to mount a filesystem by read_super() in fs/super.c
 * Return a super block, the main structure of a filesystem
 *
 * NOTE : Don't store a pointer to an option, as the page containing the
 * options is freed after ntfs_read_super() returns.
 *
 * NOTE : A context switch can happen in kernel code only if the code blocks
 * (= calls schedule() in kernel/sched.c).
 */
struct super_block * ntfs_read_super(struct super_block *sb, 
				     void *options, int silent)
{
	ntfs_volume *vol;
	struct buffer_head *bh;
	int i;

	/* When the driver is compiled as a module, kmod must know when it
	 * can safely remove it from memory. To do this, each module owns a
	 * reference counter.
	 */
	MOD_INC_USE_COUNT;
	/* Don't put ntfs_debug() before MOD_INC_USE_COUNT, printk() can block
	 * so this could lead to a race condition with kmod.
	 */
	ntfs_debug(DEBUG_OTHER, "ntfs_read_super\n");

#ifdef NTFS_IN_LINUX_KERNEL
	vol = NTFS_SB2VOL(sb);
#else
	if(!(vol = ntfs_malloc(sizeof(ntfs_volume))))
		goto ntfs_read_super_dec;
	NTFS_SB2VOL(sb)=vol;
#endif
	
	if(!parse_options(vol,(char*)options))
		goto ntfs_read_super_vol;

	/* Ensure that the super block won't be used until it is completed */
	lock_super(sb);
	ntfs_debug(DEBUG_OTHER, "lock_super\n");
#if 0
	/* Set to read only, user option might reset it */
	sb->s_flags |= MS_RDONLY;
#endif

	/* Assume a 512 bytes block device for now */
	set_blocksize(sb->s_dev, 512);
	/* Read the super block (boot block) */
	if(!(bh=bread(sb->s_dev,0,512))) {
		ntfs_error("Reading super block failed\n");
		goto ntfs_read_super_unl;
	}
	ntfs_debug(DEBUG_OTHER, "Done reading boot block\n");

	/* Check for 'NTFS' magic number */
	if(!IS_NTFS_VOLUME(bh->b_data)){
		ntfs_debug(DEBUG_OTHER, "Not a NTFS volume\n");
		brelse(bh);
		goto ntfs_read_super_unl;
	}

	ntfs_debug(DEBUG_OTHER, "Going to init volume\n");
	ntfs_init_volume(vol,bh->b_data);
	ntfs_debug(DEBUG_OTHER, "MFT record at cluster 0x%X\n",vol->mft_cluster);
	brelse(bh);
	NTFS_SB(vol)=sb;
	ntfs_debug(DEBUG_OTHER, "Done to init volume\n");

	/* Inform the kernel that a device block is a NTFS cluster */
	sb->s_blocksize=vol->clustersize;
	for(i=sb->s_blocksize,sb->s_blocksize_bits=0;i != 1;i>>=1)
		sb->s_blocksize_bits++;
	set_blocksize(sb->s_dev,sb->s_blocksize);
	ntfs_debug(DEBUG_OTHER, "set_blocksize\n");

	/* Allocate a MFT record (MFT record can be smaller than a cluster) */
	if(!(vol->mft=ntfs_malloc(max(vol->mft_recordsize,vol->clustersize))))
		goto ntfs_read_super_unl;

	/* Read at least the MFT record for $MFT */
	for(i=0;i<max(vol->mft_clusters_per_record,1);i++){
		if(!(bh=bread(sb->s_dev,vol->mft_cluster+i,vol->clustersize))) {
			ntfs_error("Could not read MFT record 0\n");
			goto ntfs_read_super_mft;
		}
		ntfs_memcpy(vol->mft+i*vol->clustersize,bh->b_data,vol->clustersize);
		brelse(bh);
		ntfs_debug(DEBUG_OTHER, "Read cluster %x\n",vol->mft_cluster+i);
	}

	/* Check and fixup this MFT record */
	if(!ntfs_check_mft_record(vol,vol->mft)){
		ntfs_error("Invalid MFT record 0\n");
		goto ntfs_read_super_mft;
	}

	/* Inform the kernel about which super operations are available */
	sb->s_op = &ntfs_super_operations;
	sb->s_magic = NTFS_SUPER_MAGIC;
	
	ntfs_debug(DEBUG_OTHER, "Reading special files\n");
	if(ntfs_load_special_files(vol)){
		ntfs_error("Error loading special files\n");
		goto ntfs_read_super_mft;
	}

	ntfs_debug(DEBUG_OTHER, "Getting RootDir\n");
	/* Get the root directory */
	if(!(sb->s_root=d_alloc_root(iget(sb,FILE_ROOT),NULL))){
		ntfs_error("Could not get root dir inode\n");
		goto ntfs_read_super_mft;
	}
	unlock_super(sb);
	ntfs_debug(DEBUG_OTHER, "unlock_super\n");
	ntfs_debug(DEBUG_OTHER, "read_super: done\n");
	return sb;

ntfs_read_super_mft:
	ntfs_free(vol->mft);
ntfs_read_super_unl:
	sb->s_dev = 0;
	unlock_super(sb);
	ntfs_debug(DEBUG_OTHER, "unlock_super\n");
ntfs_read_super_vol:
	#ifndef NTFS_IN_LINUX_KERNEL
	ntfs_free(vol);
ntfs_read_super_dec:
	#endif
	ntfs_debug(DEBUG_OTHER, "read_super: done\n");
	MOD_DEC_USE_COUNT;
	return NULL;
}

/* Define the filesystem
 *
 * Define SECOND if you cannot unload ntfs, and want to avoid rebooting
 * for just one more test
 */
static struct file_system_type ntfs_fs_type = {
/* Filesystem name, as used after mount -t */
#ifndef SECOND
	"ntfs",
#else
	"ntfs2",
#endif
/* This filesystem requires a device (a hard disk)
 * May want to add FS_IBASKET when it works
 */
	FS_REQUIRES_DEV,
/* Entry point of the filesystem */
	ntfs_read_super,
/* Will point to the next filesystem in the kernel table */
	NULL
};

/* When this code is not compiled as a module, this is the main entry point,
 * called by do_sys_setup() in fs/filesystems.c
 *
 * NOTE : __initfunc() is a macro used to remove this function from memory
 * once initialization is done
 */
__initfunc(int init_ntfs_fs(void))
{
	/* Comment this if you trust klogd. There are reasons not to trust it
	 */
#if defined(DEBUG) && !defined(MODULE)
	extern int console_loglevel;
	console_loglevel=15;
#endif
	printk(KERN_NOTICE "NTFS version " NTFS_VERSION "\n");
	SYSCTL(1);
	ntfs_debug(DEBUG_OTHER, "registering %s\n",ntfs_fs_type.name);
	/* add this filesystem to the kernel table of filesystems */
	return register_filesystem(&ntfs_fs_type);
}

#ifdef MODULE
/* A module is a piece of code which can be inserted in and removed
 * from the running kernel whenever you want using lsmod, or on demand using
 * kmod
 */

/* No function of this module is needed by another module */
EXPORT_NO_SYMBOLS;
/* Only used for documentation purposes at the moment,
 * see include/linux/module.h
 */
MODULE_AUTHOR("Martin von Löwis");
MODULE_DESCRIPTION("NTFS driver");
/* no MODULE_SUPPORTED_DEVICE() */
/* Load-time parameter */
MODULE_PARM(ntdebug, "i");
MODULE_PARM_DESC(ntdebug, "Debug level");

/* When this code is compiled as a module, if you use mount -t ntfs when no
 * ntfs filesystem is registered (see /proc/filesystems), get_fs_type() in
 * fs/super.c asks kmod to load the module named ntfs in memory.
 *
 * Therefore, this function is the main entry point in this case
 */
int init_module(void)
{
	return init_ntfs_fs();
}

/* Called by kmod just before the kernel removes the module from memory */
void cleanup_module(void)
{
	SYSCTL(0);
	ntfs_debug(DEBUG_OTHER, "unregistering %s\n",ntfs_fs_type.name);
	unregister_filesystem(&ntfs_fs_type);
}
#endif

/*
 * Local variables:
 *  c-file-style: "linux"
 * End:
 */
