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
 *		Andi Kleen	:	fixed error handling for 2.1
 *		Alexey Kuznetsov:	2.1 optimisations
 *		Andi Kleen	:	Fix csum*fromiovecend for IPv6.
 */


#include <linux/errno.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/malloc.h>
#include <linux/net.h>
#include <linux/in6.h>
#include <asm/uaccess.h>
#include <asm/byteorder.h>
#include <net/checksum.h>

/*
 *	Verify iovec
 *	verify area does a simple check for completly bogus addresses
 *
 *	Save time not doing verify_area. copy_*_user will make this work
 *	in any case.
 */

int verify_iovec(struct msghdr *m, struct iovec *iov, char *address, int mode)
{
	int err=0;
	int len=0;
	int ct;
	
	if(m->msg_namelen)
	{
		if(mode==VERIFY_READ)
		{
			err=move_addr_to_kernel(m->msg_name, m->msg_namelen, address);
			if(err<0)
				return err;
		}
		
		m->msg_name = address;
	} else
		m->msg_name = NULL;

	if (m->msg_iovlen > UIO_FASTIOV)
	{
		iov = kmalloc(m->msg_iovlen*sizeof(struct iovec), GFP_KERNEL);
		if (!iov)
			return -ENOMEM;
	}
	
	err = copy_from_user(iov, m->msg_iov, sizeof(struct iovec)*m->msg_iovlen);
	if (err)
	{
		if (m->msg_iovlen > UIO_FASTIOV)
			kfree(iov);
		return -EFAULT;
	}

	for(ct=0;ct<m->msg_iovlen;ct++)
		len+=iov[ct].iov_len;

	m->msg_iov=iov;
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
				return -EFAULT;
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
				return -EFAULT;
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
			return -EFAULT;
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
 */

int csum_partial_copy_fromiovecend(unsigned char *kdata, 
				   struct iovec *iov, int offset, 
				   unsigned int len, int *csump)
{
	int partial_cnt = 0;
	int err = 0;
	int csum;

	do {
		int copy = iov->iov_len - offset;

		if (copy >= 0) {
			u8 *base = iov->iov_base + offset;

			/* Normal case (single iov component) is fastly detected */
			if (len <= copy) {
				*csump = csum_and_copy_from_user(base, kdata, 
								 len, *csump, &err);
				return err;
			}

			partial_cnt = copy % 4;
			if (partial_cnt) {
				copy -= partial_cnt;
				err |= copy_from_user(kdata+copy, base+copy, partial_cnt);
			}

			*csump = csum_and_copy_from_user(base, kdata, 
							 copy, *csump, &err);

			len   -= copy + partial_cnt;
			kdata += copy + partial_cnt;
			iov++;
			break;
		}
		iov++;
		offset = -copy;
	} while (offset > 0);

	csum = *csump;

	while (len>0)
	{
		u8 *base = iov->iov_base;
		unsigned int copy = min(len, iov->iov_len);

		/* There is a remnant from previous iov. */
		if (partial_cnt)
		{
			int par_len = 4 - partial_cnt;

			/* iov component is too short ... */
			if (par_len > copy) {
				err |= copy_from_user(kdata, base, copy);
				base += copy;
				partial_cnt += copy;
				kdata += copy;
				len -= copy;
				iov++;
				if (len)
					continue;
				*csump = csum_partial(kdata-partial_cnt, partial_cnt, csum);
				return err;
			}
			err |= copy_from_user(kdata, base, par_len);
			csum = csum_partial(kdata-partial_cnt, 4, csum);
			base += par_len;
			copy -= par_len;
			len -= par_len;
			kdata += par_len;
			partial_cnt = 0;
		}

		if (len - copy > 0)
		{
			partial_cnt = copy % 4;
			if (partial_cnt)
			{
				copy -= partial_cnt;
				err |= copy_from_user(kdata+copy, base + copy, partial_cnt);
			}
		}

		if (copy == 0)
			break;

		csum = csum_and_copy_from_user(base, kdata, copy, csum, &err);
		len   -= copy + partial_cnt;
		kdata += copy + partial_cnt;
		iov++;
	}
        *csump = csum;
	return err;
}
