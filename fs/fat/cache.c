/*
 *  linux/fs/fat/cache.c
 *
 *  Written 1992,1993 by Werner Almesberger
 *
 *  Mar 1999. AV. Changed cache, so that it uses the starting cluster instead
 *	of inode number.
 *  May 1999. AV. Fixed the bogosity with FAT32 (read "FAT28"). Fscking lusers.
 */

#include <linux/fs.h>
#include <linux/msdos_fs.h>
#include <linux/buffer_head.h>

static struct fat_cache *fat_cache,cache[FAT_CACHE];
static spinlock_t fat_cache_lock = SPIN_LOCK_UNLOCKED;

int __fat_access(struct super_block *sb, int nr, int new_value)
{
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	struct buffer_head *bh, *bh2, *c_bh, *c_bh2;
	unsigned char *p_first, *p_last;
	int copy, first, last, next, b;

	if (sbi->fat_bits == 32) {
		first = last = nr*4;
	} else if (sbi->fat_bits == 16) {
		first = last = nr*2;
	} else {
		first = nr*3/2;
		last = first+1;
	}
	b = sbi->fat_start + (first >> sb->s_blocksize_bits);
	if (!(bh = sb_bread(sb, b))) {
		printk(KERN_ERR "FAT: bread(block %d) in"
		       " fat_access failed\n", b);
		return -EIO;
	}
	if ((first >> sb->s_blocksize_bits) == (last >> sb->s_blocksize_bits)) {
		bh2 = bh;
	} else {
		if (!(bh2 = sb_bread(sb, b + 1))) {
			brelse(bh);
			printk(KERN_ERR "FAT: bread(block %d) in"
			       " fat_access failed\n", b + 1);
			return -EIO;
		}
	}
	if (sbi->fat_bits == 32) {
		p_first = p_last = NULL; /* GCC needs that stuff */
		next = CF_LE_L(((__u32 *) bh->b_data)[(first &
		    (sb->s_blocksize - 1)) >> 2]);
		/* Fscking Microsoft marketing department. Their "32" is 28. */
		next &= 0x0fffffff;
	} else if (sbi->fat_bits == 16) {
		p_first = p_last = NULL; /* GCC needs that stuff */
		next = CF_LE_W(((__u16 *) bh->b_data)[(first &
		    (sb->s_blocksize - 1)) >> 1]);
	} else {
		p_first = &((__u8 *)bh->b_data)[first & (sb->s_blocksize - 1)];
		p_last = &((__u8 *)bh2->b_data)[(first + 1) & (sb->s_blocksize - 1)];
		if (nr & 1)
			next = ((*p_first >> 4) | (*p_last << 4)) & 0xfff;
		else
			next = (*p_first+(*p_last << 8)) & 0xfff;
	}
	if (new_value != -1) {
		if (sbi->fat_bits == 32) {
			((__u32 *)bh->b_data)[(first & (sb->s_blocksize - 1)) >> 2]
				= CT_LE_L(new_value);
		} else if (sbi->fat_bits == 16) {
			((__u16 *)bh->b_data)[(first & (sb->s_blocksize - 1)) >> 1]
				= CT_LE_W(new_value);
		} else {
			if (nr & 1) {
				*p_first = (*p_first & 0xf) | (new_value << 4);
				*p_last = new_value >> 4;
			}
			else {
				*p_first = new_value & 0xff;
				*p_last = (*p_last & 0xf0) | (new_value >> 8);
			}
			mark_buffer_dirty(bh2);
		}
		mark_buffer_dirty(bh);
		for (copy = 1; copy < sbi->fats; copy++) {
			b = sbi->fat_start + (first >> sb->s_blocksize_bits)
				+ sbi->fat_length * copy;
			if (!(c_bh = sb_bread(sb, b)))
				break;
			if (bh != bh2) {
				if (!(c_bh2 = sb_bread(sb, b+1))) {
					brelse(c_bh);
					break;
				}
				memcpy(c_bh2->b_data, bh2->b_data, sb->s_blocksize);
				mark_buffer_dirty(c_bh2);
				brelse(c_bh2);
			}
			memcpy(c_bh->b_data, bh->b_data, sb->s_blocksize);
			mark_buffer_dirty(c_bh);
			brelse(c_bh);
		}
	}
	brelse(bh);
	if (bh != bh2)
		brelse(bh2);
	return next;
}

/* 
 * Returns the this'th FAT entry, -1 if it is an end-of-file entry. If
 * new_value is != -1, that FAT entry is replaced by it.
 */
int fat_access(struct super_block *sb, int nr, int new_value)
{
	int next;

	next = -EIO;
	if (nr < 2 || MSDOS_SB(sb)->clusters + 2 <= nr) {
		fat_fs_panic(sb, "invalid access to FAT (entry 0x%08x)", nr);
		goto out;
	}
	if (new_value == FAT_ENT_EOF)
		new_value = EOF_FAT(sb);

	next = __fat_access(sb, nr, new_value);
	if (next < 0)
		goto out;
	if (next >= BAD_FAT(sb))
		next = FAT_ENT_EOF;
out:
	return next;
}

void fat_cache_init(void)
{
	static int initialized;
	int count;

	spin_lock(&fat_cache_lock);
	if (initialized) {
		spin_unlock(&fat_cache_lock);
		return;
	}
	fat_cache = &cache[0];
	for (count = 0; count < FAT_CACHE; count++) {
		cache[count].sb = NULL;
		cache[count].next = count == FAT_CACHE-1 ? NULL :
		    &cache[count+1];
	}
	initialized = 1;
	spin_unlock(&fat_cache_lock);
}


void fat_cache_lookup(struct inode *inode,int cluster,int *f_clu,int *d_clu)
{
	struct fat_cache *walk;
	int first = MSDOS_I(inode)->i_start;

	if (!first)
		return;
	spin_lock(&fat_cache_lock);
	for (walk = fat_cache; walk; walk = walk->next)
		if (inode->i_sb == walk->sb
		    && walk->start_cluster == first
		    && walk->file_cluster <= cluster
		    && walk->file_cluster > *f_clu) {
			*d_clu = walk->disk_cluster;
#ifdef DEBUG
printk("cache hit: %d (%d)\n",walk->file_cluster,*d_clu);
#endif
			if ((*f_clu = walk->file_cluster) == cluster) { 
				spin_unlock(&fat_cache_lock);
				return;
			}
		}
	spin_unlock(&fat_cache_lock);
#ifdef DEBUG
printk("cache miss\n");
#endif
}


#ifdef DEBUG
static void list_cache(void)
{
	struct fat_cache *walk;

	for (walk = fat_cache; walk; walk = walk->next) {
		if (walk->sb)
			printk("<%s,%d>(%d,%d) ", walk->sb->s_id,
			       walk->start_cluster, walk->file_cluster,
			       walk->disk_cluster);
		else printk("-- ");
	}
	printk("\n");
}
#endif


void fat_cache_add(struct inode *inode,int f_clu,int d_clu)
{
	struct fat_cache *walk,*last;
	int first = MSDOS_I(inode)->i_start;

	last = NULL;
	spin_lock(&fat_cache_lock);
	for (walk = fat_cache; walk->next; walk = (last = walk)->next)
		if (inode->i_sb == walk->sb
		    && walk->start_cluster == first
		    && walk->file_cluster == f_clu) {
			if (walk->disk_cluster != d_clu) {
				printk(KERN_ERR "FAT: cache corruption"
				       " (ino %lu)\n", inode->i_ino);
				spin_unlock(&fat_cache_lock);
				fat_cache_inval_inode(inode);
				return;
			}
			/* update LRU */
			if (last == NULL) {
				spin_unlock(&fat_cache_lock);
				return;
			}
			last->next = walk->next;
			walk->next = fat_cache;
			fat_cache = walk;
#ifdef DEBUG
list_cache();
#endif
			spin_unlock(&fat_cache_lock);
			return;
		}
	walk->sb = inode->i_sb;
	walk->start_cluster = first;
	walk->file_cluster = f_clu;
	walk->disk_cluster = d_clu;
	last->next = NULL;
	walk->next = fat_cache;
	fat_cache = walk;
	spin_unlock(&fat_cache_lock);
#ifdef DEBUG
list_cache();
#endif
}


/* Cache invalidation occurs rarely, thus the LRU chain is not updated. It
   fixes itself after a while. */

void fat_cache_inval_inode(struct inode *inode)
{
	struct fat_cache *walk;
	int first = MSDOS_I(inode)->i_start;

	spin_lock(&fat_cache_lock);
	for (walk = fat_cache; walk; walk = walk->next)
		if (walk->sb == inode->i_sb
		    && walk->start_cluster == first)
			walk->sb = NULL;
	spin_unlock(&fat_cache_lock);
}


void fat_cache_inval_dev(struct super_block *sb)
{
	struct fat_cache *walk;

	spin_lock(&fat_cache_lock);
	for (walk = fat_cache; walk; walk = walk->next)
		if (walk->sb == sb)
			walk->sb = 0;
	spin_unlock(&fat_cache_lock);
}


static int fat_get_cluster(struct inode *inode, int cluster)
{
	struct super_block *sb = inode->i_sb;
	int nr,count;

	if (!(nr = MSDOS_I(inode)->i_start)) return 0;
	if (!cluster) return nr;
	count = 0;
	for (fat_cache_lookup(inode, cluster, &count, &nr);
	     count < cluster;
	     count++) {
		nr = fat_access(sb, nr, -1);
		if (nr == FAT_ENT_EOF) {
			fat_fs_panic(sb, "%s: request beyond EOF (ino %lu)",
				     __FUNCTION__, inode->i_ino);
			return -EIO;
		} else if (nr == FAT_ENT_FREE) {
			fat_fs_panic(sb, "%s: invalid cluster chain (ino %lu)",
				     __FUNCTION__, inode->i_ino);
			return -EIO;
		} else if (nr < 0)
			return nr;
	}
	fat_cache_add(inode, cluster, nr);
	return nr;
}

int fat_bmap(struct inode *inode, int sector)
{
	struct super_block *sb = inode->i_sb;
	struct msdos_sb_info *sbi = MSDOS_SB(sb);
	int cluster, offset, last_block;

	if ((sbi->fat_bits != 32) &&
	    (inode->i_ino == MSDOS_ROOT_INO || (S_ISDIR(inode->i_mode) &&
	     !MSDOS_I(inode)->i_start))) {
		if (sector >= sbi->dir_entries >> sbi->dir_per_block_bits)
			return 0;
		return sector + sbi->dir_start;
	}
	last_block = (MSDOS_I(inode)->mmu_private + (sb->s_blocksize - 1))
		>> sb->s_blocksize_bits;
	if (sector >= last_block)
		return 0;

	cluster = sector / sbi->cluster_size;
	offset  = sector % sbi->cluster_size;
	cluster = fat_get_cluster(inode, cluster);
	if (cluster < 0)
		return cluster;
	else if (!cluster)
		return 0;
	return (cluster - 2) * sbi->cluster_size + sbi->data_start + offset;
}


/* Free all clusters after the skip'th cluster. Doesn't use the cache,
   because this way we get an additional sanity check. */

int fat_free(struct inode *inode,int skip)
{
	struct super_block *sb = inode->i_sb;
	int nr,last;

	if (!(nr = MSDOS_I(inode)->i_start)) return 0;
	last = 0;
	while (skip--) {
		last = nr;
		nr = fat_access(sb, nr, -1);
		if (nr == FAT_ENT_EOF)
			return 0;
		else if (nr == FAT_ENT_FREE) {
			fat_fs_panic(sb, "%s: invalid cluster chain (ino %lu)",
				     __FUNCTION__, inode->i_ino);
			return -EIO;
		} else if (nr < 0)
			return nr;
	}
	if (last) {
		fat_access(sb, last, FAT_ENT_EOF);
		fat_cache_inval_inode(inode);
	} else {
		fat_cache_inval_inode(inode);
		MSDOS_I(inode)->i_start = 0;
		MSDOS_I(inode)->i_logstart = 0;
		mark_inode_dirty(inode);
	}

	lock_fat(sb);
	while (nr != FAT_ENT_EOF) {
		nr = fat_access(sb, nr, FAT_ENT_FREE);
		if (nr < 0)
			goto error;
		else if (nr == FAT_ENT_FREE) {
			fat_fs_panic(sb, "%s: deleting beyond EOF (ino %lu)",
				     __FUNCTION__, inode->i_ino);
			nr = -EIO;
			goto error;
		}
		if (MSDOS_SB(sb)->free_clusters != -1)
			MSDOS_SB(sb)->free_clusters++;
		inode->i_blocks -= (1 << MSDOS_SB(sb)->cluster_bits) >> 9;
	}
	fat_clusters_flush(sb);
	nr = 0;
error:
	unlock_fat(sb);

	return nr;
}
