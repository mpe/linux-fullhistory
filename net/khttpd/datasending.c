/*

kHTTPd -- the next generation

Send actual file-data to the connections

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

DataSending does the actual sending of file-data to the socket.

Note: Since asynchronous reads do not -yet- exists, this might block!

Return value:
	The number of requests that changed status (ie: made some progress)
*/

#include <linux/kernel.h>
#include <linux/skbuff.h>

#include <net/tcp.h>

#include <asm/uaccess.h>
#include <linux/smp_lock.h>

#include "structure.h"
#include "prototypes.h"

static	char	*Block[CONFIG_KHTTPD_NUMCPU];


int DataSending(const int CPUNR)
{
	struct http_request *CurrentRequest,**Prev;
	int count = 0;
	
	EnterFunction("DataSending");
	
	Prev = &(threadinfo[CPUNR].DataSendingQueue);
	CurrentRequest = threadinfo[CPUNR].DataSendingQueue;
	while (CurrentRequest!=NULL)
	{
		int ReadSize,Space;
		int retval;
		mm_segment_t oldfs;


		/* First, test if the socket has any buffer-space left.
		   If not, no need to actually try to send something.  */
		  
		
		Space=4096;
		if (CurrentRequest->sock->sk!=NULL)
		{
			lock_sock(CurrentRequest->sock->sk);
			Space = sock_wspace(CurrentRequest->sock->sk);
			release_sock(CurrentRequest->sock->sk);
		}
		
		ReadSize = min(4096,CurrentRequest->FileLength - CurrentRequest->BytesSent);
		ReadSize = min(ReadSize , Space );
		
		if (ReadSize>0)
		{
			/* This part does a redundant data-copy. To bad for now. 
			   In the future, we might want to nick the data right out
			   of the page-cache
			*/
		
			CurrentRequest->filp->f_pos = CurrentRequest->BytesSent;
		
			oldfs = get_fs(); set_fs(KERNEL_DS);
			retval = CurrentRequest->filp->f_op->read(CurrentRequest->filp, Block[CPUNR], ReadSize, &CurrentRequest->filp->f_pos);
			set_fs(oldfs);
		
			if (retval>0)
			{
				retval = SendBuffer_async(CurrentRequest->sock,Block[CPUNR],(size_t)retval);
				if (retval>0)
				{
					CurrentRequest->BytesSent += retval;
					count++;
				
				}
			}
		
		}
		lock_sock(CurrentRequest->sock->sk);
		
		/* 
		   If end-of-file or closed connection: Finish this request 
		   by moving it to the "logging" queue. 
		*/
		if ((CurrentRequest->BytesSent>=CurrentRequest->FileLength)||
		   (CurrentRequest->sock->sk->state !=TCP_ESTABLISHED))
		{
			struct http_request *Next;
			Next = CurrentRequest->Next;
						
			if  (CurrentRequest->sock->sk->state ==TCP_ESTABLISHED)
			{
			
				CurrentRequest->sock->sk->nonagle = 0;
				CurrentRequest->sock->sk->linger = 0;
				tcp_push_pending_frames(CurrentRequest->sock->sk,&(CurrentRequest->sock->sk->tp_pinfo.af_tcp));
			}
			
			release_sock(CurrentRequest->sock->sk);



			(*Prev) = CurrentRequest->Next;
			
			CurrentRequest->Next = threadinfo[CPUNR].LoggingQueue;
			threadinfo[CPUNR].LoggingQueue = CurrentRequest;	
				
			CurrentRequest = Next;
			continue;
		
		}
		release_sock(CurrentRequest->sock->sk);
		

		Prev = &(CurrentRequest->Next);	
		CurrentRequest = CurrentRequest->Next;
	}
	
	LeaveFunction("DataSending");
	return count;
}

int InitDataSending(int ThreadCount)
{
	int I,I2;
	
	EnterFunction("InitDataSending");
	I=0;
	while (I<ThreadCount)
	{
		Block[I] = (char*)get_free_page((int)GFP_KERNEL);
		if (Block[I] == NULL) 
		{
			I2=0;
			while (I2<I-1)
			{
				free_page((unsigned long)Block[I2++]);
			}
			LeaveFunction("InitDataSending - abort");
			return -1;
		}
		I++;
	}
	LeaveFunction("InitDataSending");
	return 0;		
}

void StopDataSending(const int CPUNR)
{
	struct http_request *CurrentRequest,*Next;
	
	EnterFunction("StopDataSending");
	CurrentRequest = threadinfo[CPUNR].DataSendingQueue;

	while (CurrentRequest!=NULL)
	{	
		Next = CurrentRequest->Next;
		CleanUpRequest(CurrentRequest);
		CurrentRequest=Next;		
	}
	
	threadinfo[CPUNR].DataSendingQueue = NULL;

	free_page( (unsigned long)Block[CPUNR]);
	LeaveFunction("StopDataSending");
}
