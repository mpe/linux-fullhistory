/*

kHTTPd -- the next generation

Pass connections to userspace-daemons

*/
/****************************************************************
 *	This program is free software; you can redistribute it and/or modify
 *	it under the terms of the GNU General Public License as published by
 *	the Free Software Foundation; either version 2, or (at your option)
 *	any later version.
 *
 *	This program is distributed in the hope that it will be useful,
 *	but WITHOUT ANY WARRANTY; without even the implied warranty of
 *	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *	GNU General Public License for more details.
 *
 *	You should have received a copy of the GNU General Public License
 *	along with this program; if not, write to the Free Software
 *	Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 ****************************************************************/

/*

Purpose:

Userspace() hands all requests in the queue to the userspace-daemon, if
such beast exists.

Return value:
	The number of requests that changed status
*/
#include <linux/kernel.h>

#include <linux/errno.h>
#include <linux/malloc.h>
#include <linux/net.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/smp_lock.h>
#include <linux/un.h>
#include <linux/unistd.h>
#include <linux/wait.h>

#include <net/ip.h>
#include <net/sock.h>
#include <net/tcp.h>

#include <asm/atomic.h>
#include <asm/semaphore.h>
#include <asm/processor.h>
#include <asm/uaccess.h>

#include <linux/file.h>


#include "structure.h"
#include "prototypes.h"
#include "sysctl.h"

/* prototypes of local, static functions */
static int AddSocketToAcceptQueue(struct socket *sock,const int Port);
static void purge_delayed_release(int CPUNR, int All);

static struct khttpd_delayed_release *delayed_release[CONFIG_KHTTPD_NUMCPU];


int Userspace(const int CPUNR)
{
	struct http_request *CurrentRequest,**Prev,*Next;
	
	EnterFunction("Userspace");

	
	purge_delayed_release(CPUNR,0);

	
	CurrentRequest = threadinfo[CPUNR].UserspaceQueue;
	Prev = &(threadinfo[CPUNR].UserspaceQueue);
	
	while (CurrentRequest!=NULL)
	{

		/* Clean-up the waitqueue of the socket.. Bad things happen if
		   this is forgotten. */
		if (CurrentRequest->sock!=NULL)
		{
			lock_kernel();  /* Just to be sure (2.3.11) -- Check this for 2.3.13 */
			if ((CurrentRequest->sock!=NULL)&&(CurrentRequest->sock->sk!=NULL))
			{
				/* Some TCP/IP functions call waitqueue-operations without
				   holding the sock-lock. I am not that brave.
				*/
				lock_sock(CurrentRequest->sock->sk);
				remove_wait_queue(CurrentRequest->sock->sk->sleep,&(CurrentRequest->sleep));
				release_sock(CurrentRequest->sock->sk);
			}
			unlock_kernel(); 
		} 
		

		if  (AddSocketToAcceptQueue(CurrentRequest->sock,sysctl_khttpd_clientport)>=0)
		{
			struct khttpd_delayed_release *delayed;
			
			(*Prev) = CurrentRequest->Next;
			Next = CurrentRequest->Next;
			
			
			delayed = kmalloc(sizeof(struct khttpd_delayed_release),GFP_KERNEL);
			if (delayed==NULL)
			{
				sock_release(CurrentRequest->sock);
			} else
			{
				/* Schedule socket-deletion 120 seconds from now */
				delayed->Next = delayed_release[CPUNR];
				delayed->timeout = jiffies+120*HZ;
				delayed->sock = CurrentRequest->sock;
				delayed_release[CPUNR]=delayed;
			}
				
		
			CurrentRequest->sock = NULL;	 /* We no longer own it */
			
			CleanUpRequest(CurrentRequest); 
				
			CurrentRequest = Next;
			continue;
		
		}
		else /* No userspace-daemon present, or other problems with it */
		{
			(*Prev) = CurrentRequest->Next;
			Next = CurrentRequest->Next;
			
			Send403(CurrentRequest->sock); /* Sorry, no go... */
			
			CleanUpRequest(CurrentRequest); 
				
			CurrentRequest = Next;
			continue;
		
		}

		
		Prev = &(CurrentRequest->Next);	
		CurrentRequest = CurrentRequest->Next;
	}
	
	LeaveFunction("Userspace");
	return 0;
}

void StopUserspace(const int CPUNR)
{
	struct http_request *CurrentRequest,*Next;
	
	EnterFunction("StopUserspace");
	CurrentRequest = threadinfo[CPUNR].UserspaceQueue;

	while (CurrentRequest!=NULL)
	{
		Next= CurrentRequest->Next;
		CleanUpRequest(CurrentRequest);
		CurrentRequest=Next;		
	}
	threadinfo[CPUNR].UserspaceQueue = NULL;
	purge_delayed_release(CPUNR,1);
	
	LeaveFunction("StopUserspace");
}

extern struct sock *tcp_v4_lookup_listener(u32 daddr, unsigned short hnum, int dif);


/* 
   "FindUserspace" returns the struct sock of the userspace-daemon, so that we can
   "drop" our request in the accept-queue 
*/
static struct sock *FindUserspace(const unsigned short Port)
{
	EnterFunction("FindUserspace");
	
	return tcp_v4_lookup_listener(INADDR_ANY,Port,0);
	
	return NULL;
}

static void dummy_destructor(struct open_request *req)
{
}

static struct or_calltable Dummy = 
{
 	NULL,
 	&dummy_destructor,
 	NULL
};

#define BACKLOG(sk) ((sk)->tp_pinfo.af_tcp.syn_backlog) /* lvalue! */

static int AddSocketToAcceptQueue(struct socket *sock,const int Port)
{
	struct open_request *req;
	struct sock *sk;
	struct tcp_opt *tp;
	
	EnterFunction("AddSocketToAcceptQueue");

	
	sk = FindUserspace((unsigned short)Port);	
	
	if (sk==NULL)   /* No userspace-daemon found */
	{
		return -1;
	}
	
	lock_sock(sk);
	
	if (BACKLOG(sk)>128) /* To many pending requests */
	{
		return -1;
	}
	req = tcp_openreq_alloc();
	
	if (req==NULL)
	{	
		release_sock(sk);
		return -1;	
	}
	
	req->sk		= sock->sk;
	sock->sk = NULL;
	sock->state = SS_UNCONNECTED;
	
	
	if (req->sk==NULL)
		(void)printk(KERN_CRIT "kHTTPd: Woops, the socket-buffer is NULL \n"); 
	  
	req->class	= &Dummy;
	req->sk->socket = NULL;
	req->expires    = jiffies + TCP_TIMEOUT_INIT;
	
	tp =&(sk->tp_pinfo.af_tcp);
	sk->ack_backlog++;

	tcp_inc_slow_timer(TCP_SLT_SYNACK);
	tcp_synq_queue(tp,req);	

	if (!sk->dead)
		wake_up_interruptible(sk->sleep);
		
	release_sock(sk);
	
	LeaveFunction("AddSocketToAcceptQueue");
		
	return +1;	
	
	
	
}

void InitUserspace(const int CPUNR)
{
	int I;
	
	I=0;
	while (I<CPUNR)
		delayed_release[I++]=NULL;
}


/*

This function checks in the delayed_release queue for candidate-sockets
to be released. If All != 0, all sockets are released. This is required for
unloading.

*/
static void purge_delayed_release(const int CPUNR,int All)
{
	struct khttpd_delayed_release *Current,*Next,**Prev;
	
	Prev = &(delayed_release[CPUNR]);
	
	Current = delayed_release[CPUNR];
	while (Current!=NULL)
	{
		if ((Current->timeout<=jiffies)||(All))
		{
			Next = Current->Next;
			
			*Prev = Next;
			
			sock_release(Current->sock);
			kfree(Current);
			
			Current = Next;
			continue;
		}
		
		Prev = &Current->Next;
		Current=Current->Next;
	}
}
