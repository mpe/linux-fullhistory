/*
 * Super block/filesystem wide operations
 *
 * Copryright (C) 1996 Peter J. Braam <braam@maths.ox.ac.uk> and 
 * Michael Callahan <callahan@maths.ox.ac.uk> 
 * 
 * Rewritten for Linux 2.1.  Peter Braam <braam@cs.cmu.edu>
 * Copyright (C) Carnegie Mellon University
 */

#define __NO_VERSION__
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/unistd.h>

#include <asm/system.h>
#include <asm/uaccess.h>

#include <linux/fs.h>
#include <linux/vmalloc.h>
#include <asm/segment.h>

#include <linux/coda.h>
#include <linux/coda_linux.h>
#include <linux/coda_psdev.h>
#include <linux/coda_fs_i.h>
#include <linux/coda_cache.h>

/* VFS super_block ops */
static struct super_block *coda_read_super(struct super_block *, void *, int);
static void coda_read_inode(struct inode *);
static void coda_put_inode(struct inode *);
static void coda_delete_inode(struct inode *);
static void coda_put_super(struct super_block *);
static int coda_statfs(struct super_block *sb, struct statfs *buf);

/* exported operations */
struct super_operations coda_super_operations =
{
	read_inode:	coda_read_inode,
	put_inode:	coda_put_inode,
	delete_inode:	coda_delete_inode,
	put_super:	coda_put_super,
	statfs:		coda_statfs,
};

static struct super_block * coda_read_super(struct super_block *sb, 
					    void *data, int silent)
{
        struct inode *psdev = 0, *root = 0; 
	struct coda_sb_info *sbi = NULL;
	struct venus_comm *vc = NULL;
        ViceFid fid;
	kdev_t dev = sb->s_dev;
        int error;

	ENTRY;

        vc = &coda_upc_comm;
	sbi = &coda_super_info;

        if ( sbi->sbi_sb ) {
		printk("Already mounted\n");
		EXIT;  
		return NULL;
	}

	sbi->sbi_sb = sb;
        sbi->sbi_psdev = psdev;
	sbi->sbi_vcomm = vc;
	INIT_LIST_HEAD(&(sbi->sbi_cchead));
	INIT_LIST_HEAD(&(sbi->sbi_volroothead));

        sb->u.generic_sbp = sbi;
        sb->s_blocksize = 1024;	/* XXXXX  what do we put here?? */
        sb->s_blocksize_bits = 10;
        sb->s_magic = CODA_SUPER_MAGIC;
        sb->s_dev = dev;
        sb->s_op = &coda_super_operations;

	/* get root fid from Venus: this needs the root inode */
	error = venus_rootfid(sb, &fid);
	if ( error ) {
	        printk("coda_read_super: coda_get_rootfid failed with %d\n",
		       error);
		goto error;
	}	  
	printk("coda_read_super: rootfid is %s\n", coda_f2s(&fid));
	
	/* make root inode */
        error = coda_cnode_make(&root, &fid, sb);
        if ( error || !root ) {
	    printk("Failure of coda_cnode_make for root: error %d\n", error);
	    goto error;
	} 

	printk("coda_read_super: rootinode is %ld dev %d\n", 
	       root->i_ino, root->i_dev);
	sbi->sbi_root = root;
	sb->s_root = d_alloc_root(root);
	EXIT;  
        return sb;

 error:
	EXIT;  
	if (sbi) {
		sbi->sbi_vcomm = NULL;
		sbi->sbi_root = NULL;
		sbi->sbi_sb = NULL;
	}
        if (root) {
                iput(root);
        }
        return NULL;
}

static void coda_put_super(struct super_block *sb)
{
        struct coda_sb_info *sb_info;

        ENTRY;

	coda_cache_clear_all(sb);
	sb_info = coda_sbp(sb);
	coda_super_info.sbi_sb = NULL;
	printk("Coda: Bye bye.\n");
	memset(sb_info, 0, sizeof(* sb_info));

	EXIT;
}

/* all filling in of inodes postponed until lookup */
static void coda_read_inode(struct inode *inode)
{
	struct coda_inode_info *cii;
	ENTRY;
	cii = ITOC(inode);
	cii->c_magic = 0;
	return;
}

static void coda_put_inode(struct inode *inode) 
{
	ENTRY;

	CDEBUG(D_INODE,"ino: %ld, count %d\n", inode->i_ino, inode->i_count);
		
	if ( inode->i_count == 1 ) {
                write_inode_now(inode);
		inode->i_nlink = 0;
        }
}

static void coda_delete_inode(struct inode *inode)
{
        struct coda_inode_info *cii;
        struct inode *open_inode;

        ENTRY;
        CDEBUG(D_SUPER, " inode->ino: %ld, count: %d\n", 
	       inode->i_ino, inode->i_count);        

        cii = ITOC(inode);
	if ( inode->i_ino == CTL_INO || cii->c_magic != CODA_CNODE_MAGIC ) {
	        clear_inode(inode);
		return;
	}

	if ( ! list_empty(&cii->c_volrootlist) ) {
		list_del(&cii->c_volrootlist);
		INIT_LIST_HEAD(&cii->c_volrootlist);
        }

        open_inode = cii->c_ovp;
        if ( open_inode ) {
                CDEBUG(D_SUPER, "DELINO cached file: ino %ld count %d.\n",  
		       open_inode->i_ino,  open_inode->i_count);
                cii->c_ovp = NULL;
		inode->i_mapping = &inode->i_data;
                iput(open_inode);
        }
	
	coda_cache_clear_inode(inode);
	CDEBUG(D_DOWNCALL, "clearing inode: %ld, %x\n", inode->i_ino, cii->c_flags);
	inode->u.coda_i.c_magic = 0;
        clear_inode(inode);
	EXIT;
}

int coda_notify_change(struct dentry *de, struct iattr *iattr)
{
	struct inode *inode = de->d_inode;
        struct coda_inode_info *cii;
        struct coda_vattr vattr;
        int error;
	
	ENTRY;
        memset(&vattr, 0, sizeof(vattr)); 
        cii = ITOC(inode);
        CHECK_CNODE(cii);

        coda_iattr_to_vattr(iattr, &vattr);
        vattr.va_type = C_VNON; /* cannot set type */
	CDEBUG(D_SUPER, "vattr.va_mode %o\n", vattr.va_mode);

        error = venus_setattr(inode->i_sb, &cii->c_fid, &vattr);

        if ( !error ) {
	        coda_vattr_to_iattr(inode, &vattr); 
		coda_cache_clear_inode(inode);
        }
	CDEBUG(D_SUPER, "inode.i_mode %o, error %d\n", 
	       inode->i_mode, error);

	EXIT;
        return error;
}

static int coda_statfs(struct super_block *sb, struct statfs *buf)
{
	int error;

	error = venus_statfs(sb, buf);

	if (error) {
		/* fake something like AFS does */
		buf->f_blocks = 9000000;
		buf->f_bfree  = 9000000;
		buf->f_bavail = 9000000;
		buf->f_files  = 9000000;
		buf->f_ffree  = 9000000;
	}

	/* and fill in the rest */
	buf->f_type = CODA_SUPER_MAGIC;
	buf->f_bsize = 1024;
	buf->f_namelen = CODA_MAXNAMLEN;

	return 0; 
}


/* init_coda: used by filesystems.c to register coda */

DECLARE_FSTYPE( coda_fs_type, "coda", coda_read_super, 0);

int init_coda_fs(void)
{
	return register_filesystem(&coda_fs_type);
}



