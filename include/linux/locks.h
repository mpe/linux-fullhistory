#ifndef _LINUX_LOCKS_H
#define _LINUX_LOCKS_H

#ifndef _LINUX_MM_H
#include <linux/mm.h>
#endif

/*
 * Unlocked, temporary IO buffer_heads gets moved to the reuse_list
 * once their page becomes unlocked.  
 */
extern struct buffer_head *reuse_list;

/*
 * Buffer cache locking - note that interrupts may only unlock, not
 * lock buffers.
 */
extern void __wait_on_buffer(struct buffer_head *);

extern inline void wait_on_buffer(struct buffer_head * bh)
{
	if (test_bit(BH_Lock, &bh->b_state))
		__wait_on_buffer(bh);
}

extern inline void lock_buffer(struct buffer_head * bh)
{
	if (set_bit(BH_Lock, &bh->b_state))
		__wait_on_buffer(bh);
}

extern inline void unlock_buffer(struct buffer_head * bh)
{
	struct buffer_head *tmp = bh;
	int page_locked = 0;
	unsigned long flags;
	
	clear_bit(BH_Lock, &bh->b_state);
	wake_up(&bh->b_wait);
	do {
		if (test_bit(BH_Lock, &tmp->b_state)) {
			page_locked = 1;
			break;
		}
		tmp=tmp->b_this_page;
	} while (tmp && tmp != bh);
	save_flags(flags);
	if (!page_locked) {
		struct page *page = mem_map + MAP_NR(bh->b_data);
		page->locked = 0;
		wake_up(&page->wait);
		tmp = bh;
		cli();
		do {
			if (test_bit(BH_FreeOnIO, &tmp->b_state)) {
				tmp->b_next_free = reuse_list;
				reuse_list = tmp;
				clear_bit(BH_FreeOnIO, &tmp->b_state);
			}
			tmp = tmp->b_this_page;
		} while (tmp && tmp != bh);
		restore_flags(flags);
	}
}

/*
 * super-block locking. Again, interrupts may only unlock
 * a super-block (although even this isn't done right now.
 * nfs may need it).
 */
extern void __wait_on_super(struct super_block *);

extern inline void wait_on_super(struct super_block * sb)
{
	if (sb->s_lock)
		__wait_on_super(sb);
}

extern inline void lock_super(struct super_block * sb)
{
	if (sb->s_lock)
		__wait_on_super(sb);
	sb->s_lock = 1;
}

extern inline void unlock_super(struct super_block * sb)
{
	sb->s_lock = 0;
	wake_up(&sb->s_wait);
}

#endif /* _LINUX_LOCKS_H */

