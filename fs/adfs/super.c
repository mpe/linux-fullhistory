/*
 *  linux/fs/adfs/super.c
 *
 * Copyright (C) 1997 Russell King
 */

#include <linux/module.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/adfs_fs.h>
#include <linux/malloc.h>
#include <linux/sched.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/locks.h>
#include <linux/init.h>

#include <asm/bitops.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#include <stdarg.h>

static void adfs_put_super (struct super_block *sb);
static int adfs_statfs (struct super_block *sb, struct statfs *buf, int bufsiz);
void adfs_read_inode (struct inode *inode);

void adfs_error (struct super_block *sb, const char *function, const char *fmt, ...)
{
	char error_buf[128];
	va_list args;

	va_start (args, fmt);
	vsprintf (error_buf, fmt, args);
	va_end (args);

	printk (KERN_CRIT "ADFS-fs error (device %s)%s%s: %s\n",
		kdevname (sb->s_dev), function ? ": " : "",
		function ? function : "", error_buf);
}

unsigned char adfs_calccrosscheck (struct super_block *sb, char *map)
{
	unsigned int v0, v1, v2, v3;
	int i;

	v0 = v1 = v2 = v3 = 0;
	for (i = sb->s_blocksize - 4; i; i -= 4) {
		v0 += map[i]     + (v3 >> 8);
		v3 &= 0xff;
		v1 += map[i + 1] + (v0 >> 8);
		v0 &= 0xff;
		v2 += map[i + 2] + (v1 >> 8);
		v1 &= 0xff;
		v3 += map[i + 3] + (v2 >> 8);
		v2 &= 0xff;
	}
	v0 +=           v3 >> 8;
	v1 += map[1] + (v0 >> 8);
	v2 += map[2] + (v1 >> 8);
	v3 += map[3] + (v2 >> 8);

	return v0 ^ v1 ^ v2 ^ v3;
}

static int adfs_checkmap (struct super_block *sb)
{
	unsigned char crosscheck = 0, zonecheck = 1;
	int i;

	for (i = 0; i < sb->u.adfs_sb.s_map_size; i++) {
		char *map;

		map = sb->u.adfs_sb.s_map[i]->b_data;
		if (adfs_calccrosscheck (sb, map) != map[0]) {
			adfs_error (sb, "adfs_checkmap", "zone %d fails zonecheck", i);
			zonecheck = 0;
		}
		crosscheck ^= map[3];
	}
	if (crosscheck != 0xff)
		adfs_error (sb, "adfs_checkmap", "crosscheck != 0xff");
	return crosscheck == 0xff && zonecheck;
}

static struct super_operations adfs_sops = {
	adfs_read_inode,
	NULL,
	NULL,
	NULL,
	NULL,
	adfs_put_super,
	NULL,
	adfs_statfs,
	NULL
};

static void adfs_put_super (struct super_block *sb)
{
	int i;

	for (i = 0; i < sb->u.adfs_sb.s_map_size; i++)
		brelse (sb->u.adfs_sb.s_map[i]);
	kfree (sb->u.adfs_sb.s_map);
	brelse (sb->u.adfs_sb.s_sbh);
	MOD_DEC_USE_COUNT;
}

struct super_block *adfs_read_super (struct super_block *sb, void *data, int silent)
{
	struct adfs_discrecord *dr;
	struct buffer_head *bh;
	unsigned char *b_data;
	kdev_t dev = sb->s_dev;
	int i, j;

	MOD_INC_USE_COUNT;
	lock_super (sb);
	set_blocksize (dev, BLOCK_SIZE);
	if (!(bh = bread (dev, ADFS_DISCRECORD / BLOCK_SIZE, BLOCK_SIZE))) {
		unlock_super (sb);
		adfs_error (sb, NULL, "unable to read superblock");
		MOD_DEC_USE_COUNT;
		return NULL;
	}

	b_data = bh->b_data + (ADFS_DISCRECORD % BLOCK_SIZE);

	if (adfs_checkbblk (b_data)) {
		if (!silent)
			printk ("VFS: Can't find an adfs filesystem on dev "
				"%s.\n", kdevname(dev));
failed_mount:
		unlock_super (sb);
		if (bh)
			brelse (bh);
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	dr = (struct adfs_discrecord *)(b_data + ADFS_DR_OFFSET);

	sb->s_blocksize_bits = dr->log2secsize;
	sb->s_blocksize = 1 << sb->s_blocksize_bits;
	if (sb->s_blocksize != BLOCK_SIZE &&
	    (sb->s_blocksize == 512 || sb->s_blocksize == 1024 ||
	     sb->s_blocksize == 2048 || sb->s_blocksize == 4096)) {

		brelse (bh);
		set_blocksize (dev, sb->s_blocksize);
		bh = bread (dev, ADFS_DISCRECORD / sb->s_blocksize, sb->s_blocksize);
		if (!bh) {
			adfs_error (sb, NULL, "couldn't read superblock on "
				"2nd try.");
			goto failed_mount;
		}
		b_data = bh->b_data + (ADFS_DISCRECORD % sb->s_blocksize);
		if (adfs_checkbblk (b_data)) {
			adfs_error (sb, NULL, "disc record mismatch, very weird!");
			goto failed_mount;
		}
		dr = (struct adfs_discrecord *)(b_data + ADFS_DR_OFFSET);
	}
	if (sb->s_blocksize != bh->b_size) {
		if (!silent)
			printk (KERN_ERR "VFS: Unsupported blocksize on dev "
				"%s.\n", kdevname (dev));
		goto failed_mount;
	}
	/* blocksize on this device should now be set to the adfs log2secsize */

	sb->u.adfs_sb.s_sbh		= bh;
	sb->u.adfs_sb.s_dr		= dr;

	/* s_zone_size = size of 1 zone (1 sector) * bits_in_byte - zone_spare =>
	 * number of map bits in a zone
	 */
	sb->u.adfs_sb.s_zone_size	= (8 << dr->log2secsize) - dr->zone_spare;

	/* s_ids_per_zone = bit size of 1 zone / min. length of fragment block =>
	 * number of ids in one zone
	 */
	sb->u.adfs_sb.s_ids_per_zone	= sb->u.adfs_sb.s_zone_size / (dr->idlen + 1);

	/* s_idlen = length of 1 id */
	sb->u.adfs_sb.s_idlen		= dr->idlen;

	/* map size (in sectors) = number of zones */
	sb->u.adfs_sb.s_map_size	= dr->nzones;

	/* zonesize = size of sector - zonespare */
	sb->u.adfs_sb.s_zonesize	= (sb->s_blocksize << 3) - dr->zone_spare;

	/* map start (in sectors) = start of zone (number of zones) / 2 */
	sb->u.adfs_sb.s_map_block	= (dr->nzones >> 1) * sb->u.adfs_sb.s_zone_size -
					   ((dr->nzones > 1) ? 8 * ADFS_DR_SIZE : 0);

	/* (signed) number of bits to shift left a map address to a sector address */
	sb->u.adfs_sb.s_map2blk		= dr->log2bpmb - dr->log2secsize;

	if (sb->u.adfs_sb.s_map2blk >= 0)
		sb->u.adfs_sb.s_map_block <<= sb->u.adfs_sb.s_map2blk;
	else
		sb->u.adfs_sb.s_map_block >>= -sb->u.adfs_sb.s_map2blk;

	printk (KERN_DEBUG "ADFS: zone size %d, IDs per zone %d, map address %X size %d sectors\n",
		sb->u.adfs_sb.s_zone_size, sb->u.adfs_sb.s_ids_per_zone,
		sb->u.adfs_sb.s_map_block, sb->u.adfs_sb.s_map_size);
	printk (KERN_DEBUG "ADFS: sector size %d, map bit size %d\n",
		1 << dr->log2secsize, 1 << dr->log2bpmb);

	sb->s_magic = ADFS_SUPER_MAGIC;
	sb->s_flags |= MS_RDONLY;	/* we don't support writing yet */

	sb->u.adfs_sb.s_map = kmalloc (sb->u.adfs_sb.s_map_size *
				sizeof (struct buffer_head *), GFP_KERNEL);
	if (sb->u.adfs_sb.s_map == NULL) {
		adfs_error (sb, NULL, "not enough memory");
		goto failed_mount;
	}

	for (i = 0; i < sb->u.adfs_sb.s_map_size; i++) {
		sb->u.adfs_sb.s_map[i] = bread (dev,
						sb->u.adfs_sb.s_map_block + i,
						sb->s_blocksize);
		if (!sb->u.adfs_sb.s_map[i]) {
			for (j = 0; j < i; j++)
				brelse (sb->u.adfs_sb.s_map[j]);
			kfree (sb->u.adfs_sb.s_map);
			adfs_error (sb, NULL, "unable to read map");
			goto failed_mount;
		}
	}
	if (!adfs_checkmap (sb)) {
		for (i = 0; i < sb->u.adfs_sb.s_map_size; i++)
			brelse (sb->u.adfs_sb.s_map[i]);
		adfs_error (sb, NULL, "map corrupted");
		goto failed_mount;
	}

	dr = (struct adfs_discrecord *)(sb->u.adfs_sb.s_map[0]->b_data + 4);
	unlock_super (sb);

	/*
	 * set up enough so that it can read an inode
	 */
	sb->s_op = &adfs_sops;
	sb->u.adfs_sb.s_root = adfs_inode_generate (dr->root, 0);
	sb->s_root = d_alloc_root(iget(sb, sb->u.adfs_sb.s_root), NULL);

	if (!sb->s_root) {
		sb->s_dev = 0;
		for (i = 0; i < sb->u.adfs_sb.s_map_size; i++)
			brelse (sb->u.adfs_sb.s_map[i]);
		brelse (bh);
		adfs_error (sb, NULL, "get root inode failed\n");
		MOD_DEC_USE_COUNT;
		return NULL;
	}
	return sb;
}

static int adfs_statfs (struct super_block *sb, struct statfs *buf, int bufsiz)
{
	struct statfs tmp;
	const unsigned int nidlen = sb->u.adfs_sb.s_idlen + 1;

	tmp.f_type = ADFS_SUPER_MAGIC;
	tmp.f_bsize = sb->s_blocksize;
	tmp.f_blocks = (sb->u.adfs_sb.s_dr->disc_size) >> (sb->s_blocksize_bits);
	tmp.f_files = tmp.f_blocks >> nidlen;
	{
		unsigned int i, j = 0;
		const unsigned mask = (1 << (nidlen - 1)) - 1;
		for (i = 0; i < sb->u.adfs_sb.s_map_size; i++) {
			const char *map = sb->u.adfs_sb.s_map[i]->b_data;
			unsigned freelink, mapindex = 24;
			j -= nidlen;
			do {
				unsigned char k, l, m;
				unsigned off = (mapindex - nidlen) >> 3;
				unsigned rem;
				const unsigned boff = mapindex & 7;

				/* get next freelink */

				k = map[off++];
				l = map[off++];
				m = map[off++];
				freelink = (m << 16) | (l << 8) | k;
				rem = freelink >> (boff + nidlen - 1);
				freelink = (freelink >> boff) & mask;
				mapindex += freelink;

				/* find its length and add it to running total */

				while (rem == 0) {
					j += 8;
					rem = map[off++];
				}
				if ((rem & 0xff) == 0) j+=8, rem>>=8;
				if ((rem & 0xf) == 0) j+=4, rem>>=4;
				if ((rem & 0x3) == 0) j+=2, rem>>=2;
				if ((rem & 0x1) == 0) j+=1;
				j += nidlen - boff;
				if (freelink <= nidlen) break;
			} while (mapindex < 8 * sb->s_blocksize);
			if (mapindex > 8 * sb->s_blocksize)
				adfs_error (sb, NULL, "oversized free fragment\n");
			else if (freelink)
				adfs_error (sb, NULL, "undersized free fragment\n");
		}
		tmp.f_bfree = tmp.f_bavail = j <<
			(sb->u.adfs_sb.s_dr->log2bpmb - sb->s_blocksize_bits);
	}
	tmp.f_ffree = tmp.f_bfree >> nidlen;
	tmp.f_namelen = ADFS_NAME_LEN;
	return copy_to_user (buf, &tmp, bufsiz) ? -EFAULT : 0;
}

static struct file_system_type adfs_fs_type = {
	"adfs", FS_REQUIRES_DEV, adfs_read_super, NULL
};

__initfunc(int init_adfs_fs (void))
{
	return register_filesystem (&adfs_fs_type);
}

#ifdef MODULE
int init_module (void)
{
	return init_adfs_fs();
}

void cleanup_module (void)
{
	unregister_filesystem (&adfs_fs_type);
}
#endif
