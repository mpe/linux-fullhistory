/*
 * INET		An implementation of the TCP/IP protocol suite for the LINUX
 *		operating system.  INET is implemented using the  BSD Socket
 *		interface as the means of communication with the user level.
 *
 *		The IP to API glue.
 *		
 * Authors:	see ip.c
 *
 * Fixes:
 *		Many		:	Split from ip.c , see ip.c for history.
 *		Martin Mares	:	TOS setting fixed.
 *		Alan Cox	:	Fixed a couple of oopses in Martin's 
 *					TOS tweaks.
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/icmp.h>
#include <linux/netdevice.h>
#include <net/sock.h>
#include <net/ip.h>
#include <net/icmp.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/firewall.h>
#include <linux/ip_fw.h>
#include <net/checksum.h>
#include <linux/route.h>
#include <linux/mroute.h>
#include <net/route.h>

#include <asm/segment.h>

#ifdef CONFIG_IP_MULTICAST

/*
 *	Write an multicast group list table for the IGMP daemon to
 *	read.
 */
 
int ip_mc_procinfo(char *buffer, char **start, off_t offset, int length, int dummy)
{
	off_t pos=0, begin=0;
	struct ip_mc_list *im;
	unsigned long flags;
	int len=0;
	struct device *dev;
	
	len=sprintf(buffer,"Device    : Count\tGroup    Users Timer\n");  
	save_flags(flags);
	cli();
	
	for(dev = dev_base; dev; dev = dev->next)
	{
                if((dev->flags&IFF_UP)&&(dev->flags&IFF_MULTICAST))
                {
                        len+=sprintf(buffer+len,"%-10s: %5d\n",
					dev->name, dev->mc_count);
                        for(im = dev->ip_mc_list; im; im = im->next)
                        {
                                len+=sprintf(buffer+len,
					"\t\t\t%08lX %5d %d:%08lX\n",
                                        im->multiaddr, im->users,
					im->tm_running, im->timer.expires-jiffies);
                                pos=begin+len;
                                if(pos<offset)
                                {
                                        len=0;
                                        begin=pos;
                                }
                                if(pos>offset+length)
                                        break;
                        }
                }
	}
	restore_flags(flags);
	*start=buffer+(offset-begin);
	len-=(offset-begin);
	if(len>length)
		len=length;	
	return len;
}


/*
 *	Socket option code for IP. This is the end of the line after any TCP,UDP etc options on
 *	an IP socket.
 *
 *	We implement IP_TOS (type of service), IP_TTL (time to live).
 */

static struct device *ip_mc_find_devfor(unsigned long addr)
{
	struct device *dev;
	for(dev = dev_base; dev; dev = dev->next)
	{
		if((dev->flags&IFF_UP)&&(dev->flags&IFF_MULTICAST)&&
			(dev->pa_addr==addr))
			return dev;
	}

	return NULL;
}

#endif

int ip_setsockopt(struct sock *sk, int level, int optname, char *optval, int optlen)
{
	int val,err;
	unsigned char ucval;
#if defined(CONFIG_IP_FIREWALL) || defined(CONFIG_IP_ACCT)
	struct ip_fw tmp_fw;
#endif	
	if (optval == NULL)
	{
		val=0;
		ucval=0;
	}
	else
	{
		err=verify_area(VERIFY_READ, optval, sizeof(int));
		if(err)
			return err;
		val = get_user((int *) optval);
		ucval=get_user((unsigned char *) optval);
	}
	
	if(level!=SOL_IP)
		return -EOPNOTSUPP;
#ifdef CONFIG_IP_MROUTE
	if(optname>=MRT_BASE && optname <=MRT_BASE+10)
	{
		return ip_mroute_setsockopt(sk,optname,optval,optlen);
	}
#endif
	
	switch(optname)
	{
		case IP_OPTIONS:
	          {
			  struct options * opt = NULL;
			  struct options * old_opt;
			  if (optlen > 40 || optlen < 0)
			  	return -EINVAL;
			  err = verify_area(VERIFY_READ, optval, optlen);
			  if (err)
			  	return err;
			  opt = kmalloc(sizeof(struct options)+((optlen+3)&~3), GFP_KERNEL);
			  if (!opt)
			  	return -ENOMEM;
			  memset(opt, 0, sizeof(struct options));
			  if (optlen)
			  	memcpy_fromfs(opt->__data, optval, optlen);
			  while (optlen & 3)
			  	opt->__data[optlen++] = IPOPT_END;
			  opt->optlen = optlen;
			  opt->is_data = 1;
			  opt->is_setbyuser = 1;
			  if (optlen && ip_options_compile(opt, NULL)) 
			  {
				  kfree_s(opt, sizeof(struct options) + optlen);
				  return -EINVAL;
			  }
			  /*
			   * ANK: I'm afraid that receive handler may change
			   * options from under us.
			   */
			  cli();
			  old_opt = sk->opt;
			  sk->opt = opt;
			  sti();
			  if (old_opt)
			  	kfree_s(old_opt, sizeof(struct optlen) + old_opt->optlen);
			  return 0;
		  }
		case IP_TOS:		/* This sets both TOS and Precedence */
			if (val<0 || val>63)	/* Reject setting of unused bits */
				return -EINVAL;
			if ((val&7) > 4 && !suser())	/* Only root can set Prec>4 */
				return -EPERM;
			sk->ip_tos=val;
			switch (val & 0x38) {
				case IPTOS_LOWDELAY:
					sk->priority=SOPRI_INTERACTIVE;
					break;
				case IPTOS_THROUGHPUT:
					sk->priority=SOPRI_BACKGROUND;
					break;
				default:
					sk->priority=SOPRI_NORMAL;
					break;
			}
			return 0;
		case IP_TTL:
			if(val<1||val>255)
				return -EINVAL;
			sk->ip_ttl=val;
			return 0;
		case IP_HDRINCL:
			if(sk->type!=SOCK_RAW)
				return -ENOPROTOOPT;
			sk->ip_hdrincl=val?1:0;
			return 0;
#ifdef CONFIG_IP_MULTICAST
		case IP_MULTICAST_TTL: 
		{
			sk->ip_mc_ttl=(int)ucval;
	                return 0;
		}
		case IP_MULTICAST_LOOP: 
		{
			if(ucval!=0 && ucval!=1)
				 return -EINVAL;
			sk->ip_mc_loop=(int)ucval;
			return 0;
		}
		case IP_MULTICAST_IF: 
		{
			struct in_addr addr;
			struct device *dev=NULL;
			
			/*
			 *	Check the arguments are allowable
			 */

			err=verify_area(VERIFY_READ, optval, sizeof(addr));
			if(err)
				return err;
				
			memcpy_fromfs(&addr,optval,sizeof(addr));
			
			
			/*
			 *	What address has been requested
			 */
			
			if(addr.s_addr==INADDR_ANY)	/* Default */
			{
				sk->ip_mc_name[0]=0;
				return 0;
			}
			
			/*
			 *	Find the device
			 */
			 
			dev=ip_mc_find_devfor(addr.s_addr);
						
			/*
			 *	Did we find one
			 */
			 
			if(dev) 
			{
				strcpy(sk->ip_mc_name,dev->name);
				return 0;
			}
			return -EADDRNOTAVAIL;
		}
		
		case IP_ADD_MEMBERSHIP: 
		{
		
/*
 *	FIXME: Add/Del membership should have a semaphore protecting them from re-entry
 */
			struct ip_mreq mreq;
			__u32 route_src;
			struct rtable *rt;
			struct device *dev=NULL;
			
			/*
			 *	Check the arguments.
			 */

			err=verify_area(VERIFY_READ, optval, sizeof(mreq));
			if(err)
				return err;

			memcpy_fromfs(&mreq,optval,sizeof(mreq));

			/* 
			 *	Get device for use later
			 */

			if(mreq.imr_interface.s_addr==INADDR_ANY) 
			{
				/*
				 *	Not set so scan.
				 */
				if((rt=ip_rt_route(mreq.imr_multiaddr.s_addr,0))!=NULL)
				{
					dev=rt->rt_dev;
					route_src = rt->rt_src;
					atomic_dec(&rt->rt_use);
					ip_rt_put(rt);
				}
			}
			else
			{
				/*
				 *	Find a suitable device.
				 */
				
				dev=ip_mc_find_devfor(mreq.imr_interface.s_addr);
			}
			
			/*
			 *	No device, no cookies.
			 */
			 
			if(!dev)
				return -ENODEV;
				
			/*
			 *	Join group.
			 */
			 
			return ip_mc_join_group(sk,dev,mreq.imr_multiaddr.s_addr);
		}
		
		case IP_DROP_MEMBERSHIP: 
		{
			struct ip_mreq mreq;
			struct rtable *rt;
			__u32 route_src;
			struct device *dev=NULL;

			/*
			 *	Check the arguments
			 */
			 
			err=verify_area(VERIFY_READ, optval, sizeof(mreq));
			if(err)
				return err;

			memcpy_fromfs(&mreq,optval,sizeof(mreq));

			/*
			 *	Get device for use later 
			 */
 
			if(mreq.imr_interface.s_addr==INADDR_ANY) 
			{
				if((rt=ip_rt_route(mreq.imr_multiaddr.s_addr,0))!=NULL)
			        {
					dev=rt->rt_dev;
					atomic_dec(&rt->rt_use);
					route_src = rt->rt_src;
					ip_rt_put(rt);
				}
			}
			else 
			{
			
				dev=ip_mc_find_devfor(mreq.imr_interface.s_addr);
			}
			
			/*
			 *	Did we find a suitable device.
			 */
			 
			if(!dev)
				return -ENODEV;
				
			/*
			 *	Leave group
			 */
			 
			return ip_mc_leave_group(sk,dev,mreq.imr_multiaddr.s_addr);
		}
#endif			
#ifdef CONFIG_IP_FIREWALL
		case IP_FW_INSERT_IN:
		case IP_FW_INSERT_OUT:
		case IP_FW_INSERT_FWD:
		case IP_FW_APPEND_IN:
		case IP_FW_APPEND_OUT:
		case IP_FW_APPEND_FWD:
		case IP_FW_DELETE_IN:
		case IP_FW_DELETE_OUT:
		case IP_FW_DELETE_FWD:
		case IP_FW_CHECK_IN:
		case IP_FW_CHECK_OUT:
		case IP_FW_CHECK_FWD:
		case IP_FW_FLUSH_IN:
		case IP_FW_FLUSH_OUT:
		case IP_FW_FLUSH_FWD:
		case IP_FW_ZERO_IN:
		case IP_FW_ZERO_OUT:
		case IP_FW_ZERO_FWD:
		case IP_FW_POLICY_IN:
		case IP_FW_POLICY_OUT:
		case IP_FW_POLICY_FWD:
		case IP_FW_MASQ_TIMEOUTS:
			if(!suser())
				return -EPERM;
			if(optlen>sizeof(tmp_fw) || optlen<1)
				return -EINVAL;
			err=verify_area(VERIFY_READ,optval,optlen);
			if(err)
				return err;
			memcpy_fromfs(&tmp_fw,optval,optlen);
			err=ip_fw_ctl(optname, &tmp_fw,optlen);
			return -err;	/* -0 is 0 after all */
			
#endif
#ifdef CONFIG_IP_ACCT
		case IP_ACCT_INSERT:
		case IP_ACCT_APPEND:
		case IP_ACCT_DELETE:
		case IP_ACCT_FLUSH:
		case IP_ACCT_ZERO:
			if(!suser())
				return -EPERM;
			if(optlen>sizeof(tmp_fw) || optlen<1)
				return -EINVAL;
			err=verify_area(VERIFY_READ,optval,optlen);
			if(err)
				return err;
			memcpy_fromfs(&tmp_fw, optval,optlen);
			err=ip_acct_ctl(optname, &tmp_fw,optlen);
			return -err;	/* -0 is 0 after all */
#endif
		/* IP_OPTIONS and friends go here eventually */
		default:
			return(-ENOPROTOOPT);
	}
}

/*
 *	Get the options. Note for future reference. The GET of IP options gets the
 *	_received_ ones. The set sets the _sent_ ones.
 */

int ip_getsockopt(struct sock *sk, int level, int optname, char *optval, int *optlen)
{
	int val,err;
#ifdef CONFIG_IP_MULTICAST
	int len;
#endif
	
	if(level!=SOL_IP)
		return -EOPNOTSUPP;

#ifdef CONFIG_IP_MROUTE
	if(optname>=MRT_BASE && optname <=MRT_BASE+10)
	{
		return ip_mroute_getsockopt(sk,optname,optval,optlen);
	}
#endif

	switch(optname)
	{
		case IP_OPTIONS:
			{
				unsigned char optbuf[sizeof(struct options)+40];
				struct options * opt = (struct options*)optbuf;
				err = verify_area(VERIFY_WRITE, optlen, sizeof(int));
				if (err)
					return err;
				cli();
				opt->optlen = 0;
				if (sk->opt)
					memcpy(optbuf, sk->opt, sizeof(struct options)+sk->opt->optlen);
				sti();
				if (opt->optlen == 0) 
				{
					put_fs_long(0,(unsigned long *) optlen);
					return 0;
				}
				err = verify_area(VERIFY_WRITE, optval, opt->optlen);
				if (err)
					return err;
/*
 * Now we should undo all the changes done by ip_options_compile().
 */
				if (opt->srr) 
				{
					unsigned  char * optptr = opt->__data+opt->srr-sizeof(struct  iphdr);
					memmove(optptr+7, optptr+3, optptr[1]-7);
					memcpy(optptr+3, &opt->faddr, 4);
				}
				if (opt->rr_needaddr) 
				{
					unsigned  char * optptr = opt->__data+opt->rr-sizeof(struct  iphdr);
					memset(&optptr[optptr[2]-1], 0, 4);
					optptr[2] -= 4;
				}
				if (opt->ts) 
				{
					unsigned  char * optptr = opt->__data+opt->ts-sizeof(struct  iphdr);
					if (opt->ts_needtime) 
					{
						memset(&optptr[optptr[2]-1], 0, 4);
						optptr[2] -= 4;
					}
					if (opt->ts_needaddr) 
					{
						memset(&optptr[optptr[2]-1], 0, 4);
						optptr[2] -= 4;
					}
				}
				put_fs_long(opt->optlen, (unsigned long *) optlen);
				memcpy_tofs(optval, opt->__data, opt->optlen);
			}
			return 0;
		case IP_TOS:
			val=sk->ip_tos;
			break;
		case IP_TTL:
			val=sk->ip_ttl;
			break;
		case IP_HDRINCL:
			val=sk->ip_hdrincl;
			break;
#ifdef CONFIG_IP_MULTICAST			
		case IP_MULTICAST_TTL:
			val=sk->ip_mc_ttl;
			break;
		case IP_MULTICAST_LOOP:
			val=sk->ip_mc_loop;
			break;
		case IP_MULTICAST_IF:
			err=verify_area(VERIFY_WRITE, optlen, sizeof(int));
			if(err)
  				return err;
  			len=strlen(sk->ip_mc_name);
  			err=verify_area(VERIFY_WRITE, optval, len);
		  	if(err)
  				return err;
  			put_user(len,(int *) optlen);
			memcpy_tofs((void *)optval,sk->ip_mc_name, len);
			return 0;
#endif
		default:
			return(-ENOPROTOOPT);
	}
	err=verify_area(VERIFY_WRITE, optlen, sizeof(int));
	if(err)
		return err;
	put_user(sizeof(int),(int *) optlen);

	err=verify_area(VERIFY_WRITE, optval, sizeof(int));
	if(err)
		return err;
	put_user(val,(int *) optval);

	return(0);
}
