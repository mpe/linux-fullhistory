/*
 *  linux/fs/inode.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 */

#include <linux/stat.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/string.h>

#include <asm/system.h>

#define NR_IHASH 512

/*
 * Be VERY careful when you access the inode hash table. There
 * are some rather scary race conditions you need to take care of:
 *  - P1 tries to open file "xx", calls "iget()" with the proper
 *    inode number, but blocks because it's not on the list.
 *  - P2 deletes file "xx", gets the inode (which P1 has just read,
 *    but P1 hasn't woken up to the fact yet)
 *  - P2 iput()'s the inode, which now has i_nlink = 0
 *  - P1 wakes up and has the inode, but now P2 has made that
 *    inode invalid (but P1 has no way of knowing that).
 *
 * The "updating" counter makes sure that when P1 blocks on the
 * iget(), P2 can't delete the inode from under it because P2
 * will wait until P1 has been able to update the inode usage
 * count so that the inode will stay in use until everybody has
 * closed it..
 */
static struct inode_hash_entry {
	struct inode * inode;
	int updating;
} hash_table[NR_IHASH];

static struct inode * first_inode;
static struct wait_queue * inode_wait = NULL;
/* Keep these next two contiguous in memory for sysctl.c */
int nr_inodes = 0, nr_free_inodes = 0;
int max_inodes = NR_INODE;

static inline int const hashfn(kdev_t dev, unsigned int i)
{
	return (HASHDEV(dev) ^ i) % NR_IHASH;
}

static inline struct inode_hash_entry * const hash(kdev_t dev, int i)
{
	return hash_table + hashfn(dev, i);
}

static inline void insert_inode_free(struct inode *inode)
{
	struct inode * prev, * next = first_inode;

	first_inode = inode;
	prev = next->i_prev;
	inode->i_next = next;
	inode->i_prev = prev;
	prev->i_next = inode;
	next->i_prev = inode;
}

static inline void remove_inode_free(struct inode *inode)
{
	if (first_inode == inode)
		first_inode = first_inode->i_next;
	if (inode->i_next)
		inode->i_next->i_prev = inode->i_prev;
	if (inode->i_prev)
		inode->i_prev->i_next = inode->i_next;
	inode->i_next = inode->i_prev = NULL;
}

void insert_inode_hash(struct inode *inode)
{
	struct inode_hash_entry *h;
	h = hash(inode->i_dev, inode->i_ino);

	inode->i_hash_next = h->inode;
	inode->i_hash_prev = NULL;
	if (inode->i_hash_next)
		inode->i_hash_next->i_hash_prev = inode;
	h->inode = inode;
}

static inline void remove_inode_hash(struct inode *inode)
{
	struct inode_hash_entry *h;
	h = hash(inode->i_dev, inode->i_ino);

	if (h->inode == inode)
		h->inode = inode->i_hash_next;
	if (inode->i_hash_next)
		inode->i_hash_next->i_hash_prev = inode->i_hash_prev;
	if (inode->i_hash_prev)
		inode->i_hash_prev->i_hash_next = inode->i_hash_next;
	inode->i_hash_prev = inode->i_hash_next = NULL;
}

static inline void put_last_free(struct inode *inode)
{
	remove_inode_free(inode);
	inode->i_prev = first_inode->i_prev;
	inode->i_prev->i_next = inode;
	inode->i_next = first_inode;
	inode->i_next->i_prev = inode;
}

int grow_inodes(void)
{
	struct inode * inode;
	int i;

	if (!(inode = (struct inode*) get_free_page(GFP_KERNEL)))
		return -ENOMEM;

	i=PAGE_SIZE / sizeof(struct inode);
	nr_inodes += i;
	nr_free_inodes += i;

	if (!first_inode)
		inode->i_next = inode->i_prev = first_inode = inode++, i--;

	for ( ; i ; i-- )
		insert_inode_free(inode++);
	return 0;
}

unsigned long inode_init(unsigned long start, unsigned long end)
{
	memset(hash_table, 0, sizeof(hash_table));
	first_inode = NULL;
	return start;
}

static void __wait_on_inode(struct inode *);

static inline void wait_on_inode(struct inode * inode)
{
	if (inode->i_lock)
		__wait_on_inode(inode);
}

static inline void lock_inode(struct inode * inode)
{
	wait_on_inode(inode);
	inode->i_lock = 1;
}

static inline void unlock_inode(struct inode * inode)
{
	inode->i_lock = 0;
	wake_up(&inode->i_wait);
}

/*
 * Note that we don't want to disturb any wait-queues when we discard
 * an inode.
 *
 * Argghh. Got bitten by a gcc problem with inlining: no way to tell
 * the compiler that the inline asm function 'memset' changes 'inode'.
 * I've been searching for the bug for days, and was getting desperate.
 * Finally looked at the assembler output... Grrr.
 *
 * The solution is the weird use of 'volatile'. Ho humm. Have to report
 * it to the gcc lists, and hope we can do this more cleanly some day..
 */
void clear_inode(struct inode * inode)
{
	struct wait_queue * wait;

	truncate_inode_pages(inode, 0);
	wait_on_inode(inode);
	if (IS_WRITABLE(inode)) {
		if (inode->i_sb && inode->i_sb->dq_op)
			inode->i_sb->dq_op->drop(inode);
	}
	remove_inode_hash(inode);
	remove_inode_free(inode);
	wait = ((volatile struct inode *) inode)->i_wait;
	if (inode->i_count)
		nr_free_inodes++;
	memset(inode,0,sizeof(*inode));
	((volatile struct inode *) inode)->i_wait = wait;
	insert_inode_free(inode);
}

int fs_may_mount(kdev_t dev)
{
	struct inode * inode, * next;
	int i;

	next = first_inode;
	for (i = nr_inodes ; i > 0 ; i--) {
		inode = next;
		next = inode->i_next;	/* clear_inode() changes the queues.. */
		if (inode->i_dev != dev)
			continue;
		if (inode->i_count || inode->i_dirt || inode->i_lock)
			return 0;
		clear_inode(inode);
	}
	return 1;
}

int fs_may_umount(kdev_t dev, struct inode * mount_root)
{
	struct inode * inode;
	int i;

	inode = first_inode;
	for (i=0 ; i < nr_inodes ; i++, inode = inode->i_next) {
		if (inode->i_dev != dev || !inode->i_count)
			continue;
		if (inode == mount_root && inode->i_count ==
		    (inode->i_mount != inode ? 1 : 2))
			continue;
		return 0;
	}
	return 1;
}

int fs_may_remount_ro(kdev_t dev)
{
	struct file * file;
	int i;

	/* Check that no files are currently opened for writing. */
	for (file = first_file, i=0; i<nr_files; i++, file=file->f_next) {
		if (!file->f_count || !file->f_inode ||
		    file->f_inode->i_dev != dev)
			continue;
		if (S_ISREG(file->f_inode->i_mode) && (file->f_mode & 2))
			return 0;
	}
	return 1;
}

static void write_inode(struct inode * inode)
{
	if (!inode->i_dirt)
		return;
	wait_on_inode(inode);
	if (!inode->i_dirt)
		return;
	if (!inode->i_sb || !inode->i_sb->s_op || !inode->i_sb->s_op->write_inode) {
		inode->i_dirt = 0;
		return;
	}
	inode->i_lock = 1;	
	inode->i_sb->s_op->write_inode(inode);
	unlock_inode(inode);
}

static inline void read_inode(struct inode * inode)
{
	lock_inode(inode);
	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->read_inode)
		inode->i_sb->s_op->read_inode(inode);
	unlock_inode(inode);
}

/* POSIX UID/GID verification for setting inode attributes */
int inode_change_ok(struct inode *inode, struct iattr *attr)
{
	/*
	 *	If force is set do it anyway.
	 */
	 
	if (attr->ia_valid & ATTR_FORCE)
		return 0;

	/* Make sure a caller can chown */
	if ((attr->ia_valid & ATTR_UID) &&
	    (current->fsuid != inode->i_uid ||
	     attr->ia_uid != inode->i_uid) && !fsuser())
		return -EPERM;

	/* Make sure caller can chgrp */
	if ((attr->ia_valid & ATTR_GID) &&
	    (!in_group_p(attr->ia_gid) && attr->ia_gid != inode->i_gid) &&
	    !fsuser())
		return -EPERM;

	/* Make sure a caller can chmod */
	if (attr->ia_valid & ATTR_MODE) {
		if ((current->fsuid != inode->i_uid) && !fsuser())
			return -EPERM;
		/* Also check the setgid bit! */
		if (!fsuser() && !in_group_p((attr->ia_valid & ATTR_GID) ? attr->ia_gid :
					     inode->i_gid))
			attr->ia_mode &= ~S_ISGID;
	}

	/* Check for setting the inode time */
	if ((attr->ia_valid & ATTR_ATIME_SET) &&
	    ((current->fsuid != inode->i_uid) && !fsuser()))
		return -EPERM;
	if ((attr->ia_valid & ATTR_MTIME_SET) &&
	    ((current->fsuid != inode->i_uid) && !fsuser()))
		return -EPERM;
	return 0;
}

/*
 * Set the appropriate attributes from an attribute structure into
 * the inode structure.
 */
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
	inode->i_dirt = 1;
}

/*
 * notify_change is called for inode-changing operations such as
 * chown, chmod, utime, and truncate.  It is guaranteed (unlike
 * write_inode) to be called from the context of the user requesting
 * the change.
 */

int notify_change(struct inode * inode, struct iattr *attr)
{
	int retval;

	attr->ia_ctime = CURRENT_TIME;
	if (attr->ia_valid & (ATTR_ATIME | ATTR_MTIME)) {
		if (!(attr->ia_valid & ATTR_ATIME_SET))
			attr->ia_atime = attr->ia_ctime;
		if (!(attr->ia_valid & ATTR_MTIME_SET))
			attr->ia_mtime = attr->ia_ctime;
	}

	if (inode->i_sb && inode->i_sb->s_op  &&
	    inode->i_sb->s_op->notify_change) 
		return inode->i_sb->s_op->notify_change(inode, attr);

	if ((retval = inode_change_ok(inode, attr)) != 0)
		return retval;

	inode_setattr(inode, attr);
	return 0;
}

/*
 * bmap is needed for demand-loading and paging: if this function
 * doesn't exist for a filesystem, then those things are impossible:
 * executables cannot be run from the filesystem etc...
 *
 * This isn't as bad as it sounds: the read-routines might still work,
 * so the filesystem would be otherwise ok (for example, you might have
 * a DOS filesystem, which doesn't lend itself to bmap very well, but
 * you could still transfer files to/from the filesystem)
 */
int bmap(struct inode * inode, int block)
{
	if (inode->i_op && inode->i_op->bmap)
		return inode->i_op->bmap(inode,block);
	return 0;
}

void invalidate_inodes(kdev_t dev)
{
	struct inode * inode, * next;
	int i;

	next = first_inode;
	for(i = nr_inodes ; i > 0 ; i--) {
		inode = next;
		next = inode->i_next;		/* clear_inode() changes the queues.. */
		if (inode->i_dev != dev)
			continue;
		if (inode->i_count || inode->i_dirt || inode->i_lock) {
			printk("VFS: inode busy on removed device %s\n",
			       kdevname(dev));
			continue;
		}
		clear_inode(inode);
	}
}

void sync_inodes(kdev_t dev)
{
	int i;
	struct inode * inode;

	inode = first_inode;
	for(i = 0; i < nr_inodes*2; i++, inode = inode->i_next) {
		if (dev && inode->i_dev != dev)
			continue;
		wait_on_inode(inode);
		if (inode->i_dirt)
			write_inode(inode);
	}
}

void iput(struct inode * inode)
{
	if (!inode)
		return;
	wait_on_inode(inode);
	if (!inode->i_count) {
		printk("VFS: iput: trying to free free inode\n");
		printk("VFS: device %s, inode %lu, mode=0%07o\n",
			kdevname(inode->i_rdev), inode->i_ino, inode->i_mode);
		return;
	}
	if (inode->i_pipe)
		wake_up_interruptible(&PIPE_WAIT(*inode));
repeat:
	if (inode->i_count>1) {
		inode->i_count--;
		return;
	}

	wake_up(&inode_wait);
	if (inode->i_pipe) {
		unsigned long page = (unsigned long) PIPE_BASE(*inode);
		PIPE_BASE(*inode) = NULL;
		free_page(page);
	}

	if (inode->i_sb && inode->i_sb->s_op && inode->i_sb->s_op->put_inode) {
		inode->i_sb->s_op->put_inode(inode);
		if (!inode->i_nlink)
			return;
	}

	if (inode->i_dirt) {
		write_inode(inode);	/* we can sleep - so do again */
		wait_on_inode(inode);
		goto repeat;
	}

	if (IS_WRITABLE(inode)) {
		if (inode->i_sb && inode->i_sb->dq_op) {
			/* Here we can sleep also. Let's do it again
			 * Dmitry Gorodchanin 02/11/96 
			 */
			inode->i_lock = 1;
			inode->i_sb->dq_op->drop(inode);
			unlock_inode(inode);
			goto repeat;
		}
	}
	
	inode->i_count--;

	if (inode->i_mmap) {
		printk("iput: inode %lu on device %s still has mappings.\n",
			inode->i_ino, kdevname(inode->i_dev));
		inode->i_mmap = NULL;
	}

	nr_free_inodes++;
	return;
}

static inline unsigned long value(struct inode * inode)
{
	if (inode->i_lock)  
		return 1000;
	if (inode->i_dirt)
		return 1000;
	return inode->i_nrpages;
}

struct inode * get_empty_inode(void)
{
	static int ino = 0;
	struct inode * inode, * best;
	unsigned long badness = 1000;
	int i;

	if (nr_inodes < max_inodes && nr_free_inodes < (nr_inodes >> 1))
		grow_inodes();
repeat:
	inode = first_inode;
	best = NULL;
	for (i = nr_inodes/2; i > 0; i--,inode = inode->i_next) {
		if (!inode->i_count) {
			unsigned long i = value(inode);
			if (i < badness) {
				best = inode;
				if ((badness = i) == 0)
					break;
			}
		}
	}
	if (badness)
		if (nr_inodes < max_inodes) {
			if (grow_inodes() == 0)
				goto repeat;
		}
	inode = best;
	if (!inode) {
		printk("VFS: No free inodes - contact Linus\n");
		sleep_on(&inode_wait);
		goto repeat;
	}
	if (inode->i_lock) {
		wait_on_inode(inode);
		goto repeat;
	}
	if (inode->i_dirt) {
		write_inode(inode);
		goto repeat;
	}
	if (inode->i_count)
		goto repeat;
	clear_inode(inode);
	inode->i_count = 1;
	inode->i_nlink = 1;
	inode->i_version = ++event;
	inode->i_sem.count = 1;
	inode->i_ino = ++ino;
	inode->i_dev = 0;
	nr_free_inodes--;
	if (nr_free_inodes < 0) {
		printk ("VFS: get_empty_inode: bad free inode count.\n");
		nr_free_inodes = 0;
	}
	return inode;
}

struct inode * get_pipe_inode(void)
{
	struct inode * inode;
	extern struct inode_operations pipe_inode_operations;

	if (!(inode = get_empty_inode()))
		return NULL;
	if (!(PIPE_BASE(*inode) = (char*) __get_free_page(GFP_USER))) {
		iput(inode);
		return NULL;
	}
	inode->i_op = &pipe_inode_operations;
	inode->i_count = 2;	/* sum of readers/writers */
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
	return inode;
}

struct inode *__iget(struct super_block * sb, int nr, int crossmntp)
{
	static struct wait_queue * update_wait = NULL;
	struct inode_hash_entry * h;
	struct inode * inode;
	struct inode * empty = NULL;

	if (!sb)
		panic("VFS: iget with sb==NULL");
	h = hash(sb->s_dev, nr);
repeat:
	for (inode = h->inode; inode ; inode = inode->i_hash_next)
		if (inode->i_dev == sb->s_dev && inode->i_ino == nr)
			goto found_it;
	if (!empty) {
		/*
		 * If we sleep here before we have found an inode
		 * we need to make sure nobody does anything bad
		 * to the inode while we sleep, because otherwise
		 * we may return an inode that is not valid any
		 * more when we wake up..
		 */
		h->updating++;
		empty = get_empty_inode();
		if (!--h->updating)
			wake_up(&update_wait);
		if (empty)
			goto repeat;
		return (NULL);
	}
	inode = empty;
	inode->i_sb = sb;
	inode->i_dev = sb->s_dev;
	inode->i_ino = nr;
	inode->i_flags = sb->s_flags;
	put_last_free(inode);
	insert_inode_hash(inode);
	read_inode(inode);
	goto return_it;

found_it:
	if (!inode->i_count)
		nr_free_inodes--;
	inode->i_count++;
	wait_on_inode(inode);
	if (inode->i_dev != sb->s_dev || inode->i_ino != nr) {
		printk("Whee.. inode changed from under us. Tell Linus\n");
		iput(inode);
		goto repeat;
	}
	if (crossmntp && inode->i_mount) {
		struct inode * tmp = inode->i_mount;
		tmp->i_count++;
		iput(inode);
		inode = tmp;
		wait_on_inode(inode);
	}
	if (empty)
		iput(empty);

return_it:
	while (h->updating)
		sleep_on(&update_wait);
	return inode;
}

/*
 * The "new" scheduling primitives (new as of 0.97 or so) allow this to
 * be done without disabling interrupts (other than in the actual queue
 * updating things: only a couple of 386 instructions). This should be
 * much better for interrupt latency.
 */
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
