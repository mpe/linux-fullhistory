/*
 *  cache.c
 *
 * Copyright (C) 1997 by Bill Hawes
 *
 * Routines to support directory cacheing using the page cache.
 * Right now this only works for smbfs, but will be generalized
 * for use with other filesystems.
 */

#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/dirent.h>
#include <linux/smb_fs.h>

#include <asm/page.h>

#define SMBFS_PARANOIA 1
/* #define SMBFS_DEBUG_VERBOSE 1 */

static inline struct inode * 
get_cache_inode(struct cache_head *cachep)
{
	return (mem_map + MAP_NR((unsigned long) cachep))->inode;
}

/*
 * Get a pointer to the cache_head structure,
 * mapped as the page at offset 0. The page is
 * kept locked while we're using the cache.
 */
struct cache_head *
smb_get_dircache(struct dentry * dentry)
{
	struct inode * inode = dentry->d_inode;
	struct cache_head * cachep;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_get_dircache: finding cache for %s/%s\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
	cachep = (struct cache_head *) get_cached_page(inode, 0, 1);
	if (!cachep)
		goto out;
	if (cachep->valid)
	{
		struct cache_index * index = cachep->index;
		struct cache_block * block;
		unsigned long offset;
		int i;

		cachep->valid = 0;
		/*
		 * Here we only want to find existing cache blocks,
		 * not add new ones.
		 */
		for (i = 0; i < cachep->pages; i++, index++) {
#ifdef SMBFS_PARANOIA
if (index->block)
printk("smb_get_dircache: cache %s/%s has existing block!\n",
dentry->d_parent->d_name.name, dentry->d_name.name);
#endif
			offset = PAGE_SIZE + (i << PAGE_SHIFT);
			block = (struct cache_block *) get_cached_page(inode,
								offset, 0);
			if (!block)
				goto out;
			index->block = block;
		}
		cachep->valid = 1;
	}
out:
	return cachep;
}

/*
 * Unlock and release the data blocks.
 */
static void
smb_free_cache_blocks(struct cache_head * cachep)
{
	struct cache_index * index = cachep->index;
	int i;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_free_cache_blocks: freeing %d blocks\n", cachep->pages);
#endif
	for (i = 0; i < cachep->pages; i++, index++)
	{
		if (index->block)
		{
			put_cached_page((unsigned long) index->block);
			index->block = NULL;
		}
	}
}

/*
 * Unlocks and releases the dircache.
 */
void
smb_free_dircache(struct cache_head * cachep)
{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_free_dircache: freeing cache\n");
#endif
	smb_free_cache_blocks(cachep);
	put_cached_page((unsigned long) cachep);
}

/*
 * Initializes the dircache. We release any existing data blocks,
 * and then clear the cache_head structure.
 */
void
smb_init_dircache(struct cache_head * cachep)
{
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_init_dircache: initializing cache, %d blocks\n", cachep->pages);
#endif
	smb_free_cache_blocks(cachep);
	memset(cachep, 0, sizeof(struct cache_head));
}

/*
 * Add a new entry to the cache.  This assumes that the
 * entries are coming in order and are added to the end.
 */
void
smb_add_to_cache(struct cache_head * cachep, struct cache_dirent *entry,
			off_t fpos)
{
	struct inode * inode = get_cache_inode(cachep);
	struct cache_index * index;
	struct cache_block * block;
	unsigned long page_off;
	unsigned int nent, offset, len = entry->len;
	unsigned int needed = len + sizeof(struct cache_entry);

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_add_to_cache: cache inode %p, status %d, adding %s at %ld\n",
inode, cachep->status, entry->d_name, fpos);
#endif
	/*
	 * Don't do anything if we've had an error ...
	 */
	if (cachep->status)
		goto out;

	index = &cachep->index[cachep->idx];
	if (!index->block)
		goto get_block;

	/* space available? */
	if (needed < index->space)
	{
	add_entry:
		nent = index->num_entries;
		index->num_entries++;
		index->space -= needed;
		offset = index->space + 
			 index->num_entries * sizeof(struct cache_entry);
		block = index->block;
		memcpy(&block->cb_data.names[offset], entry->name, len);
		block->cb_data.table[nent].namelen = len;
		block->cb_data.table[nent].offset = offset;
		block->cb_data.table[nent].ino = entry->ino;
		cachep->entries++;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_add_to_cache: added entry %s, len=%d, pos=%ld, entries=%d\n",
entry->d_name, len, fpos, cachep->entries);
#endif
		return;
	}
	/*
	 * This block is full ... advance the index.
	 */
	cachep->idx++;
	if (cachep->idx > NINDEX) /* not likely */
		goto out_full;
	index++;
#ifdef SMBFS_PARANOIA
if (index->block)
printk("smb_add_to_cache: new index already has block!\n");
#endif

	/*
	 * Get the next cache block
	 */
get_block:
	cachep->pages++;
	page_off = PAGE_SIZE + (cachep->idx << PAGE_SHIFT);
	block = (struct cache_block *) get_cached_page(inode, page_off, 1);
	if (block)
	{
		index->block = block;
		index->space = PAGE_SIZE;
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_add_to_cache: inode=%p, pages=%d, block at %ld\n",
inode, cachep->pages, page_off);
#endif
		goto add_entry;
	}
	/*
	 * On failure, just set the return status ...
	 */
out_full:
	cachep->status = -ENOMEM;
out:
	return;
}

int
smb_find_in_cache(struct cache_head * cachep, off_t pos, 
		struct cache_dirent *entry)
{
	struct cache_index * index = cachep->index;
	struct cache_block * block;
	unsigned int i, nent, offset = 0;
	off_t next_pos = 2;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_find_in_cache: cache %p, looking for pos=%ld\n", cachep, pos);
#endif
	for (i = 0; i < cachep->pages; i++, index++)
	{
		if (pos < next_pos)
			break;
		nent = pos - next_pos;
		next_pos += index->num_entries;
		if (pos >= next_pos)
			continue; 
		/*
		 * The entry is in this block. Note: we return
		 * then name as a reference with _no_ null byte.
		 */
		block = index->block;
		entry->ino = block->cb_data.table[nent].ino;
		entry->len = block->cb_data.table[nent].namelen;
		offset = block->cb_data.table[nent].offset;
		entry->name = &block->cb_data.names[offset];
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_find_in_cache: found %s, len=%d, pos=%ld\n",
entry->name, entry->len, pos);
#endif
		break;
	}
	return offset;
}

int
smb_refill_dircache(struct cache_head * cachep, struct dentry *dentry)
{
	struct inode * inode = dentry->d_inode;
	int result;

#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_refill_dircache: cache %s/%s, blocks=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name, cachep->pages);
#endif
	/*
	 * Fill the cache, starting at position 2.
	 */
retry:
	inode->u.smbfs_i.cache_valid |= SMB_F_CACHEVALID;
	result = smb_proc_readdir(dentry, 2, cachep);
	if (result < 0)
	{
#ifdef SMBFS_PARANOIA
printk("smb_refill_dircache: readdir failed, result=%d\n", result);
#endif
		goto out;
	}

	/*
	 * Check whether the cache was invalidated while
	 * we were doing the scan ...
	 */
	if (!(inode->u.smbfs_i.cache_valid & SMB_F_CACHEVALID))
	{
#ifdef SMBFS_PARANOIA
printk("smb_refill_dircache: cache invalidated, retrying\n");
#endif
		goto retry;
	}

	result = cachep->status;
	if (!result)
	{
		cachep->valid = 1;
	}
#ifdef SMBFS_DEBUG_VERBOSE
printk("smb_refill_cache: cache %s/%s status=%d, entries=%d\n",
dentry->d_parent->d_name.name, dentry->d_name.name,
cachep->status, cachep->entries);
#endif

out:
	return result;
}

void
smb_invalid_dir_cache(struct inode * dir)
{
	/*
	 * Get rid of any unlocked pages, and clear the
	 * 'valid' flag in case a scan is in progress.
	 */
	invalidate_inode_pages(dir);
	dir->u.smbfs_i.cache_valid &= ~SMB_F_CACHEVALID;
	dir->u.smbfs_i.oldmtime = 0;
}

