/*
 * include/linux/buffer_head.h
 *
 * Everything to do with buffer_heads.
 */

#ifndef _LINUX_BUFFER_HEAD_H
#define _LINUX_BUFFER_HEAD_H

#include <linux/types.h>
#include <linux/fs.h>
#include <linux/linkage.h>
#include <asm/atomic.h>

enum bh_state_bits {
	BH_Uptodate,	/* Contains valid data */
	BH_Dirty,	/* Is dirty */
	BH_Lock,	/* Is locked */
	BH_Req,		/* Has been submitted for I/O */

	BH_Mapped,	/* Has a disk mapping */
	BH_New,		/* Disk mapping was newly created by get_block */
	BH_Async_Read,	/* Is under end_buffer_async_read I/O */
	BH_Async_Write,	/* Is under end_buffer_async_write I/O */

	BH_Boundary,	/* Block is followed by a discontiguity */
	BH_PrivateStart,/* not a state bit, but the first bit available
			 * for private allocation by other entities
			 */
};

#define MAX_BUF_PER_PAGE (PAGE_CACHE_SIZE / 512)

struct page;
struct kiobuf;
struct buffer_head;
struct address_space;
typedef void (bh_end_io_t)(struct buffer_head *bh, int uptodate);

/*
 * Keep related fields in common cachelines.  The most commonly accessed
 * field (b_state) goes at the start so the compiler does not generate
 * indexed addressing for it.
 */
struct buffer_head {
	/* First cache line: */
	unsigned long b_state;		/* buffer state bitmap (see above) */
	atomic_t b_count;		/* users using this block */
	struct buffer_head *b_this_page;/* circular list of page's buffers */
	struct page *b_page;		/* the page this bh is mapped to */

	sector_t b_blocknr;		/* block number */
	u32 b_size;			/* block size */
	char *b_data;			/* pointer to data block */

	struct block_device *b_bdev;
	bh_end_io_t *b_end_io;		/* I/O completion */
 	void *b_private;		/* reserved for b_end_io */
	struct list_head b_assoc_buffers; /* associated with another mapping */
};


/*
 * macro tricks to expand the set_buffer_foo(), clear_buffer_foo()
 * and buffer_foo() functions.
 */
#define BUFFER_FNS(bit, name)						\
static inline void set_buffer_##name(struct buffer_head *bh)		\
{									\
	set_bit(BH_##bit, &(bh)->b_state);				\
}									\
static inline void clear_buffer_##name(struct buffer_head *bh)		\
{									\
	clear_bit(BH_##bit, &(bh)->b_state);				\
}									\
static inline int buffer_##name(struct buffer_head *bh)			\
{									\
	return test_bit(BH_##bit, &(bh)->b_state);			\
}

/*
 * test_set_buffer_foo() and test_clear_buffer_foo()
 */
#define TAS_BUFFER_FNS(bit, name)					\
static inline int test_set_buffer_##name(struct buffer_head *bh)	\
{									\
	return test_and_set_bit(BH_##bit, &(bh)->b_state);		\
}									\
static inline int test_clear_buffer_##name(struct buffer_head *bh)	\
{									\
	return test_and_clear_bit(BH_##bit, &(bh)->b_state);		\
}									\

/*
 * Emit the buffer bitops functions.   Note that there are also functions
 * of the form "mark_buffer_foo()".  These are higher-level functions which
 * do something in addition to setting a b_state bit.
 */
BUFFER_FNS(Uptodate, uptodate)
BUFFER_FNS(Dirty, dirty)
TAS_BUFFER_FNS(Dirty, dirty)
BUFFER_FNS(Lock, locked)
TAS_BUFFER_FNS(Lock, locked)
BUFFER_FNS(Req, req)
BUFFER_FNS(Mapped, mapped)
BUFFER_FNS(New, new)
BUFFER_FNS(Async_Read, async_read)
BUFFER_FNS(Async_Write, async_write)
BUFFER_FNS(Boundary, boundary)

#define bh_offset(bh)		((unsigned long)(bh)->b_data & ~PAGE_MASK)
#define touch_buffer(bh)	mark_page_accessed(bh->b_page)

/* If we *know* page->private refers to buffer_heads */
#define page_buffers(page)					\
	({							\
		if (!PagePrivate(page))				\
			BUG();					\
		((struct buffer_head *)(page)->private);	\
	})
#define page_has_buffers(page)	PagePrivate(page)
#define set_page_buffers(page, buffers)				\
	do {							\
		SetPagePrivate(page);				\
		page->private = (unsigned long)buffers;		\
	} while (0)
#define clear_page_buffers(page)				\
	do {							\
		ClearPagePrivate(page);				\
		page->private = 0;				\
	} while (0)

#define invalidate_buffers(dev)	__invalidate_buffers((dev), 0)
#define destroy_buffers(dev)	__invalidate_buffers((dev), 1)


/*
 * Declarations
 */

void FASTCALL(mark_buffer_dirty(struct buffer_head *bh));
void init_buffer(struct buffer_head *, bh_end_io_t *, void *);
void set_bh_page(struct buffer_head *bh,
		struct page *page, unsigned long offset);
int try_to_free_buffers(struct page *);
void create_empty_buffers(struct page *, unsigned long,
			unsigned long b_state);
void end_buffer_io_sync(struct buffer_head *bh, int uptodate);

/* Things to do with buffers at mapping->private_list */
void buffer_insert_list(spinlock_t *lock,
			struct buffer_head *, struct list_head *);
void mark_buffer_dirty_inode(struct buffer_head *bh, struct inode *inode);
int write_mapping_buffers(struct address_space *mapping);
int inode_has_buffers(struct inode *);
void invalidate_inode_buffers(struct inode *);
int fsync_buffers_list(spinlock_t *lock, struct list_head *);
int sync_mapping_buffers(struct address_space *mapping);
void unmap_underlying_metadata(struct block_device *bdev, sector_t block);

void mark_buffer_async_read(struct buffer_head *bh);
void mark_buffer_async_write(struct buffer_head *bh);
void invalidate_bdev(struct block_device *, int);
void __invalidate_buffers(kdev_t dev, int);
int sync_blockdev(struct block_device *bdev);
void __wait_on_buffer(struct buffer_head *);
void sleep_on_buffer(struct buffer_head *bh);
void wake_up_buffer(struct buffer_head *bh);
int fsync_dev(kdev_t);
int fsync_bdev(struct block_device *);
int fsync_super(struct super_block *);
int fsync_no_super(struct block_device *);
struct buffer_head *__get_hash_table(struct block_device *, sector_t, int);
struct buffer_head * __getblk(struct block_device *, sector_t, int);
void __brelse(struct buffer_head *);
void __bforget(struct buffer_head *);
struct buffer_head * __bread(struct block_device *, int, int);
void wakeup_bdflush(void);
struct buffer_head *alloc_buffer_head(void);
void free_buffer_head(struct buffer_head * bh);
void FASTCALL(unlock_buffer(struct buffer_head *bh));

/*
 * Generic address_space_operations implementations for buffer_head-backed
 * address_spaces.
 */
int try_to_release_page(struct page * page, int gfp_mask);
int block_invalidatepage(struct page *page, unsigned long offset);
int block_write_full_page(struct page*, get_block_t*);
int block_read_full_page(struct page*, get_block_t*);
int block_prepare_write(struct page*, unsigned, unsigned, get_block_t*);
int cont_prepare_write(struct page*, unsigned, unsigned, get_block_t*,
				unsigned long *);
int generic_cont_expand(struct inode *inode, loff_t size) ;
int block_commit_write(struct page *page, unsigned from, unsigned to);
int block_sync_page(struct page *);
sector_t generic_block_bmap(struct address_space *, sector_t, get_block_t *);
int generic_commit_write(struct file *, struct page *, unsigned, unsigned);
int block_truncate_page(struct address_space *, loff_t, get_block_t *);
int generic_direct_IO(int, struct inode *, struct kiobuf *,
			unsigned long, int, get_block_t *);
int file_fsync(struct file *, struct dentry *, int);

#define OSYNC_METADATA	(1<<0)
#define OSYNC_DATA	(1<<1)
#define OSYNC_INODE	(1<<2)
int generic_osync_inode(struct inode *, int);


/*
 * inline definitions
 */

static inline void get_bh(struct buffer_head * bh)
{
        atomic_inc(&(bh)->b_count);
}

static inline void put_bh(struct buffer_head *bh)
{
        smp_mb__before_atomic_dec();
        atomic_dec(&bh->b_count);
}

/*
 * If an error happens during the make_request, this function
 * has to be recalled. It marks the buffer as clean and not
 * uptodate, and it notifys the upper layer about the end
 * of the I/O.
 */
static inline void buffer_IO_error(struct buffer_head * bh)
{
	clear_buffer_dirty(bh);

	/*
	 * b_end_io has to clear the BH_Uptodate bitflag in the read error
	 * case, however buffer contents are not necessarily bad if a
	 * write fails
	 */
	bh->b_end_io(bh, buffer_uptodate(bh));
}


static inline void brelse(struct buffer_head *buf)
{
	if (buf)
		__brelse(buf);
}

static inline void bforget(struct buffer_head *buf)
{
	if (buf)
		__bforget(buf);
}

static inline struct buffer_head * sb_bread(struct super_block *sb, int block)
{
	return __bread(sb->s_bdev, block, sb->s_blocksize);
}

static inline struct buffer_head * sb_getblk(struct super_block *sb, int block)
{
	return __getblk(sb->s_bdev, block, sb->s_blocksize);
}

static inline struct buffer_head *
sb_get_hash_table(struct super_block *sb, int block)
{
	return __get_hash_table(sb->s_bdev, block, sb->s_blocksize);
}

static inline void
map_bh(struct buffer_head *bh, struct super_block *sb, int block)
{
	set_buffer_mapped(bh);
	bh->b_bdev = sb->s_bdev;
	bh->b_blocknr = block;
}

static inline void wait_on_buffer(struct buffer_head * bh)
{
	if (buffer_locked(bh))
		__wait_on_buffer(bh);
}

static inline void lock_buffer(struct buffer_head * bh)
{
	while (test_set_buffer_locked(bh))
		__wait_on_buffer(bh);
}

/*
 * Debug
 */

void __buffer_error(char *file, int line);
#define buffer_error() __buffer_error(__FILE__, __LINE__)

#endif /* _LINUX_BUFFER_HEAD_H */
