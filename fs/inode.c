/*
 * fs/inode.c
 *
 * Complete reimplementation
 * (C) 1997 Thomas Schoebel-Theuer
 */

/* Everything here is intended to be MP-safe. However, other parts
 * of the kernel are not yet MP-safe, in particular the inode->i_count++
 * that are spread over everywhere. These should be replaced by
 * iinc() as soon as possible. Since I have no MP machine, I could
 * not test it.
 */
#include <linux/config.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/string.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/dlists.h>
#include <linux/dalloc.h>
#include <linux/omirr.h>

/* #define DEBUG */

#define HASH_SIZE 1024 /* must be a power of 2 */
#define NR_LEVELS 4

#define ST_AGED      1
#define ST_HASHED    2
#define ST_EMPTY     4
#define ST_TO_READ   8
#define ST_TO_WRITE 16
#define ST_TO_PUT   32
#define ST_TO_DROP  64
#define ST_IO       (ST_TO_READ|ST_TO_WRITE|ST_TO_PUT|ST_TO_DROP)
#define ST_WAITING 128
#define ST_FREEING 256
#define ST_IBASKET 512

/* The idea is to keep empty inodes in a separate list, so no search
 * is required as long as empty inodes exit.
 * All reusable inodes occurring in the hash table with i_count==0
 * are also registered in the ringlist aged_i[level], but in LRU order.
 * Used inodes with i_count>0 are kept solely in the hashtable and in
 * all_i, but in no other list.
 * The level is used for multilevel aging to avoid thrashing; each
 * time i_count decreases to 0, the inode is inserted into the next level
 * ringlist. Cache reusage is simply by taking the _last_ element from the
 * lowest-level ringlist that contains inodes.
 * In contrast to the old code, there isn't any O(n) search overhead now
 * in iget/iput (if you make HASH_SIZE large enough).
 */
static struct inode * hashtable[HASH_SIZE];/* linked with i_hash_{next,prev} */
static struct inode * all_i = NULL;        /* linked with i_{next,prev} */
static struct inode * empty_i = NULL;      /* linked with i_{next,prev} */
static struct inode * aged_i[NR_LEVELS+1]; /* linked with i_lru_{next,prev} */
static int aged_reused[NR_LEVELS+1];       /* # removals from aged_i[level] */
static int age_table[NR_LEVELS+1] = { /* You may tune this. */
	1, 4, 10, 100, 1000
}; /* after which # of uses to increase to the next level */

/* This is for kernel/sysctl.c */

/* Just aligning plain ints and arrays thereof doesn't work reliably.. */
struct {
	int nr_inodes;
	int nr_free_inodes;
	int aged_count[NR_LEVELS+1];        /* # in each level */
} inodes_stat;

int max_inodes = NR_INODE;
unsigned long last_inode = 0;

void inode_init(void)
{
	memset(hashtable, 0, sizeof(hashtable));
	memset(aged_i, 0, sizeof(aged_i));
	memset(aged_reused, 0, sizeof(aged_reused));
	memset(&inodes_stat, 0, sizeof(inodes_stat));
}

/* Intended for short locks of the above global data structures.
 * Could be replaced with spinlocks completely, since there is
 * no blocking during manipulation of the static data; however the
 * lock in invalidate_inodes() may last relatively long.
 */
#ifdef __SMP__
struct semaphore vfs_sem = MUTEX;
#endif

DEF_INSERT(all,struct inode,i_next,i_prev)
DEF_REMOVE(all,struct inode,i_next,i_prev)
	
DEF_INSERT(lru,struct inode,i_lru_next,i_lru_prev)
DEF_REMOVE(lru,struct inode,i_lru_next,i_lru_prev)

DEF_INSERT(hash,struct inode,i_hash_next,i_hash_prev)
DEF_REMOVE(hash,struct inode,i_hash_next,i_hash_prev)

DEF_INSERT(ibasket,struct inode,i_basket_next,i_basket_prev)
DEF_REMOVE(ibasket,struct inode,i_basket_next,i_basket_prev)

#ifdef DEBUG
extern void printpath(struct dentry * entry);
struct inode * xtst[15000];
int xcnt = 0;

void xcheck(char * txt, struct inode * p)
{
	int i;
	for(i=xcnt-1; i>=0; i--)
		if(xtst[i] == p)
			return;
	printk("Bogus inode %p in %s\n", p, txt);
}
#else
#define xcheck(t,p) /*nothing*/
#endif

static inline struct inode * grow_inodes(void)
{
	struct inode * res;
	struct inode * inode = res = (struct inode*)__get_free_page(GFP_KERNEL);
	int size = PAGE_SIZE;
	if(!inode)
		return NULL;
	
	size -= sizeof(struct inode);
	inode++;
	inodes_stat.nr_inodes++;
#ifdef DEBUG
xtst[xcnt++]=res;
#endif
	while(size >= sizeof(struct inode)) {
#ifdef DEBUG
xtst[xcnt++]=inode;
#endif
		inodes_stat.nr_inodes++;
		inodes_stat.nr_free_inodes++;
		insert_all(&empty_i, inode);
		inode->i_status = ST_EMPTY;
		inode++;
		size -= sizeof(struct inode);
	}
	return res;
}

static inline int hash(dev_t i_dev, unsigned long i_ino)
{
	return ((int)i_ino ^ ((int)i_dev << 6)) & (HASH_SIZE-1);
}

static inline blocking void wait_io(struct inode * inode, unsigned short flags)
{
	while(inode->i_status & flags) {
		struct wait_queue wait = {current, NULL};
		inode->i_status |= ST_WAITING;
		vfs_unlock();
		add_wait_queue(&inode->i_wait, &wait);
		sleep_on(&inode->i_wait);
		remove_wait_queue(&inode->i_wait, &wait);
		vfs_lock();
	}
}

static inline blocking void set_io(struct inode * inode,
				   unsigned short waitflags,
				   unsigned short setflags)
{
	wait_io(inode, waitflags);
	inode->i_status |= setflags;
	vfs_unlock();
}

static inline blocking int release_io(struct inode * inode, unsigned short flags)
{
	int res = 0; 
	vfs_lock();
	inode->i_status &= ~flags;
	if(inode->i_status & ST_WAITING) {
		inode->i_status &= ~ST_WAITING;
		vfs_unlock();
		wake_up(&inode->i_wait);
		res = 1;
	}
	return res;
}

static inline blocking void _io(void (*op)(struct inode*), struct inode * inode,
				unsigned short waitflags, unsigned short setflags)
{
	/* Do nothing if the same op is already in progress. */
	if(op && !(inode->i_status & setflags)) {
		set_io(inode, waitflags, setflags);
		op(inode);
		if(release_io(inode, setflags)) {
			/* Somebody grabbed my inode from under me. */
#ifdef DEBUG
			printk("_io grab!\n");
#endif
                        vfs_lock();
		}
	}
}

blocking int _free_ibasket(struct super_block * sb)
{
	if(sb->s_ibasket) {
		struct inode * delinquish = sb->s_ibasket->i_basket_prev;
#if 0
printpath(delinquish->i_dentry);
printk(" delinquish\n");
#endif
		_clear_inode(delinquish, 0, 1);
		return 1;
	}
	return 0;
}

static /*inline*/ void _put_ibasket(struct inode * inode)
{
	struct super_block * sb = inode->i_sb;
	if(!(inode->i_status & ST_IBASKET)) {
		inode->i_status |= ST_IBASKET;
		insert_ibasket(&sb->s_ibasket, inode);
		sb->s_ibasket_count++;
		if(sb->s_ibasket_count > sb->s_ibasket_max)
			(void)_free_ibasket(sb);
	}
}

blocking void _clear_inode(struct inode * inode, int external, int verbose)
{
xcheck("_clear_inode",inode);
	if(inode->i_status & ST_IBASKET) {
		struct super_block * sb = inode->i_sb;
		remove_ibasket(&sb->s_ibasket, inode);
		sb->s_ibasket_count--;
		inode->i_status &= ~ST_IBASKET;
#if 0
printpath(inode->i_dentry);
printk(" put_inode\n");
#endif
		_io(sb->s_op->put_inode, inode, ST_TO_PUT|ST_TO_WRITE, ST_TO_PUT);
		if(inode->i_status & ST_EMPTY)
			return;
	}
	if(inode->i_status & ST_HASHED)
		remove_hash(&hashtable[hash(inode->i_dev, inode->i_ino)], inode);
	if(inode->i_status & ST_AGED) {
		/* "cannot happen" when called from an fs because at least
		 * the caller must use it. Can happen when called from
		 * invalidate_inodes(). */
		if(verbose)
			printk("VFS: clearing aged inode\n");
		if(atomic_read(&inode->i_count))
			printk("VFS: aged inode is in use\n");
		remove_lru(&aged_i[inode->i_level], inode);
		inodes_stat.aged_count[inode->i_level]--;
	}
	if(!external && inode->i_status & ST_IO) {
		printk("VFS: clearing inode during IO operation\n");
	}
	if(!(inode->i_status & ST_EMPTY)) {
		remove_all(&all_i, inode);
		inode->i_status = ST_EMPTY;
		while(inode->i_dentry) {
			d_del(inode->i_dentry, D_NO_CLEAR_INODE);
		}
		if(inode->i_pages) {
			vfs_unlock(); /* may block, can that be revised? */
			truncate_inode_pages(inode, 0);
			vfs_lock();
		}
		insert_all(&empty_i, inode);
		inodes_stat.nr_free_inodes++;
	} else if(external)
		printk("VFS: empty inode is unnecessarily cleared multiple "
		       "times by an fs\n");
        else
		printk("VFS: clearing empty inode\n");
	inode->i_status = ST_EMPTY;
	/* The inode is not really cleared any more here, but only once
	 * when taken from empty_i. This saves instructions and processor
	 * cache pollution.
	 */
}

void insert_inode_hash(struct inode * inode)
{
xcheck("insert_inode_hash",inode);
	vfs_lock();
	if(!(inode->i_status & ST_HASHED)) {
		insert_hash(&hashtable[hash(inode->i_dev, inode->i_ino)], inode);
		inode->i_status |= ST_HASHED;
	} else
		printk("VFS: trying to hash an inode again\n");
	vfs_unlock();
}

blocking struct inode * _get_empty_inode(void)
{
	struct inode * inode;
	int retry = 0;

retry:
	inode = empty_i;
	if(inode) {
		remove_all(&empty_i, inode);
		inodes_stat.nr_free_inodes--;
	} else if(inodes_stat.nr_inodes < max_inodes || retry > 2) {
		inode = grow_inodes();
	}
	if(!inode) {
		int level;
		int usable = 0;
		for(level = 0; level <= NR_LEVELS; level++)
			if(aged_i[level]) {
				inode = aged_i[level]->i_lru_prev;
				/* Here is the picking strategy, tune this */
				if(aged_reused[level] < (usable++ ?
							 inodes_stat.aged_count[level] :
							 2))
					break;
				aged_reused[level] = 0;
			}
		if(inode) {
			if(!(inode->i_status & ST_AGED))
				printk("VFS: inode aging inconsistency\n");
			if(atomic_read(&inode->i_count) + inode->i_ddir_count)
				printk("VFS: i_count of aged inode is not zero\n");
			if(inode->i_dirt)
				printk("VFS: Hey, somebody made my aged inode dirty\n");
			_clear_inode(inode, 0, 0);
			goto retry;
		}
	}
	if(!inode) {
		vfs_unlock();
		schedule();
		if(retry > 10)
			panic("VFS: cannot repair inode shortage");
		if(retry > 2)
			printk("VFS: no free inodes\n");
		retry++;
		vfs_lock();
		goto retry;
	}
xcheck("get_empty_inode",inode);
	memset(inode, 0, sizeof(struct inode));
	atomic_set(&inode->i_count, 1);
	inode->i_nlink = 1;
	sema_init(&inode->i_sem, 1);
	inode->i_ino = ++last_inode;
	inode->i_version = ++event;
	insert_all(&all_i, inode);
	return inode;
}

static inline blocking struct inode * _get_empty_inode_hashed(dev_t i_dev,
							      unsigned long i_ino)
{
	struct inode ** base = &hashtable[hash(i_dev, i_ino)];
	struct inode * inode = *base;
	if(inode) do {
		if(inode->i_ino == i_ino && inode->i_dev == i_dev) {
			atomic_inc(&inode->i_count);
			printk("VFS: inode %lx is already in use\n", i_ino);
			return inode;
		}
		inode = inode->i_hash_next;
	} while(inode != *base);
	inode = _get_empty_inode();
	inode->i_dev = i_dev;
	inode->i_ino = i_ino;
	insert_hash(base, inode);
	inode->i_status |= ST_HASHED;
	return inode;
}

blocking struct inode * get_empty_inode_hashed(dev_t i_dev, unsigned long i_ino)
{
	struct inode * inode;

	vfs_lock();
	inode = _get_empty_inode_hashed(i_dev, i_ino);
	vfs_unlock();
	return inode;
}

void _get_inode(struct inode * inode)
{
	if(inode->i_status & ST_IBASKET) {
		inode->i_status &= ~ST_IBASKET;
		remove_ibasket(&inode->i_sb->s_ibasket, inode);
		inode->i_sb->s_ibasket_count--;
	}
	if(inode->i_status & ST_AGED) {
		inode->i_status &= ~ST_AGED;
		remove_lru(&aged_i[inode->i_level], inode);
		inodes_stat.aged_count[inode->i_level]--;
		aged_reused[inode->i_level]++;
		if(S_ISDIR(inode->i_mode))
			/* make dirs less thrashable */
			inode->i_level = NR_LEVELS-1;
		else if(inode->i_nlink > 1)
			/* keep hardlinks totally separate */
			inode->i_level = NR_LEVELS;
		else if(++inode->i_reuse_count >= age_table[inode->i_level]
			&& inode->i_level < NR_LEVELS-1)
			inode->i_level++;
		if(atomic_read(&inode->i_count) != 1)
			printk("VFS: inode count was not zero\n");
	} else if(inode->i_status & ST_EMPTY)
		printk("VFS: invalid reuse of empty inode\n");
}

blocking struct inode * __iget(struct super_block * sb,
			       unsigned long i_ino,
			       int crossmntp)
{
	struct inode ** base;
	struct inode * inode;
	dev_t i_dev;
	
	if(!sb)
		panic("VFS: iget with sb == NULL");
	i_dev = sb->s_dev;
	if(!i_dev)
		panic("VFS: sb->s_dev is NULL\n");
	base = &hashtable[hash(i_dev, i_ino)];
	vfs_lock();
	inode = *base;
	if(inode) do {
		if(inode->i_ino == i_ino && inode->i_dev == i_dev) {
			atomic_inc(&inode->i_count);
			_get_inode(inode);

			 /* Allow concurrent writes/puts. This is in particular
			  * useful e.g. when syncing large chunks.
			  * I hope the i_dirty flag is everywhere set as soon
			  * as _any_ modifcation is made and _before_
			  * giving up control, so no harm should occur if data
			  * is modified during writes, because it will be
			  * rewritten again (does a short inconsistency on the
			  * disk harm?)
			  */
			wait_io(inode, ST_TO_READ);
			vfs_unlock();
			goto done;
		}
		inode = inode->i_hash_next;
	} while(inode != *base);
	inode = _get_empty_inode_hashed(i_dev, i_ino);
	inode->i_sb = sb;
	inode->i_flags = sb->s_flags;
	if(sb->s_op && sb->s_op->read_inode) {
		set_io(inode, 0, ST_TO_READ); /* do not wait at all */
		sb->s_op->read_inode(inode);
		if(release_io(inode, ST_TO_READ))
			goto done;
	}
	vfs_unlock();
done:
	while(crossmntp && inode->i_mount) {
		struct inode * tmp = inode->i_mount;
		iinc(tmp);
		iput(inode);
		inode = tmp;
	}
xcheck("_iget",inode);
	return inode;
}

blocking void __iput(struct inode * inode)
{
	struct super_block * sb;
xcheck("_iput",inode);
	if(atomic_read(&inode->i_count) + inode->i_ddir_count < 0)
		printk("VFS: i_count is negative\n");
	if((atomic_read(&inode->i_count) + inode->i_ddir_count) ||
	   (inode->i_status & ST_FREEING)) {
		return;
	}
	inode->i_status |= ST_FREEING;
#ifdef CONFIG_OMIRR
	if(inode->i_status & ST_MODIFIED) {
		inode->i_status &= ~ST_MODIFIED;
		omirr_printall(inode, " W %ld ", CURRENT_TIME);
	}
#endif
	if(inode->i_pipe) {
		free_page((unsigned long)PIPE_BASE(*inode));
		PIPE_BASE(*inode)= NULL;
	}
	if((sb = inode->i_sb)) {
		if(sb->s_type && (sb->s_type->fs_flags & FS_NO_DCACHE)) {
			/* See dcache.c:_d_del() for the details...  -DaveM */
			if(inode->i_dentry && !(inode->i_dentry->d_flag & D_DDELIP)) {
				while(inode->i_dentry)
					d_del(inode->i_dentry, D_NO_CLEAR_INODE);
				if(atomic_read(&inode->i_count) + inode->i_ddir_count)
					goto done;
			}
		}
		if(sb->s_op) {
			if(inode->i_nlink <= 0 && inode->i_dent_count &&
			   !(inode->i_status & (ST_EMPTY|ST_IBASKET)) &&
			   (sb->s_type->fs_flags & FS_IBASKET)) {
				_put_ibasket(inode);
				goto done;
			}
			if(!inode->i_dent_count ||
			   (sb->s_type->fs_flags & FS_NO_DCACHE)) {
				_io(sb->s_op->put_inode, inode, 
				    ST_TO_PUT|ST_TO_WRITE, ST_TO_PUT);
				if(atomic_read(&inode->i_count) + inode->i_ddir_count)
					goto done;
				if(inode->i_nlink <= 0) {
					if(!(inode->i_status & ST_EMPTY)) {
						_clear_inode(inode, 0, 1);
					}
					goto done;
				}
			}
			if(inode->i_dirt) {
				inode->i_dirt = 0;
				_io(sb->s_op->write_inode, inode,
				    ST_TO_PUT|ST_TO_WRITE, ST_TO_WRITE);
				if(atomic_read(&inode->i_count) + inode->i_ddir_count)
					goto done;
			}
		}
		if(IS_WRITABLE(inode) && sb->dq_op) {
			/* can operate in parallel to other ops ? */
			_io(sb->dq_op->drop, inode, 0, ST_TO_DROP);
			if(atomic_read(&inode->i_count) + inode->i_ddir_count)
				goto done;
		}
	}
	if(inode->i_mmap)
		printk("VFS: inode has mappings\n");
	if(inode->i_status & ST_AGED) {
		printk("VFS: reaging inode\n");
#if defined(DEBUG)
printpath(inode->i_dentry);
printk("\n");
#endif
		goto done;
	}
	if(!(inode->i_status & (ST_HASHED|ST_EMPTY))) {
		_clear_inode(inode, 0, 1);
		goto done;
	}
	if(inode->i_status & ST_EMPTY) {
		printk("VFS: aging an empty inode\n");
		goto done;
	}
	insert_lru(&aged_i[inode->i_level], inode);
	inodes_stat.aged_count[inode->i_level]++;
	inode->i_status |= ST_AGED;
done:
	inode->i_status &= ~ST_FREEING;
}

blocking void _iput(struct inode * inode)
{
	vfs_lock();
	__iput(inode);
	vfs_unlock();
}

blocking void sync_inodes(kdev_t dev)
{
	struct inode * inode;
	vfs_lock();
	inode = all_i;
	if(inode) do {
xcheck("sync_inodes",inode);
		if(inode->i_dirt && (inode->i_dev == dev || !dev)) {
			if(inode->i_sb && inode->i_sb->s_op &&
			   !(inode->i_status & ST_FREEING)) {
				inode->i_dirt = 0; 
				_io(inode->i_sb->s_op->write_inode, inode,
				    ST_IO, ST_TO_WRITE);
			}
		}
		inode = inode->i_next;
	} while(inode != all_i);
	vfs_unlock();
}

blocking int _check_inodes(kdev_t dev, int complain)
{
	struct inode * inode;
	int bad = 0;

	vfs_lock();
startover:
	inode = all_i;
	if(inode) do {
		struct inode * next;
xcheck("_check_inodes",inode);
		next = inode->i_next;
		if(inode->i_dev == dev) {
			if(inode->i_dirt || atomic_read(&inode->i_count)) {
				bad++;
			} else {
				_clear_inode(inode, 0, 0);

				/* _clear_inode() may recursively clear other
				 * inodes, probably also the next one.
				 */
				if(next->i_status & ST_EMPTY)
					goto startover;
			}
		}
		inode = next;
	} while(inode != all_i);
	vfs_unlock();
	if(complain && bad)
		printk("VFS: %d inode(s) busy on removed device `%s'\n",
		       bad, kdevname(dev));
	return (bad == 0);
}

/*inline*/ void invalidate_inodes(kdev_t dev)
{
	/* Requires two passes, because of the new dcache holding
	 * directories with i_count > 1.
	 */
	(void)_check_inodes(dev, 0);
	(void)_check_inodes(dev, 1);
}

/*inline*/ int fs_may_mount(kdev_t dev)
{
	return _check_inodes(dev, 0);
}

int fs_may_remount_ro(kdev_t dev)
{
	(void)dev;
	return 1; /* not checked any more */
}

int fs_may_umount(kdev_t dev, struct inode * mount_root)
{
	struct inode * inode;
	vfs_lock();
	inode = all_i;
	if(inode) do {
xcheck("fs_may_umount",inode);
		if(inode->i_dev == dev && atomic_read(&inode->i_count))
			if(inode != mount_root || atomic_read(&inode->i_count) > 
			   (inode->i_mount == inode ? 2 : 1)) {
				vfs_unlock();
				return 0;
			}
		inode = inode->i_next;
	} while(inode != all_i);
	vfs_unlock();
	return 1;
}

extern struct inode_operations pipe_inode_operations;

blocking struct inode * get_pipe_inode(void)
{
	struct inode * inode = get_empty_inode();

	PIPE_BASE(*inode) = (char*)__get_free_page(GFP_USER);
	if(!(PIPE_BASE(*inode))) {
		iput(inode);
		return NULL;
	}
	inode->i_blksize = PAGE_SIZE;
	inode->i_pipe = 1;
	inode->i_mode = S_IFIFO | S_IRUSR | S_IWUSR;
	atomic_inc(&inode->i_count);
	inode->i_uid = current->fsuid;
	inode->i_gid = current->fsgid;
	inode->i_atime = inode->i_mtime = inode->i_ctime = CURRENT_TIME;
	inode->i_op = &pipe_inode_operations;
	PIPE_READERS(*inode) = PIPE_WRITERS(*inode) = 1;

	/* I hope this does not introduce security problems.
	 * Please check and give me response.
	 */
	{
		char dummyname[32];
		struct qstr dummy = { dummyname, 0 };
		struct dentry * new;
		sprintf(dummyname, ".anonymous-pipe-%06lud", inode->i_ino);
		dummy.len = strlen(dummyname);
		vfs_lock();
		new = d_alloc(the_root, dummy.len, 0);
		if(new)
			d_add(new, inode, &dummy, D_BASKET);
		vfs_unlock();
	}
	return inode;
}

int bmap(struct inode * inode, int block)
{
	if (inode->i_op && inode->i_op->bmap)
		return inode->i_op->bmap(inode, block);
	return 0;
}
