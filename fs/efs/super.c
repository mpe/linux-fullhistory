/*
 * super.c
 *
 * Copyright (c) 1999 Al Smith
 *
 * Portions derived from work (c) 1995,1996 Christian Vogelgsang.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/locks.h>
#include <linux/efs_fs.h>
#include <linux/efs_vh.h>
#include <linux/efs_fs_sb.h>

static struct file_system_type efs_fs_type = {
	"efs",			/* filesystem name */
	FS_REQUIRES_DEV,	/* fs_flags */
	efs_read_super,		/* entry function pointer */
	NULL 			/* next */
};

static struct super_operations efs_superblock_operations = {
	efs_read_inode,	/* read_inode */
	NULL,		/* write_inode */
	NULL,		/* put_inode */
	NULL,		/* delete_inode */
	NULL,		/* notify_change */
	efs_put_super,	/* put_super */
	NULL,		/* write_super */
	efs_statfs,	/* statfs */
	NULL		/* remount */
};

__initfunc(int init_efs_fs(void)) {
	return register_filesystem(&efs_fs_type);
}

#ifdef MODULE
EXPORT_NO_SYMBOLS;

int init_module(void) {
	printk("EFS: "EFS_VERSION" - http://aeschi.ch.eu.org/efs/\n");
	return init_efs_fs();
}

void cleanup_module(void) {
	unregister_filesystem(&efs_fs_type);
}
#endif

static efs_block_t efs_validate_vh(struct volume_header *vh) {
	int		i;
	unsigned int	cs, csum, *ui;
	efs_block_t	sblock = 0; /* shuts up gcc */
	struct pt_types	*pt_entry;
	int		pt_type, slice = -1;

	if (be32_to_cpu(vh->vh_magic) != VHMAGIC) {
		/*
		 * assume that we're dealing with a partition and allow
		 * read_super() to try and detect a valid superblock
		 * on the next block.
		 */
		return 0;
	}

	ui = ((unsigned int *) (vh + 1)) - 1;
	for(csum = 0; ui >= ((unsigned int *) vh);) {
		cs = *ui--;
		csum += be32_to_cpu(cs);
	}
	if (csum) {
		printk(KERN_INFO "EFS: SGI disklabel: checksum bad, label corrupted\n");
		return 0;
	}

#ifdef DEBUG
	printk(KERN_DEBUG "EFS: bf: \"%16s\"\n", vh->vh_bootfile);

	for(i = 0; i < NVDIR; i++) {
		int	j;
		char	name[VDNAMESIZE+1];

		for(j = 0; j < VDNAMESIZE; j++) {
			name[j] = vh->vh_vd[i].vd_name[j];
		}
		name[j] = (char) 0;

		if (name[0]) {
			printk(KERN_DEBUG "EFS: vh: %8s block: 0x%08x size: 0x%08x\n",
				name,
				(int) be32_to_cpu(vh->vh_vd[i].vd_lbn),
				(int) be32_to_cpu(vh->vh_vd[i].vd_nbytes));
		}
	}
#endif

	for(i = 0; i < NPARTAB; i++) {
		pt_type = (int) be32_to_cpu(vh->vh_pt[i].pt_type);
		for(pt_entry = sgi_pt_types; pt_entry->pt_name; pt_entry++) {
			if (pt_type == pt_entry->pt_type) break;
		}
#ifdef DEBUG
		if (be32_to_cpu(vh->vh_pt[i].pt_nblks)) {
			printk(KERN_DEBUG "EFS: pt %2d: start: %08d size: %08d type: 0x%02x (%s)\n",
				i,
				(int) be32_to_cpu(vh->vh_pt[i].pt_firstlbn),
				(int) be32_to_cpu(vh->vh_pt[i].pt_nblks),
				pt_type,
				(pt_entry->pt_name) ? pt_entry->pt_name : "unknown");
		}
#endif
		if (IS_EFS(pt_type)) {
			sblock = be32_to_cpu(vh->vh_pt[i].pt_firstlbn);
			slice = i;
		}
	}

	if (slice == -1) {
		printk(KERN_NOTICE "EFS: partition table contained no EFS partitions\n");
#ifdef DEBUG
	} else {
		printk(KERN_INFO "EFS: using slice %d (type %s, offset 0x%x)\n",
			slice,
			(pt_entry->pt_name) ? pt_entry->pt_name : "unknown",
			sblock);
#endif
	}
	return(sblock);
}

static int efs_validate_super(struct efs_sb_info *sb, struct efs_super *super) {

	if (!IS_EFS_MAGIC(be32_to_cpu(super->fs_magic))) return -1;

	sb->fs_magic     = be32_to_cpu(super->fs_magic);
	sb->total_blocks = be32_to_cpu(super->fs_size);
	sb->first_block  = be32_to_cpu(super->fs_firstcg);
	sb->group_size   = be32_to_cpu(super->fs_cgfsize);
	sb->data_free    = be32_to_cpu(super->fs_tfree);
	sb->inode_free   = be32_to_cpu(super->fs_tinode);
	sb->inode_blocks = be16_to_cpu(super->fs_cgisize);
	sb->total_groups = be16_to_cpu(super->fs_ncg);
    
	return 0;    
}

struct super_block *efs_read_super(struct super_block *s, void *d, int silent) {
	kdev_t dev = s->s_dev;
	struct efs_sb_info *sb;
	struct buffer_head *bh;

	MOD_INC_USE_COUNT;
	lock_super(s);
  
 	sb = SUPER_INFO(s);

	set_blocksize(dev, EFS_BLOCKSIZE);
  
	/* read the vh (volume header) block */
	bh = bread(dev, 0, EFS_BLOCKSIZE);

	if (!bh) {
		printk(KERN_ERR "EFS: cannot read volume header\n");
		goto out_no_fs_ul;
	}

	/*
	 * if this returns zero then we didn't find any partition table.
	 * this isn't (yet) an error - just assume for the moment that
	 * the device is valid and go on to search for a superblock.
	 */
	sb->fs_start = efs_validate_vh((struct volume_header *) bh->b_data);
	brelse(bh);

	if (sb->fs_start == -1) {
		goto out_no_fs_ul;
	}

	bh = bread(dev, sb->fs_start + EFS_SUPER, EFS_BLOCKSIZE);
	if (!bh) {
		printk(KERN_ERR "EFS: cannot read superblock\n");
		goto out_no_fs_ul;
	}
		
	if (efs_validate_super(sb, (struct efs_super *) bh->b_data)) {
#ifdef DEBUG
		printk(KERN_WARNING "EFS: invalid superblock at block %u\n", sb->fs_start + EFS_SUPER);
#endif
		brelse(bh);
		goto out_no_fs_ul;
	}
	brelse(bh);
 
	s->s_magic		= EFS_SUPER_MAGIC;
	s->s_blocksize		= EFS_BLOCKSIZE;
	s->s_blocksize_bits	= EFS_BLOCKSIZE_BITS;
	if (!(s->s_flags & MS_RDONLY)) {
#ifdef DEBUG
		printk(KERN_INFO "EFS: forcing read-only mode\n");
#endif
		s->s_flags |= MS_RDONLY;
	}
	s->s_op   = &efs_superblock_operations;
	s->s_dev  = dev;
	s->s_root = d_alloc_root(iget(s, EFS_ROOTINODE));
	unlock_super(s);
 
	if (!(s->s_root)) {
		printk(KERN_ERR "EFS: get root inode failed\n");
		goto out_no_fs;
	}

	if (check_disk_change(s->s_dev)) {
		printk(KERN_ERR "EFS: device changed\n");
		goto out_no_fs;
	}

	return(s);

out_no_fs_ul:
	unlock_super(s);
out_no_fs:
	s->s_dev = 0;
	MOD_DEC_USE_COUNT;
	return(NULL);
}

void efs_put_super(struct super_block *s) {
	MOD_DEC_USE_COUNT;
}

int efs_statfs(struct super_block *s, struct statfs *buf, int bufsiz) {
	struct statfs ret;
	struct efs_sb_info *sb = SUPER_INFO(s);

	ret.f_type    = EFS_SUPER_MAGIC;	/* efs magic number */
	ret.f_bsize   = EFS_BLOCKSIZE;		/* blocksize */
	ret.f_blocks  = sb->total_groups *	/* total data blocks */
			(sb->group_size - sb->inode_blocks);
	ret.f_bfree   = sb->data_free;		/* free data blocks */
	ret.f_bavail  = sb->data_free;		/* free blocks for non-root */
	ret.f_files   = sb->total_groups *	/* total inodes */
			sb->inode_blocks *
			(EFS_BLOCKSIZE / sizeof(struct efs_dinode));
	ret.f_ffree   = sb->inode_free;	/* free inodes */
	ret.f_fsid.val[0] = (sb->fs_magic >> 16) & 0xffff; /* fs ID */
	ret.f_fsid.val[1] =  sb->fs_magic        & 0xffff; /* fs ID */
	ret.f_namelen = EFS_MAXNAMELEN;		/* max filename length */

	return copy_to_user(buf, &ret, bufsiz) ? -EFAULT : 0;
}

