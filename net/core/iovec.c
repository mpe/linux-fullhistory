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
 *		Pedro Roque	:	Added memcpy_fromiovecend and
 *					csum_..._fromiovecend.
 *      Andi Kleen  :   fixed error handling for 2.1
 */


#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <asm/checksum.h>

extern inline int min(int x, int y)
{
	return x>y?y:x;
}


/*
 *	Verify iovec
 *	verify area does a simple check for completly bogus addresses
 */

int verify_iovec(struct msghdr *m, struct iovec *iov, char *address, int mode)
{
	int err=0;
	int len=0;
	int ct;
	
	if(m->msg_name!=NULL)
	{
		if(mode==VERIFY_READ)
		{
			err=move_addr_to_kernel(m->msg_name, m->msg_namelen, address);
		}
		else
		{
			err=verify_area(mode, m->msg_name, m->msg_namelen);
		}
		
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
		err = copy_from_user(&iov[ct], &m->msg_iov[ct],
				     sizeof(struct iovec));
		if (err)
			return err;
		
		err = verify_area(mode, iov[ct].iov_base, iov[ct].iov_len);
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
 
int memcpy_toiovec(struct iovec *iov, unsigned char *kdata, int len)
{
	int err; 
	while(len>0)
	{
		if(iov->iov_len)
		{
			int copy = min(iov->iov_len,len);
			err = copy_to_user(iov->iov_base,kdata,copy);
			if (err) 
			    return err;
			kdata+=copy;
			len-=copy;
			iov->iov_len-=copy;
			iov->iov_base+=copy;
		}
		iov++;
	}
	return 0; 
}

/*
 *	Copy iovec to kernel.
 */
 
int memcpy_fromiovec(unsigned char *kdata, struct iovec *iov, int len)
{
	int err; 
	while(len>0)
	{
		if(iov->iov_len)
		{
			int copy=min(len,iov->iov_len);
			err = copy_from_user(kdata, iov->iov_base, copy);
			if (err)
			{
				return err; 
			}
			len-=copy;
			kdata+=copy;
			iov->iov_base+=copy;
			iov->iov_len-=copy;
		}
		iov++;
	}
	return 0; 
}


/*
 *	For use with ip_build_xmit
 */

int memcpy_fromiovecend(unsigned char *kdata, struct iovec *iov, int offset,
			int len)
{
	int err; 
	while(offset>0)
	{
		if (offset > iov->iov_len)
		{
			offset -= iov->iov_len;

		}
		else
		{
			u8 *base;
			int copy;

			base = iov->iov_base + offset;
			copy = min(len, iov->iov_len - offset);
			offset = 0;

			err = copy_from_user(kdata, base, copy);
			if (err)
			{
				return err;
			}
			len-=copy;
			kdata+=copy;
		}
		iov++;
	}

	while (len>0)
	{
		int copy=min(len, iov->iov_len);
		err = copy_from_user(kdata, iov->iov_base, copy);
		if (err)
		{
			return err;
		}
		len-=copy;
		kdata+=copy;
		iov++;
	}
	return 0;
}

/*
 *	And now for the all-in-one: copy and checksum from a user iovec
 *	directly to a datagram
 *	Calls to csum_partial but the last must be in 32 bit chunks
 *
 *	ip_build_xmit must ensure that when fragmenting only the last
 *	call to this function will be unaligned also.
 *
 *	FIXME: add an error handling path when a copy/checksum from
 *	user space failed because of a invalid pointer.
 */

unsigned int csum_partial_copy_fromiovecend(unsigned char *kdata, 
					    struct iovec *iov, int offset, 
					    int len, int csum)
{
	__u32	partial;
	__u32	partial_cnt = 0;

	while(offset>0)
	{
		if (offset > iov->iov_len)
		{
			offset -= iov->iov_len;

		}
		else
		{
			u8 *base;
			int copy;

			base = iov->iov_base + offset;
			copy = min(len, iov->iov_len - offset);
			offset = 0;

			partial_cnt = copy % 4;
			if (partial_cnt)
			{
				copy -= partial_cnt;
				copy_from_user(&partial, base + copy,
					       partial_cnt);
			}

			/*	
			 *	FIXME: add exception handling to the
			 *	csum functions and set *err when an
			 *	exception occurs.
			 */
			csum = csum_partial_copy_fromuser(base, kdata, 
							  copy, csum);

			len   -= copy + partial_cnt;
			kdata += copy + partial_cnt;
		}
		iov++;	
	}

	while (len>0)
	{
		u8 *base = iov->iov_base;
		int copy=min(len, iov->iov_len);
		
		if (partial_cnt)
		{
			int par_len = 4 - partial_cnt;

			copy_from_user(&partial, base + partial_cnt, par_len);
			csum = csum_partial((u8*) &partial, 4, csum);
			base += par_len;
			copy -= par_len;
			partial_cnt = 0;
		}

		if (len - copy > 0)
		{
			partial_cnt = copy % 4;
			if (partial_cnt)
			{
				copy -= partial_cnt;
				copy_from_user(&partial, base + copy,
					       partial_cnt);
			}
		}

		csum = csum_partial_copy_fromuser(base, kdata, copy, csum);
		len   -= copy + partial_cnt;
		kdata += copy + partial_cnt;
		iov++;
	}

	return csum;
}
