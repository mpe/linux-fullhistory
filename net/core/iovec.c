/*
 *	iovec manipulation routines.
 *
 *
 *		This program is free software; you can redistribute it and/or
 *		modify it under the terms of the GNU General Public License
 *		as published by the Free Software Foundation; either version
 *		2 of the License, or (at your option) any later version.
 *
 *	Fixes:
 *		Andrew Lunn	:	Errors in iovec copying.
 */


#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/net.h>
#include <asm/segment.h>


extern inline int min(int x, int y)
{
	return x>y?y:x;
}

int verify_iovec(struct msghdr *m, struct iovec *iov, char *address, int mode)
{
	int err=0;
	int len=0;
	int ct;
	
	if(m->msg_name!=NULL)
	{
		if(mode==VERIFY_READ) {
			err=move_addr_to_kernel(m->msg_name, m->msg_namelen, address);
		} else
			err=verify_area(mode, m->msg_name, m->msg_namelen);
		if(err<0)
			return err;
		m->msg_name = address;
	}
	if(m->msg_control!=NULL)
	{
		err=verify_area(mode, m->msg_control, m->msg_controllen);
		if(err)
			return err;
	}
	
	for(ct=0;ct<m->msg_iovlen;ct++)
	{
		err=verify_area(VERIFY_READ, &m->msg_iov[ct], sizeof(struct iovec));
		if(err)
			return err;
		memcpy_fromfs(&iov[ct], &m->msg_iov[ct], sizeof(struct iovec));
		err=verify_area(mode, iov[ct].iov_base, iov[ct].iov_len);
		if(err)
			return err;
		len+=iov[ct].iov_len;
	}
	m->msg_iov=&iov[0];
	return len;
}

/*
 *	Copy kernel to iovec.
 */
 
void memcpy_toiovec(struct iovec *iov, unsigned char *kdata, int len)
{
	while(len>0)
	{
		if(iov->iov_len)
		{
			int copy = min(iov->iov_len,len);
			memcpy_tofs(iov->iov_base,kdata,copy);
			kdata+=copy;
			len-=copy;
			iov->iov_len-=copy;
			iov->iov_base+=copy;
		}
		iov++;
	}
}

/*
 *	Copy iovec to kernel.
 */
 
void memcpy_fromiovec(unsigned char *kdata, struct iovec *iov, int len)
{
	while(len>0)
	{
		if(iov->iov_len)
		{
			int copy=min(len,iov->iov_len);
			memcpy_fromfs(kdata, iov->iov_base, copy);
			len-=copy;
			kdata+=copy;
			iov->iov_base+=copy;
			iov->iov_len-=copy;
		}
		iov++;
	}
}
