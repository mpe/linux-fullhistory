/*
 * linux/fs/inode.c: Keeping track of inodes.
 *
 * Copyright (C) 1991, 1992  Linus Torvalds
 * Copyright (C) 1997 David S. Miller
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/string.h>

int nr_inodes = 0, nr_free_inodes = 0;
int max_inodes = NR_INODE;

#define INODE_HASHSZ	1024

static struct inode *inode_hash[INODE_HASHSZ];

/* All the details of hashing and lookup. */
#define hashfn(dev, i) ((HASHDEV(dev) + ((i) ^ ((i) >> 10))) & (INODE_HASHSZ - 1))

__inline__ void insert_inode_hash(struct inode *inode)
{
	struct inode **htable = &inode_hash[hashfn(inode->i_dev, inode->i_ino)];
	if((inode->i_hash_next = *htable) != NULL)
		(*htable)->i_hash_pprev = &inode->i_hash_next;
	*htable = inode;
	inode->i_hash_pprev = htable;
}

#define hash_inode(inode) insert_inode_hash(inode)

static inline void unhash_inode(struct inode *inode)
{
	if(inode->i_hash_pprev) {
		if(inode->i_hash_next)
			inode->i_hash_next->i_hash_pprev = inode->i_hash_pprev;
		*(inode->i_hash_pprev) = inode->i_hash_next;
		inode->i_hash_pprev = NULL;
	}
}

static inline struct inode *find_inode(unsigned int hashent,
				       kdev_t dev, unsigned long ino)
{
	struct inode *inode;

	for(inode = inode_hash[hashent]; inode; inode = inode->i_hash_next)
		if(inode->i_dev == dev && inode->i_ino == ino)
			break;
	return inode;
}

/* Free list queue and management. */
static struct free_inode_queue {
	struct inode *head;
	struct inode **last;
} free_inodes = { NULL, &free_inodes.head };

static inline void put_inode_head(struct inode *inode)
{
	if((inode->i_next = free_inodes.head) != NULL)
		free_inodes.head->i_pprev = &inode->i_next;
	else
		free_inodes.last = &inode->i_next;
	free_inodes.head = inode;
	inode->i_pprev = &free_inodes.head;
	nr_free_inodes++;
}

static inline void put_inode_last(struct inode *inode)
{
	inode->i_next = NULL;
	inode->i_pprev = free_inodes.last;
	*free_inodes.last = inode;
	free_inodes.last = &inode->i_next;
	nr_free_inodes++;
}

static inline void remove_free_inode(struct inode *inode)
{
	if(inode->i_pprev) {
		if(inode->i_next)
			inode->i_next->i_pprev = inode->i_pprev;
		else
			free_inodes.last = inode->i_pprev;
		*inode->i_pprev = inode->i_next;
		inode->i_pprev = NULL;
		nr_free_inodes--;
	}
}

/* This is the in-use queue, if i_count > 0 (as far as we can tell)
 * the sucker is here.
 */
static struct inode *inuse_list = NULL;

static inline void put_inuse(struct inode *inode)
{
	if((inode->i_next = inuse_list) != NULL)
		inuse_list->i_pprev = &inode->i_next;
	inuse_list = inode;
	inode->i_pprev = &inuse_list;
}

static inline void remove_inuse(struct inode *inode)
{
	if(inode->i_pprev) {
		if(inode->i_next)
			inode->i_next->i_pprev = inode->i_pprev;
		*inode->i_pprev = inode->i_next;
		inode->i_pprev = NULL;
	}
}

/* Locking and unlocking inodes, plus waiting for locks to clear. */
static void __wait_on_inode(struct inode *);

static inline void wait_on_inode(struct inode *inode)
{
	if(inode->i_lock)
		__wait_on_inode(inode);
}

static inline void lock_inode(struct inode *inode)
{
	if(inode->i_lock)
		__wait_on_inode(inode);
	inode->i_lock = 1;
}

static inline void unlock_inode(struct inode *inode)
{
	inode->i_lock = 0;
	wake_up(&inode->i_wait);
}

static void __wait_on_inode(struct inode * inode)
{
	struct wait_queue wait = { current, NULL };

	add_wait_queue(&inode->i_wait, &wait);
repeat:
	current->state = TASK_UNINTERRUPTIBLE;
	if (inode->i_lock) {
		schedule();
		goto repeat;
	}
	remove_wait_queue(&inode->i_wait, &wait);
	current->state = TASK_RUNNING;
}

/* Clear an inode of all it's identity, this is exported to the world. */
void clear_inode(struct inode *inode)
{
	struct wait_queue *wait;

	/* So we don't disappear. */
	inode->i_count++;

	truncate_inode_pages(inode, 0);
	wait_on_inode(inode);
	if(IS_WRITABLE(inode) && inode->i_sb && inode->i_sb->dq_op)
		inode->i_sb->dq_op->drop(inode);

	if(--inode->i_count > 0)
		remove_inuse(inode);
	else
		remove_free_inode(inode);
	unhash_inode(inode);
	wait = inode->i_wait;
	memset(inode, 0, sizeof(*inode)); barrier();
	inode->i_wait = wait;
	put_inode_head(inode);	/* Pages zapped, put at the front. */
}

/* These check the validity of a mount/umount type operation, we essentially
 * check if there are any inodes hanging around which prevent this operation
 * from occurring.  We also clear out clean inodes referencing this device.
 */
int fs_may_mount(kdev_t dev)
{
	struct inode *inode;
	int pass = 0;

	inode = free_inodes.head;
repeat:
	while(inode) {
		struct inode *next = inode->i_next;
		if(inode->i_dev != dev)
			goto next;
		if(inode->i_count || inode->i_dirt || inode->i_lock)
			return 0;
		clear_inode(inode);
	next:
		inode = next;
	}
	if(pass == 0) {
		inode = inuse_list;
		pass = 1;
		goto repeat;
	}
	return 1; /* Tis' cool bro. */
}

int fs_may_umount(kdev_t dev, struct inode *iroot)
{
	struct inode *inode;
	int pass = 0;

	inode = free_inodes.head;
repeat:
	for(; inode; inode = inode->i_next) {
		if(inode->i_dev != dev || !inode->i_count)
			continue;
		if(inode == iroot &&
		   (inode->i_count == (inode->i_mount == inode ? 2 : 1)))
			continue;
		return 0;
	}
	if(pass == 0) {
		inode = inuse_list;
		pass = 1;
		goto repeat;
	}
	return 1; /* Tis' cool bro. */
}

/* This belongs in file_table.c, not here... */
int fs_may_remount_ro(kdev_t dev)
{
	struct file * file;

	/* Check that no files are currently opened for writing. */
	for (file = inuse_filps; file; file = file->f_next) {
		if (!file->f_inode || file->f_inode->i_dev != dev)
			continue;
		if (S_ISREG(file->f_inode->i_mode) && (file->f_mode & 2))
			return 0;
	}
	return 1; /* Tis' cool bro. */
}

/* Reading/writing inodes. */
static void write_inode(struct inode *inode)
{
	if(inode->i_dirt) {
		wait_on_inode(inode);
		if(inode->i_dirt) {
			if(inode->i_sb		&&
			   inode->i_sb->s_op	&&
			   inode->i_sb->s_op->write_inode) {
				inode->i_lock = 1;
				inode->i_sb->s_op->write_inode(inode);
				unlock_inode(inode);
			} else {
				inode->i_dirt = 0;
			}
		}
	}
}

static inline void read_inode(struct inode *inode)
{
	if(inode->i_sb		&&
	   inode->i_sb->s_op	&&
	   inode->i_sb->s_op->read_inode) {
		lock_inode(inode);
		inode->i_sb->s_op->read_inode(inode);
		unlock_inode(inode);
	}
}

int inode_change_ok(struct inode *inode, struct iattr *attr)
{
	if(!(attr->ia_valid & ATTR_FORCE)) {
		unsigned short fsuid = current->fsuid;
		uid_t iuid = inode->i_uid;
		int not_fsuser = !fsuser();

		if(((attr->ia_valid & ATTR_UID) &&
		    ((fsuid != iuid) ||
		     (attr->ia_uid != iuid)) && not_fsuser)	||

		   ((attr->ia_valid & ATTR_GID) &&
		    (!in_group_p(attr->ia_gid) &&
		     (attr->ia_gid != inode->i_gid)) && not_fsuser)	||

		   ((attr->ia_valid & (ATTR_ATIME_SET | ATTR_MTIME_SET)) &&
		    (fsuid != iuid) && not_fsuser))
			return -EPERM;

		if(attr->ia_valid & ATTR_MODE) {
			gid_t grp;
			if(fsuid != iuid && not_fsuser)
				return -EPERM;
			grp = attr->ia_valid & ATTR_GID ? attr->ia_gid : inode->i_gid;
			if(not_fsuser && !in_group_p(grp))
				attr->ia_mode &= ~S_ISGID;
		}
	}
	return 0;
}

void inode_setattr(struct inode *inode, struct iattr *attr)
{
	if (attr->ia_valid & ATTR_UID)
		inode->i_uid = attr->ia_uid;
	if (attr->ia_valid & ATTR_GID)
		inode->i_gid = attr->ia_gid;
	if (attr->ia_valid & ATTR_SIZE)
		inode->i_size = attr->ia_size;
	if (attr->ia_valid & ATTR_ATIME)
		inode->i_atime = attr->ia_atime;
	if (attr->ia_valid & ATTR_MTIME)
		inode->i_mtime = attr->ia_mtime;
	if (attr->ia_valid & ATTR_CTIME)
		inode->i_ctime = attr->ia_ctime;
	if (attr->ia_valid & ATTR_MODE) {
		inode->i_mode = attr->ia_mode;
		if (!fsuser() && !in_group_p(inode->i_gid))
			inode->i_mode &= ~S_ISGID;
	}
	if (attr->ia_valid & ATTR_ATTR_FLAG)
		inode->i_attr_flags = attr->ia_attr_flags;
	inode->i_dirt = 1;
}

int notify_change(struct inode *inode, struct iattr *attr)
{
	attr->ia_ctime = CURRENT_TIME;
	if (attr->ia_valid & (ATTR_ATIME | ATTR_MTIME)) {
		if (!(attr->ia_valid & ATTR_ATIME_SET))
			attr->ia_atime = attr->ia_ctime;
		if (!(attr->ia_valid & ATTR_MTIME_SET))
			attr->ia_mtime = attr->ia_ctime;
	}

	if (inode->i_sb		&&
	    inode->i_sb->s_op	&&
	    inode->i_sb->s_op->notify_change) 
		return inode->i_sb->s_op->notify_change(inode, attr);

	if(inode_change_ok(inode, attr) != 0)
		return -EPERM;

	inode_setattr(inode, attr);
	return 0;
}

int bmap(struct inode *inode, int block)
{
	if(inode->i_op && inode->i_op->bmap)
		return inode->i_op->bmap(inode, block);
	return 0;
}

void invalidate_inodes(kdev_t dev)
{
	struct inode *inode;
	int pass = 0;

	inode = free_inodes.head;
repeat:
	while(inode) {
		struct inode *next = inode->i_next;
		if(inode->i_dev != dev)
			goto next;
		clear_inode(inode);
	next:
		inode = next;
	}
	if(pass == 0) {
		inode = inuse_list;
		pass = 1;
		goto repeat;
	}
}

void sync_inodes(kdev_t dev)
{
	struct inode *inode;
	int pass = 0;

	inode = free_inodes.head;
repeat:
	while(inode) {
		struct inode *next = inode->i_next;
		if(dev && inode->i_dev != dev)
			goto next;
		wait_on_inode(inode);
		write_inode(inode);
	next:
		inode = next;
	}
	if(pass == 0) {
		inode = inuse_list;
		pass = 1;
		goto repeat;
	}
}

static struct wait_queue *inode_wait, *update_wait;

void iput(struct inode *inode)
{
	if(!inode)
		return;
	wait_on_inode(inode);
	if(!inode->i_count) {
		printk("VFS: Freeing free inode, tell DaveM\n");
		return;
	}
	if(inode->i_pipe)
		wake_up_interruptible(&PIPE_WAIT(*inode));
we_slept:
	if(inode->i_count > 1) {
		inode->i_count--;
	} else {
		wake_up(&inode_wait);
		if(inode->i_pipe) {
			free_page((unsigned long)PIPE_BASE(*inode));
			PIPE_BASE(*inode) = NULL;
		}
		if(inode->i_sb		&&
		   inode->i_sb->s_op	&&
		   inode->i_sb->s_op->put_inode) {
			inode->i_sb->s_op->put_inode(inode);
			if(!inode->i_nlink)
				return;
		}
		if(inode->i_dirt) {
			write_inode(inode);
			wait_on_inode(inode);
			goto we_slept;
		}
		if(IS_WRITABLE(inode)		&&
		   inode->i_sb			&&
		   inode->i_sb->dq_op) {
			inode->i_lock = 1;
			inode->i_sb->dq_op->drop(inode);
			unlock_inode(inode);
			goto we_slept;
		}
		/* There is a serious race leading to here, watch out. */
		if(--inode->i_count == 0) {
			remove_inuse(inode);
			put_inode_last(inode);	/* Place at end of LRU free queue */
		}
	}
}

static kmem_cache_t *inode_cachep;

static void grow_inodes(void)
{
	int i = 16;

	while(i--) {
		struct inode *inode;
		
		inode = kmem_cache_alloc(inode_cachep, SLAB_KERNEL);
		if(!inode)
			return;
		memset(inode, 0, sizeof(*inode));
		put_inode_head(inode);
		nr_inodes++;
	}
}

/* We have to be really careful, it's really easy to run yourself into
 * inefficient sequences of events.  The first problem is that when you
 * steal a non-referenced inode you run the risk of zaping a considerable
 * number of page cache entries, which might get refernced once again.
 * But if you are growing the inode set to quickly, you suck up ram
 * and cause other problems.
 *
 * We approach the problem in the following way, we take two things into
 * consideration.  Firstly we take a look at how much we have "committed"
 * to this inode already (i_nrpages), this accounts for the cost of getting
 * those pages back if someone should reference that inode soon.  We also
 * attempt to factor in i_blocks, which says "how much of a problem could
 * this potentially be".  It still needs some tuning though.  -DaveM
 */
#define BLOCK_FACTOR_SHIFT	5	/* It is not factored in as much. */
static struct inode *find_best_candidate_weighted(struct inode *inode)
{
	struct inode *best = NULL;

	if(inode) {
		unsigned long bestscore = 1000;
		int limit = nr_free_inodes >> 2;
		do {
			if(!(inode->i_lock | inode->i_dirt)) {
				int myscore = inode->i_nrpages;

				myscore += (inode->i_blocks >> BLOCK_FACTOR_SHIFT);
				if(myscore < bestscore) {
					bestscore = myscore;
					best = inode;
				}
			}
			inode = inode->i_next;
		} while(inode && --limit);
	}
	return best;
}

static inline struct inode *find_best_free(struct inode *inode)
{
	if(inode) {
		int limit = nr_free_inodes >> 5;
		do {
			if(!inode->i_nrpages)
				return inode;
			inode = inode->i_next;
		} while(inode && --limit);
	}
	return NULL;
}

struct inode *get_empty_inode(void)
{
	static int ino = 0;
	struct inode *inode;

repeat:
	inode = find_best_free(free_inodes.head);
	if(!inode)
		goto pressure;
got_it:
	inode->i_count++;
	truncate_inode_pages(inode, 0);
	wait_on_inode(inode);
	if(IS_WRITABLE(inode) && inode->i_sb && inode->i_sb->dq_op)
		inode->i_sb->dq_op->drop(inode);
	unhash_inode(inode);
	remove_free_inode(inode);

	memset(inode, 0, sizeof(*inode));
	inode->i_count = 1;
	inode->i_nlink = 1;
	inode->i_version = ++event;
	sema_init(&inode->i_sem, 1);
	inode->i_ino = ++ino;
	inode->i_dev = 0;
	put_inuse(inode);
	return inode;
pressure:
	if(nr_inodes < max_inodes) {
		grow_inodes();
		goto repeat;
	}
	inode = find_best_candidate_weighted(free_inodes.head);
	if(!inode) {
		printk("VFS: No free inodes, contact DaveM\n");
		sleep_on(&inode_wait);
		goto repeat;
	}
	if(inode->i_lock) {
		wait_on_inode(inode);
		goto repeat;
	} else if(inode->i_dirt) {
		write_inode(inode);
		goto repeat;
	}
	goto got_it;
}

struct inode *get_pipe_inode(void)
{
	extern struct inode_operations pipe_inode_operations;
	struct inode *inode = get_empty_inode();

	if(inode) {
		unsigned long page = __get_free_page(GFP_USER);
		if(!page) {
			iput(inode);
			inode = NULL;
		} else {
			PIPE_BASE(*inode) = (char *) page;
			inode->i_op = &pipe_inode_operations;
			inode->i_count = 2;
			PIPE_WAIT(*inode) = NULL;
			PIPE_START(*inode) = PIPE_LEN(*inode) = 0;
			PIPE_RD_OPENERS(*inode) = PIPE_WR_OPENERS(*inode) = 0;
			PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 1;
			PIPE_LOCK(*inode) = 0;
			inode->i_pipe = 1;
			inode->i_mode |= S_IFIFO | S_IRUSR | S_IWUSR;
			inode->i_uid = current->fsuid;
			inode->i_gid = current->fsgid;
			inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
			inode->i_blksize = PAGE_SIZE;
		}
	}
	return inode;
}

static int inode_updating[INODE_HASHSZ];

struct inode *__iget(struct super_block *sb, int nr, int crossmntp)
{
	unsigned int hashent = hashfn(sb->s_dev, nr);
	struct inode *inode, *empty = NULL;

we_slept:
	if((inode = find_inode(hashent, sb->s_dev, nr)) == NULL) {
		if(empty == NULL) {
			inode_updating[hashent]++;
			empty = get_empty_inode();
			if(!--inode_updating[hashent])
				wake_up(&update_wait);
			goto we_slept;
		}
		inode = empty;
		inode->i_sb = sb;
		inode->i_dev = sb->s_dev;
		inode->i_ino = nr;
		inode->i_flags = sb->s_flags;
		hash_inode(inode);
		read_inode(inode);
	} else {
		if(!inode->i_count++) {
			remove_free_inode(inode);
			put_inuse(inode);
		}
		wait_on_inode(inode);
		if(crossmntp && inode->i_mount) {
			struct inode *mp = inode->i_mount;
			mp->i_count++;
			iput(inode);
			wait_on_inode(inode = mp);
		}
		if(empty)
			iput(empty);
	}
	while(inode_updating[hashent])
		sleep_on(&update_wait);
	return inode;
}

void inode_init(void)
{
	int i;

	inode_cachep = kmem_cache_create("inode", sizeof(struct inode),
					 sizeof(unsigned long) * 4,
					 SLAB_HWCACHE_ALIGN, NULL, NULL);
	if(!inode_cachep)
		panic("Cannot create inode SLAB cache\n");

	for(i = 0; i < INODE_HASHSZ; i++)
		inode_hash[i] = NULL;
}
