/*
 *  linux/fs/affs/inode.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 * 
 *  (C) 1992  Eric Youngdale Modified for ISO9660 filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/affs_fs.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/amigaffs.h>
#include <linux/major.h>
#include <linux/blkdev.h>
#include <asm/system.h>
#include <asm/segment.h>

extern int *blk_size[];
extern struct timezone sys_tz;

#define MIN(a,b) (((a)<(b))?(a):(b))

void
affs_put_super(struct super_block *sb)
{
	int	 i;

	pr_debug("affs_put_super()\n");

	lock_super(sb);
	for (i = 0; i < sb->u.affs_sb.s_bm_count; i++)
		affs_brelse(sb->u.affs_sb.s_bitmap[i].bm_bh);
	ROOT_END_S(sb->u.affs_sb.s_root_bh->b_data,sb)->bm_flag = htonl(1);
	secs_to_datestamp(CURRENT_TIME,&ROOT_END_S(sb->u.affs_sb.s_root_bh->b_data,sb)->disk_altered);
	affs_fix_checksum(sb->s_blocksize,sb->u.affs_sb.s_root_bh->b_data,5);
	mark_buffer_dirty(sb->u.affs_sb.s_root_bh,1);

	if (sb->u.affs_sb.s_flags & SF_PREFIX)
		kfree(sb->u.affs_sb.s_prefix);
	kfree(sb->u.affs_sb.s_bitmap);
	affs_brelse(sb->u.affs_sb.s_root_bh);
	set_blocksize(sb->s_dev,BLOCK_SIZE);
	sb->s_dev = 0;
	unlock_super(sb);
	MOD_DEC_USE_COUNT;
	return;
}

static void
affs_write_super(struct super_block *sb)
{
	int			 i, clean = 2;

	if ((sb->u.affs_sb.s_flags & SF_USE_MP) && !sb->u.affs_sb.s_uid && sb->s_covered) {
		sb->s_mounted->i_uid = sb->u.affs_sb.s_uid = sb->s_covered->i_uid;
		sb->s_mounted->i_gid = sb->u.affs_sb.s_gid = sb->s_covered->i_gid;
		sb->u.affs_sb.s_flags &= ~SF_USE_MP;
	}
	if (!(sb->s_flags & MS_RDONLY)) {
		lock_super(sb);
		for (i = 0, clean = 1; i < sb->u.affs_sb.s_bm_count; i++) {
			if (sb->u.affs_sb.s_bitmap[i].bm_bh) {
				if (buffer_dirty(sb->u.affs_sb.s_bitmap[i].bm_bh)) {
					clean = 0;
					break;
				}
			}
		}
		unlock_super(sb);
		ROOT_END_S(sb->u.affs_sb.s_root_bh->b_data,sb)->bm_flag = htonl(clean);
		secs_to_datestamp(CURRENT_TIME,
				  &ROOT_END_S(sb->u.affs_sb.s_root_bh->b_data,sb)->disk_altered);
		affs_fix_checksum(sb->s_blocksize,sb->u.affs_sb.s_root_bh->b_data,5);
		mark_buffer_dirty(sb->u.affs_sb.s_root_bh,1);
		sb->s_dirt = !clean;	/* redo until bitmap synced */
	} else
		sb->s_dirt = 0;

	pr_debug("AFFS: write_super() at %d, clean=%d\n",CURRENT_TIME,clean);
}

static struct super_operations affs_sops = { 
	affs_read_inode,
	affs_notify_change,
	affs_write_inode,
	affs_put_inode,	
	affs_put_super,
	affs_write_super,
	affs_statfs,
	NULL			/* remount */
};

int
affs_parent_ino(struct inode *dir)
{
	int root_ino = (dir->i_sb->u.affs_sb.s_root_block);

	if (!S_ISDIR (dir->i_mode)) {
		printk ("affs_parent_ino: argument is not a directory\n");
		return root_ino;
	}
	if (dir->i_ino == root_ino)
		return root_ino;
	return dir->u.affs_i.i_parent;
}

static int
parse_options(char *options, uid_t *uid, gid_t *gid, int *mode, int *reserved, int *root,
		int *blocksize, char **prefix, char *volume, unsigned long *mount_opts)
{
	char	*this_char, *value;
	int	 f;

	/* Fill in defaults */

	*uid        = 0;
	*gid        = 0;
	*reserved   = 2;
	*root       = -1;
	*blocksize  = -1;
	*prefix     = "/";
	volume[0]   = ':';
	volume[1]   = 0;
	*mount_opts = 0;
	if (!options)
		return 1;
	for (this_char = strtok(options,","); this_char; this_char = strtok(NULL,",")) {
		f = 0;
		if ((value = strchr(this_char,'=')) != NULL)
			*value++ = 0;
		if (!strcmp(this_char,"protect")) {
			if (value) {
				printk("AFFS: option protect does not take an argument\n");
				return 0;
			}
			*mount_opts |= SF_IMMUTABLE;
		}
		if (!strcmp(this_char,"usemp")) {
			if (value) {
				printk("AFFS: option usemp does not take an argument\n");
				return 0;
			}
			*mount_opts |= SF_USE_MP;
		}
		else if (!strcmp(this_char,"verbose")) {
			if (value) {
				printk("AFFS: option verbose does not take an argument\n");
				return 0;
			}
			*mount_opts |= SF_VERBOSE;
		}
		else if ((f = !strcmp(this_char,"uid")) || !strcmp(this_char,"setuid")) {
			if (!value)
				*uid = current->uid;
			else if (!*value) {
				printk("AFFS: argument for uid option missing\n");
				return 0;
			} else {
				*uid = simple_strtoul(value,&value,0);
				if (*value)
					return 0;
				if (!f)
					*mount_opts |= SF_SETUID;
			}
		}
		else if ((f = !strcmp(this_char,"gid")) || !strcmp(this_char,"setgid")) {
			if (!value)
				*gid = current->gid;
			else if (!*value) {
				printk("AFFS: argument for gid option missing\n");
				return 0;
			} else {
				*gid = simple_strtoul(value,&value,0);
				if (*value)
					return 0;
				if (!f)
					*mount_opts |= SF_SETGID;
			}
		}
		else if (!strcmp(this_char,"prefix")) {
			if (!value) {
				printk("AFFS: The prefix option requires an argument\n");
				return 0;
			}
			*prefix = kmalloc(strlen(value) + 1,GFP_KERNEL);
			if (!*prefix)
				return 0;
			strcpy(*prefix,value);
			*mount_opts |= SF_PREFIX;
		}
		else if (!strcmp(this_char,"volume")) {
			if (!value) {
				printk("AFFS: The volume option requires an argument\n");
				return 0;
			}
			if (strlen(value) > 30)
				value[30] = 0;
			strcpy(volume,value);
		}
		else if (!strcmp(this_char,"mode")) {
			if (!value || !*value) {
				printk("AFFS: The mode option requires an argument\n");
				return 0;
			}
			*mode = simple_strtoul(value,&value,8) & 0777;
			if (*value)
				return 0;
			*mount_opts |= SF_SETMODE;
		}
		else if (!strcmp(this_char,"reserved")) {
			if (!value || !*value) {
				printk("AFFS: The reserved option requires an argument\n");
				return 0;
			}
			*reserved = simple_strtoul(value,&value,0);
			if (*value)
				return 0;
		}
		else if (!strcmp(this_char,"root")) {
			if (!value || !*value) {
				printk("AFFS: The root option requires an argument\n");
				return 0;
			}
			*root = simple_strtoul(value,&value,0);
			if (*value)
				return 0;
		}
		else if (!strcmp(this_char,"bs")) {
			if (!value || !*value) {
				printk("AFFS: The bs option requires an argument\n");
				return 0;
			}
			*blocksize = simple_strtoul(value,&value,0);
			if (*value)
				return 0;
			if (*blocksize != 512 && *blocksize != 1024 && *blocksize != 2048
			    && *blocksize != 4096) {
				printk ("AFFS: Invalid blocksize (512, 1024, 2048, 4096 allowed).\n");
				return 0;
			}
		}
		/* Silently ignore the quota options */
		else if (!strcmp (this_char, "grpquota")
			 || !strcmp (this_char, "noquota")
			 || !strcmp (this_char, "quota")
			 || !strcmp (this_char, "usrquota"))
			;
		else {
			printk("AFFS: Unrecognized mount option %s\n", this_char);
			return 0;
		}
	}
	return 1;
}

/* This function definitely needs to be split up. Some fine day I'll
 * hopefully have the guts to do so. Until then: sorry for the mess.
 */

struct super_block *
affs_read_super(struct super_block *s,void *data, int silent)
{
	struct buffer_head	*bh = NULL;
	struct buffer_head	*bb;
	kdev_t			 dev = s->s_dev;
	int			 root_block;
	int			 size;
	__u32			 chksum;
	__u32			*bm;
	int			 ptype, stype;
	int			 mapidx;
	int			 num_bm;
	int			 i, j;
	int			 key;
	int			 blocksize;
	uid_t			 uid;
	gid_t			 gid;
	int			 reserved;
	int			 az_no;
	unsigned long		 mount_flags;
	unsigned long		 offset;

	pr_debug("affs_read_super(%s)\n",data ? (const char *)data : "no options");

	MOD_INC_USE_COUNT;

	if (!parse_options(data,&uid,&gid,&i,&reserved,&root_block,
	    &blocksize,&s->u.affs_sb.s_prefix,s->u.affs_sb.s_volume,&mount_flags)) {
		s->s_dev = 0;
		printk("AFFS: error parsing options.\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	lock_super(s);

	/* Get the size of the device in 512-byte blocks.
	 * If we later see that the partition uses bigger
	 * blocks, we will have to change it.
	 */

	size = blksize_size[MAJOR(dev)][MINOR(dev)];
	size = (size ? size : BLOCK_SIZE) / 512 * blk_size[MAJOR(dev)][MINOR(dev)];

	s->u.affs_sb.s_bitmap  = NULL;
	s->u.affs_sb.s_root_bh = NULL;
	s->u.affs_sb.s_flags   = mount_flags;
	s->u.affs_sb.s_mode    = i;
	s->u.affs_sb.s_uid     = uid;
	s->u.affs_sb.s_gid     = gid;

	if (size == 0) {
		s->s_dev = 0;
		unlock_super(s);
		printk("affs_read_super: could not determine device size\n");
		goto out;
	}
	s->u.affs_sb.s_partition_size = size;
	s->u.affs_sb.s_reserved       = reserved;

	/* Try to find root block. Its location may depend on the block size. */

	s->u.affs_sb.s_hashsize = 0;
	if (blocksize > 0) {
		chksum = blocksize;
		num_bm = blocksize;
	} else {
		chksum = 512;
		num_bm = 4096;
	}
	for (blocksize = chksum; blocksize <= num_bm; blocksize <<= 1, size >>= 1) {
		if (root_block < 0)
			s->u.affs_sb.s_root_block = (reserved + size - 1) / 2;
		else
			s->u.affs_sb.s_root_block = root_block;
		pr_debug("Trying bs=%d bytes, root at %d, size=%d blocks (%d reserved)\n",
			 blocksize,s->u.affs_sb.s_root_block,size,reserved);
		set_blocksize(dev,blocksize);
		bh = affs_bread(dev,s->u.affs_sb.s_root_block,blocksize);
		if (!bh) {
			printk("AFFS: unable to read root block\n");
			goto out;
		}
		if (!affs_checksum_block(blocksize,bh->b_data,&ptype,&stype) &&
		    ptype == T_SHORT && stype == ST_ROOT) {
			s->s_blocksize          = blocksize;
			s->u.affs_sb.s_hashsize = blocksize / 4 - 56;
			break;
		}
		affs_brelse(bh);
		bh = NULL;
	}
	if (!s->u.affs_sb.s_hashsize) {
		affs_brelse(bh);
		if (!silent)
			printk("AFFS: Can't find a valid root block on device %s\n",kdevname(dev));
		goto out;
	}
	root_block = s->u.affs_sb.s_root_block;

	s->u.affs_sb.s_partition_size   = size;
	s->s_blocksize_bits             = blocksize == 512 ? 9 :
					  blocksize == 1024 ? 10 :
					  blocksize == 2048 ? 11 : 12;

	/* Find out which kind of FS we have */
	bb = affs_bread(dev,0,s->s_blocksize);
	if (bb) {
		chksum = htonl(*(__u32 *)bb->b_data);
		switch (chksum) {
			case MUFS_FS:
			case MUFS_INTLFFS:
				s->u.affs_sb.s_flags |= SF_MUFS;
				/* fall thru */
			case FS_INTLFFS:
				s->u.affs_sb.s_flags |= SF_INTL;
				break;
			case MUFS_FFS:
				s->u.affs_sb.s_flags |= SF_MUFS;
				break;
			case FS_FFS:
				break;
			case MUFS_OFS:
				s->u.affs_sb.s_flags |= SF_MUFS;
				/* fall thru */
			case FS_OFS:
				s->u.affs_sb.s_flags |= SF_OFS;
				break;
			case MUFS_INTLOFS:
				s->u.affs_sb.s_flags |= SF_MUFS;
				/* fall thru */
			case FS_INTLOFS:
				s->u.affs_sb.s_flags |= SF_INTL | SF_OFS;
				break;
			case FS_DCOFS:
			case FS_DCFFS:
			case MUFS_DCOFS:
			case MUFS_DCFFS:
				if (!silent)
					printk("AFFS: Unsupported filesystem on device %s: %08X\n",
					        kdevname(dev),chksum);
				if (0)
			default:
				printk("AFFS: Unknown filesystem on device %s: %08X\n",
				       kdevname(dev),chksum);
				affs_brelse(bb);
				goto out;
		}
		affs_brelse(bb);
	} else {
		printk("AFFS: Can't get boot block.\n");
		goto out;
	}
	if (mount_flags & SF_VERBOSE) {
		chksum = ntohl(chksum);
		printk("AFFS: Mounting volume \"%*s\": Type=%.3s\\%c, Blocksize=%d\n",
		       GET_END_PTR(struct root_end,bh->b_data,blocksize)->disk_name[0],
		       &GET_END_PTR(struct root_end,bh->b_data,blocksize)->disk_name[1],
		       (char *)&chksum,((char *)&chksum)[3] + '0',blocksize);
	}

	s->s_magic = AFFS_SUPER_MAGIC;
	s->s_flags = MS_NODEV | MS_NOSUID;

	/* Keep super block in cache */
	if (!(s->u.affs_sb.s_root_bh = affs_bread(dev,root_block,s->s_blocksize))) {
		printk("AFFS: Can't read root block a second time\n");
		goto out;
	}

	/* Allocate space for bitmaps, zones and others */

	size   = s->u.affs_sb.s_partition_size - reserved;
	num_bm = (size + s->s_blocksize * 8 - 32 - 1) / (s->s_blocksize * 8 - 32);
	az_no  = (size + AFFS_ZONE_SIZE - 1) / (AFFS_ZONE_SIZE - 32);
	ptype  = num_bm * sizeof(struct affs_bm_info) + 
		 az_no * sizeof(struct affs_alloc_zone) +
		 MAX_ZONES * sizeof(struct affs_zone);
	pr_debug("num_bm=%d, az_no=%d, sum=%d\n",num_bm,az_no,ptype);
	if (!(s->u.affs_sb.s_bitmap = kmalloc(ptype,GFP_KERNEL))) {
		printk("AFFS: Not enough memory.\n");
		goto out;
	}
	memset(s->u.affs_sb.s_bitmap,0,ptype);

	s->u.affs_sb.s_zones   = (struct affs_zone *)&s->u.affs_sb.s_bitmap[num_bm];
	s->u.affs_sb.s_alloc   = (struct affs_alloc_zone *)&s->u.affs_sb.s_zones[MAX_ZONES];
	s->u.affs_sb.s_num_az  = az_no;

	mapidx = 0;

	if (ROOT_END_S(bh->b_data,s)->bm_flag == 0) {
		if (!(s->s_flags & MS_RDONLY)) {
			printk("AFFS: Bitmap invalid - mounting %s read only.\n",kdevname(dev));
			s->s_flags |= MS_RDONLY;
		}
		affs_brelse(bh);
		bh = NULL;
		goto nobitmap;
	}

	/* The following section is ugly, I know. Especially because of the
	 * reuse of some variables that are not named properly.
	 */

	key    = root_block;
	ptype  = s->s_blocksize / 4 - 49;
	stype  = ptype + 25;
	offset = s->u.affs_sb.s_reserved;
	az_no  = 0;
	while (bh) {
		bm = (__u32 *)bh->b_data;
		for (i = ptype; i < stype && bm[i]; i++, mapidx++) {
			if (mapidx >= num_bm) {
				printk("AFFS: Not enough bitmap space!?\n");
				goto out;
			}
			bb = affs_bread(s->s_dev,htonl(bm[i]),s->s_blocksize);
			if (bb) {
				if (affs_checksum_block(s->s_blocksize,bb->b_data,NULL,NULL) &&
				    !(s->s_flags & MS_RDONLY)) {
					printk("AFFS: Bitmap (%d,key=%lu) invalid - "
					       "mounting %s read only.\n",mapidx,htonl(bm[i]),
						kdevname(dev));
					s->s_flags |= MS_RDONLY;
				}
				/* Mark unused bits in the last word as allocated */
				if (size <= s->s_blocksize * 8 - 32) {	/* last bitmap */
					ptype = size / 32 + 1;		/* word number */
					key   = size & 0x1F;		/* used bits */
					if (key) {
						chksum = ntohl(0x7FFFFFFF >> (31 - key));
						((__u32 *)bb->b_data)[ptype] &= chksum;
						affs_fix_checksum(s->s_blocksize,bb->b_data,0);
						mark_buffer_dirty(bb,1);
					}
					ptype = (size + 31) & ~0x1F;
					size  = 0;
					if (!(s->s_flags & MS_RDONLY))
						s->u.affs_sb.s_flags |= SF_BM_VALID;
				} else {
					ptype = s->s_blocksize * 8 - 32;
					size -= ptype;
				}
				s->u.affs_sb.s_bitmap[mapidx].bm_firstblk = offset;
				s->u.affs_sb.s_bitmap[mapidx].bm_bh       = NULL;
				s->u.affs_sb.s_bitmap[mapidx].bm_key      = htonl(bm[i]);
				s->u.affs_sb.s_bitmap[mapidx].bm_count    = 0;
				offset += ptype;

				for (j = 0; ptype > 0; j++, az_no++, ptype -= key) {
					key = MIN(ptype,AFFS_ZONE_SIZE);	/* size in bits */
					s->u.affs_sb.s_alloc[az_no].az_size = key / 32;
					s->u.affs_sb.s_alloc[az_no].az_free =
						affs_count_free_bits(key / 8,bb->b_data +
								     j * (AFFS_ZONE_SIZE / 8) + 4);
				}
				affs_brelse(bb);
			} else {
				printk("AFFS: Can't read bitmap.\n");
				goto out;
			}
		}
		key   = htonl(bm[stype]);		/* Next block of bitmap pointers	*/
		ptype = 0;
		stype = s->s_blocksize / 4 - 1;
		affs_brelse(bh);
		if (key) {
			if (!(bh = affs_bread(s->s_dev,key,s->s_blocksize))) {
				printk("AFFS: Can't read bitmap extension.\n");
				goto out;
			}
		} else
			bh = NULL;
	}
	if (mapidx != num_bm) {
		printk("AFFS: Got only %d bitmap blocks, expected %d\n",mapidx,num_bm);
		goto out;
	}
nobitmap:
	s->u.affs_sb.s_bm_count  = mapidx;

	/* set up enough so that it can read an inode */

	s->s_dev       = dev;
	s->s_op        = &affs_sops;
	s->s_mounted   = iget(s,root_block);
	s->s_dirt      = 1;
	unlock_super(s);

	if (!(s->s_mounted)) {
		s->s_dev = 0;
		printk("AFFS: get root inode failed\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	/* create data zones if the fs is mounted r/w */

	if (!(s->s_flags & MS_RDONLY)) {
		ROOT_END(s->u.affs_sb.s_root_bh->b_data,s->s_mounted)->bm_flag = 0;
		secs_to_datestamp(CURRENT_TIME,&ROOT_END(s->u.affs_sb.s_root_bh->b_data,
				  s->s_mounted)->disk_altered);
		affs_fix_checksum(s->s_blocksize,s->u.affs_sb.s_root_bh->b_data,5);
		mark_buffer_dirty(s->u.affs_sb.s_root_bh,1);
		affs_make_zones(s);
	}

	pr_debug("AFFS: s_flags=%lX\n",s->s_flags);
	return s;

 out: /* Kick out for various error conditions */
	affs_brelse (bh);
	affs_brelse(s->u.affs_sb.s_root_bh);
	if (s->u.affs_sb.s_bitmap)
		kfree(s->u.affs_sb.s_bitmap);
	s->s_dev = 0;
	unlock_super(s);
	MOD_DEC_USE_COUNT;
	return NULL;
}

void
affs_statfs(struct super_block *sb, struct statfs *buf, int bufsiz)
{
	int		 free;
	struct statfs	 tmp;

	pr_debug("AFFS: statfs() partsize=%d, reserved=%d\n",sb->u.affs_sb.s_partition_size,
	     sb->u.affs_sb.s_reserved);

	free          = affs_count_free_blocks(sb);
	tmp.f_type    = AFFS_SUPER_MAGIC;
	tmp.f_bsize   = sb->s_blocksize;
	tmp.f_blocks  = sb->u.affs_sb.s_partition_size - sb->u.affs_sb.s_reserved;
	tmp.f_bfree   = free;
	tmp.f_bavail  = free;
	tmp.f_files   = 0;
	tmp.f_ffree   = 0;
	memcpy_tofs(buf,&tmp,bufsiz);
}

void
affs_read_inode(struct inode *inode)
{
	struct buffer_head	*bh, *lbh;
	struct file_front	*file_front;
	struct file_end		*file_end;
	int			 block;
	unsigned long		 prot;
	int			 ptype, stype;
	unsigned short		 id;

	pr_debug("AFFS: read_inode(%lu)\n",inode->i_ino);
	lbh   = NULL;
	block = inode->i_ino;
	if (!(bh = affs_bread(inode->i_dev,block,AFFS_I2BSIZE(inode)))) {
		printk("AFFS: unable to read i-node block %d\n",block);
		return;
	}
	if (affs_checksum_block(AFFS_I2BSIZE(inode),bh->b_data,&ptype,&stype) || ptype != T_SHORT) {
		printk("AFFS: read_inode(): checksum or type (ptype=%d) error on inode %d\n",
		       ptype,block);
		affs_brelse(bh);
		return;
	}

	file_front = (struct file_front *)bh->b_data;
	file_end   = GET_END_PTR(struct file_end, bh->b_data,AFFS_I2BSIZE(inode));
	prot       = (htonl(file_end->protect) & ~0x10) ^ FIBF_OWNER;

	inode->u.affs_i.i_protect   = prot;
	inode->u.affs_i.i_parent    = htonl(file_end->parent);
	inode->u.affs_i.i_original  = 0;	
	inode->u.affs_i.i_zone      = 0;
	inode->u.affs_i.i_hlink     = 0;
	inode->u.affs_i.i_pa_cnt    = 0;
	inode->u.affs_i.i_pa_next   = 0;
	inode->u.affs_i.i_pa_last   = 0;
	inode->u.affs_i.i_ext[0]    = 0;
	inode->u.affs_i.i_max_ext   = 0;
	inode->u.affs_i.i_lastblock = -1;
	inode->i_nlink              = 1;
	inode->i_mode               = 0;

	if (inode->i_sb->u.affs_sb.s_flags & SF_SETMODE)
		inode->i_mode = inode->i_sb->u.affs_sb.s_mode;
	else
		inode->i_mode = prot_to_mode(prot);
	
	if (inode->i_sb->u.affs_sb.s_flags & SF_SETUID)
		inode->i_uid = inode->i_sb->u.affs_sb.s_uid;
	else {
		id = htons(file_end->owner_uid);
		if (inode->i_sb->u.affs_sb.s_flags & SF_MUFS) {
			if (id == 0 || id == 0xFFFF)
				id ^= ~0;
		}
		inode->i_uid = id;
	}
	if (inode->i_sb->u.affs_sb.s_flags & SF_SETGID)
		inode->i_gid = inode->i_sb->u.affs_sb.s_gid;
	else {
		id = htons(file_end->owner_gid);
		if (inode->i_sb->u.affs_sb.s_flags & SF_MUFS) {
			if (id == 0 || id == 0xFFFF)
				id ^= ~0;
		}
		inode->i_gid = id;
	}

	switch (htonl(file_end->secondary_type)) {
		case ST_ROOT:
			inode->i_uid   = inode->i_sb->u.affs_sb.s_uid;
			inode->i_gid   = inode->i_sb->u.affs_sb.s_gid;
		case ST_USERDIR:
			if (htonl(file_end->secondary_type) == ST_USERDIR ||
			    inode->i_sb->u.affs_sb.s_flags & SF_SETMODE) {
				if (inode->i_mode & S_IRUSR)
					inode->i_mode |= S_IXUSR;
				if (inode->i_mode & S_IRGRP)
					inode->i_mode |= S_IXGRP;
				if (inode->i_mode & S_IROTH)
					inode->i_mode |= S_IXOTH;
				inode->i_mode |= S_IFDIR;
			} else
				inode->i_mode = S_IRUGO | S_IXUGO | S_IWUSR | S_IFDIR;
			inode->i_size  = 0;
			break;
		case ST_LINKDIR:
			inode->u.affs_i.i_original = htonl(file_end->original);
			inode->u.affs_i.i_hlink    = 1;
			inode->i_mode             |= S_IFDIR;
			inode->i_size              = 0;
			break;
		case ST_LINKFILE:
			inode->u.affs_i.i_original = htonl(file_end->original);
			inode->u.affs_i.i_hlink    = 1;
			if (!(lbh = affs_bread(inode->i_dev,inode->u.affs_i.i_original,
			                  AFFS_I2BSIZE(inode)))) {
				affs_brelse(bh);
				printk("AFFS: unable to read i-node block %ld\n",inode->i_ino);
				return;
			}
			file_end = GET_END_PTR(struct file_end,lbh->b_data,AFFS_I2BSIZE(inode));
		case ST_FILE:
			inode->i_mode |= S_IFREG;
			inode->i_size  = htonl(file_end->byte_size);
			if (inode->i_sb->u.affs_sb.s_flags & SF_OFS)
				block = AFFS_I2BSIZE(inode) - 24;
			else
				block = AFFS_I2BSIZE(inode);
			inode->u.affs_i.i_lastblock = ((inode->i_size + block - 1) / block) - 1;
			break;
		case ST_SOFTLINK:
			inode->i_mode |= S_IFLNK;
			inode->i_size  = 0;
			break;
	}

	inode->i_mtime = inode->i_atime = inode->i_ctime
		       = (htonl(file_end->created.ds_Days) * (24 * 60 * 60) +
		         htonl(file_end->created.ds_Minute) * 60 +
			 htonl(file_end->created.ds_Tick) / 50 +
			 ((8 * 365 + 2) * 24 * 60 * 60)) +
			 sys_tz.tz_minuteswest * 60;
	affs_brelse(bh);
	affs_brelse(lbh);
	
	inode->i_op = NULL;
	if (S_ISREG(inode->i_mode)) {
		if (inode->i_sb->u.affs_sb.s_flags & SF_OFS) {
			inode->i_op = &affs_file_inode_operations_ofs;
		} else {
			inode->i_op = &affs_file_inode_operations;
		}
	} else if (S_ISDIR(inode->i_mode))
		inode->i_op = &affs_dir_inode_operations;
	else if (S_ISLNK(inode->i_mode))
		inode->i_op = &affs_symlink_inode_operations;
}

void
affs_write_inode(struct inode *inode)
{
	struct buffer_head *bh;
	struct file_end	   *file_end;
	short		    uid, gid;

	pr_debug("AFFS: write_inode(%lu)\n",inode->i_ino);

	inode->i_dirt = 0;
	if (!inode->i_nlink)
		return;
	if (!(bh = bread(inode->i_dev,inode->i_ino,AFFS_I2BSIZE(inode)))) {
		printk("AFFS: Unable to read block of inode %ld on %s\n",
		       inode->i_ino,kdevname(inode->i_dev));
		return;
	}
	file_end = GET_END_PTR(struct file_end, bh->b_data,AFFS_I2BSIZE(inode));
	if (file_end->secondary_type == htonl(ST_ROOT)) {
		secs_to_datestamp(inode->i_mtime,&ROOT_END(bh->b_data,inode)->disk_altered);
	} else {
		file_end->protect   = ntohl(inode->u.affs_i.i_protect ^ FIBF_OWNER);
		file_end->byte_size = ntohl(inode->i_size);
		secs_to_datestamp(inode->i_mtime,&file_end->created);
		if (!(inode->i_ino == inode->i_sb->u.affs_sb.s_root_block)) {
			uid = inode->i_uid;
			gid = inode->i_gid;
			if (inode->i_sb->u.affs_sb.s_flags & SF_MUFS) {
				if (inode->i_uid == 0 || inode->i_uid == 0xFFFF)
					uid = inode->i_uid ^ ~0;
				if (inode->i_gid == 0 || inode->i_gid == 0xFFFF)
					gid = inode->i_gid ^ ~0;
			}
			if (!(inode->i_sb->u.affs_sb.s_flags & SF_SETUID))
				file_end->owner_gid = ntohs(uid);
			if (!(inode->i_sb->u.affs_sb.s_flags & SF_SETGID))
				file_end->owner_gid = ntohs(gid);
		}
	}
	affs_fix_checksum(AFFS_I2BSIZE(inode),bh->b_data,5);
	mark_buffer_dirty(bh,1);
	brelse(bh);
}

int
affs_notify_change(struct inode *inode, struct iattr *attr)
{
	int error;

	pr_debug("AFFS: notify_change(%lu,0x%x)\n",inode->i_ino,attr->ia_valid);

	error = inode_change_ok(inode,attr);
	if (error)
		return error;
	
	if (((attr->ia_valid & ATTR_UID) && (inode->i_sb->u.affs_sb.s_flags & SF_SETUID)) ||
	    ((attr->ia_valid & ATTR_GID) && (inode->i_sb->u.affs_sb.s_flags & SF_SETGID)) ||
	    ((attr->ia_valid & ATTR_MODE) &&
	     (inode->i_sb->u.affs_sb.s_flags & (SF_SETMODE | SF_IMMUTABLE))))
		error = -EPERM;
	
	if (error)
		return (inode->i_sb->u.affs_sb.s_flags & SF_QUIET) ? 0 : error;
	
	if (attr->ia_valid & ATTR_MODE)
		inode->u.affs_i.i_protect = mode_to_prot(attr->ia_mode);

	inode_setattr(inode,attr);

	return 0;
}

void
affs_put_inode(struct inode *inode)
{
	pr_debug("AFFS: put_inode(ino=%lu, nlink=%u)\n",inode->i_ino,inode->i_nlink);
	if (inode->i_nlink) {
		return;
	}
	inode->i_size = 0;
	if (S_ISREG(inode->i_mode) && !inode->u.affs_i.i_hlink)
		affs_truncate(inode);
	affs_free_block(inode->i_sb,inode->i_ino);
	clear_inode(inode);
}

struct inode *
affs_new_inode(const struct inode *dir)
{
	struct inode		*inode;
	struct super_block	*sb;
	int			 block;

	if (!dir || !(inode = get_empty_inode()))
		return NULL;
	
	sb = dir->i_sb;
	inode->i_sb    = sb;
	inode->i_flags = sb->s_flags;

	if (!(block = affs_new_header((struct inode *)dir))) {
		iput(inode);
		return NULL;
	}

	inode->i_count   = 1;
	inode->i_nlink   = 1;
	inode->i_dev     = sb->s_dev;
	inode->i_uid     = current->fsuid;
	inode->i_gid     = current->fsgid;
	inode->i_dirt    = 1;
	inode->i_ino     = block;
	inode->i_op      = NULL;
	inode->i_blocks  = 0;
	inode->i_size    = 0;
	inode->i_mode    = 0;
	inode->i_blksize = 0;
	inode->i_mtime   = inode->i_atime = inode->i_ctime = CURRENT_TIME;

	inode->u.affs_i.i_original  = 0;
	inode->u.affs_i.i_parent    = dir->i_ino;
	inode->u.affs_i.i_zone      = 0;
	inode->u.affs_i.i_hlink     = 0;
	inode->u.affs_i.i_pa_cnt    = 0;
	inode->u.affs_i.i_pa_next   = 0;
	inode->u.affs_i.i_pa_last   = 0;
	inode->u.affs_i.i_ext[0]    = 0;
	inode->u.affs_i.i_max_ext   = 0;
	inode->u.affs_i.i_lastblock = -1;

	insert_inode_hash(inode);

	return inode;
}

int
affs_add_entry(struct inode *dir, struct inode *link, struct inode *inode,
	       const char *name, int len, int type)
{
	struct buffer_head	*dir_bh;
	struct buffer_head	*inode_bh;
	struct buffer_head	*link_bh;
	int			 hash;

	pr_debug("AFFS: add_entry(dir=%lu,inode=%lu,\"%*s\",type=%d\n",dir->i_ino,inode->i_ino,
		 len,name,type);

	dir_bh      = affs_bread(dir->i_dev,dir->i_ino,AFFS_I2BSIZE(dir));
	inode_bh    = affs_bread(inode->i_dev,inode->i_ino,AFFS_I2BSIZE(inode));
	link_bh     = NULL;
	if (!dir_bh || !inode_bh) {
		affs_brelse(dir_bh);
		affs_brelse(inode_bh);
		return -ENOSPC;
	}
	if (link) {
		link_bh = affs_bread(link->i_dev,link->i_ino,AFFS_I2BSIZE(link));
		if (!link_bh) {
			affs_brelse(dir_bh);
			affs_brelse(inode_bh);
			return -EINVAL;
		}
	}
	((struct dir_front *)inode_bh->b_data)->primary_type = ntohl(T_SHORT);
	((struct dir_front *)inode_bh->b_data)->own_key      = ntohl(inode->i_ino);

	if (len > 30)		/* truncate name quietly */
		len = 30;
	DIR_END(inode_bh->b_data,inode)->dir_name[0] = len;
	strncpy(DIR_END(inode_bh->b_data,inode)->dir_name + 1,name,len);
	DIR_END(inode_bh->b_data,inode)->secondary_type = ntohl(type);
	DIR_END(inode_bh->b_data,inode)->parent         = ntohl(dir->i_ino);
	hash = affs_hash_name(name,len,AFFS_I2FSTYPE(dir),AFFS_I2HSIZE(dir));

	lock_super(inode->i_sb);
	DIR_END(inode_bh->b_data,inode)->hash_chain = 
				((struct dir_front *)dir_bh->b_data)->hashtable[hash];
	((struct dir_front *)dir_bh->b_data)->hashtable[hash] = ntohl(inode->i_ino);
	if (link_bh) {
		LINK_END(inode_bh->b_data,inode)->original   = ntohl(link->i_ino);
		LINK_END(inode_bh->b_data,inode)->link_chain = 
						FILE_END(link_bh->b_data,link)->link_chain;
		FILE_END(link_bh->b_data,link)->link_chain   = ntohl(inode->i_ino);
		affs_fix_checksum(AFFS_I2BSIZE(link),link_bh->b_data,5);
		link->i_version = ++event;
		link->i_dirt    = 1;
		mark_buffer_dirty(link_bh,1);
	}
	affs_fix_checksum(AFFS_I2BSIZE(inode),inode_bh->b_data,5);
	affs_fix_checksum(AFFS_I2BSIZE(dir),dir_bh->b_data,5);
	dir->i_version = ++event;
	dir->i_mtime   = dir->i_atime = dir->i_ctime = CURRENT_TIME;
	unlock_super(inode->i_sb);

	dir->i_dirt    = 1;
	inode->i_dirt  = 1;
	mark_buffer_dirty(dir_bh,1);
	mark_buffer_dirty(inode_bh,1);
	affs_brelse(dir_bh);
	affs_brelse(inode_bh);
	affs_brelse(link_bh);

	return 0;
}

static struct file_system_type affs_fs_type = {
	affs_read_super,
	"affs",
	1,
	NULL
};

int
init_affs_fs(void)
{
	return register_filesystem(&affs_fs_type);
}

#ifdef MODULE

int
init_module(void)
{
	int	 status;
	if ((status = init_affs_fs()) == 0)
		register_symtab(0);
	return status;
}

void
cleanup_module(void)
{
	unregister_filesystem(&affs_fs_type);
}

#endif
