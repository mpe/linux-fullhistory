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
 *
 */
 
#include <linux/kernel.h>
#include <linux/major.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/stat.h>
#include <linux/socket.h>
#include <linux/un.h>
#include <linux/fcntl.h>
#include <linux/termios.h>
#include <linux/socket.h>
#include <linux/sockios.h>
#include <linux/net.h>
#include <linux/in.h>
#include <linux/fs.h>
#include <linux/malloc.h>
#include <asm/segment.h>
#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/tcp.h>
#include <net/af_unix.h>
#include <linux/proc_fs.h>

/* Internal data structures and random procedures: */

#define MAX_STACK 1000		/* Maximum depth of tree (about 1 page) */
static unix_socket **stack;	/* stack of objects to mark */
static int in_stack = 0;	/* first free entry in stack */


extern inline unix_socket *unix_get_socket(struct file *filp)
{
	unix_socket * u_sock = NULL;
	struct inode *inode = filp->f_inode;

	/*
	 *	Socket ?
	 */
	if (inode && inode->i_sock) {
		struct socket * s = &inode->u.socket_i;

		/*
		 *	AF_UNIX ?
		 */
		if (s->ops == &unix_proto_ops)
			u_sock = s->data;
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
	if (in_stack == MAX_STACK)
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

extern inline void maybe_mark_and_push(unix_socket *x)
{
	if (x->protinfo.af_unix.marksweep&MARKED)
		return;
	x->protinfo.af_unix.marksweep|=MARKED;
	push_stack(x);
}


/* The external entry point: unix_gc() */

void unix_gc(void)
{
	static int in_unix_gc=0;
	unix_socket *s;
	unix_socket *next;
	
	/*
	 *	Avoid a recursive GC.
	 */

	if(in_unix_gc)
		return;
	in_unix_gc=1;
	
	stack=(unix_socket **)get_free_page(GFP_KERNEL);
	
	/*
	 *	Assume everything is now unmarked 
	 */

	/* Invariant to be maintained:
		- everything marked is either:
		-- (a) on the stack, or
		-- (b) has all of its children marked
		- everything on the stack is always marked
		- nothing is ever pushed onto the stack twice, because:
		-- nothing previously marked is ever pushed on the stack
	 */

	/*
	 *	Push root set
	 */

	for(s=unix_socket_list;s!=NULL;s=s->next)
	{
		/*
		 *	If all instances of the descriptor are not
		 *	in flight we are in use.
		 */
		if(s->socket && s->socket->file && s->socket->file->f_count > s->protinfo.af_unix.inflight)
			maybe_mark_and_push(s);
	}

	/*
	 *	Mark phase 
	 */

	while (!empty_stack())
	{
		unix_socket *x = pop_stack();
		unix_socket *f=NULL,*sk;
		struct sk_buff *skb;
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
			if(skb->h.filp)
			{
				/*
				 *	Process the descriptors of this socket
				 */
				int nfd=*(int *)skb->h.filp;
				struct file **fp=(struct file **)(skb->h.filp+sizeof(int));
				while(nfd--)
				{
					/*
					 *	Get the socket the fd matches if
					 *	it indeed does so
					 */
					if((sk=unix_get_socket(*fp++))!=NULL)
					{
						/*
						 *	Remember the first, mark the
						 *	rest.
						 */
						if(f==NULL)
							f=sk;
						else
							maybe_mark_and_push(sk);
					}
				}
			}
			skb=skb->next;
		}
		/*
		 *	Handle first born specially 
		 */

		if (f) 
		{
			if (!(f->protinfo.af_unix.marksweep&MARKED))
			{
				f->protinfo.af_unix.marksweep|=MARKED;
				x=f;
				f=NULL;
				goto tail;
			}
		}
	}

	/*
	 *	Sweep phase.  NOTE: this part dominates the time complexity 
	 */

	for(s=unix_socket_list;s!=NULL;s=next)
	{
		next=s->next;
		if (!(s->protinfo.af_unix.marksweep&MARKED))
		{
			/*
			 *	We exist only in the passing tree of sockets
			 *	that is no longer connected to active descriptors
			 *	Time to die..
			 *
			 *	Subtle item: We will correctly sweep out the
			 *	socket that has just been closed by the user.
			 *	We must not close this as we are in the middle
			 *	of its close at this moment. Skip that file
			 *	using f_count==0 to spot it.
			 */
			 
			if(s->socket && s->socket->file && s->socket->file->f_count)
				close_fp(s->socket->file);
		}
		else
			s->protinfo.af_unix.marksweep&=~MARKED;	/* unmark everything for next collection */
	}
	
	in_unix_gc=0;
	
	free_page((long)stack);
}
