/*
 *	IP multicast routing support for mrouted 3.6
 *
 *		(c) 1995 Alan Cox, <alan@cymru.net>
 *	  Linux Consultancy and Custom Driver Development
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 */

#include <asm/system.h>
#include <asm/segment.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/fcntl.h>
#include <linux/socket.h>
#include <linux/in.h>
#include <linux/inet.h>
#include <linux/netdevice.h>
#include <linux/mroute.h>
#include <net/ip.h>
#include <net/protocol.h>
#include <linux/skbuff.h>
#include <net/sock.h>
#include <net/icmp.h>
#include <net/udp.h>
#include <net/checksum.h>

#ifdef CONFIG_IP_MROUTE

/*
 *	Multicast router conrol variables
 */

static struct vif_device vif_table[MAXVIFS];
static unsigned long vifc_map;
int mroute_do_pim = 0;
 
/*
 *	Socket options and virtual interface manipulation. The whole
 *	virtual interface system is a complete heap, but unfortunately
 *	thats how BSD mrouted happens to think. Maybe one day with a proper
 *	MOSPF/PIM router set up we can clean this up.
 */
 
int ip_mroute_setsockopt(struct sock *sk,int optname,char *optval,int optlen)
{
	int err;
	struct vifctl vif;
	
	if(optname!=MRT_INIT)
	{
		if(sk!=mroute_socket)
			return -EACCES;
	}
	
	switch(optname)
	{
		case MRT_INIT:
			if(sk->type!=SOCK_RAW || sk->num!=IPPROTO_IGMP)
				return -EOPNOTSUPP;
			if(optlen!=sizeof(int))
				return -ENOPROTOOPT;
			if((err=verify_area(VERIFY_READ,optval,sizeof(int)))<0)
				return err;
			if(get_user((int *)optval)!=1)
				return -ENOPROTOOPT;
			if(mroute_socket)
				return -EADDRINUSE;
			mroute_socket=sk;
			/* Initialise state */
			return 0;
		case MRT_DONE:
			mroute_close(sk);
			mroute_socket=NULL;
			return 0;
		case MRT_ADD_VIF:
		case MRT_DEL_VIF:
			if(optlen!=sizeof(vif))
				return -EINVAL;
			if((err=verify_area(VERIFY_READ, optval, sizeof(vif)))<0)
				return err;
			memcpy_fromfs(&vif,optval,sizeof(vif));
			if(vif.vifc_vifi > MAXVIFS)
				return -ENFILE;
			if(optname==MRT_ADD_VIF)
			{
				struct vif_device *v=&vif_table[vif.vifc_vifi];
				struct device *dev;
				/* Empty vif ? */
				if(vifc_map&(1<<vif.vifc_vifi))
					return -EADDRINUSE;
				/* Find the interface */
				dev=ip_dev_find(vif.vifc_lcl_addr.s_addr);
				if(!dev)
					return -EADDRNOTAVAIL;
				/* Must be tunnelled or multicastable */
				if(vif.vifc_flags&VIFF_TUNNEL)
				{
					if(vif.vifc_flags&VIFF_SRCRT)
						return -EOPNOTSUPP;
					/* IPIP will do all the work */
				}
				else
				{
					if(dev->flags&IFF_MULTICAST)
					{
						/* Most ethernet cards dont know
						   how to do this yet.. */
						dev->flags|=IFF_ALLMULTI;
						dev_mc_upload(dev);
					}
					else
					{
						/* We are stuck.. */
						return -EOPNOTSUPP;
					}
				}
				/*
				 *	Fill in the VIF structures
				 */
				cli();
				v->rate_limit=vif.vifc_rate_limit;
				v->local=vif.vifc_lcl_addr.s_addr;
				v->remote=vif.vifc_rmt_addr.s_addr;
				v->flags=vif.vifc_flags;
				v->threshold=vif.vifc_threshold;
				v->dev=dev;
				v->bytes_in = 0;
				v->bytes_out = 0;
				v->pkt_in = 0;
				v->pkt_out = 0;
				vifc_map|=(1<<vif.vifc_vifi);
				sti();
				return 0;
			}
			else
			/*
			 *	VIF deletion
			 */
			{
				struct vif_device *v=&vif_table[vif.vifc_vifi];
				if(vifc_map&(1<<vif.vifc_vifi))
				{
					if(!(v->flags&VIFF_TUNNEL))
					{
						v->dev->flags&=~IFF_ALLMULTI;
						dev_mc_upload(v->dev);
					}
					vifc_map&=~(1<<vif.vifc_vifi);
					return 0;					
				}
				else
					return -EADDRNOTAVAIL;
			}
		/*
		 *	Manipulate the forwarding caches. These live
		 *	in a sort of kernel/user symbiosis.
		 */
		case MRT_ADD_MFC:
		case MRT_DEL_MFC:
			return -EOPNOTSUPP;
		/*
		 *	Control PIM assert.
		 */
		case MRT_ASSERT:
			if(optlen!=sizeof(int))
				return -EINVAL;
			if((err=verify_area(VERIFY_READ, optval,sizeof(int)))<0)
				return err;
			mroute_do_pim= (optval)?1:0;
			return 0;
		/*
		 *	Spurious command, or MRT_VERSION which you cannot
		 *	set.
		 */
		default:
			return -EOPNOTSUPP;
	}
}

/*
 *	Getsock opt support for the multicast routing system.
 */
 
int ip_mroute_getsockopt(struct sock *sk,int optname,char *optval,int *optlen)
{
	int olr;
	int err;

	if(sk!=mroute_socket)
		return -EACCES;
	if(optname!=MRT_VERSION && optname!=MRT_ASSERT)
		return -EOPNOTSUPP;
	
	olr=get_user(optlen);
	if(olr!=sizeof(int))
		return -EINVAL;
	err=verify_area(VERIFY_WRITE, optval,sizeof(int));
	if(err)
		return err;
	put_user(sizeof(int),optlen);
	if(optname==MRT_VERSION)
		put_user(0x0305,(int *)optval);
	else
		put_user(mroute_do_pim,(int *)optval);
	return 0;
}

/*
 *	The IP multicast ioctl support routines.
 */
 
int ipmr_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	int err;
	struct sioc_sg_req sr;
	struct sioc_vif_req vr;
	struct vif_device *vif;
	
	switch(cmd)
	{
		case SIOCGETVIFCNT:
			err=verify_area(VERIFY_WRITE, (void *)arg, sizeof(vr));
			if(err)
				return err;
			memcpy_fromfs(&vr,(void *)arg,sizeof(sr));
			if(vr.vifi>=MAXVIFS)
				return -EINVAL;
			vif=&vif_table[vr.vifi];
			if(vifc_map&(1<<vr.vifi))
			{
				vr.icount=vif->pkt_in;
				vr.ocount=vif->pkt_out;
				vr.ibytes=vif->bytes_in;
				vr.obytes=vif->bytes_out;
				memcpy_tofs((void *)arg,&vr,sizeof(sr));
				return 0;
			}
			return -EADDRNOTAVAIL;
		case SIOCGETSGCNT:
			err=verify_area(VERIFY_WRITE, (void *)arg, sizeof(sr));
			if(err)
				return err;
			memcpy_fromfs(&sr,(void *)arg,sizeof(sr));
			memcpy_tofs((void *)arg,&sr,sizeof(sr));
			return 0;
		default:
			return -EINVAL;
	}
}

/*
 *	Close the multicast socket, and clear the vif tables etc
 */
 
void mroute_close(struct sock *sk)
{
	int i;
	struct vif_device *v=&vif_table[0];
		
	/*
	 *	Shut down all active vif entries
	 */
	 
	for(i=0;i<MAXVIFS;i++)
	{
		if(vifc_map&(1<<i))
		{
			if(!(v->flags&VIFF_TUNNEL))
			{
				v->dev->flags&=~IFF_ALLMULTI;
				dev_mc_upload(v->dev);
			}
		}
		v++;
	}		
	vifc_map=0;	
}

#endif
