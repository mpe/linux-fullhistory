/*
 * NET3:	Garbage Collector For AF_UNIX sockets
 *
 * Garbage Collector:
 *	Copyright (C) Barak A. Pearlmutter.
 *	Released under the GPL version 2 or later.
 *
 * Chopped about by Alan Cox 22/3/96 to make it fit the AF_UNIX socket problem.
 * If it doesn't work blame me, it worked when Barak sent it.
 *
 * Assumptions:
 *
 *  - object w/ a bit
 *  - free list
 *
 * Current optimizations:
 *
 *  - explicit stack instead of recursion
 *  - tail recurse on first born instead of immediate push/pop
 *
 *  Future optimizations:
 *
 *  - don't just push entire root set; process in place
 *  - use linked list for internal stack
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 *  Fixes:
 *	Alan Cox	07 Sept	1997	Vmalloc internal stack as needed.
 *					Cope with changing max_files.
 *	Al Viro		11 Oct 1998
 *		Graph may have cycles. That is, we can send the descriptor
 *		of foo to bar and vice versa. Current code chokes on that.
 *		Fix: move SCM_RIGHTS ones into the separate list and then
 *		skb_free() them all instead of doing explicit fput's.
 *		Another problem: since fput() may block somebody may
 *		create a new unix_socket when we are in the middle of sweep
 *		phase. Fix: revert the logic wrt MARKED. Mark everything
 *		upon the beginning and unmark non-junk ones.
 *
 *		[12 Oct 1998] AAARGH! New code purges all SCM_RIGHTS
 *		sent to connect()'ed but still not accept()'ed sockets.
 *		Fixed. Old code had slightly different problem here:
 *		extra fput() in situation when we passed the descriptor via
 *		such socket and closed it (descriptor). That would happen on
 *		each unix_gc() until the accept(). Since the struct file in
 *		question would go to the free list and might be reused...
 *		That might be the reason of random oopses on close_fp() in
 *		unrelated processes.
 *
 */
 
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/string.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <linux/net.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <linux/file.h>
#include <linux/proc_fs.h>
#include <linux/vmalloc.h>

#include <net/sock.h>
#include <net/tcp.h>
#include <net/af_unix.h>
#include <net/scm.h>

/* Internal data structures and random procedures: */

static unix_socket **stack;	/* stack of objects to mark */
static int in_stack = 0;	/* first free entry in stack */
static int max_stack;		/* Top of stack */

extern inline unix_socket *unix_get_socket(struct file *filp)
{
	unix_socket * u_sock = NULL;
	struct inode *inode = filp->f_dentry->d_inode;

	/*
	 *	Socket ?
	 */
	if (inode && inode->i_sock) {
		struct socket * sock = &inode->u.socket_i;
		struct sock * s = sock->sk;

		/*
		 *	PF_UNIX ?
		 */
		if (s && sock->ops && sock->ops->family == PF_UNIX)
			u_sock = s;
	}
	return u_sock;
}

/*
 *	Keep the number of times in flight count for the file
 *	descriptor if it is for an AF_UNIX socket.
 */
 
void unix_inflight(struct file *fp)
{
	unix_socket *s=unix_get_socket(fp);
	if(s)
		s->protinfo.af_unix.inflight++;
}

void unix_notinflight(struct file *fp)
{
	unix_socket *s=unix_get_socket(fp);
	if(s)
		s->protinfo.af_unix.inflight--;
}


/*
 *	Garbage Collector Support Functions
 */
 
extern inline void push_stack(unix_socket *x)
{
	if (in_stack == max_stack)
		panic("can't push onto full stack");
	stack[in_stack++] = x;
}

extern inline unix_socket *pop_stack(void)
{
	if (in_stack == 0)
		panic("can't pop empty gc stack");
	return stack[--in_stack];
}

extern inline int empty_stack(void)
{
	return in_stack == 0;
}

extern inline void maybe_unmark_and_push(unix_socket *x)
{
	if (!(x->protinfo.af_unix.marksweep&MARKED))
		return;
	x->protinfo.af_unix.marksweep&=~MARKED;
	push_stack(x);
}


/* The external entry point: unix_gc() */

void unix_gc(void)
{
	static int in_unix_gc=0;
	int i;
	unix_socket *s;
	struct sk_buff_head hitlist;
	struct sk_buff *skb;
	
	/*
	 *	Avoid a recursive GC.
	 */

	if(in_unix_gc)
		return;
	in_unix_gc=1;
	
	if(stack==NULL || max_files>max_stack)
	{
		if(stack)
			vfree(stack);
		stack=(unix_socket **)vmalloc(max_files*sizeof(struct unix_socket *));
		if(stack==NULL)
		{
			printk(KERN_NOTICE "unix_gc: deferred due to low memory.\n");
			in_unix_gc=0;
			return;
		}
		max_stack=max_files;
	}
	
	forall_unix_sockets(i, s)
	{
		s->protinfo.af_unix.marksweep|=MARKED;
	}
	/*
	 *	Everything is now marked 
	 */

	/* Invariant to be maintained:
		- everything unmarked is either:
		-- (a) on the stack, or
		-- (b) has all of its children unmarked
		- everything on the stack is always unmarked
		- nothing is ever pushed onto the stack twice, because:
		-- nothing previously unmarked is ever pushed on the stack
	 */

	/*
	 *	Push root set
	 */

	forall_unix_sockets(i, s)
	{
		/*
		 *	If all instances of the descriptor are not
		 *	in flight we are in use.
		 */
		if(s->socket && s->socket->file &&
		   s->socket->file->f_count > s->protinfo.af_unix.inflight)
			maybe_unmark_and_push(s);
	}

	/*
	 *	Mark phase 
	 */

	while (!empty_stack())
	{
		unix_socket *x = pop_stack();
		unix_socket *f=NULL,*sk;
tail:		
		skb=skb_peek(&x->receive_queue);
		
		/*
		 *	Loop through all but first born 
		 */
		
		while(skb && skb != (struct sk_buff *)&x->receive_queue)
		{
			/*
			 *	Do we have file descriptors ?
			 */
			if(UNIXCB(skb).fp)
			{
				/*
				 *	Process the descriptors of this socket
				 */
				int nfd=UNIXCB(skb).fp->count;
				struct file **fp = UNIXCB(skb).fp->fp;
				while(nfd--)
				{
					/*
					 *	Get the socket the fd matches if
					 *	it indeed does so
					 */
					if((sk=unix_get_socket(*fp++))!=NULL)
					{
						/*
						 *	Remember the first,
						 *	unmark the rest.
						 */
						if(f==NULL)
							f=sk;
						else
							maybe_unmark_and_push(sk);
					}
				}
			}
			/* We have to scan not-yet-accepted ones too */
			if (UNIXCB(skb).attr & MSG_SYN) {
				if (f==NULL)
					f=skb->sk;
				else
					maybe_unmark_and_push(skb->sk);
			}
			skb=skb->next;
		}
		/*
		 *	Handle first born specially 
		 */

		if (f) 
		{
			if ((f->protinfo.af_unix.marksweep&MARKED))
			{
				f->protinfo.af_unix.marksweep&=~MARKED;
				x=f;
				f=NULL;
				goto tail;
			}
		}
	}

	skb_queue_head_init(&hitlist);

	forall_unix_sockets(i, s)
	{
		if (s->protinfo.af_unix.marksweep&MARKED)
		{
			struct sk_buff *nextsk;
			skb=skb_peek(&s->receive_queue);
			while(skb && skb != (struct sk_buff *)&s->receive_queue)
			{
				nextsk=skb->next;
				/*
				 *	Do we have file descriptors ?
				 */
				if(UNIXCB(skb).fp)
				{
					skb_unlink(skb);
					skb_queue_tail(&hitlist,skb);
				}
				skb=nextsk;
			}
		}
	}

	/*
	 *	Here we are. Hitlist is filled. Die.
	 */

	while ((skb=skb_dequeue(&hitlist))!=NULL) {
		kfree_skb(skb);
	}

	in_unix_gc=0;
}
