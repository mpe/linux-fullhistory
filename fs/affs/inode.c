/*
 *  linux/fs/affs/inode.c
 *
 *  (C) 1994  Geert Uytterhoeven - Modified for MultiUserFileSystem
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1992  Eric Youngdale Modified for ISO9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */

#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/locks.h>

#include <asm/system.h>
#include <asm/segment.h>
#include <linux/errno.h>

#include <linux/genhd.h>

#include "amigaffs.h"

extern int check_cdrom_media_change(int, int);

#ifdef LEAK_CHECK
static int check_malloc = 0;
static int check_bread = 0;
#endif

void affs_put_super(struct super_block *sb)
{
	lock_super(sb);

#ifdef LEAK_CHECK
	printk("Outstanding mallocs:%d, outstanding buffers: %d\n",
	       check_malloc, check_bread);
#endif
	sb->s_dev = 0;
	unlock_super(sb);
	return;
}

static struct super_operations affs_sops = {
	affs_read_inode,
	NULL,			/* notify_change */
	NULL,			/* write_inode */
	NULL,			/* put_inode */
	affs_put_super,
	NULL,			/* write_super */
	affs_statfs,
	NULL			/* remount */
};

int affs_parent_ino(struct inode *dir)
{
	int root_ino = (dir->i_sb->u.affs_sb.s_root_block
			- dir->i_sb->u.affs_sb.s_partition_offset);

	if (!S_ISDIR (dir->i_mode)) {
		printk ("affs_parent_ino: argument is not a directory\n");
		return root_ino;
	}
	if (dir->i_ino == root_ino)
		return root_ino;
	return dir->u.affs_i.i_parent;
}

static int parse_options(char *options, struct affs_options *optp)
{
	char *this_opt,*value,*end;
	int n;

	optp->offset = 0;
	optp->size = 0;
	optp->root = 0;
	optp->conv_links = 0;

	if (!options)
		return 1;
	for (this_opt = strtok(options,","); this_opt; this_opt = strtok(NULL,",")) {
		if ((value = strchr(this_opt,'='))) *value++ = 0;

		if (!strcmp(this_opt,"offset") && value) {
			n = simple_strtoul (value, &end, 10);
			if (end == value || *end != 0)
				return 0;
			optp->offset = n;
		}
		else if (!strcmp(this_opt,"size") && value) {
			n = simple_strtoul (value, &end, 10);
			if (end == value || *end != 0 || n <= 0)
				return 0;
			optp->size = n;
		}
		else if (!strcmp(this_opt,"root") && value) {
			n = simple_strtoul (value, &end, 10);
			if (end == value || *end != 0 || n <= 0)
				return 0;
			optp->root = n;
		}
		else if (!strcmp(this_opt,"conv_symlinks")) {
			optp->conv_links = 1;
		}
		else return 0;
	}
	return 1;
}

/* Is this The Right Way?  Should I be locking something? */

static int get_device_size (dev_t dev)
{
	struct gendisk *gd_p;
	int dev_size = 0;

	for (gd_p = gendisk_head ; gd_p ; gd_p=gd_p->next) {
		if (gd_p->major != MAJOR(dev))
			continue;
		dev_size = gd_p->part[MINOR(dev)].nr_sects;
		break;
	}
	return dev_size;
}

struct super_block *affs_read_super(struct super_block *s,void *data,
				    int silent)
{
	struct buffer_head *bh;
	int dev = s->s_dev;
	int root_block;
	int ptype, stype;
	void *root_data;
	struct affs_options *optp;

	optp = &s->u.affs_sb.s_options;

	if (!parse_options((char *) data, optp)) {
		s->s_dev = 0;
		printk ("AFFS: bad mount options\n");
		return NULL;
	}

	lock_super(s);

	root_block = 0;
	if (optp->size) {
		s->u.affs_sb.s_partition_size = optp->size;
	}
	else {
		int size = get_device_size (dev);
		if (size == 0) {
			s->s_dev = 0;
			unlock_super(s);
			printk ("affs_read_super: could not"
				"determine device size\n");
		}
		s->u.affs_sb.s_partition_size = size;
	}

	s->u.affs_sb.s_partition_offset = optp->offset;
	root_block = optp->root;

	if (!root_block)
		root_block = (s->u.affs_sb.s_partition_offset
			      + s->u.affs_sb.s_partition_size / 2
			      + (s->u.affs_sb.s_partition_size & 1));
	s->u.affs_sb.s_root_block = root_block;

	s->u.affs_sb.s_block_size = AFFS_BLOCK_SIZE;

#if 0
	printk ("affs_read_super: dev=0x%04x offset=%d "
		"size=%d root=%d blocksize=%d\n",
		dev,
		s->u.affs_sb.s_partition_offset,
		s->u.affs_sb.s_partition_size,
		s->u.affs_sb.s_root_block,
		s->u.affs_sb.s_block_size);
#endif

	bh = affs_sread (dev, root_block, &root_data);
	if (!bh) {
		s->s_dev = 0;
		unlock_super(s);
		printk ("AFFS: unable to read superblock\n");
		return NULL;
	}

	if (affs_checksum_block (AFFS_BLOCK_SIZE, root_data, &ptype, &stype)
	    || ptype != T_SHORT || stype != ST_ROOT) {
		printk ("AFFS: invalid root block %d on device 0x%04x\n",
			root_block, dev);
		goto out;
	}

#if 1
{
	char *name;
	int len;
	char buf[33];
	len = affs_get_file_name (AFFS_BLOCK_SIZE, root_data, &name);
	memcpy (buf,name,len);
	buf[len] = 0;
#if 0
	printk ("affs_read_super: volume name \"%s\"\n", buf);
#endif
}
#endif

	s->s_magic = AFFS_SUPER_MAGIC;

	s->s_flags = MS_RDONLY | MS_NODEV | MS_NOSUID;

	brelse(bh);

	/* set up enough so that it can read an inode */
	s->s_dev = dev;
	s->s_op = &affs_sops;
	s->s_blocksize = AFFS_BUFFER_SIZE;
	s->s_mounted = iget (s, root_block - s->u.affs_sb.s_partition_offset);

	unlock_super(s);

	if (!(s->s_mounted)) {
		s->s_dev = 0;
		printk("AFFS: get root inode failed\n");
		return NULL;
	}

	return s;

 out: /* Kick out for various error conditions */
	brelse (bh);
	s->s_dev = 0;
	unlock_super(s);
	return NULL;
}

void affs_statfs (struct super_block *sb, struct statfs *buf, int bufsiz)
{
#ifdef DEBUG
	printk ("AFFS: affs_statfs called\n");
#endif
	put_fs_long(AFFS_SUPER_MAGIC, &buf->f_type);
	put_fs_long(sb->u.affs_sb.s_block_size, &buf->f_bsize);
	put_fs_long(sb->u.affs_sb.s_partition_size, &buf->f_blocks);
	put_fs_long(0, &buf->f_bfree);
	put_fs_long(0, &buf->f_bavail);
	put_fs_long(0, &buf->f_files);
	put_fs_long(0, &buf->f_ffree);
	/* Don't know what value to put in buf->f_fsid */
}

static int prot_table[9][2] = {
	{PROT_OTR_EXECUTE, PROT_OTR_EXECUTE},	/* other: 1 = allowed */
	{PROT_OTR_WRITE, PROT_OTR_WRITE},
	{PROT_OTR_READ, PROT_OTR_READ},
	{PROT_GRP_EXECUTE, PROT_GRP_EXECUTE},	/* group: 1 = allowed */
	{PROT_GRP_WRITE, PROT_GRP_WRITE},
	{PROT_GRP_READ, PROT_GRP_READ},
	{PROT_EXECUTE, 0},			/* owner: 0 = allowed */
	{PROT_WRITE, 0},
	{PROT_READ, 0}
};

void affs_read_inode(struct inode * inode)
{
	struct buffer_head *bh;
	int block;
	void *fh_data;
	struct file_front *file_front;
	struct file_end *file_end;
	int i;
	struct hardlink_end *link_end;
	int link;	

#ifdef DEBUG
	printk ("AFFS: entering affs_read_inode\n");
#endif

	inode->i_nlink = 1; /* at least */
	do {
		link = 0;
		block = inode->i_ino;
		if (!(bh=affs_pread (inode, block, &fh_data))) {
			printk("AFFS: unable to read i-node block %d\n", block);
			return;
		}

		file_front = (struct file_front *) fh_data;
		file_end = GET_END_PTR (struct file_end, fh_data, /* coincidently the same as  dir_end */
					AFFS_I2BSIZE (inode));

		/* don't use bitmap data for mode, uid & gid of the rootblock */
		if (block == inode->i_sb->u.affs_sb.s_root_block) {
			inode->u.affs_i.i_protect = 0;
			inode->u.affs_i.i_parent = block;

			inode->i_mode = S_IRWXUGO | S_IFDIR | S_ISVTX ;  /* drwxrwxrwt */
			inode->i_nlink = 2; /* at least ..... */

			inode->i_size = 0;  /* some different idea ? */

			inode->i_uid = 0;
			inode->i_gid = 0;
		}
		else { 

			inode->u.affs_i.i_protect = file_end->protect;
			inode->u.affs_i.i_parent = swap_long (file_end->parent);

			inode->i_mode = 0;
			for (i = 0; i < 9; i++)
				if ((prot_table[i][0] & inode->u.affs_i.i_protect) == prot_table[i][1])
					inode->i_mode |= 1<<i;
			switch(swap_long(file_end->secondary_type)) {
				case ST_USERDIR:
					inode->i_mode |= ((inode->i_mode & 0444)>>2) | S_IFDIR;

					inode->i_nlink++; /* There are always at least 2.  It is
							       hard to figure out what is correct*/
					inode->i_size = 0;
				break;
				case ST_SOFTLINK:
					inode->i_mode |= S_IFLNK;
					inode->i_size  = 0;
				break;
				case ST_LINKFILE:   /* doing things very easy (not really correct) */
				case ST_LINKDIR:    /* code is _very_ inefficient (see below) */

				  /* Where is struct link_end defined?
				     ... I don't know what is going on
				     here, someone else should
				     probably spend some time on this */
					link_end = (struct hardlink_end *)file_end;
					inode->i_ino = link_end->original;
					inode->i_nlink += 2; /* It's hard to say what's correct */
					brelse(bh);
					link = 1;
				break;
				default:
					printk("affs: unknown secondary type %ld; assuming file\n",
						file_end->secondary_type);
				case ST_FILE:
					inode->i_mode |= S_IFREG;
					inode->i_size = swap_long (file_end->byte_size);
				break;
				}
			if (file_end->uid == 0xffff)
				inode->i_uid = 0;	/* root uid */
			else if (file_end->uid == 0x0000) {
				umode_t mode;
				inode->i_uid = -1;	/* unknown uid */

				/*
				 * change the mode of the inode to duplicate the
				 * perms of the user in the group and other fields;
				 * the assumption is that this isn't a MultiUser
				 * filesystem/file, so the permissions should be
				 * the same for all users
				 */
				mode = (inode->i_mode >> 6) & 7;
				inode->i_mode |= (mode << 3) | (mode);
			} else
				inode->i_uid = file_end->uid;
			if (file_end->gid == 0xffff)
				inode->i_gid = 0;	/* root gid */
			else if (file_end->gid == 0x0000)
				inode->i_gid = -1;	/* unknown gid */
			else
				inode->i_gid = file_end->gid;
		}
	}
	while (link);

#ifdef DEBUG
	printk ("AFFS: read inode %d: size=%d\n", block, inode->i_size);
#endif
	inode->i_mtime = inode->i_atime = inode->i_ctime
		= (swap_long (file_end->created.ds_Days) * (24 * 60 * 60)
		   + swap_long (file_end->created.ds_Minute) * 60
		   + swap_long (file_end->created.ds_Tick) / 50
		   + ((8 * 365 + 2) * 24 * 60 * 60));

	brelse(bh);

	inode->i_op = NULL;
	if (S_ISREG(inode->i_mode))
		inode->i_op = &affs_file_inode_operations;
	else if (S_ISDIR(inode->i_mode))
		inode->i_op = &affs_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &affs_symlink_inode_operations;
}


#ifdef LEAK_CHECK
#undef malloc
#undef free_s
#undef bread
#undef brelse

void * leak_check_malloc(unsigned int size){
  void * tmp;
  check_malloc++;
  tmp = kmalloc(size, GFP_ATOMIC);
  return tmp;
}

void leak_check_free_s(void * obj, int size){
  check_malloc--;
  return kfree_s(obj, size);
}

struct buffer_head * leak_check_bread(int dev, int block, int size){
  check_bread++;
  return bread(dev, block, size);
}

void leak_check_brelse(struct buffer_head * bh){
  check_bread--;
  return brelse(bh);
}

#endif
