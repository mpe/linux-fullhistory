/*
 * JFFS2 -- Journalling Flash File System, Version 2.
 *
 * Copyright (C) 2001, 2002 Red Hat, Inc.
 *
 * Created by David Woodhouse <dwmw2@cambridge.redhat.com>
 *
 * The original JFFS, from which the design for JFFS2 was derived,
 * was designed and implemented by Axis Communications AB.
 *
 * The contents of this file are subject to the Red Hat eCos Public
 * License Version 1.1 (the "Licence"); you may not use this file
 * except in compliance with the Licence.  You may obtain a copy of
 * the Licence at http://www.redhat.com/
 *
 * Software distributed under the Licence is distributed on an "AS IS"
 * basis, WITHOUT WARRANTY OF ANY KIND, either express or implied.
 * See the Licence for the specific language governing rights and
 * limitations under the Licence.
 *
 * The Original Code is JFFS2 - Journalling Flash File System, version 2
 *
 * Alternatively, the contents of this file may be used under the
 * terms of the GNU General Public License version 2 (the "GPL"), in
 * which case the provisions of the GPL are applicable instead of the
 * above.  If you wish to allow the use of your version of this file
 * only under the terms of the GPL and not to allow others to use your
 * version of this file under the RHEPL, indicate your decision by
 * deleting the provisions above and replace them with the notice and
 * other provisions required by the GPL.  If you do not delete the
 * provisions above, a recipient may use your version of this file
 * under either the RHEPL or the GPL.
 *
 * $Id: nodemgmt.c,v 1.63 2002/03/08 14:54:09 dwmw2 Exp $
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/mtd/mtd.h>
#include <linux/interrupt.h>
#include "nodelist.h"

/**
 *	jffs2_reserve_space - request physical space to write nodes to flash
 *	@c: superblock info
 *	@minsize: Minimum acceptable size of allocation
 *	@ofs: Returned value of node offset
 *	@len: Returned value of allocation length
 *	@prio: Allocation type - ALLOC_{NORMAL,DELETION}
 *
 *	Requests a block of physical space on the flash. Returns zero for success
 *	and puts 'ofs' and 'len' into the appriopriate place, or returns -ENOSPC
 *	or other error if appropriate.
 *
 *	If it returns zero, jffs2_reserve_space() also downs the per-filesystem
 *	allocation semaphore, to prevent more than one allocation from being
 *	active at any time. The semaphore is later released by jffs2_commit_allocation()
 *
 *	jffs2_reserve_space() may trigger garbage collection in order to make room
 *	for the requested allocation.
 */

static int jffs2_do_reserve_space(struct jffs2_sb_info *c,  uint32_t minsize, uint32_t *ofs, uint32_t *len);

int jffs2_reserve_space(struct jffs2_sb_info *c, uint32_t minsize, uint32_t *ofs, uint32_t *len, int prio)
{
	int ret = -EAGAIN;
	int blocksneeded = JFFS2_RESERVED_BLOCKS_WRITE;
	/* align it */
	minsize = PAD(minsize);

	if (prio == ALLOC_DELETION)
		blocksneeded = JFFS2_RESERVED_BLOCKS_DELETION;

	D1(printk(KERN_DEBUG "jffs2_reserve_space(): Requested 0x%x bytes\n", minsize));
	down(&c->alloc_sem);

	D1(printk(KERN_DEBUG "jffs2_reserve_space(): alloc sem got\n"));

	spin_lock_bh(&c->erase_completion_lock);

	/* this needs a little more thought */
	while(ret == -EAGAIN) {
		while(c->nr_free_blocks + c->nr_erasing_blocks < blocksneeded) {
			int ret;

			up(&c->alloc_sem);
			if (c->dirty_size < c->sector_size) {
				D1(printk(KERN_DEBUG "Short on space, but total dirty size 0x%08x < sector size 0x%08x, so -ENOSPC\n", c->dirty_size, c->sector_size));
				spin_unlock_bh(&c->erase_completion_lock);
				return -ENOSPC;
			}
			D1(printk(KERN_DEBUG "Triggering GC pass. nr_free_blocks %d, nr_erasing_blocks %d, free_size 0x%08x, dirty_size 0x%08x, used_size 0x%08x, erasing_size 0x%08x, bad_size 0x%08x (total 0x%08x of 0x%08x)\n",
				  c->nr_free_blocks, c->nr_erasing_blocks, c->free_size, c->dirty_size, c->used_size, c->erasing_size, c->bad_size,
				  c->free_size + c->dirty_size + c->used_size + c->erasing_size + c->bad_size, c->flash_size));
			spin_unlock_bh(&c->erase_completion_lock);
			
			ret = jffs2_garbage_collect_pass(c);
			if (ret)
				return ret;

			cond_resched();

			if (signal_pending(current))
				return -EINTR;

			down(&c->alloc_sem);
			spin_lock_bh(&c->erase_completion_lock);
		}

		ret = jffs2_do_reserve_space(c, minsize, ofs, len);
		if (ret) {
			D1(printk(KERN_DEBUG "jffs2_reserve_space: ret is %d\n", ret));
		}
	}
	spin_unlock_bh(&c->erase_completion_lock);
	if (ret)
		up(&c->alloc_sem);
	return ret;
}

int jffs2_reserve_space_gc(struct jffs2_sb_info *c, uint32_t minsize, uint32_t *ofs, uint32_t *len)
{
	int ret = -EAGAIN;
	minsize = PAD(minsize);

	D1(printk(KERN_DEBUG "jffs2_reserve_space_gc(): Requested 0x%x bytes\n", minsize));

	spin_lock_bh(&c->erase_completion_lock);
	while(ret == -EAGAIN) {
		ret = jffs2_do_reserve_space(c, minsize, ofs, len);
		if (ret) {
		        D1(printk(KERN_DEBUG "jffs2_reserve_space_gc: looping, ret is %d\n", ret));
		}
	}
	spin_unlock_bh(&c->erase_completion_lock);
	return ret;
}

/* Called with alloc sem _and_ erase_completion_lock */
static int jffs2_do_reserve_space(struct jffs2_sb_info *c,  uint32_t minsize, uint32_t *ofs, uint32_t *len)
{
	struct jffs2_eraseblock *jeb = c->nextblock;
	
 restart:
	if (jeb && minsize > jeb->free_size) {
		/* Skip the end of this block and file it as having some dirty space */
		/* If there's a pending write to it, flush now */
		if (c->wbuf_len) {
			spin_unlock_bh(&c->erase_completion_lock);
			D1(printk(KERN_DEBUG "jffs2_do_reserve_space: Flushing write buffer\n"));			    
			jffs2_flush_wbuf(c, 1);
			spin_lock_bh(&c->erase_completion_lock);
			/* We know nobody's going to have changed nextblock. Just continue */
		}
		c->dirty_size += jeb->free_size;
		c->free_size -= jeb->free_size;
		jeb->dirty_size += jeb->free_size;
		jeb->free_size = 0;
		D1(printk(KERN_DEBUG "Adding full erase block at 0x%08x to dirty_list (free 0x%08x, dirty 0x%08x, used 0x%08x\n",
			  jeb->offset, jeb->free_size, jeb->dirty_size, jeb->used_size));
		list_add_tail(&jeb->list, &c->dirty_list);
		c->nextblock = jeb = NULL;
	}
	
	if (!jeb) {
		struct list_head *next;
		/* Take the next block off the 'free' list */

		if (list_empty(&c->free_list)) {

			DECLARE_WAITQUEUE(wait, current);
			
			if (!c->nr_erasing_blocks && 
			    !list_empty(&c->erasable_list)) {
				struct jffs2_eraseblock *ejeb;

				ejeb = list_entry(c->erasable_list.next, struct jffs2_eraseblock, list);
				list_del(&ejeb->list);
				list_add_tail(&ejeb->list, &c->erase_pending_list);
				c->nr_erasing_blocks++;
				D1(printk(KERN_DEBUG "jffs2_do_reserve_space: Triggering erase of erasable block at 0x%08x\n",
					  ejeb->offset));
			}

			if (!c->nr_erasing_blocks && 
			    !list_empty(&c->erasable_pending_wbuf_list)) {
				D1(printk(KERN_DEBUG "jffs2_do_reserve_space: Flushing write buffer\n"));
				/* c->nextblock is NULL, no update to c->nextblock allowed */			    
				spin_unlock_bh(&c->erase_completion_lock);
				jffs2_flush_wbuf(c, 1);
				spin_lock_bh(&c->erase_completion_lock);
				/* Have another go. It'll be on the erasable_list now */
				return -EAGAIN;
			}

			if (!c->nr_erasing_blocks) {
				/* Ouch. We're in GC, or we wouldn't have got here.
				   And there's no space left. At all. */
				printk(KERN_CRIT "Argh. No free space left for GC. nr_erasing_blocks is %d. nr_free_blocks is %d. (erasableempty: %s, erasingempty: %s, erasependingempty: %s)\n", 
				       c->nr_erasing_blocks, c->nr_free_blocks, list_empty(&c->erasable_list)?"yes":"no", 
				       list_empty(&c->erasing_list)?"yes":"no", list_empty(&c->erase_pending_list)?"yes":"no");
				return -ENOSPC;
			}
			/* Make sure this can't deadlock. Someone has to start the erases
			   of erase_pending blocks */
			set_current_state(TASK_INTERRUPTIBLE);
			add_wait_queue(&c->erase_wait, &wait);
			D1(printk(KERN_DEBUG "Waiting for erases to complete. erasing_blocks is %d. (erasableempty: %s, erasingempty: %s, erasependingempty: %s)\n", 
				  c->nr_erasing_blocks, list_empty(&c->erasable_list)?"yes":"no",
				  list_empty(&c->erasing_list)?"yes":"no", list_empty(&c->erase_pending_list)?"yes":"no"));
			if (!list_empty(&c->erase_pending_list)) {
				D1(printk(KERN_DEBUG "Triggering pending erases\n"));
				jffs2_erase_pending_trigger(c);
			}
			spin_unlock_bh(&c->erase_completion_lock);
			schedule();
			remove_wait_queue(&c->erase_wait, &wait);
			spin_lock_bh(&c->erase_completion_lock);
			if (signal_pending(current)) {
				return -EINTR;
			}
			/* An erase may have failed, decreasing the
			   amount of free space available. So we must
			   restart from the beginning */
			return -EAGAIN;
		}

		next = c->free_list.next;
		list_del(next);
		c->nextblock = jeb = list_entry(next, struct jffs2_eraseblock, list);
		c->nr_free_blocks--;

		/* On NAND free_size == sector_size, cleanmarker is in spare area !*/
		if (jeb->free_size != c->sector_size - 
			(jffs2_cleanmarker_oob(c)) ? 0 : sizeof(struct jffs2_unknown_node)) {

			printk(KERN_WARNING "Eep. Block 0x%08x taken from free_list had free_size of 0x%08x!!\n", jeb->offset, jeb->free_size);
			goto restart;
		}
	}
	/* OK, jeb (==c->nextblock) is now pointing at a block which definitely has
	   enough space */
	*ofs = jeb->offset + (c->sector_size - jeb->free_size);
	*len = jeb->free_size;

	if (jeb->used_size == PAD(sizeof(struct jffs2_unknown_node)) &&
	    !jeb->first_node->next_in_ino) {
		/* Only node in it beforehand was a CLEANMARKER node (we think). 
		   So mark it obsolete now that there's going to be another node
		   in the block. This will reduce used_size to zero but We've 
		   already set c->nextblock so that jffs2_mark_node_obsolete()
		   won't try to refile it to the dirty_list.
		*/
		spin_unlock_bh(&c->erase_completion_lock);
		jffs2_mark_node_obsolete(c, jeb->first_node);
		spin_lock_bh(&c->erase_completion_lock);
	}

	D1(printk(KERN_DEBUG "jffs2_do_reserve_space(): Giving 0x%x bytes at 0x%x\n", *len, *ofs));
	return 0;
}

/**
 *	jffs2_add_physical_node_ref - add a physical node reference to the list
 *	@c: superblock info
 *	@new: new node reference to add
 *	@len: length of this physical node
 *	@dirty: dirty flag for new node
 *
 *	Should only be used to report nodes for which space has been allocated 
 *	by jffs2_reserve_space.
 *
 *	Must be called with the alloc_sem held.
 */
 
int jffs2_add_physical_node_ref(struct jffs2_sb_info *c, struct jffs2_raw_node_ref *new, uint32_t len, int dirty)
{
	struct jffs2_eraseblock *jeb;

	len = PAD(len);
	jeb = &c->blocks[new->flash_offset / c->sector_size];
	D1(printk(KERN_DEBUG "jffs2_add_physical_node_ref(): Node at 0x%x, size 0x%x\n", new->flash_offset & ~3, len));
#if 1
	if (jeb != c->nextblock || (new->flash_offset & ~3) != jeb->offset + (c->sector_size - jeb->free_size)) {
		printk(KERN_WARNING "argh. node added in wrong place\n");
		jffs2_free_raw_node_ref(new);
		return -EINVAL;
	}
#endif
	spin_lock_bh(&c->erase_completion_lock);

	if (!jeb->first_node)
		jeb->first_node = new;
	if (jeb->last_node)
		jeb->last_node->next_phys = new;
	jeb->last_node = new;

	jeb->free_size -= len;
	c->free_size -= len;
	if (dirty) {
		new->flash_offset |= 1;
		jeb->dirty_size += len;
		c->dirty_size += len;
	} else {
		jeb->used_size += len;
		c->used_size += len;
	}

	if (!jeb->free_size && !jeb->dirty_size) {
		/* If it lives on the dirty_list, jffs2_reserve_space will put it there */
		D1(printk(KERN_DEBUG "Adding full erase block at 0x%08x to clean_list (free 0x%08x, dirty 0x%08x, used 0x%08x\n",
			  jeb->offset, jeb->free_size, jeb->dirty_size, jeb->used_size));
		if (c->wbuf_len) {
			/* Flush the last write in the block if it's outstanding */
			spin_unlock_bh(&c->erase_completion_lock);
			jffs2_flush_wbuf(c, 1);
			spin_lock_bh(&c->erase_completion_lock);
		}

		list_add_tail(&jeb->list, &c->clean_list);
		c->nextblock = NULL;
	}
	ACCT_SANITY_CHECK(c,jeb);
	ACCT_PARANOIA_CHECK(jeb);

	spin_unlock_bh(&c->erase_completion_lock);

	return 0;
}


void jffs2_complete_reservation(struct jffs2_sb_info *c)
{
	D1(printk(KERN_DEBUG "jffs2_complete_reservation()\n"));
	jffs2_garbage_collect_trigger(c);
	up(&c->alloc_sem);
}

void jffs2_mark_node_obsolete(struct jffs2_sb_info *c, struct jffs2_raw_node_ref *ref)
{
	struct jffs2_eraseblock *jeb;
	int blocknr;
	struct jffs2_unknown_node n;
	int ret;
	size_t retlen;

	if(!ref) {
		printk(KERN_NOTICE "EEEEEK. jffs2_mark_node_obsolete called with NULL node\n");
		return;
	}
	if (ref->flash_offset & 1) {
		D1(printk(KERN_DEBUG "jffs2_mark_node_obsolete called with already obsolete node at 0x%08x\n", ref->flash_offset &~3));
		return;
	}
	blocknr = ref->flash_offset / c->sector_size;
	if (blocknr >= c->nr_blocks) {
		printk(KERN_NOTICE "raw node at 0x%08x is off the end of device!\n", ref->flash_offset);
		BUG();
	}
	jeb = &c->blocks[blocknr];
	if (jeb->used_size < ref->totlen) {
		printk(KERN_NOTICE "raw node of size 0x%08x freed from erase block %d at 0x%08x, but used_size was already 0x%08x\n",
		       ref->totlen, blocknr, ref->flash_offset, jeb->used_size);
		BUG();
	}

	spin_lock_bh(&c->erase_completion_lock);
	jeb->used_size -= ref->totlen;
	jeb->dirty_size += ref->totlen;
	c->used_size -= ref->totlen;
	c->dirty_size += ref->totlen;
	ref->flash_offset |= 1;
	
	ACCT_SANITY_CHECK(c, jeb);

	ACCT_PARANOIA_CHECK(jeb);

	if (c->flags & JFFS2_SB_FLAG_MOUNTING) {
		/* Mount in progress. Don't muck about with the block
		   lists because they're not ready yet, and don't actually
		   obliterate nodes that look obsolete. If they weren't 
		   marked obsolete on the flash at the time they _became_
		   obsolete, there was probably a reason for that. */
		spin_unlock_bh(&c->erase_completion_lock);
		return;
	}

	if (jeb == c->nextblock) {
		D2(printk(KERN_DEBUG "Not moving nextblock 0x%08x to dirty/erase_pending list\n", jeb->offset));
	} else if (!jeb->used_size) {
		if (jeb == c->gcblock) {
			D1(printk(KERN_DEBUG "gcblock at 0x%08x completely dirtied. Clearing gcblock...\n", jeb->offset));
			c->gcblock = NULL;
		} else {
			D1(printk(KERN_DEBUG "Eraseblock at 0x%08x completely dirtied. Removing from (dirty?) list...\n", jeb->offset));
			list_del(&jeb->list);
		}
		if (c->wbuf_len) {
			D1(printk(KERN_DEBUG "...and adding to erasable_pending_wbuf_list\n"));
			list_add_tail(&jeb->list, &c->erasable_pending_wbuf_list);

			/* We've changed the rules slightly. After
			   writing a node you now mustn't drop the
			   alloc_sem before you've finished all the
			   list management - this is so that when we
			   get here, we know that no other nodes have
			   been written, and the above check on wbuf
			   is valid - wbuf_len is nonzero IFF the node
			   which obsoletes this node is still in the
			   wbuf.

			   So we BUG() if that new rule is broken, to
			   make sure we catch it and fix it.
			*/
			if (!down_trylock(&c->alloc_sem)) {
				up(&c->alloc_sem);
				printk(KERN_CRIT "jffs2_mark_node_obsolete() called with wbuf active but alloc_sem not locked!\n");
				BUG();
			}
		} else {
			D1(printk(KERN_DEBUG "...and adding to erasable_list\n"));
			list_add_tail(&jeb->list, &c->erasable_list);
		}
		D1(printk(KERN_DEBUG "Done OK\n"));
	} else if (jeb == c->gcblock) {
		D2(printk(KERN_DEBUG "Not moving gcblock 0x%08x to dirty_list\n", jeb->offset));
	} else if (jeb->dirty_size == ref->totlen) {
		D1(printk(KERN_DEBUG "Eraseblock at 0x%08x is freshly dirtied. Removing from clean list...\n", jeb->offset));
		list_del(&jeb->list);
		D1(printk(KERN_DEBUG "...and adding to dirty_list\n"));
		list_add_tail(&jeb->list, &c->dirty_list);
	}

	spin_unlock_bh(&c->erase_completion_lock);

	if (!jffs2_can_mark_obsolete(c))
		return;
	if (jffs2_is_readonly(c))
		return;

	D1(printk(KERN_DEBUG "obliterating obsoleted node at 0x%08x\n", ref->flash_offset &~3));
	ret = jffs2_flash_read(c, ref->flash_offset &~3, sizeof(n), &retlen, (char *)&n);
	if (ret) {
		printk(KERN_WARNING "Read error reading from obsoleted node at 0x%08x: %d\n", ref->flash_offset &~3, ret);
		return;
	}
	if (retlen != sizeof(n)) {
		printk(KERN_WARNING "Short read from obsoleted node at 0x%08x: %d\n", ref->flash_offset &~3, retlen);
		return;
	}
	if (PAD(n.totlen) != PAD(ref->totlen)) {
		printk(KERN_WARNING "Node totlen on flash (0x%08x) != totlen in node ref (0x%08x)\n", n.totlen, ref->totlen);
		return;
	}
	if (!(n.nodetype & JFFS2_NODE_ACCURATE)) {
		D1(printk(KERN_DEBUG "Node at 0x%08x was already marked obsolete (nodetype 0x%04x\n", ref->flash_offset &~3, n.nodetype));
		return;
	}
	n.nodetype &= ~JFFS2_NODE_ACCURATE;
	ret = jffs2_flash_write(c, ref->flash_offset&~3, sizeof(n), &retlen, (char *)&n);
	if (ret) {
		printk(KERN_WARNING "Write error in obliterating obsoleted node at 0x%08x: %d\n", ref->flash_offset &~3, ret);
		return;
	}
	if (retlen != sizeof(n)) {
		printk(KERN_WARNING "Short write in obliterating obsoleted node at 0x%08x: %d\n", ref->flash_offset &~3, retlen);
		return;
	}
}
