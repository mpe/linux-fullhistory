/*
 *  inode.c
 *
 *  Copyright (C) 1995 by Paal-Kr. Engstad and Volker Lendecke
 *
 */

#include <linux/module.h>

#include <asm/system.h>
#include <asm/segment.h>

#include <linux/sched.h>
#include <linux/smb_fs.h>
#include <linux/smbno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/errno.h>
#include <linux/locks.h>
#include <linux/fcntl.h>
#include <linux/malloc.h>

extern int close_fp(struct file *filp);

static void smb_put_inode(struct inode *);
static void smb_read_inode(struct inode *);
static void smb_put_super(struct super_block *);
static void smb_statfs(struct super_block *, struct statfs *, int bufsiz);

static struct super_operations smb_sops = {
	smb_read_inode,         /* read inode */
	smb_notify_change,      /* notify change */
	NULL,			/* write inode */
	smb_put_inode,		/* put inode */
	smb_put_super,		/* put superblock */
	NULL,			/* write superblock */
	smb_statfs,		/* stat filesystem */
	NULL
        };

/* smb_read_inode: Called from iget, it only traverses the allocated
   smb_inode_info's and initializes the inode from the data found
   there.  It does not allocate or deallocate anything. */

static void
smb_read_inode(struct inode *inode)
{
        /* Our task should be extremely simple here. We only have to
           look up the information somebody else (smb_iget) put into
           the inode tree. The address of this information is the
           inode->i_ino. Just to make sure everything went well, we
           check it's there. */

        struct smb_inode_info *inode_info
                = (struct smb_inode_info *)(inode->i_ino);

#if 1
        struct smb_inode_info *root = &(SMB_SERVER(inode)->root);
        struct smb_inode_info *check_info = root;

        do {
                if (inode_info == check_info) {
                        if (check_info->state == SMB_INODE_LOOKED_UP) {
                                DDPRINTK("smb_read_inode: found it!\n");
                                goto good;
                        }
                        else {
                                printk("smb_read_inode: "
                                       "state != SMB_INODE_LOOKED_UP\n");
                                return;
                        }
                }
                check_info = check_info->next;
        } while (check_info != root);

        /* Ok, now we're in trouble. The inode info is not there. What
           should we do now??? */
        printk("smb_read_inode: inode info not found\n");
        return;

 good:
#endif
        inode_info->state = SMB_INODE_VALID;

        SMB_INOP(inode) = inode_info;

        if (SMB_INOP(inode)->finfo.attr & aDIR)
                inode->i_mode = SMB_SERVER(inode)->m.dir_mode;
        else
                inode->i_mode = SMB_SERVER(inode)->m.file_mode;

        DDPRINTK("smb_read_inode: inode->i_mode = %u\n", inode->i_mode);

        inode->i_nlink   = 1;
        inode->i_uid     = SMB_SERVER(inode)->m.uid;
        inode->i_gid     = SMB_SERVER(inode)->m.gid;
        inode->i_size    = SMB_INOP(inode)->finfo.size;
        inode->i_blksize = 1024;
        inode->i_rdev    = 0;
        if ((inode->i_blksize != 0) && (inode->i_size != 0))
                inode->i_blocks =
                        (inode->i_size - 1) / inode->i_blksize + 1;
        else
                inode->i_blocks = 0;

        inode->i_mtime = SMB_INOP(inode)->finfo.mtime;
        inode->i_ctime = SMB_INOP(inode)->finfo.ctime;
        inode->i_atime = SMB_INOP(inode)->finfo.atime;

        if (S_ISREG(inode->i_mode))
                inode->i_op = &smb_file_inode_operations;
        else if (S_ISDIR(inode->i_mode))
                inode->i_op = &smb_dir_inode_operations;
        else
                inode->i_op = NULL;

}

static void
smb_put_inode(struct inode *inode)
{
        struct smb_dirent *finfo = SMB_FINFO(inode);

        if (finfo->opened != 0) {

                /* smb_proc_close wants mtime in finfo */
                finfo->mtime = inode->i_mtime;
                
                if (smb_proc_close(SMB_SERVER(inode), finfo)) {
                        /* We can't do anything but complain. */
                        printk("smb_put_inode: could not close\n");
                }
        }
        
        smb_free_inode_info(SMB_INOP(inode));

        if (S_ISDIR(inode->i_mode)) {
                DDPRINTK("smb_put_inode: put directory %ld\n",
                         inode->i_ino);
                smb_invalid_dir_cache(inode->i_ino);
        }                

	clear_inode(inode);
}

static void
smb_put_super(struct super_block *sb)
{
        struct smb_server *server = &(SMB_SBP(sb)->s_server);

        smb_proc_disconnect(server);
	close_fp(server->sock_file);

	lock_super(sb);

        smb_free_all_inodes(server);

        smb_kfree_s(server->packet, server->max_xmit);

	sb->s_dev = 0;
        smb_kfree_s(SMB_SBP(sb), sizeof(struct smb_sb_info));

	unlock_super(sb);

        MOD_DEC_USE_COUNT;
}


/* Hmm, should we do this like the NFS mount command does? Guess so.. */
struct super_block *
smb_read_super(struct super_block *sb, void *raw_data, int silent)
{
	struct smb_mount_data *data = (struct smb_mount_data *) raw_data;
	struct smb_server *server;
        struct smb_sb_info *smb_sb;
	unsigned int fd;
	struct file *filp;
	kdev_t dev = sb->s_dev;
	int error;

	if (!data) {
		printk("smb_read_super: missing data argument\n");
		sb->s_dev = 0;
		return NULL;
	}
	fd = data->fd;
	if (data->version != SMB_MOUNT_VERSION) {
		printk("smb warning: mount version %s than kernel\n",
		       (data->version < SMB_MOUNT_VERSION) ?
                       "older" : "newer");
	}
	if (fd >= NR_OPEN || !(filp = current->files->fd[fd])) {
		printk("smb_read_super: invalid file descriptor\n");
		sb->s_dev = 0;
		return NULL;
	}
	if (!S_ISSOCK(filp->f_inode->i_mode)) {
		printk("smb_read_super: not a socket!\n");
		sb->s_dev = 0;
		return NULL;
	}

        /* We must malloc our own super-block info */
        smb_sb = (struct smb_sb_info *)smb_kmalloc(sizeof(struct smb_sb_info),
                                                   GFP_KERNEL);

        if (smb_sb == NULL) {
                printk("smb_read_super: could not alloc smb_sb_info\n");
                return NULL;
        }

	filp->f_count += 1; 

	lock_super(sb);

        SMB_SBP(sb) = smb_sb;
        
	sb->s_blocksize = 1024; /* Eh...  Is this correct? */
	sb->s_blocksize_bits = 10;
	sb->s_magic = SMB_SUPER_MAGIC;
	sb->s_dev = dev;
	sb->s_op = &smb_sops;

	server = &(SMB_SBP(sb)->s_server);
	server->sock_file = filp;
	server->lock = 0;
	server->wait = NULL;
        server->packet = NULL;
	server->max_xmit = data->max_xmit;
	if (server->max_xmit <= 0)
		server->max_xmit = SMB_DEF_MAX_XMIT;
   
	server->tid = 0;
	server->pid = current->pid;
	server->mid = current->pid + 20;

        server->m = *data;
	server->m.file_mode = (server->m.file_mode &
                             (S_IRWXU|S_IRWXG|S_IRWXO)) | S_IFREG;
	server->m.dir_mode  = (server->m.dir_mode &
                             (S_IRWXU|S_IRWXG|S_IRWXO)) | S_IFDIR;

        smb_init_root(server);

        /*
         * Make the connection to the server
         */
        
	error = smb_proc_connect(server);

	unlock_super(sb);

	if (error < 0) {
		sb->s_dev = 0;
		printk("smb_read_super: Failed connection, bailing out "
                       "(error = %d).\n", -error);
                goto fail;
	}

        if (server->protocol >= PROTOCOL_LANMAN2)
                server->case_handling = CASE_DEFAULT;
        else
                server->case_handling = CASE_LOWER;

	if ((error = smb_proc_dskattr(sb, &(SMB_SBP(sb)->s_attr))) < 0) {
		sb->s_dev = 0;
		printk("smb_read_super: could not get super block "
                       "attributes\n");
                smb_kfree_s(server->packet, server->max_xmit);
                goto fail;
	}

	if ((error = smb_stat_root(server)) < 0) {
		sb->s_dev = 0;
		printk("smb_read_super: could not get root dir attributes\n");
                smb_kfree_s(server->packet, server->max_xmit);
                goto fail;
	}

	DPRINTK("smb_read_super : %u %u %u %u\n",
                SMB_SBP(sb)->s_attr.total,
                SMB_SBP(sb)->s_attr.blocksize,
                SMB_SBP(sb)->s_attr.allocblocks,
                SMB_SBP(sb)->s_attr.free);

        DPRINTK("smb_read_super: SMB_SBP(sb) = %x\n", (int)SMB_SBP(sb));

	if (!(sb->s_mounted = iget(sb, (int)&(server->root)))) {
		sb->s_dev = 0;
		printk("smb_read_super: get root inode failed\n");
                smb_kfree_s(server->packet, server->max_xmit);
                goto fail;
	}

        MOD_INC_USE_COUNT;
	return sb;

 fail:
	filp->f_count -= 1; 
        smb_kfree_s(SMB_SBP(sb), sizeof(struct smb_sb_info));
        return NULL;
}

static void 
smb_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	int error;
	struct smb_dskattr attr;
	struct statfs tmp;
	
	error = smb_proc_dskattr(sb, &attr);

	if (error) {
		printk("smb_statfs: dskattr error = %d\n", -error);
		attr.total = attr.allocblocks = attr.blocksize =
                        attr.free = 0;
	}

	tmp.f_type = SMB_SUPER_MAGIC;
	tmp.f_bsize = attr.blocksize*attr.allocblocks;
	tmp.f_blocks = attr.total;
	tmp.f_bfree = attr.free;
	tmp.f_bavail = attr.free;
	tmp.f_files = -1;
	tmp.f_ffree = -1;
	tmp.f_namelen = SMB_MAXPATHLEN;
	memcpy_tofs(buf, &tmp, bufsiz);
}

/* DO MORE */
int
smb_notify_change(struct inode *inode, struct iattr *attr)
{
	int error = 0;

	if ((error = inode_change_ok(inode, attr)) < 0)
		return error;

	if (((attr->ia_valid & ATTR_UID) && 
	     (attr->ia_uid != SMB_SERVER(inode)->m.uid)))
		return -EPERM;

	if (((attr->ia_valid & ATTR_GID) && 
	     (attr->ia_uid != SMB_SERVER(inode)->m.gid)))
                return -EPERM;

	if (((attr->ia_valid & ATTR_MODE) &&
	     (attr->ia_mode & ~(S_IFREG | S_IFDIR | S_IRWXU | S_IRWXG | S_IRWXO))))
		return -EPERM;

        if ((attr->ia_valid & ATTR_SIZE) != 0) {

                if ((error = smb_make_open(inode, O_WRONLY)) < 0)
                        goto fail;

                if ((error = smb_proc_trunc(SMB_SERVER(inode),
                                            SMB_FINFO(inode)->fileid,
                                            attr->ia_size)) < 0)
                        goto fail;

        }

        if ((attr->ia_valid & (ATTR_CTIME | ATTR_MTIME | ATTR_ATIME)) != 0) {

                struct smb_dirent finfo;

                finfo.attr  = 0;

                if ((attr->ia_valid & ATTR_CTIME) != 0)
                        finfo.ctime = attr->ia_ctime;
                else
                        finfo.ctime = inode->i_ctime;

                if ((attr->ia_valid & ATTR_MTIME) != 0)
                        finfo.mtime = attr->ia_mtime;
                else
                        finfo.mtime = inode->i_mtime;

                if ((attr->ia_valid & ATTR_ATIME) != 0)
                        finfo.atime = attr->ia_atime;
                else
                        finfo.atime = inode->i_atime;

                if ((error = smb_proc_setattr(SMB_SERVER(inode),
                                              inode, &finfo)) >= 0) {
                        inode->i_ctime = finfo.ctime;
                        inode->i_mtime = finfo.mtime;
                        inode->i_atime = finfo.atime;
                }
        }

 fail:
        smb_invalid_dir_cache((unsigned long)(SMB_INOP(inode)->dir));

	return error;
}

		
#ifdef DEBUG_SMB_MALLOC
int smb_malloced;
int smb_current_malloced;
#endif

static struct file_system_type smb_fs_type = {
        smb_read_super, "smbfs", 0, NULL
        };

int init_smb_fs(void)
{
        return register_filesystem(&smb_fs_type);
}

#ifdef MODULE
int init_module(void)
{
	int status;

        DPRINTK("smbfs: init_module called\n");

#ifdef DEBUG_SMB_MALLOC
        smb_malloced = 0;
        smb_current_malloced = 0;
#endif

        smb_init_dir_cache();

	if ((status = init_smb_fs()) == 0)
		register_symtab(0);
	return status;
}

void
cleanup_module(void)
{
        DPRINTK("smbfs: cleanup_module called\n");
        smb_free_dir_cache();
        unregister_filesystem(&smb_fs_type);
#ifdef DEBUG_SMB_MALLOC
        printk("smb_malloced: %d\n", smb_malloced);
        printk("smb_current_malloced: %d\n", smb_current_malloced);
#endif
}

#endif
