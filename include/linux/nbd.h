#ifndef LINUX_NBD_H
#define LINUX_NBD_H

#include <linux/ioctl.h>
#include <asm/types.h>

#define NBD_SET_SOCK _IO( 0xab, 0 )
#define NBD_SET_BLKSIZE _IO( 0xab, 1 )
#define NBD_SET_SIZE _IO( 0xab, 2 )
#define NBD_DO_IT _IO( 0xab, 3 )
#define NBD_CLEAR_SOCK _IO( 0xab, 4 )
#define NBD_CLEAR_QUE _IO( 0xab, 5 )
#define NBD_PRINT_DEBUG _IO( 0xab, 6 )

#ifdef MAJOR_NR

#include <linux/locks.h>

#define LOCAL_END_REQUEST

#include <linux/blk.h>

#ifdef PARANOIA
extern int requests_in;
extern int requests_out;
#endif

static void 
nbd_end_request(struct request *req)
{
#ifdef PARANOIA
	requests_out++;
#endif
	if (end_that_request_first( req, !req->errors, "nbd" ))
		return;
	end_that_request_last( req );
}

#define MAX_NBD 128
#endif

struct nbd_device {
	int refcnt;	
	int flags;
	int harderror;		/* Code of hard error			*/
#define NBD_READ_ONLY 0x0001
#define NBD_WRITE_NOCHK 0x0002
	struct socket * sock;
	struct file * file; 		/* If == NULL, device is not ready, yet	*/
	int magic;			/* FIXME: not if debugging is off	*/
	struct request *head;	/* Requests are added here...			*/
	struct request *tail;
};

/* This now IS in some kind of include file...	*/

#define NBD_REQUEST_MAGIC 0x12560953
#define NBD_REPLY_MAGIC 0x96744668
#define LO_MAGIC 0x68797548

struct nbd_request {
	__u32 magic;
	__u32 from;
	__u32 len;
	char handle[8];
	__u32 type;	/* == READ || == WRITE 	*/
};

struct nbd_reply {
	__u32 magic;
	char handle[8];		/* handle you got from request	*/
	__u32 error;		/* 0 = ok, else error	*/
};
#endif
