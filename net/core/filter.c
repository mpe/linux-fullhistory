/*
 * Linux Socket Filter - Kernel level socket filtering
 *
 * Author:
 *     Jay Schulist <Jay.Schulist@spacs.k12.wi.us>
 *
 * Based on the design of:
 *     - The Berkeley Packet Filter
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/config.h>
#if defined(CONFIG_FILTER)

#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/if_packet.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <linux/filter.h>

/*
 * Decode and apply filter instructions to the skb->data.
 * Return length to keep, 0 for none. skb is the data we are
 * filtering, filter is the array of filter instructions, and
 * len is the number of filter blocks in the array.
 */
 
int sk_run_filter(unsigned char *data, int len, struct sock_filter *filter, int flen)
{
	struct sock_filter *fentry;	/* We walk down these */
	u32 A = 0;	   		/* Accumulator */
	u32 X = 0;   			/* Index Register */
	u32 mem[BPF_MEMWORDS];		/* Scratch Memory Store */
	int k;
	int pc;
	int *t;

	/*
	 * Process array of filter instructions.
	 */

	for(pc = 0; pc < flen; pc++)
	{
		fentry = &filter[pc];
		if(fentry->code & BPF_X)
			t=&X;
		else
			t=&fentry->k;
			
		switch(fentry->code)
		{
			case BPF_ALU|BPF_ADD|BPF_X:
			case BPF_ALU|BPF_ADD|BPF_K:
				A += *t;
				continue;

			case BPF_ALU|BPF_SUB|BPF_X:
			case BPF_ALU|BPF_SUB|BPF_K:
				A -= *t;
				continue;

			case BPF_ALU|BPF_MUL|BPF_X:
			case BPF_ALU|BPF_MUL|BPF_K:
				A *= *t;
				continue;

			case BPF_ALU|BPF_DIV|BPF_X:
			case BPF_ALU|BPF_DIV|BPF_K:
				if(*t == 0)
					return (0);
				A /= *t;
				continue;

			case BPF_ALU|BPF_AND|BPF_X:
			case BPF_ALU|BPF_AND|BPF_K:
				A &= *t;
				continue;

			case BPF_ALU|BPF_OR|BPF_X:
			case BPF_ALU|BPF_OR|BPF_K:
				A |= *t;
				continue;

			case BPF_ALU|BPF_LSH|BPF_X:
			case BPF_ALU|BPF_LSH|BPF_K:
				A <<= *t;
				continue;

			case BPF_ALU|BPF_RSH|BPF_X:
			case BPF_ALU|BPF_RSH|BPF_K:
				A >>= *t;
				continue;

			case BPF_ALU|BPF_NEG:
				A = -A;
				continue;

			case BPF_JMP|BPF_JA:
				pc += fentry->k;
				continue;

			case BPF_JMP|BPF_JGT|BPF_K:
				pc += (A > fentry->k) ? fentry->jt : fentry->jf;
				continue;

			case BPF_JMP|BPF_JGE|BPF_K:
				pc += (A >= fentry->k) ? fentry->jt : fentry->jf;
				continue;

			case BPF_JMP|BPF_JEQ|BPF_K:
				pc += (A == fentry->k) ? fentry->jt : fentry->jf;
				continue;

			case BPF_JMP|BPF_JSET|BPF_K:
				pc += (A & fentry->k) ? fentry->jt : fentry->jf;
				continue;

			case BPF_JMP|BPF_JGT|BPF_X:
				pc += (A > X) ? fentry->jt : fentry->jf;
				continue;

			case BPF_JMP|BPF_JGE|BPF_X:
				pc += (A >= X) ? fentry->jt : fentry->jf;
				continue;

			case BPF_JMP|BPF_JEQ|BPF_X:
				pc += (A == X) ? fentry->jt : fentry->jf;
				continue;

			case BPF_JMP|BPF_JSET|BPF_X:
				pc += (A & X) ? fentry->jt : fentry->jf;
				continue;
			case BPF_LD|BPF_W|BPF_ABS:
				k = fentry->k;
				if(k + sizeof(long) > len)
					return (0);
				A = ntohl(*(long*)&data[k]);
				continue;

			case BPF_LD|BPF_H|BPF_ABS:
				k = fentry->k;
				if(k + sizeof(short) > len)
					return (0);
				A = ntohs(*(short*)&data[k]);
				continue;

			case BPF_LD|BPF_B|BPF_ABS:
				k = fentry->k;
				if(k >= len)
					return (0);
				A = data[k];
				continue;

			case BPF_LD|BPF_W|BPF_LEN:
				A = len;
				continue;

			case BPF_LDX|BPF_W|BPF_LEN:
				X = len;
				continue;

                      case BPF_LD|BPF_W|BPF_IND:
				k = X + fentry->k;
				if(k + sizeof(u32) > len)
					return (0);
                                A = ntohl(*(u32 *)&data[k]);
				continue;

                       case BPF_LD|BPF_H|BPF_IND:
				k = X + fentry->k;
				if(k + sizeof(u16) > len)
					return (0);
				A = ntohs(*(u16*)&data[k]);
				continue;

                       case BPF_LD|BPF_B|BPF_IND:
				k = X + fentry->k;
				if(k >= len)
					return (0);
				A = data[k];
				continue;

			case BPF_LDX|BPF_B|BPF_MSH:
				/*
				 *	Hack for BPF to handle TOS etc
				 */
				k = fentry->k;
				if(k >= len)
					return (0);
				X = (data[fentry->k] & 0xf) << 2;
				continue;

			case BPF_LD|BPF_IMM:
				A = fentry->k;
				continue;

			case BPF_LDX|BPF_IMM:
				X = fentry->k;
				continue;

                       case BPF_LD|BPF_MEM:
				A = mem[fentry->k];
				continue;

			case BPF_LDX|BPF_MEM:
				X = mem[fentry->k];
				continue;

			case BPF_MISC|BPF_TAX:
				X = A;
				continue;

			case BPF_MISC|BPF_TXA:
				A = X;
				continue;

			case BPF_RET|BPF_K:
				return ((unsigned int)fentry->k);

			case BPF_RET|BPF_A:
				return ((unsigned int)A);

			case BPF_ST:
				mem[fentry->k] = A;
				continue;

			case BPF_STX:
				mem[fentry->k] = X;
				continue;



			default:
				/* Invalid instruction counts as RET */
				return (0);
		}
	}

	printk(KERN_ERR "Filter ruleset ran off the end.\n");
	return (0);
}

/*
 * Check the user's filter code. If we let some ugly
 * filter code slip through kaboom!
 */

int sk_chk_filter(struct sock_filter *filter, int flen)
{
	struct sock_filter *ftest;
        int pc;

       /*
        * Check the filter code now.
        */
	for(pc = 0; pc < flen; pc++)
	{
		/*
                 *	All jumps are forward as they are not signed
                 */
                 
                ftest = &filter[pc];
		if(BPF_CLASS(ftest->code) == BPF_JMP)
		{	
			/*
			 *	But they mustn't jump off the end.
			 */
			if(BPF_OP(ftest->code) == BPF_JA)
			{
				if(pc + ftest->k + 1>= (unsigned)flen)
					return (-EINVAL);
			}
                        else
			{
				/*
				 *	For conditionals both must be safe
				 */
 				if(pc + ftest->jt +1 >= flen || pc + ftest->jf +1 >= flen)
					return (-EINVAL);
			}
                }

                /*
                 *	Check that memory operations use valid addresses.
                 */
                 
                if(ftest->k <0 || ftest->k >= BPF_MEMWORDS)
                {
                	/*
                	 *	But it might not be a memory operation...
                	 */
                	 
                	if (BPF_CLASS(ftest->code) == BPF_ST)
                		return -EINVAL;
			if((BPF_CLASS(ftest->code) == BPF_LD) && 
				(BPF_MODE(ftest->code) == BPF_MEM))
	                        	return (-EINVAL);
		}
        }

	/*
	 *	The program must end with a return. We don't care where they
	 *	jumped within the script (its always forwards) but in the
	 *	end they _will_ hit this.
	 */
	 
        return (BPF_CLASS(filter[flen - 1].code) == BPF_RET)?0:-EINVAL;
}

/*
 * Attach the user's filter code. We first run some sanity checks on
 * it to make sure it does not explode on us later.
 */

int sk_attach_filter(struct sock_fprog *fprog, struct sock *sk)
{
	struct sock_filter *fp, *old_filter; 
	int fsize = sizeof(struct sock_filter) * fprog->len;
	int err;

	/* Make sure new filter is there and in the right amounts. */
        if(fprog->filter == NULL || fprog->len == 0 || fsize > BPF_MAXINSNS)
                return (-EINVAL);

	if((err = sk_chk_filter(fprog->filter, fprog->len))==0)
	{
		/* If existing filter, remove it first */
		if(sk->filter)
		{
			old_filter = sk->filter_data;
			kfree_s(old_filter, (sizeof(old_filter) * sk->filter));
			sk->filter_data = NULL;
		}

		fp = (struct sock_filter *)kmalloc(fsize, GFP_KERNEL);
		if(fp == NULL)
			return (-ENOMEM);

		memset(fp,0,sizeof(*fp));
		memcpy(fp, fprog->filter, fsize);	/* Copy instructions */

		sk->filter = fprog->len;	/* Number of filter blocks */
		sk->filter_data = fp;		/* Filter instructions */
	}

	return (err);
}
#endif /* CONFIG_FILTER */
